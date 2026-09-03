#include "shareintentreceiver.h"
#include "stickerstore.h"
#include "androidutils.h"
#include "dialogpopup.h"

#include <QGuiApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>
#include <QVariant>
#if defined(Q_OS_ANDROID)
#include <QJniObject>
#endif
#include <QVector>
#include <functional>

namespace {

struct PendingShare {
    enum Type { Image = 0, Generic = 1 };
    Type type = Generic;
    QByteArray bytes;
    QString mime;
    QString text;
    QString reason;
};

QMutex s_mutex;
QVector<PendingShare> s_queue;
std::function<void()> s_navigator;
std::function<QQuickItem*()> s_confirmHost;
bool s_draining = false;

// ── 第一步：待确认元信息（单笔，不重复）──
PendingShareMeta s_pendingMeta;
bool s_metaReady = false;

void processOne(const PendingShare& item)
{
    // 第二步填入：后台 importImageBytesAsync
    if (item.type == PendingShare::Image) {
        QString err;
        if (StickerStore::instance()->importImageBytes(item.bytes, &err)) {
            showAndroidToast(QStringLiteral("已导入到「粘贴板」"));
            auto nav = s_navigator;
            if (nav) nav();
        } else {
            showAndroidToast(err.isEmpty() ? QStringLiteral("导入失败") : err);
        }
    } else {
        showAndroidToast(item.text.isEmpty()
            ? QStringLiteral("共享内容已收到") : item.text);
    }
}

void runOnMainThread(std::function<void()> fn)
{
    auto* app = QCoreApplication::instance();
    if (!app) return;
    if (QThread::currentThread() == app->thread()) {
        fn();
    } else {
        QMetaObject::invokeMethod(app, std::move(fn), Qt::QueuedConnection);
    }
}

} // namespace

void registerShareNavigator(std::function<void()> navigateToStickerHome)
{
    QMutexLocker lock(&s_mutex);
    s_navigator = std::move(navigateToStickerHome);
}

void registerShareConfirmHost(std::function<QQuickItem*()> host)
{
    QMutexLocker lock(&s_mutex);
    s_confirmHost = std::move(host);
}

PendingShareMeta takePendingShareMeta()
{
    QMutexLocker lock(&s_mutex);
    PendingShareMeta m;
    if (s_metaReady) {
        m = std::move(s_pendingMeta);
        s_metaReady = false;
    }
    return m;
}

