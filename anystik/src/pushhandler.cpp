#include "pushhandler.h"
#include <QDebug>

#if defined(Q_OS_ANDROID)

#include "androidutils.h"
#include <jni.h>
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QSettings>
#include <QUuid>
#include <QTimerEvent>

static PushHandler* s_instance = nullptr;
static const char* UP_CLASS = "org/unifiedpush/android/connector/UnifiedPush";
static QAtomicInt s_ignoredMsgCount = 0;

static QString pushInstance()
{
    return "default"; // UP connector 为每个 instance 生成独立 token，UUID 无实际作用
    QSettings s;
    QString token = s.value("pushDeviceToken").toString();
    if (token.isEmpty()) {
        token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        s.setValue("pushDeviceToken", token);
        s.sync();
    }
    return token;
}

static QString distInstance(const QString& distPkg)
{
    if (distPkg == "io.heckel.ntfy")                       return "default_ntfy";
    if (distPkg == "org.unifiedpush.distributor.sunup")     return "default_sunup";
    if (distPkg == "com.github.gotify.up")                  return "default_gotifyup";
    if (distPkg == "org.unifiedpush.distributor.nextpush")  return "default_nextpush";
    if (distPkg == "org.unifiedpush.distributor.gcompat")   return "default_gcompat";
    return "default";
}

// ═══════════════════════════════════════════════════════════════════
// 已知 UP distributor 包名 → 友好名称映射
// ═══════════════════════════════════════════════════════════════════

static const QHash<QString, QString> s_upDisplayNames = {
    {"io.heckel.ntfy",                       "ntfy"},
    {"org.unifiedpush.distributor.sunup",    "Sunup"},
    {"com.github.gotify.up",                 "Gotify-UP"},
    {"org.unifiedpush.distributor.nextpush", "NextPush"},
    {"org.unifiedpush.distributor.gcompat",  "gCompat-UP"},
};

static bool checkMethod(const char* methodName, const char* sig)
{
    QJniEnvironment env;
    jclass clazz = env.findClass(UP_CLASS);
    if (!clazz) {
        qWarning() << "[PushHandler] class not found:" << UP_CLASS;
        return false;
    }
    jmethodID mid = env.findStaticMethod(clazz, methodName, sig);
    if (!mid) {
        qWarning() << "[PushHandler] method not found:" << methodName << sig;
        return false;
    }
    return true;
}

PushHandler* PushHandler::instance()
{
    return s_instance;
}

static bool ntfyshPushInstalled = false;

bool PushHandler::isNtfyInstalled()
{
    return ntfyshPushInstalled;
}

bool PushHandler::isConnected()
{
    return s_instance ? s_instance->m_isConnected : false;
}

bool PushHandler::isRegistering()
{
    return s_instance ? s_instance->m_isRegistering : false;
}

void PushHandler::setConnected(bool v)
{
    m_isConnected = v;
}

void PushHandler::setRegistering(bool v)
{
    m_isRegistering = v;
}

QString PushHandler::upDistributorDisplayName(const QString& packageName)
{
    auto it = s_upDisplayNames.find(packageName);
    if (it != s_upDisplayNames.end()) {
        return it.value();
    }
    int lastDot = packageName.lastIndexOf('.');
    return lastDot >= 0 ? packageName.mid(lastDot + 1) : packageName;
}

QVector<QPair<QString,QString>> PushHandler::knownDistributors()
{
    QVector<QPair<QString,QString>> result;
    for (auto it = s_upDisplayNames.constBegin(); it != s_upDisplayNames.constEnd(); ++it) {
        result.append({it.key(), it.value()});
    }
    return result;
}

QStringList PushHandler::installedDistributors()
{
    QStringList result;
    if (!s_instance) return result;

    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
        auto ctx = QNativeInterface::QAndroidApplication::context();
        QJniObject list = QJniObject::callStaticMethod<jobject>(
            UP_CLASS,
            "getDistributors",
            "(Landroid/content/Context;)Ljava/util/List;",
            ctx.object());
        if (!list.isValid()) return;

        QStringList distributors;
        jint size = list.callMethod<jint>("size", "()I");
        for (jint i = 0; i < size; i++) {
            QJniObject item = list.callObjectMethod("get", "(I)Ljava/lang/Object;", i);
            distributors.append(item.toString());
        }

        QMetaObject::invokeMethod(s_instance, [distributors]() {
            s_instance->m_installedDistributorsCache = distributors;
            emit s_instance->distributorsUpdated(distributors);
        }, Qt::QueuedConnection);
    });

    // 返回缓存（首次可能为空，后续会有值）
    return s_instance ? s_instance->m_installedDistributorsCache : result;
}

