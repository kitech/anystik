#include "imagesearchclient.h"
#include "httpua.h"

#ifdef QT3_BUILD
#include "qstring_shim.h"
#include "qjson_shim.h"
#include "qnam_shim.h"
#include "qconnect_slots.h"
#include "qba_shim.h"
#include "qurl_shim.h"
#include "qregularexpression_shim.h"
#include <qurl.h>
#else
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkCookieJar>
#include <QNetworkCookie>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>
#endif

// Qt3 没有 QString::fromUtf8(const QByteArray&)：QMemArray<char> 的隐式
// operator const char* 会把它当 C 串、len 取 -1 → strlen。qnam_shim 的
// readAll() 返回 `QByteArray out(n)`（**无 NUL 终止**），于是越界读。
// 同理 QStringList::removeDuplicates() 是 Qt4.1 才有的成员。
#ifdef QT3_BUILD
#define QLSTIK_BODY_TO_TEXT(ba) qFromUtf8BA(ba)
#define QLSTIK_DEDUP(list)      qStringListRemoveDuplicates(list)
#define QLSTIK_PCT_ENCODE(s)    qToPercentEncoding(s)
// 连接 finished：Qt3 的 connect 是 QObject 的**静态成员**且只收字符串宏，
// pimpl 又已不是 QObject（见下），故此处走 qconnect_slots 的全局模板，
// 必须 `using ::connect;` 把它带进名字查找（§6.1 模式）。
#define QLSTIK_CONNECT_FINISHED(reply, ctx, fn) \
    do { using ::connect; \
         connect((reply), &QNetworkReply::finished, (ctx), (fn)); } while (0)
#else
#define QLSTIK_BODY_TO_TEXT(ba) QString::fromUtf8(ba)
#define QLSTIK_DEDUP(list)      ((list).removeDuplicates())
#define QLSTIK_PCT_ENCODE(s)    QUrl::toPercentEncoding(s)
// Qt5+ 的 connect 也是 QObject 静态成员，pimpl 非 QObject 时必须显式限定，
// 否则报 "'connect' was not declared in this scope"（实测 Qt6 编译）。
#define QLSTIK_CONNECT_FINISHED(reply, ctx, fn) \
    QObject::connect((reply), &QNetworkReply::finished, (ctx), (fn))
#endif

// private network + parse implementation (pimpl), owner = ImageSearchClient.
// 刻意**不是** QObject：Qt3 的 qmake(1.07a) 只对 HEADERS 里的头跑 moc，不扫 .cpp
// （实测 .cpp 里的 #include "x.moc" 永远找不到文件），搬进公开头又等于泄露实现。
// 「pimpl 纯 C++ 类 + lambda 槽」三端同构，见 sitelistclient.cpp 同处说明。
class ImageSearchClientPrivate
{
public:
    explicit ImageSearchClientPrivate(ImageSearchClient* owner)
        : m_owner(owner)
    {
        m_nam.setCookieJar(new QNetworkCookieJar(&m_nam));
        m_nam.setTransferTimeout(20000);
    }

    ~ImageSearchClientPrivate()
    {
        abortAll();
    }

    void abortAll()
    {
        if (!m_reply)
            return;
        // 同 sitelistclient.cpp：abort() 同步发 finished，原顺序会先被
        // onFinished 置空 m_reply，返回后解引用即崩。必须先摘链再 abort。
        QNetworkReply* const reply = m_reply;
        m_reply = nullptr;
        reply->disconnect();
        reply->abort();
        reply->deleteLater();
    }

    bool search(ImageSearchClient::Engine engine, const QString& keyword,
                int maxResults)
    {
        abortAll();
        m_engine = engine;

        QNetworkRequest req;
        QUrl url;
        switch (engine) {
        case ImageSearchClient::Engine::Bing:
            url = QUrl(QStringLiteral(
                "https://www.bing.com/images/async?q=%1&first=%2&count=%3&mmasync=1")
                .arg(QString::fromUtf8(QLSTIK_PCT_ENCODE(keyword)),
                     QStringLiteral("0"),
                     QString::number(maxResults)));
            req.setRawHeader("Referer", "https://www.bing.com/images/");
            break;
        case ImageSearchClient::Engine::Yandex:
            url = QUrl(QStringLiteral(
                "https://yandex.com/images/search?text=%1")
                .arg(QString::fromUtf8(QLSTIK_PCT_ENCODE(keyword))));
            req.setRawHeader("Referer", "https://yandex.com/");
            break;
        }
        req.setUrl(url);
        req.setRawHeader("User-Agent", kHttpUserAgent());
        req.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8");
        req.setRawHeader("Accept",
            "text/html,application/xhtml+xml,application/json;q=0.9,*/*;q=0.8");

        QNetworkReply* reply = m_nam.get(req);
        m_reply = reply;
        QLSTIK_CONNECT_FINISHED(reply, m_owner, [this]() { onFinished(); });
        return true;
    }

private:
    void onFinished()
    {
        if (!m_reply)
            return;
        const QByteArray html = m_reply->readAll();
        QStringList urls;
        switch (m_engine) {
        case ImageSearchClient::Engine::Bing:
            urls = parseBing(html);
            break;
        case ImageSearchClient::Engine::Yandex:
            urls = parseYandex(html);
            break;
        }
        if (urls.isEmpty()) {
            emit m_owner->errorOccurred(
                m_engine, QStringLiteral("解析不到图片（可能被校验码拦截）"));
        } else {
            emit m_owner->resultsReady(m_engine, urls);
        }
        m_reply->disconnect();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    static QStringList parseBing(const QByteArray& html)
    {
        QStringList out;
        const QString text = QLSTIK_BODY_TO_TEXT(html);
        static const QRegularExpression re(
            QStringLiteral("m=\"(.*?)\""),
            QRegularExpression::DotMatchesEverythingOption);
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            QString json = it.next().captured(1);
            json.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
            json.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
            static const QRegularExpression murlRe(
                QStringLiteral("\"murl\"\\s*:\\s*\"([^\"]+)\""));
            const auto m = murlRe.match(json);
            if (m.hasMatch())
                out << m.captured(1);
        }
        QLSTIK_DEDUP(out);
        return out;
    }

    static QStringList parseYandex(const QByteArray& html)
    {
        QStringList out;
        const QString text = QLSTIK_BODY_TO_TEXT(html);
        static const QRegularExpression re(
            QStringLiteral("data-bem=\"(.*?)\""),
            QRegularExpression::DotMatchesEverythingOption);
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            QString json = it.next().captured(1);
            json.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
            json.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
            static const QRegularExpression hrefRe(
                QStringLiteral("\"img_href\"\\s*:\\s*\"([^\"]+)\""));
            const auto m = hrefRe.match(json);
            if (m.hasMatch())
                out << m.captured(1);
        }
        QLSTIK_DEDUP(out);
        return out;
    }

    ImageSearchClient* const m_owner;
    QNetworkAccessManager m_nam;
    QNetworkReply* m_reply = nullptr;
    ImageSearchClient::Engine m_engine = ImageSearchClient::Engine::Bing;
};

/* ================= 公开接口 ================= */
ImageSearchClient::ImageSearchClient(QObject* parent)
    : QObject(parent)
    , m_priv(new ImageSearchClientPrivate(this))
{
}

ImageSearchClient::~ImageSearchClient()
{
    delete m_priv;
}

bool ImageSearchClient::search(Engine engine, const QString& keyword,
                               int maxResults)
{
    return m_priv->search(engine, keyword, maxResults);
}

void ImageSearchClient::abortAll()
{
    m_priv->abortAll();
}
