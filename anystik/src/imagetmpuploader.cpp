#include "imagetmpuploader.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QUrl>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

namespace {

// storage.to 三段流端点（复刻 fedlet/tmpfile.go）
const QUrl kStorageToInitURL(QStringLiteral("https://storage.to/api/upload/init"));
const QUrl kStorageToConfURL(QStringLiteral("https://storage.to/api/upload/confirm"));

enum class Host {
    Catbox,
    Litterbox,
    Mhimg,
    ScdnIo,
    TmpfileLink,
    TempfileOrg,
    StorageTo,
};

const int kHostCount = 7;

QString hostName(Host host)
{
    switch (host) {
        case Host::Catbox:   return QStringLiteral("catbox");
        case Host::Litterbox: return QStringLiteral("litterbox");
        case Host::Mhimg:     return QStringLiteral("mhimg.cn");
        case Host::ScdnIo:    return QStringLiteral("img.scdn.io");
        case Host::TmpfileLink: return QStringLiteral("tmpfile.link");
        case Host::TempfileOrg: return QStringLiteral("tempfile.org");
        case Host::StorageTo:   return QStringLiteral("storage.to");
    }
    return QString();
}

QUrl hostUrl(Host host)
{
    switch (host) {
        case Host::Catbox:   return QUrl(QStringLiteral("https://catbox.moe/user/api.php"));
        case Host::Litterbox: return QUrl(
            QStringLiteral("https://litterbox.catbox.moe/resources/internals/api.php"));
        case Host::Mhimg:     return QUrl(QStringLiteral("https://mhimg.cn/api/v1/upload"));
        case Host::ScdnIo:    return QUrl(QStringLiteral("https://img.scdn.io/api/v1.php"));
        case Host::TmpfileLink: return QUrl(QStringLiteral("https://tmpfile.link/api/upload"));
        case Host::TempfileOrg: return QUrl(QStringLiteral("https://tempfile.org/api/upload/local"));
        case Host::StorageTo:   return kStorageToInitURL;
    }
    return QUrl();
}

QNetworkRequest makeRequest(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setTransferTimeout(30000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setAttribute(QNetworkRequest::Http2DirectAttribute, false);
    req.setRawHeader("User-Agent", "anystik/1.0");
    return req;
}

QHttpMultiPart* buildMultiPart(Host host, const QString& filePath)
{
    auto* file = new QFile(filePath);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        return nullptr;
    }

    auto* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    file->setParent(multi);

    auto addText = [multi](const QString& name, const QByteArray& value) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
            QVariant(QString::fromLatin1("form-data; name=\"%1\"").arg(name)));
        part.setBody(value);
        multi->append(part);
    };
    auto addFile = [multi, file](const QString& name) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
            QVariant(QString::fromLatin1("form-data; name=\"%1\"; filename=\"%2\"")
                .arg(name, QFileInfo(file->fileName()).fileName())));
        part.setBodyDevice(file);
        multi->append(part);
    };

    switch (host) {
        case Host::Catbox:
            addText(QStringLiteral("reqtype"), QByteArrayLiteral("fileupload"));
            addFile(QStringLiteral("fileToUpload"));
            break;
        case Host::Litterbox:
            addText(QStringLiteral("reqtype"), QByteArrayLiteral("fileupload"));
            addText(QStringLiteral("time"), QByteArrayLiteral("1h"));
            addFile(QStringLiteral("fileToUpload"));
            break;
        case Host::Mhimg:
            // 国内降级：带 1 小时过期时间，配合临时上传语义
            addText(QStringLiteral("expired_at"),
                QDateTime::currentDateTime().addSecs(3600)
                    .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")).toUtf8());
            addFile(QStringLiteral("file"));
            break;
        case Host::ScdnIo:
            addText(QStringLiteral("outputFormat"), QByteArrayLiteral("auto"));
            addFile(QStringLiteral("image"));
            break;
        case Host::TmpfileLink:
            addFile(QStringLiteral("file"));
            break;
        case Host::TempfileOrg:
            addFile(QStringLiteral("files"));
            break;
        case Host::StorageTo:
            break;  // 三段流经 startStorageToStep，不在此构造 multipart
    }
    return multi;
}