PushProviderType PushHandler::providerType() const
{
    return m_providerType;
}

QString PushHandler::currentDistributor() const
{
    return m_currentDistributor;
}

QString PushHandler::currentDistributorDisplayName() const
{
    return upDistributorDisplayName(m_currentDistributor);
}

void PushHandler::setProviderType(PushProviderType type)
{
    m_providerType = type;
    QSettings().setValue("pushProvider", static_cast<int>(type));
}

void PushHandler::setCurrentDistributor(const QString& dist)
{
    m_currentDistributor = dist;
}

void PushHandler::start()
{
    if (s_instance) return;
    s_instance = new PushHandler();
    s_instance->setConnected(false);
    s_instance->setRegistering(false);
    QObject::connect(s_instance, &PushHandler::registrationSent,
        s_instance, &PushHandler::startRegistrationTimeout);

    qDebug() << "[PushHandler] initialized, device=" << pushInstance();
    showAndroidToast("Push 初始化...");
}

void PushHandler::registerDevice()
{
    if (!s_instance) return;

    // 检查 provider type
    PushProviderType type = static_cast<PushProviderType>(
        QSettings().value("pushProvider", 0).toInt());
    s_instance->m_providerType = type;

    if (type == PushProviderType::Gotify) {
        qDebug() << "[PushHandler] Gotify provider not yet implemented";
        return;
    }

    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
        auto ctx = QNativeInterface::QAndroidApplication::context();

        // 检查是否已有 saved distributor
        QJniObject saved = QJniObject::callStaticMethod<jobject>(
            UP_CLASS,
            "getSavedDistributor",
            "(Landroid/content/Context;)Ljava/lang/String;",
            ctx.object());
        QString savedDistributor = saved.toString();

        if (!savedDistributor.isEmpty()) {
            qDebug() << "[PushHandler]已有 distributor:" << savedDistributor;

            QJniObject::callStaticMethod<void>(
                "io/fedlet/mobutil/PushServiceImpl",
                "setActiveDistributor",
                "(Ljava/lang/String;)V",
                QJniObject::fromString(savedDistributor).object());

            // 更新当前 distributor 显示名
            QMetaObject::invokeMethod(s_instance, [savedDistributor]() {
                s_instance->m_currentDistributor = savedDistributor;
            }, Qt::QueuedConnection);

            // 检测 1：调用前验证 register 方法签名
            const char* registerSig = "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V";
            if (!checkMethod("register", registerSig)) {
                QMetaObject::invokeMethod(s_instance, []() {
                    s_instance->setConnected(false);
                    s_instance->setRegistering(false);
                    emit s_instance->registrationFailed("register 方法签名错误");
                    emit s_instance->statusChanged();
                }, Qt::QueuedConnection);
                return;
            }

            QJniObject::callStaticMethod<void>(
                UP_CLASS,
                "register",
                registerSig,
                ctx.object(),
                QJniObject::fromString(distInstance(savedDistributor)).object(),
                QJniObject().object(),
                QJniObject().object());

            // 检测 4：状态验证
            QJniObject verify = QJniObject::callStaticMethod<jobject>(
                UP_CLASS,
                "getSavedDistributor",
                "(Landroid/content/Context;)Ljava/lang/String;",
                ctx.object());
            if (verify.toString() != savedDistributor) {
                qWarning() << "[PushHandler] distributor state changed after register";
                QMetaObject::invokeMethod(s_instance, []() {
                    s_instance->setConnected(false);
                    s_instance->setRegistering(false);
                    emit s_instance->registrationFailed("distributor 状态异常");
                    emit s_instance->statusChanged();
                }, Qt::QueuedConnection);
                return;
            }

            qDebug() << "[PushHandler] register sent to" << savedDistributor;
            QMetaObject::invokeMethod(s_instance, []() {
                s_instance->setRegistering(true);
                s_instance->setConnected(false);
                emit s_instance->registrationSent();
                emit s_instance->statusChanged();
            }, Qt::QueuedConnection);
            return;
        }

        // 没有 saved distributor，获取可用列表
        QJniObject list = QJniObject::callStaticMethod<jobject>(
            UP_CLASS,
            "getDistributors",
            "(Landroid/content/Context;)Ljava/util/List;",
            ctx.object());

        // 检测 3：返回值有效性
        if (!list.isValid()) {
            qWarning() << "[PushHandler] getDistributors returned invalid";
            QMetaObject::invokeMethod(s_instance, []() {
                s_instance->setConnected(false);
                s_instance->setRegistering(false);
                emit s_instance->registrationFailed("getDistributors 调用失败");
                emit s_instance->statusChanged();
            }, Qt::QueuedConnection);
            return;
        }

        QStringList distributors;
        jint size = list.callMethod<jint>("size", "()I");
        for (jint i = 0; i < size; i++) {
            QJniObject item = list.callObjectMethod("get", "(I)Ljava/lang/Object;", i);
            distributors.append(item.toString());
        }
        qDebug() << "[PushHandler] distributors:" << distributors;

        // 回到 Qt 线程处理
        QMetaObject::invokeMethod(s_instance, [distributors]() {
            // 缓存已安装列表
            s_instance->m_installedDistributorsCache = distributors;

            if (distributors.isEmpty()) {
                qWarning() << "[PushHandler] no distributors found";
                s_instance->setConnected(false);
                s_instance->setRegistering(false);
                emit s_instance->registrationFailed("未找到 UnifiedPush 分发器，请安装 ntfy/Sunup 等");
                emit s_instance->statusChanged();
                return;
            }
            s_instance->setRegistering(true);
            s_instance->setConnected(false);
            emit s_instance->registrationSent();
            emit s_instance->statusChanged();
            if (distributors.size() == 1) {
                s_instance->selectDistributor(distributors.first());
                return;
            }
            emit s_instance->distributorsFound(distributors);
        }, Qt::QueuedConnection);
    });
}

