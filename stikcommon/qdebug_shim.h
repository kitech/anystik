#ifndef QLSTIK_QDEBUG_SHIM_H
#define QLSTIK_QDEBUG_SHIM_H

// qDebug/qInfo/qWarning/qCritical/qFatal 垫片 —— **同时支持 Qt3 与 Qt4 两个版本**。
//
// 命名：本垫片面向 Qt3 → Qt4 两个版本，故内部符号一律用 Qt34 / qt34 前缀
//   （与 qlcomp 的 compat34.h / compatcore34.h 同一约定）。
//
// 目标：把「新版 Qt 的日志接口」在旧 Qt 上补齐，且**行为不退化成旧接口**：
//   · 五个级别齐全：Debug / Info / Warning / Critical / Fatal，不互相合并降级；
//   · 流式（`qInfo() << a << b`）与 printf 风格（`qInfo("%d", n)`）都能用；
//   · 每条消息带**真实调用点的 file:line 与函数名**，而不是 [?]/[from qDebug]。
//
// 各版本缺口：
//   Qt3（< 0x040000）：没有 QDebug 流（Qt4.3 才有）、没有 qInfo（Qt5.5 才有）、
//     没有 qCritical（Qt4 才有）；只有 printf 风格的 qDebug/qWarning/qFatal，
//     且不知道调用点。→ 本垫片在 Qt3 下补全**全部五个级别**（流 + printf）。
//   Qt4（0x040000 ~ 0x050000）：qDebug/qWarning/qCritical 的流与 printf 都已原生
//     存在，唯独缺 qInfo。→ Qt4 下**只补 qInfo**，其余沿用 Qt4 原生，绝不改写
//     既有行为（Qt4 的 QDebug 运算符覆盖远比本垫片全，覆盖它只会徒增风险）。
//
// 调用点如何带进处理器：与 Qt5 相同的对象式宏
//     #define qInfo QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info
//   宏在**调用点**展开，故 file/line/func 就是真实调用位置。处理器侧 Qt3 与 Qt4
//   都没有 QMessageLogContext，故把调用点展平成 C 参数传进处理器（见下）。
//
// ⚠ 历史：旧垫片只能打印 `[from qDebug]`，调用点靠 qthooks.cpp 的 backtrace 猜；
//   且 qInfo 被实现成 qDebug、无 critical。本版全部纠正。

// ⚠ QT_VERSION 由 <qglobal.h> 定义。本头可能被不先包含任何 Qt 头的 TU 直接
//   包含，若不先取到 QT_VERSION，下面的版本分支会把它当成 0（=Qt 史前），
//   从而在 Qt4/5/6 上走错分支（曾实测：Qt4 下误走 Qt3 分支去 include qcstring.h）。
#ifndef QT_VERSION
#include <qglobal.h>
#endif

#if QT_VERSION < 0x050000

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if QT_VERSION < 0x040000
#include <qglobal.h>
#include <qstring.h>
#include <qcstring.h>   // Qt3 的 QByteArray 类定义在此（无独立 qbytearray.h）
#else
#include <qglobal.h>
#include <qstring.h>
#include <qbytearray.h>
#endif

#include "qstring_shim.h"
#include "qglobaltype_shim.h"
#include "qba_shim.h"

// 分段送出。Qt3 的 qDebug(const QCString&) 有**内部 8KB 栈缓冲上限**，超出即
// 破坏栈。实测 /opt/qt338sh（gdb 帧 tools/qglobal.cpp:529）：内容长度
//   8000 → 存活；8100/8150 → 存活；8180 → "stack smashing detected" SIGABRT
// 即阈值在 8150~8180 之间（约 8KB 减去前缀开销）。这是 **Qt3 库自身的缺陷**。
// 故本垫片所有输出都分段送出，每段 ≤ kQt34LogChunkLimit。
//   ⚠ 上限取 4096（4KB）而非贴近 8192：给库内部前缀/对齐留足余量。
static const int kQt34LogChunkLimit = 4096;

// ── 级别：模拟 Qt5 的 QtMsgType ────────────────────────────────────────
// Qt3 只有 {QtDebugMsg, QtWarningMsg, QtFatalMsg}（qglobal.h:1017），无 info、
// 无 critical；Qt4 有 {QtDebugMsg, QtWarningMsg, QtCriticalMsg, QtFatalMsg}
// （qglobal.h:1996），仍无 info（QtInfoMsg 是 Qt5.5）。这里统一补全五级，
// 与 Qt5 一一对应，级别不再被合并降级。
enum Qt34LogLevel { Qt34Debug, Qt34Info, Qt34Warning, Qt34Critical, Qt34Fatal };

