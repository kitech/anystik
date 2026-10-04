#include "sitelistclient.h"
#include "httpua.h"

#ifdef QT3_BUILD
#include "qstring_shim.h"
#include "qjson_shim.h"
#include "qnam_shim.h"
#include "qconnect_slots.h"
#include "qba_shim.h"
#include "qurl_shim.h"
#include "qcoreapplication_shim.h"
#include "qregularexpression_shim.h"
#include <qurl.h>
#else
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
#endif

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

// Qt3 没有 QString::fromUtf8(const QByteArray&)：QMemArray<char> 的隐式
// operator const char* 会把它当 C 串、len 取 -1 → strlen。而 qnam_shim 的
// readAll() 返回 `QByteArray out(n)`（**无 NUL 终止**），于是越界读，解析结果
// 随堆内容漂移。Qt3 走 qFromUtf8BA（显式长度）。同理 Qt3 无 trimmed()。
#ifdef QT3_BUILD
#define QLSTIK_BODY_TO_TEXT(ba) qFromUtf8BA(ba)
#define QLSTIK_TRIMMED(s)       qTrimmed(s)
// QUrl 侧三缺口：isRelative()/resolved() 是 Qt4.7 才有的成员，adjusted() 与
// RemoveFilename 枚举连 Qt4 都没有（Qt4.0 引入 adjusted）。
#define QLSTIK_URL_IS_RELATIVE(u)     qUrlIsRelative(u)
#define QLSTIK_RESOLVE(base, rel)     qResolveUrl((base), (rel))
#define QLSTIK_URL_REMOVE_FILENAME(u) qUrlRemoveFilename(u)
// 连接 finished：Qt3 的 connect 是 QObject 静态成员且只收字符串宏，pimpl 又
// 不是 QObject（见下），故走 qconnect_slots 的全局模板，需 `using ::connect;`。
#define QLSTIK_CONNECT_FINISHED(reply, ctx, fn) \
    do { using ::connect; \
         connect((reply), &QNetworkReply::finished, (ctx), (fn)); } while (0)
#else
#define QLSTIK_BODY_TO_TEXT(ba) QString::fromUtf8(ba)
#define QLSTIK_TRIMMED(s)       ((s).trimmed())
#define QLSTIK_URL_IS_RELATIVE(u)     ((u).isRelative())
#define QLSTIK_RESOLVE(base, rel)     ((base).resolved(rel))
#define QLSTIK_URL_REMOVE_FILENAME(u) ((u).adjusted(QUrl::RemoveFilename))
// Qt5+ 的 connect 是 QObject 静态成员；pimpl 非 QObject 时不限定就报
// "'connect' was not declared in this scope"（实测 Qt6 编译）。
#define QLSTIK_CONNECT_FINISHED(reply, ctx, fn) \
    QObject::connect((reply), &QNetworkReply::finished, (ctx), (fn))
#endif

QStringList parseHtmlImgs(const QByteArray& html, const QUrl& base, int max)
{
    QStringList out;
    const QString text = QLSTIK_BODY_TO_TEXT(html);
    static const QRegularExpression re(
        QStringLiteral("<img[^>]+src\\s*=\\s*[\"']([^\"'\\s]+)[\"']"),
        QRegularExpression::CaseInsensitiveOption);
    auto it = re.globalMatch(text);
    while (it.hasNext() && out.size() < max) {
        const QString src = QLSTIK_TRIMMED(it.next().captured(1));
        if (src.isEmpty() || src.startsWith(QStringLiteral("data:")))
            continue;
        const QUrl u(src);
        const QString abs = QLSTIK_URL_IS_RELATIVE(u)
            ? QLSTIK_RESOLVE(base, u).toString() : u.toString();
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
            const QString abs = QLSTIK_RESOLVE(base, QUrl(path)).toString();
            if (!out.contains(abs)) out << abs;
            break; // 每表情一个静态图
        }
    }
    return out;
}

} // namespace

// private network + parse implementation (pimpl), owner = SiteListClient.
// 刻意**不是** QObject：Qt3 的 qmake(1.07a) 只对 HEADERS 里的头跑 moc，不扫 .cpp
// （实测：.cpp 里 #include "x.moc.cpp" 必然找不到文件，因为根本没生成），
// 而把 pimpl 声明搬进公开头等于把实现细节泄露出去。改用「pimpl 纯 C++ 类 +
// lambda 槽」后三端同构，且省掉一个 moc 单元。
class SiteListClientPrivate
{
public:
    explicit SiteListClientPrivate(SiteListClient* owner)
        : m_owner(owner)
    {
        m_nam.setCookieJar(new QNetworkCookieJar(&m_nam));
        m_nam.setTransferTimeout(20000);
    }

    ~SiteListClientPrivate()
    {
        abortAll();
    }

    void abortAll()
    {
        if (!m_reply)
            return;
        // abort() 会**同步**发出 finished。原写法先 abort() 再 disconnect()，
        // 于是 onFinished 在 abort() 内部就把 m_reply 置空并 deleteLater 掉
        // reply；返回后继续解引用 m_reply 即空指针崩溃。因 load() 首行即
        // abortAll()，上一次请求在途时再切站点必崩（ASAN 实测 SEGV 于
        // QObject::deleteLater）。该缺陷自 a0d79e1 起存在。
        // 正确顺序：先摘链（abort 便不再回调 onFinished）→ 先清 m_reply
        // 挡住重入 → 再 abort → 最后 deleteLater。
        QNetworkReply* const reply = m_reply;
        m_reply = nullptr;
        reply->disconnect();
        reply->abort();
        reply->deleteLater();
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
#ifdef QT3_BUILD
        // Qt3 的 QByteArray(=QMemArray<char>) 没有 (const char*) 构造，套
        // QCString 又会因尾 NUL 污染头值（qnam_shim 按 data()/size() 无损取串）；
        // 它自带的 const char* 重载直接收 C 串，语义正确且无尾字节。
        req.setRawHeader("Referer", spec.referer);
#else
        req.setRawHeader("Referer", QByteArray(spec.referer));
#endif
        req.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8");
        req.setRawHeader("Accept",
            "text/html,application/json;q=0.9,*/*;q=0.8");

        m_base = spec.baseUrl
            ? QUrl(QString::fromUtf8(spec.baseUrl))
            : QLSTIK_URL_REMOVE_FILENAME(url);
        m_max = max;

        QNetworkReply* reply = m_nam.get(req);
        m_reply = reply;
        // 上下文用 owner（真 QObject）而非 pimpl：pimpl 随 owner 析构一起死，
        // 挂在 owner 上可让 Qt 自动摘链。Qt3 侧 qconnect_slots 的模板忽略
        // ctx，只把 lambda 塞进 QNetworkReply 的 finished 槽表。
        QLSTIK_CONNECT_FINISHED(reply, m_owner, [this]() { onFinished(); });
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
        const SiteSpec& spec = kSites[static_cast<int>(m_site)];
        if (ok) {
            urls = spec.jsonIndex
                ? parseQFace(body, m_base, m_max)
                : parseHtmlImgs(body, m_base, m_max);
        }
        if (!ok || urls.isEmpty()) {
            emit m_owner->errorOccurred(m_site,
                QCoreApplication::translate("SiteListClient", "站点加载失败：%1")
                    .arg(QString::fromUtf8(spec.name)));
        } else {
            emit m_owner->resultsReady(m_site, urls);
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
