#include "androidutils.h"
#include <QCoreApplication>
#include <QDebug>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <android/log.h>
#include <cstdio>

static void logHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    const char *file = ctx.file ? ctx.file : "unknown";
    int line = ctx.line;
    int prio = ANDROID_LOG_DEBUG;
    switch (type) {
        case QtDebugMsg:    prio = ANDROID_LOG_DEBUG; break;
        case QtInfoMsg:     prio = ANDROID_LOG_INFO; break;
        case QtWarningMsg:  prio = ANDROID_LOG_WARN; break;
        case QtCriticalMsg: prio = ANDROID_LOG_ERROR; break;
        case QtFatalMsg:    prio = ANDROID_LOG_FATAL; break;
    }
    __android_log_print(prio, "anystik", "%s:%d %s", file, line, msg.toUtf8().constData());
}
#else
#include <cstdio>

static void logHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    const char *file = ctx.file ? ctx.file : "unknown";
    int line = ctx.line;
    FILE *out = (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
                ? stderr : stdout;
    fprintf(out, "%s:%d %s\n", file, line, msg.toUtf8().constData());
    fflush(out);
    if (type == QtFatalMsg) abort();
}
#endif

void installLogHandler()
{
    qInstallMessageHandler(logHandler);
}

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>

void showAndroidToast(const QString& message) {
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([message]() {
        QJniObject context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid()) return;
        QJniObject jmsg = QJniObject::fromString(message);
        QJniObject toast = QJniObject::callStaticObjectMethod(
            "android/widget/Toast",
            "makeText",
            "(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;",
            context.object(), jmsg.object(), 1);
        if (toast.isValid()) {
            toast.callMethod<void>("show", "()V");
        }
    });
}

QString androidPicturesStickerBaseDir()
{
    QJniObject javaDir = QJniObject::callStaticObjectMethod(
        "android/os/Environment",
        "getExternalStoragePublicDirectory",
        "(Ljava/lang/String;)Ljava/io/File;",
        QJniObject::fromString("Pictures").object());
    if (!javaDir.isValid())
        return QString();
    QJniObject abs = javaDir.callObjectMethod(
        "getAbsolutePath", "()Ljava/lang/String;");
    if (!abs.isValid())
        return QString();
    const QString pics = abs.toString();
    if (pics.isEmpty())
        return QString();
    // 注意：Pictures/anystik 仅当作「文件存储位置」，不主动触发
    // MediaScanner 广播，因此切到相册后图片不会实时出现在系统相册索引中。
    return pics + QStringLiteral("/anystik");
}

bool androidStorageAccessGranted()
{
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid())
        return false;
    return QJniObject::callStaticMethod<jboolean>(
        "io/fedlet/mobutil/PermissionHelper",
        "hasWriteExternalStorage",
        "(Landroid/app/Activity;)Z",
        activity.object());
}

void requestAndroidStorageAccess()
{
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid())
        return;
    QJniObject::callStaticMethod<void>(
        "io/fedlet/mobutil/PermissionHelper",
        "requestWriteExternalStorage",
        "(Landroid/app/Activity;)V",
        activity.object());
}
#else
void showAndroidToast(const QString& message) {
    Q_UNUSED(message)
}

QString androidPicturesStickerBaseDir()
{
    return QString();   // 桌面/非 Android：用 QStandardPaths::PicturesLocation
}

bool androidStorageAccessGranted()
{
    return true;        // 桌面平台无需外部存储授权
}

void requestAndroidStorageAccess()
{
    // 桌面平台空操作
}
#endif