// ── 消息处理器：模拟 Qt5 的 QtMessageHandler ──────────────────────────
// Qt5 签名 void(QtMsgType, const QMessageLogContext&, const QString&)。Qt3/Qt4
// 都没有 QMessageLogContext（Qt4 的 QtMsgHandler 只有 (QtMsgType, const char*)），
// 故把调用点展平为 C 参数（file/line/func）。
typedef void (*Qt34MessageHandler)(Qt34LogLevel level,
                                   const char* file, int line, const char* func,
                                   const char* utf8msg);

// 默认处理器：统一格式 [HH:MM:SS] [LEVEL] file:line func  msg（UTF-8 直写
// stderr）。级别各自有独立 label，info 不再落成 DEBUG。
inline void qt34DefaultMessageHandler(Qt34LogLevel level,
                                      const char* file, int line, const char* func,
                                      const char* utf8msg)
{
    const char* label = "DEBUG";
    const char* color = "\033[2m";
    switch (level) {
        case Qt34Debug:    label = "DEBUG"; color = "\033[2m";    break;
        case Qt34Info:     label = "INFO";  color = "\033[1;32m"; break;
        case Qt34Warning:  label = "WARN";  color = "\033[1;33m"; break;
        case Qt34Critical: label = "ERROR"; color = "\033[1;31m"; break;
        case Qt34Fatal:    label = "FATAL"; color = "\033[1;31m"; break;
    }
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char tbuf[16];
    strftime(tbuf, sizeof(tbuf), "%H:%M:%S", t);
    const bool tty = isatty(fileno(stderr)) != 0;
    fprintf(stderr, "%s[%s]%s %s[%s]%s %s:%d %s  %s\n",
            tty ? "\033[2m" : "", tbuf, tty ? "\033[0m" : "",
            tty ? color : "", label, tty ? "\033[0m" : "",
            file ? file : "?", line, func ? func : "?",
            utf8msg ? utf8msg : "");
    fflush(stderr);
    if (level == Qt34Fatal) abort();
}

// 处理器槽：inline 函数内的静态量，ODR 保证全程序单实例（C++98 起可用）。
inline Qt34MessageHandler& qt34MsgHandlerSlot()
{
    static Qt34MessageHandler h = 0;   // 0 → 用内置默认处理器
    return h;
}
inline Qt34MessageHandler qInstallMessageHandler(Qt34MessageHandler handler)
{
    Qt34MessageHandler prev = qt34MsgHandlerSlot();
    qt34MsgHandlerSlot() = handler;
    return prev;
}

// 分段送出。utf8 是 NUL 结尾的 UTF-8 字节串。分段只为规避 Qt3 的 8KB 栈上限
// （见文件头说明），内容与顺序不变，只是分成多条消息。
inline void qt34EmitMessage(Qt34LogLevel level, const char* file, int line,
                            const char* func, const char* utf8)
{
    Qt34MessageHandler h = qt34MsgHandlerSlot();
    if (!h) h = qt34DefaultMessageHandler;
    if (!utf8) { h(level, file, line, func, ""); return; }
    const size_t total = strlen(utf8);
    if (total <= (size_t)kQt34LogChunkLimit) {
        h(level, file, line, func, utf8);
        return;
    }
    for (size_t off = 0; off < total; off += kQt34LogChunkLimit) {
        const size_t remain = total - off;
        const size_t n = remain < (size_t)kQt34LogChunkLimit
                       ? remain : (size_t)kQt34LogChunkLimit;
        char chunk[kQt34LogChunkLimit + 1];
        memcpy(chunk, utf8 + off, n);
        chunk[n] = '\0';
        h(level, file, line, func, chunk);
    }
}