QString parseUrl(Host host, const QByteArray& body)
{
    switch (host) {
        case Host::Catbox:
        case Host::Litterbox: {
            const QString url = QString::fromUtf8(body).trimmed();
            if (url.startsWith(QLatin1String("http"))) {
                return url;
            }
            return QString();
        }
        case Host::Mhimg:
        case Host::ScdnIo: {
            const QJsonObject obj = QJsonDocument::fromJson(body).object();
            if (host == Host::Mhimg) {
                const QJsonObject links = obj.value(QStringLiteral("data"))
                    .toObject().value(QStringLiteral("links")).toObject();
                return links.value(QStringLiteral("url")).toString();
            }
            QString url = obj.value(QStringLiteral("url")).toString();
            if (url.isEmpty()) {
                url = obj.value(QStringLiteral("data")).toObject()
                          .value(QStringLiteral("url")).toString();
            }
            return url;
        }
        case Host::TmpfileLink: {
            const QJsonObject obj = QJsonDocument::fromJson(body).object();
            const QString url =
                obj.value(QStringLiteral("downloadLink")).toString();
            return url.startsWith(QLatin1String("http")) ? url : QString();
        }
        case Host::TempfileOrg: {
            const QJsonObject obj = QJsonDocument::fromJson(body).object();
            if (!obj.value(QStringLiteral("success")).toBool()) {
                return QString();
            }
            const auto files = obj.value(QStringLiteral("files")).toArray();
            if (files.isEmpty()) {
                return QString();
            }
            const QString id = files.first().toObject()
                .value(QStringLiteral("id")).toString();
            if (id.isEmpty()) {
                return QString();
            }
            return QStringLiteral("https://tempfile.org/%1/download").arg(id);
        }
        case Host::StorageTo:
            break;
    }
    return QString();
}

} // namespace

ImageTmpUploader::ImageTmpUploader(QObject* parent)
    : QObject(parent)
{
}

void ImageTmpUploader::upload(const QString& filePath)
{
    if (m_pending) {
        return;
    }
    m_pending = true;
    m_cancelling = false;
    m_filePath = filePath;
    m_hostIndex = 0;
    m_lastError.clear();
    startNextHost();
}

void ImageTmpUploader::cancel()
{
    if (!m_pending) {
        return;
    }
    m_cancelling = true;
    if (m_reply) {
        m_reply->abort();
    }
}

void ImageTmpUploader::startNextHost()
{
    if (m_hostIndex >= kHostCount) {
        m_pending = false;
        emit failed(m_lastError.isEmpty() ? tr("图床不可用") : m_lastError);
        return;
    }
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
    }

    const Host host = static_cast<Host>(m_hostIndex);
    emitStatus(m_hostIndex);

    if (host == Host::StorageTo) {
        startStorageToStep(0);
        return;
    }

    QHttpMultiPart* multi = buildMultiPart(host, m_filePath);
    if (!multi) {
        m_lastError = tr("无法打开图片文件");
        ++m_hostIndex;
        startNextHost();
        return;
    }

    auto* reply = m_nam->post(makeRequest(hostUrl(host)), multi);
    multi->setParent(reply); // reply delete 时连带释放 file 与 multi
    m_reply = reply;

    connect(reply, &QNetworkReply::uploadProgress, this,
            [this](qint64 sent, qint64 total) {
                int percent = 0;
                if (total > 0) {
                    percent = qBound(0, int(qreal(sent) * 100.0 / total), 100);
                }
                emit progressChanged(percent, sent, total, m_statusText);
            });

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, host]() {
                const QByteArray body = reply->readAll();
                reply->deleteLater();
                if (m_reply == reply) {
                    m_reply = nullptr;
                }

                if (m_cancelling) {
                    m_cancelling = false;
                    m_pending = false;
                    return;
                }

                if (reply->error() == QNetworkReply::NoError) {
                    const QString url = parseUrl(host, body);
                    if (!url.isEmpty()) {
                        m_pending = false;
                        emit uploaded(url);
                        return;
                    }
                    m_lastError = QStringLiteral("%1: %2")
                        .arg(hostName(host), tr("响应解析失败"));
                } else {
                    m_lastError = QStringLiteral("%1: %2")
                        .arg(hostName(host), reply->errorString());
                }
                const int status = reply->attribute(
                    QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (status > 0) {
                    m_lastError += QStringLiteral(" %1").arg(status);
                }
                ++m_hostIndex;
                startNextHost();
            });
}

