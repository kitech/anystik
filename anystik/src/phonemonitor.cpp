#include "phonemonitor.h"
#include <QDebug>

#if defined(Q_OS_ANDROID)

#include "androidutils.h"
#include <jni.h>
#include <QCoreApplication>
#include <QJniObject>
#include <QSettings>

static PhoneMonitor* s_instance = nullptr;

static QString jstringToQString(JNIEnv* env, jstring js)
{
    if (!js) return {};
    const char* raw = env->GetStringUTFChars(js, nullptr);
    QString s = QString::fromUtf8(raw);
    env->ReleaseStringUTFChars(js, raw);
    return s;
}

extern "C" JNIEXPORT void JNICALL
Java_io_fedlet_mobutil_PhoneStateReceiver_onCallStateChangedNative(
    JNIEnv* env, jobject /*thiz*/, jstring jState, jstring jPhoneNumber)
{
    QString state = jstringToQString(env, jState);
    QString number = jstringToQString(env, jPhoneNumber);
    qDebug() << "[PhoneMonitor] call state:" << state << "number:" << number;

    if (state == "RINGING" && s_instance) {
        QMetaObject::invokeMethod(s_instance, [number]() {
            emit s_instance->incomingCall(number);
        }, Qt::QueuedConnection);
    }
}

PhoneMonitor* PhoneMonitor::instance()
{
    return s_instance;
}

int PhoneMonitor::answerMode()
{
    return QSettings().value("phoneAnswer", 0).toInt();
}

void PhoneMonitor::setAnswerMode(int mode)
{
    QSettings().setValue("phoneAnswer", mode);

    // Sync to Java SharedPreferences for BootReceiver
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([mode]() {
        auto ctx = QNativeInterface::QAndroidApplication::context();
        QJniObject::callStaticMethod<void>(
            "io/fedlet/mobutil/PhoneStateReceiver",
            "setPhoneAnswerMode",
            "(Landroid/content/Context;I)V",
            ctx.object(), mode);

        // Dynamic register/unregister receiver
        if (mode != 0) {
            QJniObject::callStaticMethod<void>(
                "io/fedlet/mobutil/PhoneStateReceiver",
                "registerReceiver",
                "(Landroid/content/Context;)V",
                ctx.object());
        } else {
            QJniObject::callStaticMethod<void>(
                "io/fedlet/mobutil/PhoneStateReceiver",
                "unregisterReceiver",
                "(Landroid/content/Context;)V",
                ctx.object());
        }
    });
}

void PhoneMonitor::start()
{
    if (s_instance) return;
    s_instance = new PhoneMonitor();

    int mode = answerMode();
    if (mode == 0) return;

    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
        auto ctx = QNativeInterface::QAndroidApplication::context();
        QJniObject::callStaticMethod<void>(
            "io/fedlet/mobutil/PhoneStateReceiver",
            "registerReceiver",
            "(Landroid/content/Context;)V",
            ctx.object());
        qDebug() << "[PhoneMonitor] started (Android)";
    });
}

void PhoneMonitor::stop()
{
    if (!s_instance) return;

    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
        auto ctx = QNativeInterface::QAndroidApplication::context();
        QJniObject::callStaticMethod<void>(
            "io/fedlet/mobutil/PhoneStateReceiver",
            "unregisterReceiver",
            "(Landroid/content/Context;)V",
            ctx.object());
        qDebug() << "[PhoneMonitor] stopped";
    });

    delete s_instance;
    s_instance = nullptr;
}

#else

PhoneMonitor* PhoneMonitor::instance() { return nullptr; }
int PhoneMonitor::answerMode() { return 0; }
void PhoneMonitor::setAnswerMode(int) {}
void PhoneMonitor::start() {}
void PhoneMonitor::stop() {}

#endif
