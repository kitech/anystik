#include "shareintentreceiver.h"
#include "stickerstore.h"
#include "androidutils.h"

#include <QGuiApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QMutex>
#include <QThread>
#include <QVariant>
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
bool s_draining = false;

void processOne(const PendingShare& item)
{
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

void drainPendingShareIntents()
{
    // 循环 drain 直至队列空：修复逐张入队时 s_draining 期间新入队件被丢弃的问题
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