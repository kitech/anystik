#ifndef SITE_LIST_CLIENT_H
#define SITE_LIST_CLIENT_H

#ifdef QT3_BUILD
#include <qobject.h>
#include <qstring.h>
#include <qstringlist.h>
#else
#include <QObject>
#include <QStringList>
#endif

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
    // Q_ENUM 是 Qt5.5 才有的元对象注册宏，Qt3 moc 会**静默跳过**它（不报错、
    // 不生成任何东西），但宏本身在 Qt3 头里未定义 → 编译期才炸。Qt3 下 moc
    // 同样能处理 enum class 声明与含 enum class 形参的信号（已实测 qt3 moc
    // 26 版生成正常），缺的只是这个注册宏，故只屏蔽宏、不屏蔽 enum。
#ifndef QT3_BUILD
    Q_ENUM(Site)
#endif

    explicit SiteListClient(QObject* parent = nullptr);
    ~SiteListClient() override;

    // 拉取站点默认列表；同一时间仅一个在途请求（重复调用中断上一次）。
    bool load(Site site, int maxResults = 40);

    // 加载更多：预留接口，本轮恒返回 false（空响应）。
    bool loadMore();

    void abortAll();

// 一律用 signals: 而非 Q_SIGNALS:
//   * Qt3 的 moc 不认 Q_SIGNALS（该宏 Qt4 才定义），会把整段当普通访问说明符，
//     **既不生成信号函数也不报错**，链接期才炸 undefined reference。
//   * 也不能写 `#ifdef QT3_BUILD signals: #else Q_SIGNALS: #endif`：Qt3 的 moc
//     不做预处理，它把 QT3_BUILD 视作未定义而走 #else 分支，于是又撞上
//     Q_SIGNALS（实测 qt3 moc 报 "syntax error"）。反过来把 signals: 放进
//     #else 虽能过 moc，但太脆。
//   * Qt4/5/6 的 moc 都认 signals:（Qt6 仍保留该关键字宏，只是风格上偏好
//     Q_SIGNALS），语义与 Q_SIGNALS 完全等价。phonedb.h / imagetmpuploader.h
//     早已统一用 signals:。
// 附带效应：Qt3 的 signals: 展开为 protected（Qt5+ 才是 public），故 pimpl 里
// `emit m_owner->resultsReady(...)` 需要下面的 friend。
signals:
    void resultsReady(SiteListClient::Site site, const QStringList& imageUrls);
    void errorOccurred(SiteListClient::Site site, const QString& message);

private:
    friend class SiteListClientPrivate;
    SiteListClientPrivate* const m_priv;
};

#endif // SITE_LIST_CLIENT_H