void drainPendingShareIntents()
{
    // 第一步：先处理待确认元信息（弹 ConfirmPopup）
    {
        QMutexLocker lock(&s_mutex);
        if (s_metaReady && s_confirmHost) {
            PendingShareMeta meta = std::move(s_pendingMeta);
            s_metaReady = false;
            lock.unlock(); // 释放锁，避免在持锁状态调 Qt 方法

            runOnMainThread([meta]() {
                auto hostFn = s_confirmHost;
                if (!hostFn) return;
                QQuickItem* host = hostFn();
                if (!host) return;

                // 大小格式化
                const auto fmtBytes = [](qint64 n) -> QString {
                    if (n < 1024) return QStringLiteral("%1 B").arg(n);
                    if (n < 1024 * 1024)
                        return QStringLiteral("%1 KB").arg(n / 1024.0, 0, 'f', 1);
                    if (n < 1024LL * 1024 * 1024)
                        return QStringLiteral("%1 MB").arg(n / (1024.0 * 1024), 0, 'f', 1);
                    return QStringLiteral("%1 GB").arg(n / (1024.0 * 1024 * 1024), 0, 'f', 2);
                };

                QString title = QStringLiteral("导入分享");
                QStringList lines;
                QString kind = meta.action.contains(QStringLiteral("SEND_MULTIPLE"))
                    ? QStringLiteral("多文件")
                    : QStringLiteral("文件");
                lines << QStringLiteral("收到 %1 张%2").arg(meta.imageCount).arg(kind);
                if (meta.totalBytes > 0)
                    lines << QStringLiteral("大小：%1").arg(fmtBytes(meta.totalBytes));
                if (!meta.mime.isEmpty())
                    lines << QStringLiteral("类型：%1").arg(meta.mime);
                lines << QStringLiteral("来自：%1").arg(meta.sourceApp);
                if (!meta.displayName.isEmpty())
                    lines << QStringLiteral("文件名：%1").arg(meta.displayName);
                if (meta.receivedAt > 0)
                    lines << QStringLiteral("时间：%1").arg(
                        QDateTime::fromMSecsSinceEpoch(meta.receivedAt)
                            .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));

                // ── 调试验证面板：双独立来源判定主 QtActivity 状态 ──
#if defined(Q_OS_ANDROID)
                const bool qtValid = QJniObject::callStaticMethod<jboolean>(
                    "org/qtproject/qt/android/QtNative", "isActivityValid", "()Z");
                const QJniObject st = QJniObject::callStaticObjectMethod(
                    "org/qtproject/qt/android/QtNative", "getStateDetails",
                    "()Lorg/qtproject/qt/android/QtNative$ApplicationStateDetails;");
                const bool qtStarted = st.isValid() && st.getField<jboolean>("isStarted");
                const bool lcRunning = QJniObject::callStaticMethod<jboolean>(
                    "io/fedlet/mobutil/AnystikApplication",
                    "isMainActivityRunning", "()Z");
                const bool lcVisible = QJniObject::callStaticMethod<jboolean>(
                    "io/fedlet/mobutil/AnystikApplication",
                    "isMainActivityVisible", "()Z");
                lines << QStringLiteral("[Qt内部] 实例%1 已启动%2")
                           .arg(qtValid ? QStringLiteral("存活") : QStringLiteral("无"))
                           .arg(qtStarted ? QStringLiteral("✓") : QStringLiteral("✗"));
                lines << QStringLiteral("[生命周期] %1 %2")
                           .arg(lcRunning ? QStringLiteral("运行")
                                          : QStringLiteral("未运行"))
                           .arg(lcVisible ? QStringLiteral("可见")
                                          : QStringLiteral("后台"));
#endif

                ConfirmPopup::show(host, title, lines.join(QStringLiteral("\n")),
                    QStringLiteral("继续处理"),
                    QStringLiteral("取消"),
                    [meta](bool accepted) {
                        if (accepted) {
                            showAndroidToast(QStringLiteral("处理中…"));
                            // 第二步占位：在此触发后台读字节+导入
                            // TODO: 第二步实现
                        } else {
                            showAndroidToast(QStringLiteral("已取消"));
                        }
                    });
            });
            return; // 元信息弹出后立即返回，不处理队列
        }
    }

    // 原有 image/generic 队列处理（第二步填入后台 import）
    for (;;) {
        QVector<PendingShare> items;
        {
            QMutexLocker lock(&s_mutex);
            if (s_queue.isEmpty()) {
                s_draining = false;
                break;
            }
            s_draining = true;
            items.swap(s_queue);
        }
        for (const auto& item : items) {
            runOnMainThread([item]() { processOne(item); });
        }
    }
}

// ── 统一收口：扫描 pending_shares 落盘元信息 → 弹确认框 ──
void scanPendingShareDir()
{
    auto* app = QCoreApplication::instance();
    if (!app) return;

    // Java 端用 context.getFilesDir()/pending_shares 落盘。Qt 的
    // AppDataLocation 在 Android 上的具体映射与 getFilesDir() 存在歧义，
    // 因此探测两个候选路径（AppDataLocation 本身 与 AppDataLocation/files）。
    const QString base = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    const QStringList candidates{
        base + QStringLiteral("/pending_shares"),
        base + QStringLiteral("/files/pending_shares"),
    };

    QString dir;
    for (const auto& c : candidates) {
        if (QFile::exists(c + QStringLiteral("/pending_meta.json"))) {
            dir = c;
            break;
        }
    }
    if (dir.isEmpty()) return;

    QFile metaFile(dir + QStringLiteral("/pending_meta.json"));
    if (!metaFile.open(QIODevice::ReadOnly)) {
        qWarning() << "[shareintentreceiver] open meta failed:" << dir;
        return;
    }
    const QByteArray raw = metaFile.readAll();
    metaFile.close();
    if (raw.isEmpty()) return;

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[shareintentreceiver] meta parse error:" << err.errorString();
        return;
    }

    const QJsonObject obj = doc.object();
    PendingShareMeta meta;
    meta.action     = obj.value(QStringLiteral("action")).toString();
    meta.mime       = obj.value(QStringLiteral("mime")).toString();
    meta.text       = obj.value(QStringLiteral("text")).toString();
    meta.sourceApp  = obj.value(QStringLiteral("sourceApp")).toString();
    meta.imageCount = obj.value(QStringLiteral("imageCount")).toInt();
    meta.receivedAt = obj.value(QStringLiteral("receivedAt")).toVariant().toLongLong();
    meta.totalBytes = obj.value(QStringLiteral("totalBytes")).toVariant().toLongLong();
    meta.displayName= obj.value(QStringLiteral("displayName")).toString();
    if (meta.sourceApp.isEmpty()) meta.sourceApp = QStringLiteral("未知来源");

    {
        QMutexLocker lock(&s_mutex);
        s_pendingMeta = std::move(meta);
        s_metaReady = true;
    }

    qDebug() << "[shareintentreceiver] pending meta loaded"
             << "action:" << meta.action
             << "mime:" << meta.mime
             << "count:" << meta.imageCount
             << "source:" << meta.sourceApp
             << "bytes:" << meta.totalBytes;

    drainPendingShareIntents();
}

