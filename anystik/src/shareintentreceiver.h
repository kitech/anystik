#ifndef SHARE_INTENT_RECEIVER_H
#define SHARE_INTENT_RECEIVER_H

#include <functional>

// Android 分享意图（SEND / SEND_MULTIPLE）在 Qt 就绪前即可能到达：
// JNI 回调先进线程安全静态队列，主窗口就绪后由 drainPendingShareIntents() 消费。
void registerShareNavigator(std::function<void()> navigateToStickerHome);
void drainPendingShareIntents();

#endif // SHARE_INTENT_RECEIVER_H