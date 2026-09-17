#include "imagesearch.h"

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
#include <QDateTime>

namespace {

enum class Host {
    Catbox,
    Litterbox,
    Mhimg,
    ScdnIo,
};

const int kHostCount = 4;

QString hostName(Host host)
{
    switch (host) {
        case Host::Catbox:   return QStringLiteral("catbox");
        case Host::Litterbox: return QStringLiteral("litterbox");
        case Host::Mhimg:     return QStringLiteral("mhimg.cn");
        case Host::ScdnIo:    return QStringLiteral("img.scdn.io");
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
            // 国内降级：带 1 小时过期时间，配合“搜索相似”的临时上传语义
            addText(QStringLiteral("expired_at"),
                QDateTime::currentDateTime().addSecs(3600)
                    .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")).toUtf8());
            addFile(QStringLiteral("file"));
            break;
        case Host::ScdnIo:
            addText(QStringLiteral("outputFormat"), QByteArrayLiteral("auto"));
            addFile(QStringLiteral("image"));
            break;
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
    }
    return QString();
}

} // namespace

ImageSearch::ImageSearch(QObject* parent)
    : QObject(parent)
{
}

void ImageSearch::upload(const QString& filePath)
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

void ImageSearch::cancel()
{
    if (!m_pending) {
        return;
    }
    m_cancelling = true;
    if (m_reply) {
        m_reply->abort();
    }
}

void ImageSearch::startNextHost()
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
                ++m_hostIndex;
                startNextHost();
            });
}

void ImageSearch::emitStatus(int hostIndex)
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