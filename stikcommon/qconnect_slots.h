#ifndef QCONNECT_SLOTS_H
#define QCONNECT_SLOTS_H

// Qt3 新式 connect 垫片：让共享模块的
//
//     connect(sender, &Sender::signal, this, [this](...) {...});
//
// 在 Qt3 分支可用。Qt3 的 QObject::connect 只有 SIGNAL()/SLOT() 字符串宏形式，
// 不支持成员函数指针 + lambda。本头在 Qt3 下提供一组合模板重载，把
// "成员函数指针 → QNetworkReply 槽注册表" 转发；Qt4+ 走原生 connect，不包含本头。
//
// 覆盖 QNAM 系的信号面（按 qconnect_slots: 全批次 3 实际用法）：
//   finished / readyRead                    无参
//   uploadProgress(qint64,qint64)           两参
//   downloadProgress(qint64,qint64)         两参
//   error(QNetworkReply::NetworkError)      一参
//   redirected(const QUrl&)                 一参
//
// 用法：共享模块在 QT3_BUILD 分支 #include "qconnect_slots.h"（在 QNetworkReply
// 已具名后）。模板经 ADL（参数类型 QNetworkReply*）自动选中，不需额外声明。

#include "qnam_shim.h"
#include <qobject.h>
#include <functional>

// ── 无参信号（finished/readyRead/metaDataChanged）──
// 三者同签名，按函数指针值区分（非 inline 外部函数地址唯一）。
template <typename Receiver, typename Func>
void connect(QNetworkReply* sender,
             void (QNetworkReply::*sig)(),
             Receiver* ctx,
             Func fn)
{
    (void)ctx;
    if (sig == &QNetworkReply::finished) {
        sender->slots_finished().push_back(std::function<void()>(fn));
    } else if (sig == &QNetworkReply::readyRead) {
        sender->slots_readyRead().push_back(std::function<void()>(fn));
    } else {
        // metaDataChanged：无专门槽表，忽略（调用方几乎不用）
    }
}

// ── 两参信号（uploadProgress/downloadProgress）──
// upload 与 download 同签名，必须按函数指针值区分（非 inline 外部函数地址
// 在 C++14 下保证唯一，见 qnam_shim.h QNetworkReply 信号注释）。
template <typename Receiver, typename Func>
void connect(QNetworkReply* sender,
             void (QNetworkReply::*sig)(qint64, qint64),
             Receiver* ctx,
             Func fn)
{
    (void)ctx;
    if (sig == &QNetworkReply::uploadProgress) {
        sender->slots_uploadProgress().push_back(
            std::function<void(qint64,qint64)>(fn));
    } else {
        sender->slots_downloadProgress().push_back(
            std::function<void(qint64,qint64)>(fn));
    }
}

// ── 一参 error ──
template <typename Receiver, typename Func>
void connect(QNetworkReply* sender,
             void (QNetworkReply::*sig)(QNetworkReply::NetworkError),
             Receiver* ctx,
             Func fn)
{
    (void)sig; (void)ctx;
    sender->slots_error().push_back(std::function<void(QNetworkReply::NetworkError)>(fn));
}

// ── 一参 redirected ──
template <typename Receiver, typename Func>
void connect(QNetworkReply* sender,
             void (QNetworkReply::*sig)(const QUrl&),
             Receiver* ctx,
             Func fn)
{
    (void)sig; (void)ctx;
    sender->slots_redirected().push_back(std::function<void(const QUrl&)>(fn));
}

#endif // QCONNECT_SLOTS_H