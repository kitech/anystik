#ifndef QLSTIK_QDEBUG_SHIM_H
#define QLSTIK_QDEBUG_SHIM_H

// Qt3 无 qInfo()/qWarning() 流（qInfo Qt 5.5、QDebug 流 Qt 4.3 才引入）。
// 提供最小流对象：把 << 拼接进 QString，析构时抛给 Qt3 的 qDebug(const QString&)
// / qWarning(const QString&) 重载（qglobal.h 961/969 行，原生存在，UTF-8 直接打印）。

#if QT_VERSION < 0x040000
#include <qglobal.h>
#include <qstring.h>
#include <qcstring.h>   // Qt3 的 QByteArray 类定义在此（无独立 qbytearray.h）
#include <stdarg.h>
#include <stdio.h>
#include "qstring_shim.h"
#include "qglobaltype_shim.h"
#include "qba_shim.h"

class Qt3LogStream
{
public:
    explicit Qt3LogStream(bool warn) : m_warn(warn) {}
    ~Qt3LogStream()
    {
        if (m_warn) {
            qWarning(m_acc);
        } else {
            qDebug(m_acc);
        }
    }
    Qt3LogStream& noquote() { return *this; }
    Qt3LogStream& operator<<(const QString& v) { m_acc += v; return *this; }
    Qt3LogStream& operator<<(const char* v)
    {
        m_acc += QString::fromLatin1(v);
        return *this;
    }
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
        m_acc += v ? QString::fromLatin1("true")
                   : QString::fromLatin1("false");
        return *this;
    }

private:
    bool m_warn;
    QString m_acc;
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
inline void qInfo(const char* fmt, ...)
{
    if (!fmt) return;
    char stackBuf[1024];
    va_list ap;
    va_start(ap, fmt);
    const int need = vsnprintf(stackBuf, sizeof(stackBuf), fmt, ap);
    va_end(ap);
    if (need < 0) return;                 // 格式串非法：丢弃（原生 vfprintf 亦无输出）
    if (need < (int)sizeof(stackBuf)) {  // 装得下
        qDebug(QString::fromUtf8(stackBuf, need));
        return;
    }
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
    qDebug(QString::fromUtf8(qbaConstData(heapBuf), need));
}

// 同理：Qt3 的 qWarning(const char*, ...) 已原生存在（qglobal.h:971），
// 故 printf 风格 qWarning 无需垫式；上面的 qWarning() 零参流与之共存，
// 按实参个数/类型重载选择（qWarning("x=%d", 1) → 原生；qWarning() << .. → 流）。

#endif // QT_VERSION < 0x040000

#endif // QLSTIK_QDEBUG_SHIM_H