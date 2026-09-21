#include "imagesearchclient.h"

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

// private network + parse implementation (pimpl), owner = ImageSearchClient.
// single in-flight; repeat search() aborts the previous one.
class ImageSearchClientPrivate : public QObject
{
    Q_OBJECT
public:
    explicit ImageSearchClientPrivate(ImageSearchClient* owner)
        : QObject(owner), m_owner(owner)
    {
        m_nam.setCookieJar(new QNetworkCookieJar(&m_nam));
        m_nam.setTransferTimeout(20000);
    }

    ~ImageSearchClientPrivate() override
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
                .arg(QString::fromUtf8(QUrl::toPercentEncoding(keyword)),
                     QStringLiteral("0"),
                     QString::number(maxResults)));
            req.setRawHeader("Referer", "https://www.bing.com/images/");
            break;
        case ImageSearchClient::Engine::Yandex:
            url = QUrl(QStringLiteral(
                "https://yandex.com/images/search?text=%1")
                .arg(QString::fromUtf8(QUrl::toPercentEncoding(keyword))));
            req.setRawHeader("Referer", "https://yandex.com/");
            break;
        }
        req.setUrl(url);
        req.setRawHeader("User-Agent",
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
            " (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        req.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8");
        req.setRawHeader("Accept",
            "text/html,application/xhtml+xml,application/json;q=0.9,*/*;q=0.8");

        QNetworkReply* reply = m_nam.get(req);
        m_reply = reply;
        connect(reply, &QNetworkReply::finished, this,
                &ImageSearchClientPrivate::onFinished);
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
            Q_EMIT m_owner->errorOccurred(
                m_engine, QStringLiteral("解析不到图片（可能被校验码拦截）"));
        } else {
            Q_EMIT m_owner->resultsReady(m_engine, urls);
        }
        m_reply->disconnect();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    static QStringList parseBing(const QByteArray& html)
    {
        QStringList out;
        const QString text = QString::fromUtf8(html);
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
        out.removeDuplicates();
        return out;
    }

    static QStringList parseYandex(const QByteArray& html)
    {
        QStringList out;
        const QString text = QString::fromUtf8(html);
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
        out.removeDuplicates();
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

#include "imagesearchclient.moc"