void PushHandler::selectDistributor(const QString& distributor)
{
    if (!s_instance) return;

    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([distributor]() {
        auto ctx = QNativeInterface::QAndroidApplication::context();

        // 检测 1：调用前验证方法
        if (!checkMethod("saveDistributor", "(Landroid/content/Context;Ljava/lang/String;)V")) {
            QMetaObject::invokeMethod(s_instance, []() {
                s_instance->setConnected(false);
                s_instance->setRegistering(false);
                emit s_instance->registrationFailed("saveDistributor 方法不存在");
                emit s_instance->statusChanged();
            }, Qt::QueuedConnection);
            return;
        }
        const char* registerSig = "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V";
        if (!checkMethod("register", registerSig)) {
            QMetaObject::invokeMethod(s_instance, []() {
                s_instance->setConnected(false);
                s_instance->setRegistering(false);
                emit s_instance->registrationFailed("register 方法不存在");
                emit s_instance->statusChanged();
            }, Qt::QueuedConnection);
            return;
        }

        // 保存 distributor
        QJniObject::callStaticMethod<void>(
            UP_CLASS,
            "saveDistributor",
            "(Landroid/content/Context;Ljava/lang/String;)V",
            ctx.object(),
            QJniObject::fromString(distributor).object());
        qDebug() << "[PushHandler] saveDistributor:" << distributor;

        // 设置活跃 distributor（onMessageNative 过滤用）
        QJniObject::callStaticMethod<void>(
            "io/fedlet/mobutil/PushServiceImpl",
            "setActiveDistributor",
            "(Ljava/lang/String;)V",
            QJniObject::fromString(distributor).object());
        QSettings().setValue("pushActiveDistributor", distributor);

        // 注册
        QJniObject::callStaticMethod<void>(
            UP_CLASS,
            "register",
            registerSig,
            ctx.object(),
            QJniObject::fromString(distInstance(distributor)).object(),
            QJniObject().object(),
            QJniObject().object());

        qDebug() << "[PushHandler] register sent to" << distributor;
    });
}

