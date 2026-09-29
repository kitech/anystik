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

// ── manager 发送者（QWebdav 继承 QNetworkAccessManager）──
// vendor qwebdav.cpp:67-68 连的是 manager 自身的两个信号：
//   connect(this, &QWebdav::finished, this, &QWebdav::replyFinished)
//   connect(this, &QWebdav::authenticationRequired, this, &QWebdav::provideAuthenication)
// 取址得到的是 QNetworkAccessManager 的成员，故按 manager 槽表路由。
// 发送者写 QNetworkAccessManager*，QWebdav* 会隐式转换上来。
template <typename Receiver, typename Func>
void connect(QNetworkAccessManager* sender,
             void (QNetworkAccessManager::*sig)(QNetworkReply*),
             Receiver* ctx,
             Func fn)
{
    (void)ctx;
    (void)sig;
    sender->slots_finished().push_back(std::function<void(QNetworkReply*)>(fn));
}

template <typename Receiver, typename Func>
void connect(QNetworkAccessManager* sender,
             void (QNetworkAccessManager::*sig)(QNetworkReply*, QAuthenticator*),
             Receiver* ctx,
             Func fn)
{
    (void)ctx;
    (void)sig;
    sender->slots_authRequired().push_back(
        std::function<void(QNetworkReply*, QAuthenticator*)>(fn));
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

// ── sslErrors（QWebdav::sslErrors 接受链）──
template <typename Receiver, typename Func>
void connect(QNetworkReply* sender,
             void (QNetworkReply::*sig)(const QList<QSslError>&),
             Receiver* ctx,
             Func fn)
{
    (void)ctx;
    (void)sig;
    sender->slots_sslErrors().push_back(std::function<void(const QList<QSslError>&)>(fn));
}

// ── 成员函数指针槽（vendor qwebdav 的写法）──
// 上面几个重载只接 lambda；vendor 传的是 &QWebdav::replyFinished 这种成员函数
// 指针，需要把接收者 ctx 绑上才能变成可调用对象。
template <typename C, typename F>
inline std::function<void()> qBindVoid(C* ctx, F fn)
{
    return [ctx, fn]() { (ctx->*fn)(); };
}

template <typename C, typename A, typename F>
inline std::function<void(A)> qBind1(C* ctx, F fn)
{
    return [ctx, fn](A a) { (ctx->*fn)(a); };
}

template <typename C, typename A, typename B, typename F>
inline std::function<void(A, B)> qBind2(C* ctx, F fn)
{
    return [ctx, fn](A a, B b) { (ctx->*fn)(a, b); };
}

template <typename Receiver, typename Class>
void connect(QNetworkReply* sender, void (QNetworkReply::*sig)(), Receiver* ctx,
             void (Class::*fn)())
{
    (void)sig;
    // 无参信号：finished / readyRead / metaDataChanged 内部按信号名分表，
    // 这里统一注册到 finished 表（调用方用哪个信号已由成员函数签名区分不了，
    // 故以 sig 取址分派，与上面 lambda 版保持一致）。
    if (sig == &QNetworkReply::readyRead) {
        sender->slots_readyRead().push_back(qBindVoid(ctx, fn));
    } else {
        sender->slots_finished().push_back(qBindVoid(ctx, fn));
    }
}

template <typename Receiver, typename Class>
void connect(QNetworkReply* sender,
             void (QNetworkReply::*sig)(qint64, qint64), Receiver* ctx,
             void (Class::*fn)(qint64, qint64))
{
    (void)sig;
    sender->slots_downloadProgress().push_back(qBind2<Receiver, qint64, qint64>(ctx, fn));
}

template <typename Receiver, typename Class>
void connect(QNetworkReply* sender,
             void (QNetworkReply::*sig)(QNetworkReply::NetworkError), Receiver* ctx,
             void (Class::*fn)(QNetworkReply::NetworkError))
{
    (void)sig;
    sender->slots_error().push_back(
        qBind1<Receiver, QNetworkReply::NetworkError>(ctx, fn));
}

template <typename Receiver, typename Class>
void connect(QNetworkReply* sender, void (QNetworkReply::*sig)(const QUrl&),
             Receiver* ctx, void (Class::*fn)(const QUrl&))
{
    (void)sig;
    sender->slots_redirected().push_back(qBind1<Receiver, const QUrl&>(ctx, fn));
}

template <typename Receiver, typename Class>
void connect(QNetworkReply* sender,
             void (QNetworkReply::*sig)(const QList<QSslError>&), Receiver* ctx,
             void (Class::*fn)(const QList<QSslError>&))
{
    (void)sig;
    sender->slots_sslErrors().push_back(
        qBind1<Receiver, const QList<QSslError>&>(ctx, fn));
}

template <typename Receiver, typename Class>
void connect(QNetworkAccessManager* sender,
             void (QNetworkAccessManager::*sig)(QNetworkReply*), Receiver* ctx,
             void (Class::*fn)(QNetworkReply*))
{
    (void)sig;
    sender->slots_finished().push_back(qBind1<Receiver, QNetworkReply*>(ctx, fn));
}

template <typename Receiver, typename Class>
void connect(QNetworkAccessManager* sender,
             void (QNetworkAccessManager::*sig)(QNetworkReply*, QAuthenticator*),
             Receiver* ctx,
             void (Class::*fn)(QNetworkReply*, QAuthenticator*))
{
    (void)sig;
    sender->slots_authRequired().push_back(
        qBind2<Receiver, QNetworkReply*, QAuthenticator*>(ctx, fn));
}

// ── disconnect（vendor qwebdav.cpp:180-181 收尾时解两个连接）──
// 槽参数类型刻意放宽（Func 泛型）：Qt 原生 disconnect 只按**信号**移除连接，
// 不校验槽签名，而 vendor 正是把无参 replyReadyRead 挂在 redirectAllowed 上解。
// 垫片无连接 id，按「清空对应槽表」处理；QWebdav 每轮新建实例，语义等价。
template <typename Receiver, typename Func>
void disconnect(QNetworkReply* sender, void (QNetworkReply::*sig)(), Receiver*, Func)
{
    if (sig == &QNetworkReply::readyRead) {
        sender->slots_readyRead().clear();
    } else {
        sender->slots_finished().clear();
    }
}

template <typename Receiver, typename Func>
void disconnect(QNetworkReply* sender,
                void (QNetworkReply::*sig)(QNetworkReply::NetworkError), Receiver*, Func)
{
    (void)sig;
    sender->slots_error().clear();
}

template <typename Receiver, typename Func>
void disconnect(QNetworkReply* sender, void (QNetworkReply::*sig)(const QUrl&),
                Receiver*, Func)
{
    (void)sig;
    sender->slots_redirected().clear();
}

#endif // QCONNECT_SLOTS_H
