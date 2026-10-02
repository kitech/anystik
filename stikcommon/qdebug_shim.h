#ifndef QLSTIK_QDEBUG_SHIM_H
#define QLSTIK_QDEBUG_SHIM_H

// Qt3 无 qInfo()/qWarning() 流（qInfo Qt 5.5、QDebug 流 Qt 4.3 才引入）。
// 提供最小流对象：把 << 拼接进 QCString，析构时抛给 Qt3 的
// qDebug(const QCString&) / qWarning(const QCString&) 重载（qglobal.h 962/970，
// 原生存在，**原样透传字节**，故 UTF-8 中文不损）。
//
// ⚠ 为什么内部用 QCString 累加而不是 QString（实测 /opt/qt338sh 定性）：
//   qDebug(const QString&) 在 Qt3 里会按**本地 8bit 编码**再转一次才输出，
//   正确解码的 QString 反而被破坏：
//     qDebug(QString::fromUtf8("中文")) → 处理器收到乱码字节
//     qDebug(QCString("中文"))          → 原样透传，字节正确
//   故本类全程以 UTF-8 字节累积、析构时走 QCString 通道，绝不在中途转码。
//   这也意味着**不要**在本文件里用 QString::fromLatin1 处理日志文本 —— 它会
//   把 UTF-8 多字节拆成 Latin1 码点，串起来就是乱码。const char* 一律
//   fromUtf8（解码正确，且因析构走 QCString 通道，不会被二次破坏）。
//
// ⚠ 另一处 Qt3 陷阱：本类内部**必须**用 QString 累加，不能改成 QCString。
//   Qt3 的 QCString 只有 operator+=(const char*) 与 (char)
//   （qcstring.h:222-223），没有 operator+=(const QCString&)，只能靠
//   operator const char*（qcstring.h:221）隐式转换；而该转换返回**内部共享
//   缓冲**，连续追加互相覆盖。实测 /opt/qt338sh：
//     QCString b("z"), c; c += b; c += b;  →  c.length() == 2（应为 4）
//   静默丢一半数据、无任何报错。QString 的 += 语义正确。

#if QT_VERSION < 0x040000
#include <qglobal.h>
#include <qstring.h>
#include <qcstring.h>   // Qt3 的 QByteArray 类定义在此（无独立 qbytearray.h）
#include <stdarg.h>
#include <stdio.h>
#include "qstring_shim.h"
#include "qglobaltype_shim.h"
#include "qba_shim.h"

// ⚠ Qt3 的 qDebug(const QCString&) 有**内部 8KB 栈缓冲上限**，超出即破坏栈。
//   实测 /opt/qt338sh（gdb 帧 tools/qglobal.cpp:529）：内容长度
//     8000 → 存活；8100/8150 → 存活；8180 → "stack smashing detected" SIGABRT
//   即阈值在 8150~8180 之间（约 8KB 减去前缀开销）。这是 **Qt3 库自身的缺陷**
//   —— 调用方没有做错任何事，且 Qt5+ 的 QDebug 已无此问题。
//   故本垫片所有输出都经 emit() 分段送出，每段 ≤ kQt3LogChunkLimit。
//   对调用方不可见：日志内容与顺序不变，只是分成多条消息打印 —— 这也是
//   Qt3 时代长日志的常规做法。
//   ⚠ 上限取 4096（4KB）而非贴近 8192：给库内部前缀/对齐留足余量。
static const int kQt3LogChunkLimit = 4096;

// 分段送 qDebug/warning。text 已是 UTF-8 字节。
inline void emitChunked(bool warn, const QCString& text)
{
    const int total = text.length();
    if (total <= kQt3LogChunkLimit) {
        if (warn) qWarning(text); else qDebug(text);
        return;
    }
    for (int off = 0; off < total; off += kQt3LogChunkLimit) {
        const int n = qMin(kQt3LogChunkLimit, total - off);
        // ⚠ 切段用 QCString(const char*, maxlen)，maxlen = n + 1
        //   （该构造的 length() 返回 maxlen - 1，见 qInfo 内的说明）。
        const QCString part(text.data() + off, (uint)n + 1);
        if (warn) qWarning(part); else qDebug(part);
    }
}

