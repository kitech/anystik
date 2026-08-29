#include "keepalive.h"
#include <QDebug>
#include <QCoreApplication>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

void KeepAlive::start()
{
#ifdef Q_OS_ANDROID
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
        auto ctx = QNativeInterface::QAndroidApplication::context();

        QJniObject::callStaticMethod<void>(
            "io/fedlet/mobutil/PermissionHelper",
            "requestNotificationPermission",
            "(Landroid/app/Activity;)V",
            ctx.object());

        QJniObject::callStaticMethod<void>(
            "io/fedlet/mobutil/KeepAliveService",
            "startService",
            "(Landroid/content/Context;)V",
            ctx.object());
    });
#endif
}

void KeepAlive::stop()
{
#ifdef Q_OS_ANDROID
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
        auto ctx = QNativeInterface::QAndroidApplication::context();
        QJniObject::callStaticMethod<void>(
            "io/fedlet/mobutil/KeepAliveService",
            "stopService",
            "(Landroid/content/Context;)V",
            ctx.object());
    });
#endif
}
