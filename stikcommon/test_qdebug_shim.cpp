// stikcommon/test_qdebug_shim.cpp —— 日志流垫片契约（Qt3 单端）
//
// 被测对象：qdebug_shim.h
//   Qt3LogStream / qInfo() / qWarning() / qDebug() 零参流
//   qInfo(const char* fmt, ...) 变参转发
//
// 纯头文件，无需链接产品 .cpp。
//
// ── 测试手法：用 qInstallMsgHandler 拦截输出 ──────────────────────────────
// 这三个函数不返回值，只在**析构时**把累积的 QString 交给 Qt3 的
// qDebug(const QString&) / qWarning(const QString&)（qglobal.h:961/969）。
// 要断言内容只能从输出侧取。Qt3 提供了 qInstallMsgHandler(QtMsgHandler)
// （qglobal.h:1020，typedef void(*)(QtMsgType, const char*)），用它接管
// 就能同时拿到**级别**（QtDebugMsg/QtWarningMsg/QtFatalMsg，qglobal.h:1016）
// 与**内容**（已由 Qt 内部转成 UTF-8 的 const char*）。
//
// ⚠ 因此本文件断言的是「析构那一刻输出什么」，不是流对象本身的状态 ——
//   Qt3LogStream 的 m_acc 是 private，没有读取接口。测试用例里必须让流
//   对象真正析构（用一个花括号块，或让临时对象作为语句结束）。
//   写成 `qInfo() << "x";` 时临时对象在语句末析构，输出即时发生。
//
// ⚠ 处理器收到的消息**带时间戳前缀**，不是裸内容。实测 /opt/qt338sh：
//     qDebug(QString("hello")) → "[2026/10/02 12:22:28.645] hello"
//   格式为 '[' + yyyy/MM/dd HH:mm:ss.zzz + "] "，由 Qt3 的 qglobal.cpp
//   默认处理器拼上（装自定义处理器后仍走同一条拼接路径）。
//   故断言前必须用 stripTimestamp() 剥掉前缀 —— 否则每条用例都会挂，
//   且症状（内容多了个时间戳）容易被误判成「垫片多输出了东西」。
//
// ⚠ 处理器是**全局**状态。每个用例开头装、结尾卸，且用 RAII 守卫保证
//   即使断言失败也能还原（否则失败会污染后续用例、把无关用例搞挂）。

#include <qstring.h>
#include <qcstring.h>
#include <qglobal.h>
#include <stdio.h>
#include <string.h>

#include "doctest/doctest.h"
#include "qstring_shim.h"
#include "qglobaltype_shim.h"
#include "qba_shim.h"
#include "qdebug_shim.h"

// 字面量 → QString 的本地 helper。
// ⚠ 用 fromUtf8 而非 fromLatin1：本仓规范要求非 ASCII 一律走 UTF-8
//   （AGENTS.md）。本文件刻意包含中文断言（见「const char* 走 UTF-8 解码」
//   一例），fromLatin1 在这里会直接让那些用例失败 —— 正好当作守门人。
inline QString S(const char* s) { return QString::fromUtf8(s); }

// ── 拦截器：把最近一次输出存进全局缓冲 ───────────────────────────────────
static QString  g_lastMsg;
static QtMsgType g_lastType = QtDebugMsg;
static int       g_callCount = 0;
// ⚠ 全部消息（含分段）。Qt3 的 qDebug(const QCString&) 内部只有 8KB 栈
//   缓冲，超限直接破坏栈（实测 /opt/qt338sh 长度 8180 即 SIGABRT），
//   故本垫片对超长输出分段送出（每段 ≤ 4096，见 qdebug_shim.h emitChunked）。
//   验证长输出时必须拼接全部条再比对内容，不能只看最后一条。
static QStringList g_allMsgs;

// ⚠ Qt3 交给处理器的**不是裸内容**，而是带时间戳前缀的完整消息。
//   实测 /opt/qt338sh：qDebug(QString("hello")) 收到
//       "[2026/10/02 12:22:28.645] hello"
//   前缀格式固定为 '[' + yyyy/MM/dd HH:mm:ss.zzz + "] "（由 Qt3 的
//   qglobal.cpp 默认处理器拼上，qInstallMsgHandler 装上后仍走同一条路径）。
//   故断言必须剥掉前缀，否则每条用例都会因时间戳而挂。
//   剥离方式：从第一个 ']'（下标 25，固定宽度）之后切。
static QString stripTimestamp(const QString& full)
{
    const int close = full.find(']');
    if (close < 0) return full;          // 无前缀（格式异常）→ 原样返回
    return full.mid(close + 2);          // 跳 ']' 与随后的一个空格
}