// printf 风格：C++ 不能把 `...` 转发给另一个变参函数（无 va_list 提取），必须
// 自己用 va_list + vsnprintf 拼串。va_copy 复制 va_list 以跑两遍（探测长度 +
// 精确填充）。格式化语义与 libc vfprintf 完全一致（同为 vsnprintf）。
// 不校验格式串与实参是否匹配 —— 与原生 qDebug 一致（越界写法是调用方的错）。
inline void qt34PrintfVa(Qt34LogLevel level, const char* file, int line,
                         const char* func, const char* fmt, va_list ap)
{
    if (!fmt) return;
    char stackBuf[1024];
    va_list ap2;
    va_copy(ap2, ap);
    const int need = vsnprintf(stackBuf, sizeof(stackBuf), fmt, ap2);
    va_end(ap2);
    if (need < 0) return;                 // 格式串非法：丢弃（原生 vfprintf 亦无输出）
    if (need < (int)sizeof(stackBuf)) {   // 装得下
        qt34EmitMessage(level, file, line, func, stackBuf);
        return;
    }
    // 超出栈缓冲：按需精确分配后重跑一次（避免无界栈占用）
    char* heap = (char*)malloc((size_t)need + 1);
    if (!heap) return;
    va_list ap3;
    va_copy(ap3, ap);
    vsnprintf(heap, (size_t)need + 1, fmt, ap3);
    va_end(ap3);
    qt34EmitMessage(level, file, line, func, heap);
    free(heap);
}

// qint64/quint64 → QString 的版本中立转换：Qt3 只有 Q_LLONG/Q_ULLONG 别名，
// Qt4 用 qlonglong/qulonglong（无 Q_LLONG）。QString::number 两边都收 64 位整型。
#if QT_VERSION < 0x040000
#  define QT34_NUM_LL(x)  QString::number((Q_LLONG)(x))
#  define QT34_NUM_ULL(x) QString::number((Q_ULLONG)(x))
#else
#  define QT34_NUM_LL(x)  QString::number((qlonglong)(x))
#  define QT34_NUM_ULL(x) QString::number((qulonglong)(x))
#endif

// ── 流对象：把 << 拼接进 QString，析构时带调用点交给处理器 ─────────────
// ⚠ 为什么内部用 QString 累加而不是 QCString（实测踩过）：
//   Qt3 的 QCString 只有 operator+=(const char*) / (char)
//   （qcstring.h:222-223），没有 operator+=(const QCString&)，只能靠
//   operator const char*（qcstring.h:221）隐式转换 —— 而该转换返回**内部共享
//   缓冲**，连续追加会互相覆盖。实测 /opt/qt338sh：
//     QCString b("z"), c; c += b; c += b;  →  c.length() == 2（应为 4）
//   静默丢一半数据、无任何报错。QString 的 += 语义正确。
// ⚠ 最终取 UTF-8 字节送处理器：Qt3 用 QString::utf8()（返回 QCString），Qt4 用
//   QString::toUtf8()（返回 QByteArray）。两者都在析构里转成本地字节缓冲。
class Qt34LogStream
{
public:
    Qt34LogStream(Qt34LogLevel level, const char* file, int line, const char* func)
        : m_level(level), m_file(file), m_line(line), m_func(func) {}
    ~Qt34LogStream()
    {
        if (m_printfed) return;   // printf 形式已在 operator() 里输出，勿重复
#if QT_VERSION < 0x040000
        const QCString bytes = m_acc.utf8();
#else
        const QByteArray bytes = m_acc.toUtf8();
#endif
        qt34EmitMessage(m_level, m_file, m_line, m_func, bytes.data());
    }
    Qt34LogStream& noquote() { return *this; }

