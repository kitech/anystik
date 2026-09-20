#include "phonemonitor.h"
#include <QDebug>

#if defined(Q_OS_ANDROID)

#include "androidutils.h"
#include <jni.h>
#include <QCoreApplication>
#include <QJniObject>
#include <QSettings>

static PhoneMonitor* s_instance = nullptr;
static const int MaxRecords = 200;

static QString jstringToQString(JNIEnv* env, jstring js)
{
    if (!js) return {};
    const char* raw = env->GetStringUTFChars(js, nullptr);
    QString s = QString::fromUtf8(raw);
    env->ReleaseStringUTFChars(js, raw);
    return s;
}

void PhoneMonitor::addCallEvent(const QString& state, const QString& number)
{
    PhoneCallRecord rec{ state, number, QDateTime::currentMSecsSinceEpoch() };
    m_calls.append(rec);
    while (m_calls.size() > MaxRecords)
        m_calls.removeFirst();
    if (state == "RINGING")
        emit incomingCall(number);
    emit countersChanged();
    emit callRecorded();
}

void PhoneMonitor::addSmsEvent(const QString& sender, const QString& body)
{
    SmsRecord rec{ sender, body, QDateTime::currentMSecsSinceEpoch() };
    m_sms.append(rec);
    while (m_sms.size() > MaxRecords)
        m_sms.removeFirst();
    emit countersChanged();
    emit smsRecorded();
}

extern "C" JNIEXPORT void JNICALL
Java_io_fedlet_mobutil_PhoneStateReceiver_onCallStateChangedNative(
    JNIEnv* env, jobject /*thiz*/, jstring jState, jstring jPhoneNumber)
{
    QString state = jstringToQString(env, jState);
    QString number = jstringToQString(env, jPhoneNumber);
    qDebug() << "[PhoneMonitor] call state:" << state << "number:" << number;

    QMetaObject::invokeMethod(s_instance, [state, number]() {
        if (!s_instance) return;
        s_instance->addCallEvent(state, number);
    }, Qt::QueuedConnection);
}

extern "C" JNIEXPORT void JNICALL
Java_io_fedlet_mobutil_SmsReceiver_onSmsReceivedNative(
    JNIEnv* env, jobject /*thiz*/, jstring jSender, jstring jBody)
{
    QString sender = jstringToQString(env, jSender);
    QString body = jstringToQString(env, jBody);
    qDebug() << "[PhoneMonitor] sms from:" << sender;

    QMetaObject::invokeMethod(s_instance, [sender, body]() {
        if (!s_instance) return;
        s_instance->addSmsEvent(sender, body);
    }, Qt::QueuedConnection);
}

PhoneMonitor* PhoneMonitor::instance()
{
    return s_instance;
}

int PhoneMonitor::callCount() const { return m_calls.size(); }
int PhoneMonitor::smsCount() const { return m_sms.size(); }
const QList<PhoneCallRecord>& PhoneMonitor::callRecords() const { return m_calls; }
const QList<SmsRecord>& PhoneMonitor::smsRecords() const { return m_sms; }

int PhoneMonitor::answerMode()
{
    return QSettings().value("phoneAnswer", 0).toInt();
}

void PhoneMonitor::requestPermissions(bool force)
{
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([force]() {
        auto ctx = QNativeInterface::QAndroidApplication::context();
        QJniObject::callStaticMethod<void>(
            "io/fedlet/mobutil/PermissionHelper",
            "requestCallSmsPermission",
            "(Landroid/app/Activity;Z)V",
            ctx.object(), force);
    });
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
    });
}

void PhoneMonitor::start()
{
    if (s_instance) return;
    s_instance = new PhoneMonitor();

    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
        auto ctx = QNativeInterface::QAndroidApplication::context();
        QJniObject::callStaticMethod<void>(
            "io/fedlet/mobutil/PhoneStateReceiver",
            "registerReceiver",
            "(Landroid/content/Context;)V",
            ctx.object());
        QJniObject::callStaticMethod<void>(
            "io/fedlet/mobutil/SmsReceiver",
            "registerReceiver",
            "(Landroid/content/Context;)V",
            ctx.object());
        requestPermissions(false);   // 启动自动请求（防重，见 Java）
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
        QJniObject::callStaticMethod<void>(
            "io/fedlet/mobutil/SmsReceiver",
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
void PhoneMonitor::requestPermissions(bool) {}
void PhoneMonitor::addCallEvent(const QString&, const QString&) {}
void PhoneMonitor::addSmsEvent(const QString&, const QString&) {}
int PhoneMonitor::callCount() const { return 0; }
int PhoneMonitor::smsCount() const { return 0; }
const QList<PhoneCallRecord>& PhoneMonitor::callRecords() const { static QList<PhoneCallRecord> e; return e; }
const QList<SmsRecord>& PhoneMonitor::smsRecords() const { static QList<SmsRecord> e; return e; }

#endif