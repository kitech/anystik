#ifndef SITE_LIST_CLIENT_H
#define SITE_LIST_CLIENT_H

#include <QObject>
#include <QStringList>

class SiteListClientPrivate;

// 非搜索站「浏览」客户端（在线表情页）：
// 对 combo 0..6 站点做应用内列表加载 —— 隐含 GET（统一 UA + 站点 Referer +
// cookie jar + 20s 超时）；HTML 站用 <img> 正则解析，QFace 用 JSON 索引解析，
// 返回原图 URL 列表供网格展示/页内预览（不导入）。
class SiteListClient : public QObject
{
    Q_OBJECT
public:
    enum class Site { Qudoutu, Qqbiaoqing, Chinaz, Pic616, Aigei, QFace, Blobs };
    Q_ENUM(Site)

    explicit SiteListClient(QObject* parent = nullptr);
    ~SiteListClient() override;

    // 拉取站点默认列表；同一时间仅一个在途请求（重复调用中断上一次）。
    bool load(Site site, int maxResults = 40);

    // 加载更多：预留接口，本轮恒返回 false（空响应）。
    bool loadMore();

    void abortAll();

Q_SIGNALS:
    void resultsReady(SiteListClient::Site site, const QStringList& imageUrls);
    void errorOccurred(SiteListClient::Site site, const QString& message);

private:
    SiteListClientPrivate* const m_priv;
};

#endif // SITE_LIST_CLIENT_H