static void captureHandler(QtMsgType type, const char* msg)
{
    g_lastType = type;
    // ⚠ Qt3 传给处理器的已经是 UTF-8 字节（qDebug(const QString&) 内部
    //   做了 QString→UTF-8）。这里必须 fromUtf8，不能用 QString(const char*)
    //   —— 后者是 latin1 逐字节（实测中文会变乱码码点）。
    g_lastMsg = stripTimestamp(QString::fromUtf8(msg ? msg : ""));
    g_allMsgs.append(g_lastMsg);
    ++g_callCount;
}

// RAII 守卫：装上处理器，离开作用域时还原。
struct MsgCapture {
    QtMsgHandler prev;
    MsgCapture() : prev(qInstallMsgHandler(captureHandler))
    {
        g_lastMsg = QString();
        g_callCount = 0;
        g_allMsgs.clear();
    }
    ~MsgCapture() { qInstallMsgHandler(prev); }
};

TEST_CASE("qDebug() 零参流: 析构时输出到 QtDebugMsg 通道")
{
    MsgCapture cap;
    qDebug() << "hello";
    CHECK_EQ(g_callCount, 1);
    CHECK_EQ(int(g_lastType), int(QtDebugMsg));
    CHECK(g_lastMsg == S("hello"));
}

TEST_CASE("qWarning() 零参流: 走 QtWarningMsg 通道（与 qDebug 分流）")
{
    MsgCapture cap;
    qWarning() << "warn";
    CHECK_EQ(g_callCount, 1);
    CHECK_EQ(int(g_lastType), int(QtWarningMsg));
    CHECK(g_lastMsg == S("warn"));
}

TEST_CASE("qInfo() 零参流: Qt3 无独立 info 通道，落到 QtDebugMsg")
{
    // ⚠ 已知且已记录在 qdebug_shim.h 文件头的「输出通道差异」：Qt5+ 的 qInfo
    //   进 stdout、qDebug/qWarning 进 stderr 三者分流；Qt3 只有一个默认
    //   处理器，qDebug 与 qWarning 走同一通道。qt3 侧 qInfo 转发给 qDebug，
    //   故 info 与 debug 日志混合 —— 这是 Qt3 消息系统的能力限制，
    //   **不是**本垫片的缺陷。测试在此锁定该实际行为。
    MsgCapture cap;
    qInfo() << "info-line";
    CHECK_EQ(g_callCount, 1);
    CHECK_EQ(int(g_lastType), int(QtDebugMsg));
    CHECK(g_lastMsg == S("info-line"));
}

TEST_CASE("Qt3LogStream: 多次 << 累积拼接，析构时一次性输出")
{
    MsgCapture cap;
    {
        Qt3LogStream s(false);
        s << "a=" << 1 << " b=" << 2 << " ";
        // 尚未析构 → 不应有任何输出
        CHECK_EQ(g_callCount, 0);
    }   // ← 析构在此发生
    CHECK_EQ(g_callCount, 1);
    CHECK(g_lastMsg == S("a=1 b=2 "));
}

TEST_CASE("Qt3LogStream: noquote() 可链式调用且无副作用")
{
    // noquote 在 Qt3 侧是 no-op（Qt6 的 QDebug 用它抑制引号）。
    // 这里只验证它返回 *this 从而不断链。
    MsgCapture cap;
    {
        Qt3LogStream s(false);
        s.noquote() << "x" << 1;
    }
    CHECK(g_lastMsg == S("x1"));
}

TEST_CASE("Qt3LogStream: 整数各类型格式化正确")
{
    MsgCapture cap;
    {
        Qt3LogStream s(false);
        s << int(-5) << " " << (unsigned int)7 << " " << qint64(-9007199254740993LL);
    }
    // ⚠ 断言用 QString 而非 printf：QString 的数字格式化与 Qt3 的
    //   QString::number 同源，不受终端编码影响。
    CHECK(g_lastMsg == S("-5 7 -9007199254740993"));
}

TEST_CASE("Qt3LogStream: quint64 大值不被截断")
{
    // 回归点：若误用 QString::number(int) 或 (long)，超过 2^32 会溢出。
    MsgCapture cap;
    {
        Qt3LogStream s(false);
        s << quint64(18446744073709551615ULL);
    }
    CHECK(g_lastMsg == S("18446744073709551615"));
}

