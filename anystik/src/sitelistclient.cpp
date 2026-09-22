#include "sitelistclient.h"
#include "httpua.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

namespace {

struct SiteSpec {
    const char* defaultUrl; // 默认列表页
    const char* baseUrl;    // 相对路径解析基址（nullptr=列表页目录）
    const char* referer;    // 站点 Referer
    const char* name;       // 站点显示名（失败文案用）
    bool jsonIndex;         // true=QFace JSON；false=HTML <img>
};

const SiteSpec kSites[] = {
    { "https://www.qudoutu.cn/hot/", nullptr, "https://www.qudoutu.cn/",
      "去斗图", false },
    { "https://www.qqbiaoqing.com/", nullptr, "https://www.qqbiaoqing.com/",
      "QQ表情", false },
    { "https://sc.chinaz.com/", nullptr, "https://sc.chinaz.com/",
      "站长素材", false },
    { "https://616pic.com/", nullptr, "https://616pic.com/",
      "图精灵", false },
    { "https://www.aigei.com/", nullptr, "https://www.aigei.com/",
      "爱给", false },
    { "https://koishi.js.org/QFace/assets/qq_emoji/_index.json",
      "https://koishi.js.org/QFace/", "https://koishi.js.org/QFace/",
      "QFace", true },
    { "https://blobs.gg/", nullptr, "https://blobs.gg/",
      "blobs.gg", false },
};

QStringList parseHtmlImgs(const QByteArray& html, const QUrl& base, int max)
{
    QStringList out;
    const QString text = QString::fromUtf8(html);
    static const QRegularExpression re(
        QStringLiteral("<img[^>]+src\\s*=\\s*[\"']([^\"'\\s]+)[\"']"),
        QRegularExpression::CaseInsensitiveOption);
    auto it = re.globalMatch(text);
    while (it.hasNext() && out.size() < max) {
        const QString src = it.next().captured(1).trimmed();
        if (src.isEmpty() || src.startsWith(QStringLiteral("data:")))
            continue;
        const QUrl u(src);
        const QString abs = u.isRelative()
            ? base.resolved(QUrl(src)).toString() : u.toString();
        if (abs.isEmpty() || out.contains(abs))
            continue;
        out << abs;
    }
    return out;
}

QStringList parseQFace(const QByteArray& data, const QUrl& base, int max)
{
    QStringList out;
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) return out;
    for (const auto& v : doc.array()) {
        if (out.size() >= max) break;
        const QJsonObject obj = v.toObject();
        if (obj.value(QStringLiteral("isHide")).toBool()) continue;
        const QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
        for (const auto& a : assets) {
            const QJsonObject ao = a.toObject();
            if (ao.value(QStringLiteral("type")).toInt() != 0) continue;
            const QString path = ao.value(QStringLiteral("path")).toString();
            if (path.isEmpty()) continue;
            const QString abs = base.resolved(QUrl(path)).toString();
            if (!out.contains(abs)) out << abs;
            break; // 每表情一个静态图
        }
    }
    return out;
}

} // namespace

class SiteListClientPrivate : public QObject
{
    Q_OBJECT
public:
    explicit SiteListClientPrivate(SiteListClient* owner)
        : QObject(owner), m_owner(owner)
    {
        m_nam.setCookieJar(new QNetworkCookieJar(&m_nam));
        m_nam.setTransferTimeout(20000);
    }

    ~SiteListClientPrivate() override
    {
        abortAll();
    }

    void abortAll()
    {
        if (m_reply) {
            m_reply->abort();
            m_reply->disconnect();
            m_reply->deleteLater();
            m_reply = nullptr;
        }
    }

    bool load(SiteListClient::Site site, int max)
    {
        abortAll();
        m_site = site;
        const SiteSpec& spec = kSites[static_cast<int>(site)];
        const QUrl url(QString::fromUtf8(spec.defaultUrl));

        QNetworkRequest req;
        req.setUrl(url);
        req.setRawHeader("User-Agent", kHttpUserAgent());
        req.setRawHeader("Referer", QByteArray(spec.referer));
        req.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8");
        req.setRawHeader("Accept",
            "text/html,application/json;q=0.9,*/*;q=0.8");

        m_base = spec.baseUrl
            ? QUrl(QString::fromUtf8(spec.baseUrl))
            : url.adjusted(QUrl::RemoveFilename);
        m_max = max;

        QNetworkReply* reply = m_nam.get(req);
        m_reply = reply;
        connect(reply, &QNetworkReply::finished,
                this, &SiteListClientPrivate::onFinished);
        return true;
    }

    bool loadMore()
    {
        return false; // 预留空响应
    }

private:
    void onFinished()
    {
        if (!m_reply)
            return;
        const bool ok = (m_reply->error() == QNetworkReply::NoError);
        const QByteArray body = m_reply->readAll();
        QStringList urls;
        if (ok) {
            const SiteSpec& spec = kSites[static_cast<int>(m_site)];
            urls = spec.jsonIndex
                ? parseQFace(body, m_base, m_max)
                : parseHtmlImgs(body, m_base, m_max);
        }
        const SiteSpec& spec = kSites[static_cast<int>(m_site)];
        if (!ok || urls.isEmpty()) {
            Q_EMIT m_owner->errorOccurred(m_site,
                QCoreApplication::translate("SiteListClient", "站点加载失败：%1")
                    .arg(QString::fromUtf8(spec.name)));
        } else {
            Q_EMIT m_owner->resultsReady(m_site, urls);
        }
        m_reply->disconnect();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    SiteListClient* const m_owner;
    QNetworkAccessManager m_nam;
    QNetworkReply* m_reply = nullptr;
    SiteListClient::Site m_site = SiteListClient::Site::Qudoutu;
    int m_max = 40;
    QUrl m_base;
};

/* ================= 公开接口 ================= */
SiteListClient::SiteListClient(QObject* parent)
    : QObject(parent)
    , m_priv(new SiteListClientPrivate(this))
{
}

SiteListClient::~SiteListClient()
{
    delete m_priv;
}

bool SiteListClient::load(Site site, int maxResults)
{
    return m_priv->load(site, maxResults);
}

bool SiteListClient::loadMore()
{
    return m_priv->loadMore();
}

void SiteListClient::abortAll()
{
    m_priv->abortAll();
}

#include "sitelistclient.moc"