void PushHandler::switchDistributor(const QString& newDistributor)
{
    if (!s_instance) return;

    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([newDistributor]() {
        auto ctx = QNativeInterface::QAndroidApplication::context();

        // saveDistributor 已注释：不删除旧 distributor，保留 token
        // QJniObject::callStaticMethod<void>(
        //     UP_CLASS,
        //     "saveDistributor",
        //     "(Landroid/content/Context;Ljava/lang/String;)V",
        //     ctx.object(),
        //     QJniObject::fromString(newDistributor).object());

        // 直接替换 distributor（DELETE + INSERT，FK OFF 保留 token）
        QJniObject::callStaticMethod<jboolean>(
            "io/fedlet/mobutil/PushServiceImpl",
            "replaceDistributor",
            "(Landroid/content/Context;Ljava/lang/String;)Z",
            ctx.object(),
            QJniObject::fromString(newDistributor).object());
        qDebug() << "[PushHandler] replaceDistributor:" << newDistributor;

        QJniObject::callStaticMethod<void>(
            "io/fedlet/mobutil/PushServiceImpl",
            "setActiveDistributor",
            "(Ljava/lang/String;)V",
            QJniObject::fromString(newDistributor).object());
        QSettings().setValue("pushActiveDistributor", newDistributor);

        // 检查 instance 是否已注册，避免重复注册导致 topic 变化
        QString instance = distInstance(newDistributor);
        bool alreadyRegistered = false;
        {
            QJniObject ctxObj(ctx.object());
            QJniObject instObj = QJniObject::fromString(instance);
            alreadyRegistered = QJniObject::callStaticMethod<jboolean>(
                "io/fedlet/mobutil/PushServiceImpl",
                "isInstanceRegistered",
                "(Landroid/content/Context;Ljava/lang/String;)Z",
                ctxObj.object(), instObj.object());
        }
        qDebug() << "[PushHandler] switchDistributor instance:" << instance << "alreadyRegistered:" << alreadyRegistered;

        if (alreadyRegistered) {
            qDebug() << "[PushHandler] instance" << instance << "already registered, skip register";
        } else {
            // 注册
            const char* registerSig = "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V";
            if (checkMethod("register", registerSig)) {
                QJniObject::callStaticMethod<void>(
                    UP_CLASS,
                    "register",
                    registerSig,
                    ctx.object(),
                    QJniObject::fromString(instance).object(),
                    QJniObject().object(),
                    QJniObject().object());
                qDebug() << "[PushHandler] register sent to" << newDistributor;
            }
        }

        // 更新状态 + 保存活跃 distributor
        QMetaObject::invokeMethod(s_instance, [newDistributor, alreadyRegistered]() {
            s_instance->m_currentDistributor = newDistributor;
            s_instance->setRegistering(!alreadyRegistered);
            s_instance->setConnected(alreadyRegistered);
            if (alreadyRegistered) {
                emit s_instance->statusChanged();
            } else {
                emit s_instance->registrationSent();
                emit s_instance->statusChanged();
            }
        }, Qt::QueuedConnection);
    });
}

void PushHandler::stop()
{
    if (!s_instance) return;

    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
        auto ctx = QNativeInterface::QAndroidApplication::context();

        QJniObject saved = QJniObject::callStaticMethod<jobject>(
            UP_CLASS,
            "getSavedDistributor",
            "(Landroid/content/Context;)Ljava/lang/String;",
            ctx.object());
        QJniObject::callStaticMethod<void>(
            UP_CLASS,
            "unregister",
            "(Landroid/content/Context;Ljava/lang/String;)V",
            ctx.object(),
            QJniObject::fromString(distInstance(saved.toString())).object());

        qDebug() << "[PushHandler] unregistered";
    });

    delete s_instance;
    s_instance = nullptr;
}

static QString jstringToQString(JNIEnv* env, jstring js)
{
    if (!js) return {};
    const char* raw = env->GetStringUTFChars(js, nullptr);
    QString s = QString::fromUtf8(raw);
    env->ReleaseStringUTFChars(js, raw);
    return s;
}