TEST_CASE("Qt3LogStream: bool 输出 true/false 文本")
{
    MsgCapture cap;
    { Qt3LogStream s(false); s << true; }
    CHECK(g_lastMsg == S("true"));
    { Qt3LogStream s(false); s << false; }
    CHECK(g_lastMsg == S("false"));
}

TEST_CASE("Qt3LogStream: const char* 走 UTF-8 解码，中文不乱码")
{
    // ⚠ 这是本组最重要的一条：Qt3LogStream::operator<<(const char*) 曾用
    //   QString::fromLatin1(v)。调用点传的多半是**源码里的中文字面量**，
    //   那些字节是 UTF-8，fromLatin1 会把多字节序列逐字节拆成 Latin1 码点，
    //   日志里就是乱码且不报错（实测 QString(QByteArray(UTF-8"中文")) 得
    //   6 个 U+00xx 码点，而 fromUtf8 正确得 U+4E2D U+6587 两个）。
    //   本用例直接比对码点数，锁住 fromUtf8 语义。
    MsgCapture cap;
    { Qt3LogStream s(false); s << "中文"; }
    CHECK(g_lastMsg == S("中文"));
    CHECK_EQ(g_lastMsg.length(), 2);   // 2 个汉字，不是 6 个 latin1 码点
}

TEST_CASE("Qt3LogStream: const char* 的纯 ASCII 路径不变")
{
    // 从 fromLatin1 换到 fromUtf8 后，纯 ASCII 必须完全等价（不得回归）。
    MsgCapture cap;
    { Qt3LogStream s(false); s << "plain-ascii_123"; }
    CHECK(g_lastMsg == S("plain-ascii_123"));
    CHECK_EQ(g_lastMsg.length(), 15);
}

TEST_CASE("Qt3LogStream: QString 与 QLatin1String 重载")
{
    MsgCapture cap;
    {
        Qt3LogStream s(false);
        s << S("qs") << QLatin1String("la");
    }
    CHECK(g_lastMsg == S("qsla"));
}

TEST_CASE("Qt3LogStream: 纯 ASCII QString 不会被再转码")
{
    MsgCapture cap;
    { Qt3LogStream s(false); s << S("already-qstring"); }
    CHECK(g_lastMsg == S("already-qstring"));
}

TEST_CASE("qInfo(fmt, ...) 变参: printf 风格格式化")
{
    MsgCapture cap;
    qInfo("v=%d s=%s", 42, "str");
    CHECK_EQ(g_callCount, 1);
    CHECK_EQ(int(g_lastType), int(QtDebugMsg));
    CHECK(g_lastMsg == S("v=42 s=str"));
}

// 收集所有分段，模拟「用户看到的完整一行日志」。
static QString joinAll()
{
    QString all;
    for (int i = 0; i < (int)g_allMsgs.size(); ++i) all += g_allMsgs[i];
    return all;
}

TEST_CASE("qInfo(fmt, ...) 变参: 超出栈缓冲时按需堆分配且分段输出不丢内容")
{
    // 回归点一：实现里先用 1024 字节栈缓冲，need >= 1024 时改走堆分配并
    //   重跑一次 vsnprintf。截断是最容易犯也最难发现的错：日志看着正常，
    //   尾部静默丢失。
    //
    // 回归点二（Qt3 库缺陷）：qDebug(const QCString&) 内部只有 8KB 栈缓冲，
    //   实测 /opt/qt338sh 长度 8180 即 "stack smashing detected" SIGABRT
    //   （gdb 帧 tools/qglobal.cpp:529）。故本垫片对超长输出**分段**送出
    //   （每段 ≤ 4096，见 qdebug_shim.h 的 emitChunked）。
    //   ⇒ 20000 字符的输出会被拆成多条消息，打印条数 > 1 是**预期行为**，
    //   不能断言 g_callCount == 1。真正要锁的是「拼接后内容一字不差」。
    MsgCapture cap;
    QString big;
    for (int i = 0; i < 2000; ++i) big += "0123456789";   // 20000 字符
    qInfo("[%s]", big.utf8().data());

    // 分段条数：20002 字节 / 4096 → 至少 5 条
    CHECK(g_callCount > 1);
    // 每条都不超过 4096（否则就是没分段、等着撞 Qt3 的 8KB 上限）
    for (int i = 0; i < g_callCount; ++i)
        CHECK(g_allMsgs[i].length() <= 4096);

    // 拼接后必须与原文完全一致（两端 '[' ']' 保留，无字符丢失/重复）
    const QString all = joinAll();
    CHECK_EQ(all.length(), big.length() + 2);
    CHECK(all.startsWith("[0123"));
    CHECK(all.endsWith("]"));
}

