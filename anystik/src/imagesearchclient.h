#ifndef IMAGE_SEARCH_CLIENT_H
#define IMAGE_SEARCH_CLIENT_H

#include <QObject>
#include <QStringList>

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
    Q_ENUM(Engine)

    explicit ImageSearchClient(QObject* parent = nullptr);
    ~ImageSearchClient() override;

    // 发起一次后台搜索；同一时间仅允许一个在途请求（重复调用自动中断上一次）。
    bool search(Engine engine, const QString& keyword, int maxResults = 20);

    // 立即中断在途请求（页面 onStop 时调用）。
    void abortAll();

Q_SIGNALS:
    // 解析成功：结果原图 URL 列表（≤ maxResults）。
    void resultsReady(ImageSearchClient::Engine engine,
                      const QStringList& imageUrls);
    // 访问/解析失败（引擎、原因）；页面据此页内提示，不回退浏览器。
    void errorOccurred(ImageSearchClient::Engine engine,
                       const QString& message);

private:
    ImageSearchClientPrivate* const m_priv;
};

#endif // IMAGE_SEARCH_CLIENT_H