class Qt3LogStream
{
public:
    explicit Qt3LogStream(bool warn) : m_warn(warn) {}
    ~Qt3LogStream()
    {
        // ⚠ 必须走 QCString 重载（qglobal.h:962），不能用 QString 版（961）。
        //   实测 /opt/qt338sh：qDebug(const QString&) 在 Qt3 里按**本地 8bit
        //   编码**再转一次才输出，正确解码的 QString 反而被破坏 ——
        //     qDebug(QString::fromUtf8("中文")) → 处理器收到乱码字节
        //     qDebug(QCString("中文"))          → 原样透传，字节正确
        //   故析构时把 QString 取 utf8() 字节、交 QCString 直通。
        //
        // ⚠ 为什么内部仍用 QString 累加，而不是全程 QCString（实测踩过）：
        //   Qt3 的 QCString 只有 operator+=(const char*) / (char)
        //   （qcstring.h:222-223），没有 operator+=(const QCString&)，
        //   只能靠 operator const char*（qcstring.h:221）隐式转换 —— 而
        //   该转换返回内部共享缓冲，连续追加会互相覆盖。实测
        //     QCString c; c += b; c += b;  → c.length() == 2（应为 4）
        //   即静默丢数据。QString 的 += 语义正确，只是最终输出要转字节。
        //
        // ⚠ 输出经 emitChunked() 分段，规避 Qt3 qDebug 的 8KB 栈缓冲上限
        //   （实测 8180 字节即 SIGABRT），长日志不会崩栈。
        emitChunked(m_warn, QCString(m_acc.utf8()));
    }
    Qt3LogStream& noquote() { return *this; }
    Qt3LogStream& operator<<(const QString& v) { m_acc += v; return *this; }
    // ⚠ 用 fromUtf8 而非 fromLatin1：调用点传的多半是**源码里的中文字面量**，
    //   那些字节是 UTF-8。fromLatin1 会把多字节序列逐字节拆成 Latin1 码点，
    //   串起来就是乱码，且因为析构走 QCString 通道、不会二次破坏，
    //   这里解码正确就能一路正确到底。
    //   （本仓规范：非 ASCII 一律走 utf8，AGENTS.md）
    Qt3LogStream& operator<<(const char* v)
    {
        if (!v) return *this;
        m_acc += QString::fromUtf8(v);
        return *this;
    }
    // ⚠ QLatin1String 是 qstring_shim.h:76 垫出来的最小类（Qt3 无此类，
    //   Qt4.0 才引入）。它只有 operator QString()（内部 fromLatin1），
    //   没有 Qt4 原生的 latin1()/data() 成员 —— 别照 Qt4 写法调。
    Qt3LogStream& operator<<(QLatin1String v) { m_acc += QString(v); return *this; }
    Qt3LogStream& operator<<(int v)
    {
        m_acc += QString::number((long)v);
        return *this;
    }
    Qt3LogStream& operator<<(unsigned int v)
    {
        m_acc += QString::number((ulong)v);
        return *this;
    }
    Qt3LogStream& operator<<(quint64 v)
    {
        m_acc += QString::number((Q_ULLONG)v);
        return *this;
    }
    Qt3LogStream& operator<<(qint64 v)   // qglobaltype_shim 的 qint64
    {
        m_acc += QString::number((Q_LLONG)v);
        return *this;
    }
    Qt3LogStream& operator<<(bool v)
    {
        m_acc += v ? QString::fromUtf8("true")
                   : QString::fromUtf8("false");
        return *this;
    }

private:
    bool m_warn;
    QString m_acc;   // ⚠ 保持 QString 累加（QCString 的 += 会静默丢数据，见析构说明）
};

inline Qt3LogStream qInfo() { return Qt3LogStream(false); }
inline Qt3LogStream qWarning() { return Qt3LogStream(true); }

// ── qDebug() 零参流（Qt4.3 引入 QDebug << 流，Qt3 无）──────────────────
// Qt3 只原生有 `qDebug(const QString&)` / `qDebug(const QCString&)` /
// `qDebug(const char*, ...)`（qglobal.h:961-963），**没有**可链式的零参
// qDebug() 流。stickerstore.cpp 的 3 处 `qDebug() << ...` 需要它。
inline Qt3LogStream qDebug() { return Qt3LogStream(false); }

