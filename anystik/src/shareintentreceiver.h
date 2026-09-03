#ifndef SHARE_INTENT_RECEIVER_H
#define SHARE_INTENT_RECEIVER_H

#include <functional>
#include <QString>

class QQuickItem;

// ── 分享元信息（第一步：只收元信息，不读字节）──
struct PendingShareMeta {
    QString action;
    QString mime;
    QString text;
    QString sourceApp;          // 来源 app 全包名
    int imageCount = 0;
    qint64 receivedAt = 0;      // 接收时间（epoch ms）
    qint64 totalBytes = 0;      // 落盘字节总和
    QString displayName;        // 首文件原始文件名
    QString urisJson;
};

// 获取待确认分享元信息（取出即消费）；无待确认项时返回空 action
PendingShareMeta takePendingShareMeta();

// 注册导航回调（点击继续处理后跳转贴纸主页）
void registerShareNavigator(std::function<void()> navigateToStickerHome);

// 注册弹确认框的 parent 获取函数（返回当前 stickerhome 页面）
void registerShareConfirmHost(std::function<QQuickItem*()> host);

// 处理待确认项（弹 ConfirmPopup）；Qt/UI 就绪后调用
void drainPendingShareIntents();

// 扫描 pending_shares 目录的落盘元信息文件，弹出确认框。
// 冷启动（main 就绪）与热启动（onNewIntent 通知）共用此收口。
void scanPendingShareDir();

#endif // SHARE_INTENT_RECEIVER_H