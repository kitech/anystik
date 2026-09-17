#include "imageaiutil.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkCookieJar>
#include <QUrl>
#include <QUrlQuery>
#include <QDebug>

namespace {

const int kMaxHops = 8;

QNetworkRequest makeRequest(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setTransferTimeout(30000);
    // 手动跟随重定向链：Bing 经多跳后才在最后一跳 URL 的 q 参数携带图片描述
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setAttribute(QNetworkRequest::Http2DirectAttribute, false);
    req.setRawHeader("User-Agent", "anystik/1.0");
    // 描述全程约束为中文，避免 Geo 出口兜底时落到英文
    req.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.4");
    return req;
}

// 描述判据：非空，且不是镜象回显的 imgurl:xxx
bool isDescription(const QString& value)
{
    if (value.isEmpty()) {
        return false;
    }
    return !value.startsWith(QStringLiteral("imgurl:"), Qt::CaseInsensitive);
}

void logHop(const quint64 requestId, int hop, const char* tag,
            const QUrl& url, const QString& extra = QString())
{
    qInfo().noquote() << QStringLiteral("[ImageAiUtil] req=%1 hop=%2 %3 url=%4 %5")
                             .arg(requestId)
                             .arg(hop)
                             .arg(QLatin1String(tag), url.toString(), extra);
}

} // namespace

ImageAiUtil::ImageAiUtil(QObject* parent)
    : QObject(parent)
{
}

ImageAiUtil* ImageAiUtil::instance()
{
    static ImageAiUtil s_inst;
    return &s_inst;
}

quint64 ImageAiUtil::fetchDescription(const QString& imageUrl)
{
    const quint64 id = m_nextId++;
    Request req;
    req.requestId = id;
    req.imageUrl = imageUrl;
    m_pending.enqueue(req);
    if (!m_reply) {
        startNext();
    }
    return id;
}

void ImageAiUtil::cancelRequest(quint64 requestId)
{
    if (requestId == 0) {
        return;
    }
    if (m_active.requestId == requestId) {
        m_active = Request();
        if (m_reply) {
            auto* reply = m_reply;
            m_reply = nullptr;
            reply->abort();
            reply->deleteLater();
        }
        startNext();
        return;
    }
    for (int i = 0; i < m_pending.size(); ++i) {
        if (m_pending.at(i).requestId == requestId) {
            m_pending.removeAt(i);
            return;
        }
    }
}

void ImageAiUtil::cancelPending()
{
    m_pending.clear();
    m_active = Request();
    if (m_reply) {
        auto* reply = m_reply;
        m_reply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    startNext();
}

void ImageAiUtil::startNext()
{
    if (m_reply) {
        return;
    }
    if (m_pending.isEmpty()) {
        m_active = Request();
        return;
    }

    m_active = m_pending.dequeue();
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
        m_nam->setCookieJar(new QNetworkCookieJar(m_nam));
    }
    m_hopCount = 0;

    const QByteArray enc = QUrl::toPercentEncoding(m_active.imageUrl);
    const QUrl url(QStringLiteral(
        "https://www.bing.com/images/searchbyimage?cbir=sbi&imgurl=")
            + QString::fromLatin1(enc));
    issueGet(url);
}

void ImageAiUtil::issueGet(const QUrl& url)
{
    logHop(m_active.requestId, m_hopCount, "get", url);
    auto* reply = m_nam->get(makeRequest(url));
    m_reply = reply;

    connect(reply, &QNetworkReply::redirected, this,
            [this, reply](const QUrl& target) {
                logHop(m_active.requestId, m_hopCount, "redirected",
                       reply->url(),
                       QStringLiteral("→ %1").arg(target.toString()));
                handleRedirect(reply, target);
            });

    connect(reply, &QNetworkReply::finished, this,
            [this, reply]() {
                reply->deleteLater();
                if (m_reply != reply) {
                    return; // 已被取消 / redirect 路径接手
                }
                m_reply = nullptr;
                const Request done = m_active;
                const bool redirected = reply->attribute(
                    QNetworkRequest::RedirectionTargetAttribute).isValid();

                qInfo().noquote() << QStringLiteral(
                    "[ImageAiUtil] req=%1 hop=%2 finished error=%3 %4 "
                    "stat=%5 redirectAttr=%6 final=%7")
                    .arg(done.requestId)
                    .arg(m_hopCount)
                    .arg(int(reply->error()))
                    .arg(reply->errorString())
                    .arg(reply->attribute(
                        QNetworkRequest::HttpStatusCodeAttribute).toInt())
                    .arg(redirected)
                    .arg(reply->url().toString());

                if (reply->error() == QNetworkReply::NoError && redirected) {
                    // redirected 信号缺失时的兜底：按属性手动跟随一次
                    QUrl target = reply->attribute(
                        QNetworkRequest::RedirectionTargetAttribute).toUrl();
                    if (target.isRelative()) {
                        target = reply->url().resolved(target);
                    }
                    if (isDescription(QUrlQuery(target).queryItemValue(
                            QStringLiteral("q")).trimmed())) {
                        m_reply = nullptr;
                        emit descriptionReady(done.requestId, done.imageUrl,
                            QUrlQuery(target).queryItemValue(
                                QStringLiteral("q")).trimmed());
                        startNext();
                        return;
                    }
                    if (++m_hopCount > kMaxHops) {
                        emit failed(done.requestId, done.imageUrl,
                                    tr("Bing 重定向过多"));
                        startNext();
                        return;
                    }
                    m_reply = nullptr;
                    issueGet(target);
                    return;
                }

                const QString reason = reply->error() == QNetworkReply::NoError
                    ? tr("未识别出图片描述")
                    : reply->errorString();
                emit failed(done.requestId, done.imageUrl, reason);
                startNext();
            });
}

void ImageAiUtil::handleRedirect(QNetworkReply* reply, const QUrl& target)
{
    if (m_reply != reply) {
        return; // 已被取消或已切到下一跳
    }
    const Request done = m_active;
    QUrl resolved = target;
    if (resolved.isRelative()) {
        resolved = reply->url().resolved(resolved);
    }
    QString desc;
    if (!resolved.isEmpty()) {
        desc = QUrlQuery(resolved).queryItemValue(
            QStringLiteral("q")).trimmed();
    }
    logHop(done.requestId, m_hopCount, "redirect-parse",
           resolved, QStringLiteral("q=%1 desc=%2")
               .arg(desc, isDescription(desc) ? "yes" : "no"));

    if (isDescription(desc)) {
        // 拿到描述 → 结束本次
        m_reply = nullptr;
        reply->abort();
        emit descriptionReady(done.requestId, done.imageUrl, desc);
        startNext();
        return;
    }
    // 非描述跳（地域跳转 / imgurl 回显）→ 继续跟随
    if (++m_hopCount > kMaxHops) {
        m_reply = nullptr;
        reply->abort();
        emit failed(done.requestId, done.imageUrl,
                    tr("Bing 重定向过多"));
        startNext();
        return;
    }
    m_reply = nullptr;
    reply->abort();
    issueGet(resolved);
}