extern "C" JNIEXPORT void JNICALL
Java_io_fedlet_mobutil_PushServiceImpl_onNewEndpointNative(
    JNIEnv* env, jobject /*thiz*/, jstring jEndpoint, jstring jInstance)
{
    QString endpoint = jstringToQString(env, jEndpoint);
    QString instance = jstringToQString(env, jInstance);
    qDebug() << "[PushHandler] new endpoint:" << endpoint << "instance:" << instance;

    QSettings s;
    s.setValue("pushDeviceEndpoint", endpoint);
    s.sync();

    if (s_instance) {
        s_instance->cancelRegistrationTimeout();
        // 从 saved distributor 获取当前使用的 distributor
        QJniObject saved = QJniObject::callStaticMethod<jobject>(
            UP_CLASS,
            "getSavedDistributor",
            "(Landroid/content/Context;)Ljava/lang/String;",
            QNativeInterface::QAndroidApplication::context().object());
        QString currentDist = saved.toString();

        QMetaObject::invokeMethod(s_instance, [endpoint, instance, currentDist]() {
            s_instance->setConnected(true);
            s_instance->setRegistering(false);
            if (!currentDist.isEmpty()) {
                s_instance->setCurrentDistributor(currentDist);
            }
            emit s_instance->pushReceived(endpoint, instance);
            emit s_instance->statusChanged();
        }, Qt::QueuedConnection);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_io_fedlet_mobutil_PushServiceImpl_onMessageNative(
    JNIEnv* env, jobject /*thiz*/, jbyteArray jMessage, jstring jInstance, jstring jDistributor)
{
    QByteArray message;
    if (jMessage) {
        jsize len = env->GetArrayLength(jMessage);
        message.resize(len);
        env->GetByteArrayRegion(jMessage, 0, len,
            reinterpret_cast<jbyte*>(message.data()));
    }
    QString instance = jstringToQString(env, jInstance);
    QString distributor = jstringToQString(env, jDistributor);

    QString activeDist = QSettings().value("pushActiveDistributor").toString();
    if (!activeDist.isEmpty() && distributor != activeDist) {
        s_ignoredMsgCount.fetchAndAddRelaxed(1);
        qDebug() << "[PushHandler] ignored msg from" << distributor
                 << ", total ignored:" << s_ignoredMsgCount.loadRelaxed();
        return;
    }

    qDebug() << "[PushHandler] message received, size:" << message.size()
             << "instance:" << instance;

    if (s_instance) {
        QMetaObject::invokeMethod(s_instance, [message, instance]() {
            emit s_instance->pushMessage(message, instance);
        }, Qt::QueuedConnection);
    }
}

int PushHandler::ignoredMessageCount()
{
    return s_ignoredMsgCount.loadRelaxed();
}

extern "C" JNIEXPORT void JNICALL
Java_io_fedlet_mobutil_PushServiceImpl_onRegistrationFailedNative(
    JNIEnv* env, jobject /*thiz*/, jstring jReason, jstring jInstance)
{
    QString reason = jstringToQString(env, jReason);
    QString instance = jstringToQString(env, jInstance);
    qWarning() << "[PushHandler] registration failed:" << reason
               << "instance:" << instance;

    if (s_instance) {
        s_instance->cancelRegistrationTimeout();
        QMetaObject::invokeMethod(s_instance, [reason]() {
            s_instance->setConnected(false);
            s_instance->setRegistering(false);
            emit s_instance->registrationFailed(reason);
            emit s_instance->statusChanged();
        }, Qt::QueuedConnection);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_io_fedlet_mobutil_PushServiceImpl_onUnregisteredNative(
    JNIEnv* env, jobject /*thiz*/, jstring jInstance)
{
    QString instance = jstringToQString(env, jInstance);
    qDebug() << "[PushHandler] unregistered, instance:" << instance;
}

void PushHandler::startRegistrationTimeout()
{
    if (m_regTimeoutTimerId) {
        killTimer(m_regTimeoutTimerId);
    }
    m_regTimeoutTimerId = startTimer(10000);
}

void PushHandler::cancelRegistrationTimeout()
{
    if (m_regTimeoutTimerId) {
        killTimer(m_regTimeoutTimerId);
        m_regTimeoutTimerId = 0;
    }
}

void PushHandler::timerEvent(QTimerEvent* event)
{
    if (event->timerId() == m_regTimeoutTimerId) {
        m_regTimeoutTimerId = 0;
        setConnected(false);
        setRegistering(false);
        qWarning() << "[PushHandler] registration timeout - distributor not responding";
        emit registrationFailed(QString::fromUtf8("推送注册超时，请检查 ntfy 是否在运行, 安装: %1").arg(ntfyshPushInstalled));
        emit statusChanged();
    }
}

#else

void PushHandler::start() {}
void PushHandler::stop() {}
PushHandler* PushHandler::instance() { return nullptr; }
bool PushHandler::isNtfyInstalled() { return false; }
bool PushHandler::isConnected() { return false; }
bool PushHandler::isRegistering() { return false; }
QString PushHandler::upDistributorDisplayName(const QString&) { return {}; }
QStringList PushHandler::installedDistributors() { return {}; }
// 桌面平台无 UnifiedPush 分发器概念，返回空表；
// SettingsPage 由此跳过已知分发器下拉项（仅剩 Auto/Gotify 选项）
QVector<QPair<QString,QString>> PushHandler::knownDistributors() { return {}; }
void PushHandler::setConnected(bool) {}
void PushHandler::setRegistering(bool) {}
void PushHandler::registerDevice() {}
void PushHandler::selectDistributor(const QString&) {}
void PushHandler::switchDistributor(const QString&) {}
PushProviderType PushHandler::providerType() const { return PushProviderType::UnifiedPush; }
QString PushHandler::currentDistributor() const { return {}; }
QString PushHandler::currentDistributorDisplayName() const { return {}; }
void PushHandler::setProviderType(PushProviderType) {}
void PushHandler::setCurrentDistributor(const QString&) {}
int PushHandler::ignoredMessageCount() { return 0; }
void PushHandler::startRegistrationTimeout() {}
void PushHandler::cancelRegistrationTimeout() {}
void PushHandler::timerEvent(QTimerEvent*) {}

#endif