// storage.to 三段流：init(POST JSON) → PUT 原始字节到预签名 URL → confirm(POST JSON)
void ImageTmpUploader::startStorageToStep(int step)
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
    }
    const QByteArray filename = QFileInfo(m_filePath).fileName().toUtf8();
    const qint64 size = QFileInfo(m_filePath).size();

    // step 0/2：JSON POST init / confirm；step 1：PUT 原始字节
    QNetworkReply* reply = nullptr;
    QNetworkRequest req;
    if (step == 0 || step == 2) {
        const QByteArray payload = step == 0
            ? QByteArrayLiteral("{\"filename\":\"") + filename
                + QByteArrayLiteral("\",\"content_type\":\"application/octet-stream\",\"size\":")
                + QByteArray::number(size) + QByteArrayLiteral("}")
            : QByteArrayLiteral("{\"r2_key\":\"") + m_storageR2Key.toUtf8()
                + QByteArrayLiteral("\",\"filename\":\"") + filename
                + QByteArrayLiteral("\",\"content_type\":\"application/octet-stream\",\"size\":")
                + QByteArray::number(size) + QByteArrayLiteral("}");
        req = makeRequest(step == 0 ? kStorageToInitURL : kStorageToConfURL);
        req.setHeader(QNetworkRequest::ContentTypeHeader,
                      QByteArrayLiteral("application/json"));
        reply = m_nam->post(req, payload);
    } else {
        auto* file = new QFile(m_filePath);
        if (!file->open(QIODevice::ReadOnly)) {
            delete file;
            ++m_hostIndex;
            startNextHost();
            return;
        }
        req = makeRequest(QUrl(m_storageUploadUrl));
        req.setHeader(QNetworkRequest::ContentTypeHeader,
                      QByteArrayLiteral("application/octet-stream"));
        reply = m_nam->put(req, file);
        file->setParent(reply);
    }
    m_reply = reply;

    connect(reply, &QNetworkReply::uploadProgress, this,
            [this](qint64 sent, qint64 total) {
                int percent = 0;
                if (total > 0) {
                    percent = qBound(0, int(qreal(sent) * 100.0 / total), 100);
                }
                emit progressChanged(percent, sent, total, m_statusText);
            });

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, step]() {
                const QByteArray body = reply->readAll();
                reply->deleteLater();
                if (m_reply == reply) {
                    m_reply = nullptr;
                }
                if (m_cancelling) {
                    m_cancelling = false;
                    m_pending = false;
                    return;
                }
                if (reply->error() != QNetworkReply::NoError) {
                    m_lastError = QStringLiteral("storage.to: %1")
                        .arg(reply->errorString());
                    const int status = reply->attribute(
                        QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    if (status > 0) {
                        m_lastError += QStringLiteral(" %1").arg(status);
                    }
                    ++m_hostIndex;
                    startNextHost();
                    return;
                }
                switch (step) {
                case 0: {
                    const QJsonObject obj = QJsonDocument::fromJson(body).object();
                    if (!obj.value(QStringLiteral("success")).toBool()) {
                        m_lastError = QStringLiteral("storage.to: init failed");
                        ++m_hostIndex;
                        startNextHost();
                        return;
                    }
                    m_storageUploadUrl =
                        obj.value(QStringLiteral("upload_url")).toString();
                    m_storageR2Key = obj.value(QStringLiteral("r2_key")).toString();
                    if (m_storageUploadUrl.isEmpty() || m_storageR2Key.isEmpty()) {
                        m_lastError = QStringLiteral("storage.to: init empty url/key");
                        ++m_hostIndex;
                        startNextHost();
                        return;
                    }
                    startStorageToStep(1);
                    return;
                }
                case 1: {
                    startStorageToStep(2);
                    return;
                }
                case 2: {
                    const QString url = QJsonDocument::fromJson(body).object()
                        .value(QStringLiteral("file")).toObject()
                        .value(QStringLiteral("raw_url")).toString();
                    if (url.isEmpty()) {
                        m_lastError = QStringLiteral("storage.to: empty raw_url");
                        ++m_hostIndex;
                        startNextHost();
                        return;
                    }
                    m_pending = false;
                    emit uploaded(url);
                    return;
                }
                }
            });
}

void ImageTmpUploader::emitStatus(int hostIndex)
{
    const QString text = hostIndex > 0
        ? tr("第 %1/%2 · %3 失败 → %4")
              .arg(hostIndex + 1)
              .arg(kHostCount)
              .arg(hostName(static_cast<Host>(hostIndex - 1)),
                   hostName(static_cast<Host>(hostIndex)))
        : tr("第 %1/%2 · 上传中 %3")
              .arg(hostIndex + 1)
              .arg(kHostCount)
              .arg(hostName(static_cast<Host>(hostIndex)));
    m_statusText = text;
    emit progressChanged(0, 0, 0, text);
}
