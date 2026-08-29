#include "networkmonitor.h"
#include <QDebug>

#if defined(Q_OS_ANDROID)

#include "androidutils.h"
#include <jni.h>
#include <QCoreApplication>
#include <QJniObject>

extern "C" JNIEXPORT void JNICALL
Java_io_fedlet_mobutil_NetworkMonitor_onNetworkChanged(
    JNIEnv* env, jobject /*thiz*/, jboolean isConnected, jstring jNetworkType)
{
    const char* raw = env->GetStringUTFChars(jNetworkType, nullptr);
    QString networkType = QString::fromUtf8(raw);
    env->ReleaseStringUTFChars(jNetworkType, raw);

    QString msg;
    if (isConnected) {
        msg = "网络已连接: " + networkType;
    } else {
        msg = "网络已断开";
    }
    qDebug() << "[NetworkMonitor]" << msg;
    showAndroidToast(msg);
}

void NetworkMonitor::checkNetwork()
{
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
        auto ctx = QNativeInterface::QAndroidApplication::context();
        QJniObject::callStaticMethod<void>(
            "io/fedlet/mobutil/NetworkMonitor",
            "checkCurrentNetwork",
            "(Landroid/content/Context;)V",
            ctx.object());
    });
}

void NetworkMonitor::start()
{
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
        auto ctx = QNativeInterface::QAndroidApplication::context();
        QJniObject::callStaticMethod<void>(
            "io/fedlet/mobutil/NetworkMonitor",
            "startMonitoring",
            "(Landroid/content/Context;)V",
            ctx.object());
        qDebug() << "[NetworkMonitor] started (Android)";
    });
}

void NetworkMonitor::stop()
{
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
        auto ctx = QNativeInterface::QAndroidApplication::context();
        QJniObject::callStaticMethod<void>(
            "io/fedlet/mobutil/NetworkMonitor",
            "stopMonitoring",
            "(Landroid/content/Context;)V",
            ctx.object());
        qDebug() << "[NetworkMonitor] stopped";
    });
}

#elif defined(Q_OS_LINUX)

#include <QNetworkInformation>
#include <QProcess>

static void showDesktopNotification(const QString& title, const QString& message) {
    QProcess::startDetached("notify-send", {"-t", "7000", title, message});
}

static QNetworkInformation* s_netInfo = nullptr;

void NetworkMonitor::start()
{
    if (s_netInfo) return;
    if (!QNetworkInformation::loadDefaultBackend()) {
        qWarning() << "[NetworkMonitor] loadDefaultBackend failed (Linux)";
        return;
    }
    s_netInfo = QNetworkInformation::instance();
    QObject::connect(s_netInfo, &QNetworkInformation::reachabilityChanged,
        [](QNetworkInformation::Reachability r) {
            QString msg;
            if (r == QNetworkInformation::Reachability::Online) {
                msg = "网络已连接";
            } else if (r == QNetworkInformation::Reachability::Disconnected) {
                msg = "网络已断开";
            } else {
                msg = "网络状态未知";
            }
            qDebug() << "[NetworkMonitor]" << msg;
            showDesktopNotification("anystik", msg);
        });
    qDebug() << "[NetworkMonitor] started (Linux)";
}

void NetworkMonitor::stop()
{
    if (!s_netInfo) return;
    QObject::disconnect(s_netInfo, &QNetworkInformation::reachabilityChanged, nullptr, nullptr);
    s_netInfo = nullptr;
    qDebug() << "[NetworkMonitor] stopped (Linux)";
}

#elif defined(Q_OS_WINDOWS)

#include <QNetworkInformation>
#include <QProcess>

static void showDesktopNotification(const QString& title, const QString& message) {
    QString psScript = QString(
        "[Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime] | Out-Null; "
        "[Windows.Data.Xml.Dom.XmlDocument, Windows.Data.Xml.Dom.XmlDocument, ContentType = WindowsRuntime] | Out-Null; "
        "$template = @\"<toast duration='long'><visual><binding template='ToastText02'><text id='1'>%1</text><text id='2'>%2</text></binding></visual></toast>\"; "
        "$xml = New-Object Windows.Data.Xml.Dom.XmlDocument; "
        "$xml.LoadXml($template); "
        "$toast = [Windows.UI.Notifications.ToastNotification]::new($xml); "
        "[Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier('anystik').Show($toast);"
    ).arg(title, message);
    QProcess::startDetached("powershell", {"-Command", psScript});
}

static QNetworkInformation* s_netInfo = nullptr;

void NetworkMonitor::start()
{
    if (s_netInfo) return;
    if (!QNetworkInformation::loadDefaultBackend()) {
        qWarning() << "[NetworkMonitor] loadDefaultBackend failed (Windows)";
        return;
    }
    s_netInfo = QNetworkInformation::instance();
    QObject::connect(s_netInfo, &QNetworkInformation::reachabilityChanged,
        [](QNetworkInformation::Reachability r) {
            QString msg;
            if (r == QNetworkInformation::Reachability::Online) {
                msg = "网络已连接";
            } else if (r == QNetworkInformation::Reachability::Disconnected) {
                msg = "网络已断开";
            } else {
                msg = "网络状态未知";
            }
            qDebug() << "[NetworkMonitor]" << msg;
            showDesktopNotification("anystik", msg);
        });
    qDebug() << "[NetworkMonitor] started (Windows)";
}

void NetworkMonitor::stop()
{
    if (!s_netInfo) return;
    QObject::disconnect(s_netInfo, &QNetworkInformation::reachabilityChanged, nullptr, nullptr);
    s_netInfo = nullptr;
    qDebug() << "[NetworkMonitor] stopped (Windows)";
}

#elif defined(Q_OS_MACOS)

#include <QNetworkInformation>
#include <QProcess>

static void showDesktopNotification(const QString& title, const QString& message) {
    QString script = QString("display notification \"%1\" with title \"%2\"")
        .arg(message, title);
    QProcess::startDetached("osascript", {"-e", script});
}

static QNetworkInformation* s_netInfo = nullptr;

void NetworkMonitor::start()
{
    if (s_netInfo) return;
    if (!QNetworkInformation::loadDefaultBackend()) {
        qWarning() << "[NetworkMonitor] loadDefaultBackend failed (macOS)";
        return;
    }
    s_netInfo = QNetworkInformation::instance();
    QObject::connect(s_netInfo, &QNetworkInformation::reachabilityChanged,
        [](QNetworkInformation::Reachability r) {
            QString msg;
            if (r == QNetworkInformation::Reachability::Online) {
                msg = "网络已连接";
            } else if (r == QNetworkInformation::Reachability::Disconnected) {
                msg = "网络已断开";
            } else {
                msg = "网络状态未知";
            }
            qDebug() << "[NetworkMonitor]" << msg;
            showDesktopNotification("anystik", msg);
        });
    qDebug() << "[NetworkMonitor] started (macOS)";
}

void NetworkMonitor::stop()
{
    if (!s_netInfo) return;
    QObject::disconnect(s_netInfo, &QNetworkInformation::reachabilityChanged, nullptr, nullptr);
    s_netInfo = nullptr;
    qDebug() << "[NetworkMonitor] stopped (macOS)";
}

#endif
