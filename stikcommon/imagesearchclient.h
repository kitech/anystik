#ifndef IMAGE_SEARCH_CLIENT_H
#define IMAGE_SEARCH_CLIENT_H

#ifdef QT3_BUILD
#include <qobject.h>
#include <qstring.h>
#include <qstringlist.h>
#else
#include <QObject>
#include <QStringList>
#endif

class ImageSearchClientPrivate;

// ── 后台图片搜索客户端（非浏览器实现）──
// 用途：「在线表情」页 combo 新增项 Bing图片(8) / Yandex图片(9) 的应用内后台搜索：
//       隐藏 GET（Chrome UA + Referer + cookie jar + 20s 超时），结果异步返回
//       原图 URL 列表，页面内网格展示；点击=页内预览（不导入）。
// Google图片(7) 反爬极强、应用内抓取通常命中校验码，由页面走系统浏览器打开
// （带关键词）；本类不发送/解析 Google 请求。
class ImageSearchClient : public QObject
{
    Q_OBJECT
public:
    enum class Engine { Bing, Yandex };
    // Q_ENUM 是 Qt5.5 才有的宏，Qt3 头未定义（编译期炸）；Qt3 moc 能处理
    // enum class 本身与含它的信号形参（已实测），故只屏蔽宏。
#ifndef QT3_BUILD
    Q_ENUM(Engine)
#endif

    explicit ImageSearchClient(QObject* parent = nullptr);
    ~ImageSearchClient() override;

    // 发起一次后台搜索；同一时间仅允许一个在途请求（重复调用自动中断上一次）。
    bool search(Engine engine, const QString& keyword, int maxResults = 20);

    // 立即中断在途请求（页面 onStop 时调用）。
    void abortAll();

// 一律用 signals: 而非 Q_SIGNALS: —— 理由与 sitelistclient.h 完全相同：
// Qt3 的 moc 不认 Q_SIGNALS（Qt4 才定义），会把整段当普通访问说明符、既不生成
// 信号函数也不报错；而 `#ifdef QT3_BUILD` 包起来也不行，Qt3 moc 不预处理、会走
// #else 分支再撞 Q_SIGNALS（实测报 syntax error）。Qt4/5/6 的 moc 都认 signals:。
// 附带效应：Qt3 的 signals: 是 protected，故 pimpl 跨类 emit 需要下面的 friend。
signals:
    // 解析成功：结果原图 URL 列表（≤ maxResults）。
    void resultsReady(ImageSearchClient::Engine engine,
                      const QStringList& imageUrls);
    // 访问/解析失败（引擎、原因）；页面据此页内提示，不回退浏览器。
    void errorOccurred(ImageSearchClient::Engine engine,
                       const QString& message);

private:
    friend class ImageSearchClientPrivate;
    ImageSearchClientPrivate* const m_priv;
};

#endif // IMAGE_SEARCH_CLIENT_H