TEST_CASE("qInfo(fmt, ...) 变参: 恰好在分段阈值两侧都不断内容")
{
    // 锁住 4096 边界：emitChunked 在 total <= 4096 时走单条，
    // 刚过 4096 就切两段。两边都必须内容完整 —— 边界写错
    // （比如用 < 写成 <=、或 off 步进算错）会让某一边丢/重一个字。
    const int kLimit = 4096;      // 与 qdebug_shim.h 的 kQt3LogChunkLimit 对齐
    const int lens[] = {kLimit - 2, kLimit - 1, kLimit, kLimit + 1};
    for (int i = 0; i < 4; ++i) {
        MsgCapture cap;
        QString s;
        for (int j = 0; j < lens[i]; ++j) s += QChar(QChar('a').unicode() + (j % 26));
        qInfo("%s", s.utf8().data());
        CHECK_EQ(joinAll(), s);
    }
}

TEST_CASE("qDebug() 零参流: 超长日志分段输出不丢内容（同一 8KB 限制）")
{
    // 零参流析构同样经 emitChunked，所以**也有** 8KB 崩栈风险 ——
    // 之前只在 qInfo 变参路径上测到，改流实现时极易漏掉这条路径。
    MsgCapture cap;
    QString big;
    for (int i = 0; i < 2000; ++i) big += "0123456789";   // 20000 字符
    qDebug() << big;
    CHECK(g_callCount > 1);                    // 确实分段了
    for (int i = 0; i < g_callCount; ++i)
        CHECK(g_allMsgs[i].length() <= 4096);
    CHECK_EQ(joinAll(), big);                  // 拼接后一字不差
}

TEST_CASE("qInfo(fmt, ...) 变参: 空 fmt / nullptr 不崩不输出")
{
    MsgCapture cap;
    qInfo(0);                       // nullptr 保护
    CHECK_EQ(g_callCount, 0);
    qInfo("");                      // 空格式串 → 输出空消息
    CHECK_EQ(g_callCount, 1);
    CHECK(g_lastMsg.isEmpty());
}

TEST_CASE("qInfo(fmt, ...) 变参: 无占位符的纯文本")
{
    MsgCapture cap;
    qInfo("plain text");
    CHECK(g_lastMsg == S("plain text"));
}

TEST_CASE("qInfo(fmt, ...) 变参: 中文经 UTF-8 转发不乱码")
{
    // 与 qInfo() 零参流同源（都经 fromUtf8），但走的是 vsnprintf 拼串
    // 那条路径，单独锁一次。
    MsgCapture cap;
    qInfo("[%s]", "中文");
    CHECK(g_lastMsg == S("[中文]"));
}

TEST_CASE("消息通道: 同一流里 qDebug/qWarning 的级别正确落到各自通道")
{
    // 确认 warn=false → QtDebugMsg、warn=true → QtWarningMsg，
    // 即 Qt3LogStream 的 m_warn 标志真的被析构函数用上（曾有风险是
    // 构造参数传反导致全发到 warning）。
    //
    // ⚠ 必须用**作用域块**：输出发生在析构时，具名局部对象要等作用域结束
    //   才析构（且多个对象按逆序析构），写成一个作用域里两个具名对象会让
    //   最后一条日志来自后声明的那个，掩盖真实级别。
    MsgCapture cap;
    {
        Qt3LogStream s(false);
        s << "i";
        CHECK_EQ(g_callCount, 0);   // 尚未析构 → 无输出
    }
    CHECK_EQ(g_callCount, 1);
    CHECK_EQ(int(g_lastType), int(QtDebugMsg));
    CHECK(g_lastMsg == S("i"));

    {
        Qt3LogStream s(true);
        s << "w";
        CHECK_EQ(g_callCount, 1);   // 仍无新输出
    }
    CHECK_EQ(g_callCount, 2);
    CHECK_EQ(int(g_lastType), int(QtWarningMsg));
    CHECK(g_lastMsg == S("w"));
}

TEST_CASE("处理器还原: MsgCapture 析构后默认处理器恢复")
{
    // RAII 守卫自身的行为验证：装上 → 拆掉应还原为上一个处理器。
    // 若守卫没还原，后续所有用例的输出都会丢失（表现为 g_callCount 恒 0）。
    {
        MsgCapture cap;
        CHECK_EQ(g_callCount, 0);
    }
    // 守卫外再装一次，确认能独立工作
    {
        MsgCapture cap2;
        qDebug() << "after";
        CHECK_EQ(g_callCount, 1);
    }
}