// ── qInfo() 的 printf 风格重载（Qt3 完全没有 qInfo）─────────────────────
// Qt3 没有 qInfo 的任何形式（qglobal.h 961-978 只有 qDebug/qWarning/qFatal）。
// stickerstore.cpp 有 13 处 `qInfo("fmt=%d", ...)` 这类 printf 风格调用，
// 而本垫片的 `qInfo()` 是零参流，无法接收变参 —— 故补一个变参重载转发。
//
// 输出通道差异（如实说明，非静默降级）：Qt5+ 的 qInfo 默认进 **stdout**，
// qDebug/qWarning 进 stderr，三者分流；Qt3 只有一个默认消息处理器，
// qDebug 与 qWarning 都落到同一通道（stdout，见 qglobal.cpp 的
// qDefaultDebugMsgHandler → fprintf(stdout)）。所以 Qt3 侧把 qInfo 转发给
// qDebug 后，info 与 warning 日志会混在同一 stdout 流里，**无法**保持
// Qt5+ 的分流。这是 Qt3 消息系统能力不足导致的必然差异，已在此显式记录；
// 日志内容与顺序不变，只是不再按级别分文件/分通道。若日后需要分流，
// 可改为自行 vfprintf 到 stdout 并给 warning 走 stderr。
//
// 实现要点：C++ **不能**把 `...` 直接转发给另一个变参函数（无 va_list 提取），
// 必须自己用 va_list + vsnprintf 拼串。本垫式因此自己格式化，格式化语义与
// libc 的 qDebug/vfprintf 完全一致（同为 vsnprintf，%lld/%s 等修饰符由 libc 处理）。
// 不校验格式串与实参是否匹配 —— 与 Qt3 原生 qDebug 一致（越界写法是调用方的错）。
// ⚠ 输出经 emitChunked() 分段，规避 Qt3 qDebug(const QCString&) 的 8KB 栈
//   缓冲上限（实测 8180 字节即 SIGABRT，见 emitChunked 上方的完整说明）。

inline void qInfo(const char* fmt, ...)
{
    if (!fmt) return;
    char stackBuf[1024];
    va_list ap;
    va_start(ap, fmt);
    const int need = vsnprintf(stackBuf, sizeof(stackBuf), fmt, ap);
    va_end(ap);
    if (need < 0) return;                 // 格式串非法：丢弃（原生 vfprintf 亦无输出）

    // 拼出完整文本（小输出走栈，超大走堆），再分段送 qDebug。
    QCString text;
    if (need < (int)sizeof(stackBuf)) {  // 装得下
        // ⚠ 两个 Qt3 坑（均实测 /opt/qt338sh）：
        //   ① QCString(const char*, uint maxlen)（qcstring.h:138）的 length()
        //      返回 **maxlen - 1** —— 实测 QCString("abc",3).length()==2，
        //      QCString("abc",4).length()==3。故 maxlen 必须传 strlen+1。
        //   ② maxlen 传 0 **不是**空串：实测 QCString("abc",0).length()==3
        //      （走 strlen 兜底）。need==0 时走下面的空串分支。
        if (need == 0) text = QCString("");
        else           text = QCString(stackBuf, (uint)need + 1);
    } else {
        // 超出栈缓冲：按需精确分配后重跑一次（避免无界栈占用）
        // qbaUninit 是 Qt3 的 QByteArray(size) 分配构造（两参 QByteArray(int,int)
        // 在 Qt3 是 protected，外部不可用）；qbaConstData 取裸指针。
        QByteArray heapBuf = qbaUninit(need + 1);
        // qbaConstData 返回 const char*（与 Qt4+ 一致），但这里 heapBuf 是独占的
        // 局部可变缓冲，去掉 const 只是为交给 vsnprintf 的 char* 形参，不改其内容语义。
        char* out = const_cast<char*>(qbaConstData(heapBuf));
        va_start(ap, fmt);
        vsnprintf(out, (size_t)need + 1, fmt, ap);
        va_end(ap);
        // maxlen 含终止符，故传 need + 1（不是 need）
        text = QCString(qbaConstData(heapBuf), (uint)need + 1);
    }

    // 分段输出，规避 Qt3 qDebug(const QCString&) 的 8KB 栈缓冲上限
    emitChunked(false, text);
}

// 同理：Qt3 的 qWarning(const char*, ...) 已原生存在（qglobal.h:971），
// 故 printf 风格 qWarning 无需垫式；上面的 qWarning() 零参流与之共存，
// 按实参个数/类型重载选择（qWarning("x=%d", 1) → 原生；qWarning() << .. → 流）。

#endif // QT_VERSION < 0x040000

#endif // QLSTIK_QDEBUG_SHIM_H