    // 对象式宏展开后紧跟的括号，两种用法共用同一个宏（宏只是「类型 + 构造」那一截）：
    //   qInfo()          → operator()()        → 返回自身，继续接 `<< ...`
    //   qInfo(fmt, ...)  → operator()(fmt,...) → printf 风格，立即输出
    // 与 Qt5 的 `#define qInfo QMessageLogger(...).info` 用法完全一致。
    Qt34LogStream& operator()() { return *this; }
    void operator()(const char* fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        qt34PrintfVa(m_level, m_file, m_line, m_func, fmt, ap);
        va_end(ap);
        m_printfed = true;   // 已输出，析构不再重复
    }
    // 项间分隔：Qt4+ 的 QDebug 在相邻 << 项之间自动插一个空格；本垫片原先直接
    // 拼接，导致 `qInfo() << "using" << url` 输出 "usinghttps://..." 字段黏连
    // （实测 qlstik stickerlistpage.cpp:1607）。对齐 Qt 语义：首项前不加。
    Qt34LogStream& operator<<(const QString& v) { sep(); m_acc += v; return *this; }
    // ⚠ 用 fromUtf8 而非 fromLatin1：调用点传的多半是**源码里的中文字面量**，
    //   那些字节是 UTF-8。fromLatin1 会把多字节序列逐字节拆成 Latin1 码点，
    //   串起来就是乱码。（本仓规范：非 ASCII 一律走 utf8，AGENTS.md）
    Qt34LogStream& operator<<(const char* v)
    {
        if (!v) return *this;
        sep();
        m_acc += QString::fromUtf8(v);
        return *this;
    }
    // QLatin1String：Qt4 原生类是 QString(fromLatin1) 兼容的；Qt3 由
    // qstring_shim.h 提供同名最小类（只有 operator QString()）。
    Qt34LogStream& operator<<(const QLatin1String& v) { sep(); m_acc += QString(v); return *this; }
    Qt34LogStream& operator<<(int v)          { sep(); m_acc += QString::number(v); return *this; }
    Qt34LogStream& operator<<(unsigned int v) { sep(); m_acc += QString::number(v); return *this; }
    Qt34LogStream& operator<<(bool v)
    {
        sep();
        m_acc += v ? QString::fromUtf8("true") : QString::fromUtf8("false");
        return *this;
    }
    Qt34LogStream& operator<<(qint64 v)
    {
        sep();
        m_acc += QT34_NUM_LL(v);
        return *this;
    }
    Qt34LogStream& operator<<(quint64 v)
    {
        sep();
        m_acc += QT34_NUM_ULL(v);
        return *this;
    }

private:
    // 项间分隔空格（首个 << 项前不加；noquote() 不算项）
    void sep()
    {
        if (!m_first) {
            m_acc += QString::fromLatin1(" ");
        }
        m_first = false;
    }

    Qt34LogLevel m_level;
    const char*  m_file;
    int          m_line;
    const char*  m_func;
    bool         m_first = true;
    bool         m_printfed = false;   // printf 形式已自行输出（析构勿再发空消息）
    QString m_acc;   // ⚠ 保持 QString 累加（QCString 的 += 会静默丢数据，见上）
};

// ── 对象式宏：与 Qt5 用法完全一致 ─────────────────────────────────────
// Qt5 里 `qDebug` 是「半截表达式」宏：
//   #define qDebug QMessageLogger(QT_MESSAGELOG_FILE, QT_MESSAGELOG_LINE, \
//                                  QT_MESSAGELOG_FUNC).debug
// 于是 `qDebug() << a`（流式）与 `qDebug("fmt", a)`（printf）共用同一个宏。
// 本垫片把那一截换成「构造一个临时 Qt34LogStream」，紧随其后的括号由类的
// operator()() / operator()(fmt,...) 接住 —— 调用点因此真实、且写法不变。
//
// Q_FUNC_INFO 在 GCC 上是 __PRETTY_FUNCTION__；Qt3 无此宏（qglobal.h 里没有）。
// ⚠ `#ifndef qDebug`：Qt4 在 QT_NO_DEBUG_OUTPUT 下会把 qDebug/qWarning 定义成宏，
//   那种配置下不应覆盖（保持「关闭调试输出」的语义）。
#ifndef Q_FUNC_INFO
#  define Q_FUNC_INFO __PRETTY_FUNCTION__
#endif

#if QT_VERSION < 0x040000
// Qt3：五个级别全缺流式，qInfo/qCritical 连 printf 都没有 → 全部覆盖。
#  ifndef qDebug
#    define qDebug    Qt34LogStream(Qt34Debug,    __FILE__, __LINE__, Q_FUNC_INFO)
#  endif
#  ifndef qWarning
#    define qWarning  Qt34LogStream(Qt34Warning,  __FILE__, __LINE__, Q_FUNC_INFO)
#  endif
#  ifndef qCritical
#    define qCritical Qt34LogStream(Qt34Critical, __FILE__, __LINE__, Q_FUNC_INFO)
#  endif
#  ifndef qFatal
#    define qFatal    Qt34LogStream(Qt34Fatal,    __FILE__, __LINE__, Q_FUNC_INFO)
#  endif
#endif

// Qt4：只补缺失的 qInfo（Qt5.5 才引入），其余沿用 Qt4 原生。
#  ifndef qInfo
#    define qInfo    Qt34LogStream(Qt34Info,     __FILE__, __LINE__, Q_FUNC_INFO)
#  endif

#endif // QT_VERSION < 0x050000

#endif // QLSTIK_QDEBUG_SHIM_H