#if defined(Q_OS_ANDROID)
#include <jni.h>
#include <QJsonDocument>
#include <QJsonArray>

static void enqueueAndDrain(PendingShare item)
{
    {
        QMutexLocker lock(&s_mutex);
        s_queue.append(item);
    }
    drainPendingShareIntents();
}

// ── 热启动通知：receiver 已把分享内容落盘，扫描并弹确认框 ──
extern "C" JNIEXPORT void JNICALL
Java_io_fedlet_mobutil_ShareActivity_notifyPendingShare(
    JNIEnv* /*env*/, jobject /*thiz*/)
{
    runOnMainThread([]() { scanPendingShareDir(); });
}

extern "C" JNIEXPORT void JNICALL
Java_io_fedlet_mobutil_ShareActivity_onShareIntentReceived(
    JNIEnv* env, jobject /*thiz*/,
    jstring jAction, jstring jMimeType,
    jstring jText, jstring jUris)
{
    auto toStr = [env](jstring js) -> QString {
        if (!js) return {};
        const char* raw = env->GetStringUTFChars(js, nullptr);
        QString s = QString::fromUtf8(raw);
        env->ReleaseStringUTFChars(js, raw);
        return s;
    };

    QString action   = toStr(jAction);
    QString mimeType = toStr(jMimeType);
    QString text     = toStr(jText);
    QString urisJson = toStr(jUris);

    qDebug() << "[shareintentreceiver] action:" << action
             << "mime:" << mimeType;

    if (!urisJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(urisJson.toUtf8());
        int count = doc.array().size();
        PendingShare item;
        item.type = PendingShare::Generic;
        item.mime = mimeType;
        item.text = QStringLiteral("收到 %1 个共享文件").arg(count);
        enqueueAndDrain(item);
    } else if (!text.isEmpty()) {
        PendingShare item;
        item.type = PendingShare::Generic;
        item.mime = mimeType;
        item.text = text;
        enqueueAndDrain(item);
    } else {
        PendingShare item;
        item.type = PendingShare::Generic;
        item.mime = mimeType;
        item.text = QStringLiteral("共享: %1").arg(mimeType);
        enqueueAndDrain(item);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_io_fedlet_mobutil_ShareActivity_onShareImageReceived(
    JNIEnv* env, jobject /*thiz*/,
    jbyteArray jImageBytes, jstring jMimeType)
{
    PendingShare item;
    item.type = PendingShare::Image;

    if (jImageBytes) {
        const jsize len = env->GetArrayLength(jImageBytes);
        if (len > 0) {
            item.bytes.resize(int(len));
            env->GetByteArrayRegion(jImageBytes, 0, len,
                reinterpret_cast<jbyte*>(item.bytes.data()));
        }
    }

    if (jMimeType) {
        const char* raw = env->GetStringUTFChars(jMimeType, nullptr);
        item.mime = QString::fromUtf8(raw);
        env->ReleaseStringUTFChars(jMimeType, raw);
    }

    qDebug() << "[shareintentreceiver] image bytes:" << item.bytes.size()
             << "mime:" << item.mime;

    enqueueAndDrain(item);
}

#endif // Q_OS_ANDROID