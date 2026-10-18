// stikcommon/test_qdebug_shim.cpp —— 日志流垫片契约（Qt3 单端）
//
// 被测对象：qdebug_shim.h（Qt3/Qt4 版）
//   Qt34LogStream / 对象式宏 qDebug/qInfo/qWarning/qCritical/qFatal
//   宏用法与 Qt5 完全一致：`qInfo() << a << b`（流式）或 `qInfo("%d", n)`（printf）。
//
// ⚠ 2026-10-18 重写：垫片不再借道 Qt3 的 qDebug(const QCString&) 输出，而是
//   自带处理器 + 对象式宏（对标 Qt5 的 `#define qDebug QMessageLogger(...).debug`，
//   但垫片直接构造 Qt34LogStream，不再单独提供 QMessageLogger）。因此：
//     · 拦截器签名变为 Qt34MessageHandler(level, file, line, func, utf8msg)；
//     · 处理器收到的 message 是**裸 UTF-8 文本**（时间戳/级别前缀由默认处理器
//       自己拼，不再塞进消息里），故旧的 stripTimestamp() 已删除；
//     · 级别是自成的 Qt34LogLevel（Debug/Info/Warning/Critical/Fatal），
//       不再借用 Qt3 的 QtMsgType（它根本没有 info）。
//
// ⚠ 处理器是**全局**状态。每个用例开头装、结尾卸，且用 RAII 守卫保证即使断言
//   失败也能还原（否则失败会污染后续用例）。

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
//   （AGENTS.md）。本文件刻意包含中文断言，fromLatin1 会直接让那些用例失败。
inline QString S(const char* s) { return QString::fromUtf8(s); }

// ── 拦截器：把最近一次输出（含调用点）存进全局缓冲 ───────────────────────
static QString       g_lastMsg;
static Qt34LogLevel  g_lastLevel = Qt34Debug;
static const char*   g_lastFile  = 0;
static int           g_lastLine  = 0;
static const char*   g_lastFunc  = 0;
static int           g_callCount = 0;
// ⚠ 全部消息（含分段）。Qt3 的 qDebug(const QCString&) 内部只有 8KB 栈缓冲，
//   超限直接破坏栈（实测 /opt/qt338sh 长度 8180 即 SIGABRT），故本垫片对超长
//   输出分段送出（每段 ≤ 4096）。验证长输出时要拼接全部条再比对内容。
static QStringList g_allMsgs;

static void captureHandler(Qt34LogLevel level, const char* file, int line,
                           const char* func, const char* msg)
{
    g_lastLevel = level;
    g_lastFile  = file;
    g_lastLine  = line;
    g_lastFunc  = func;
    // 垫片传进来的已是 UTF-8 字节；fromUtf8 解码，绝不能用 QString(const char*)
    //（那是 latin1 逐字节，中文会变乱码码点）。
    g_lastMsg = QString::fromUtf8(msg ? msg : "");
    g_allMsgs.append(g_lastMsg);
    ++g_callCount;
}

// RAII 守卫：装上处理器，离开作用域时还原。
struct MsgCapture {
    Qt34MessageHandler prev;
    MsgCapture() : prev(qInstallMessageHandler(captureHandler))
    {
        g_lastMsg = QString();
        g_callCount = 0;
        g_allMsgs.clear();
        g_lastFile = 0;
        g_lastLine = 0;
        g_lastFunc = 0;
    }
    ~MsgCapture() { qInstallMessageHandler(prev); }
};

TEST_CASE("qDebug() 零参流: 析构时以 Qt34Debug 级别输出")
{
    MsgCapture cap;
    qDebug() << "hello";
    CHECK_EQ(g_callCount, 1);
    CHECK_EQ(int(g_lastLevel), int(Qt34Debug));
    CHECK(g_lastMsg == S("hello"));
}

TEST_CASE("qWarning() 零参流: 级别为 Qt34Warning")
{
    MsgCapture cap;
    qWarning() << "warn";
    CHECK_EQ(g_callCount, 1);
    CHECK_EQ(int(g_lastLevel), int(Qt34Warning));
    CHECK(g_lastMsg == S("warn"));
}

TEST_CASE("qInfo() 零参流: 级别为 Qt34Info（不再降级成 Debug）")
{
    MsgCapture cap;
    qInfo() << "info-line";
    CHECK_EQ(g_callCount, 1);
    CHECK_EQ(int(g_lastLevel), int(Qt34Info));
    CHECK(g_lastMsg == S("info-line"));
}

TEST_CASE("qCritical() 零参流: 级别为 Qt34Critical（旧垫片完全没有）")
{
    MsgCapture cap;
    qCritical() << "crit-line";
    CHECK_EQ(g_callCount, 1);
    CHECK_EQ(int(g_lastLevel), int(Qt34Critical));
    CHECK(g_lastMsg == S("crit-line"));
}

TEST_CASE("调用点: 处理器收到真实 file:line/func，而不是 [?]")
{
    // 垫片的核心目标之一：宏在调用点展开，file/line/func 必须真实。
    MsgCapture cap;
    qInfo() << "where";
    CHECK(g_lastFile != 0);
    CHECK(g_lastFunc != 0);
    CHECK(g_lastLine > 0);
    // 文件必须指向本测试文件（编译器按传入路径给 __FILE__，取尾部匹配以兼容
    // 相对/绝对路径两种编译方式）。
    const QString f = QString::fromUtf8(g_lastFile ? g_lastFile : "");
    CHECK(strstr(g_lastFile ? g_lastFile : "", "test_qdebug_shim.cpp") != 0);
    CHECK(g_lastFunc[0] != '\0');
}

TEST_CASE("qCritical(fmt, ...) 变参: printf 风格且级别为 Qt34Critical")
{
    MsgCapture cap;
    qCritical("code=%d", 13);
    CHECK_EQ(g_callCount, 1);
    CHECK_EQ(int(g_lastLevel), int(Qt34Critical));
    CHECK(g_lastMsg == S("code=13"));
}

TEST_CASE("Qt34LogStream: 多次 << 累积拼接，析构时一次性输出")
{
    // 项间自动插一个空格（对齐 Qt4+ QDebug 语义）；原先直接拼接导致
    // `qInfo() << "using" << url` 输出 "usinghttps://..." 字段黏连。
    // ⚠ 本用例需在析构**之前**断言 g_callCount==0，故显式具名构造（宏临时量
    //   会在整条语句末尾就析构，无法中途观察）。
    MsgCapture cap;
    {
        Qt34LogStream s(Qt34Debug, __FILE__, __LINE__, Q_FUNC_INFO);
        s << "a=" << 1 << "b=" << 2 << "end";
        CHECK_EQ(g_callCount, 0);   // 尚未析构 → 不应有任何输出
    }   // ← 析构在此发生
    CHECK_EQ(g_callCount, 1);
    CHECK(g_lastMsg == S("a= 1 b= 2 end"));
}

TEST_CASE("qDebug(): noquote() 可链式调用且无副作用")
{
    MsgCapture cap;
    qDebug().noquote() << "x" << 1;
    CHECK(g_lastMsg == S("x 1"));
}

TEST_CASE("qDebug(): 整数各类型格式化正确")
{
    MsgCapture cap;
    qDebug() << int(-5) << (unsigned int)7 << qint64(-9007199254740993LL);
    CHECK(g_lastMsg == S("-5 7 -9007199254740993"));
}

TEST_CASE("qDebug(): quint64 大值不被截断")
{
    // 回归点：若误用 QString::number(int) 或 (long)，超过 2^32 会溢出。
    MsgCapture cap;
    qDebug() << quint64(18446744073709551615ULL);
    CHECK(g_lastMsg == S("18446744073709551615"));
}

TEST_CASE("qDebug(): bool 输出 true/false 文本")
{
    MsgCapture cap;
    qDebug() << true;
    CHECK(g_lastMsg == S("true"));
    qDebug() << false;
    CHECK(g_lastMsg == S("false"));
}

TEST_CASE("qDebug(): const char* 走 UTF-8 解码，中文不乱码")
{
    // ⚠ 本组最重要的一条：operator<<(const char*) 用 fromUtf8。调用点传的多半
    //   是源码里的中文字面量（UTF-8 字节），fromLatin1 会把多字节序列逐字节
    //   拆成 Latin1 码点 → 乱码且不报错。这里直接比对码点数锁死语义。
    MsgCapture cap;
    qDebug() << "中文";
    CHECK(g_lastMsg == S("中文"));
    CHECK_EQ(g_lastMsg.length(), 2);   // 2 个汉字，不是 6 个 latin1 码点
}

TEST_CASE("qDebug(): const char* 的纯 ASCII 路径不变")
{
    MsgCapture cap;
    qDebug() << "plain-ascii_123";
    CHECK(g_lastMsg == S("plain-ascii_123"));
    CHECK_EQ(g_lastMsg.length(), 15);
}

TEST_CASE("qDebug(): QString 与 QLatin1String 重载")
{
    MsgCapture cap;
    qDebug() << S("qs") << QLatin1String("la");
    CHECK(g_lastMsg == S("qs la"));
}

TEST_CASE("qDebug(): 纯 ASCII QString 不会被再转码")
{
    MsgCapture cap;
    qDebug() << S("already-qstring");
    CHECK(g_lastMsg == S("already-qstring"));
}

TEST_CASE("qInfo(fmt, ...) 变参: printf 风格格式化")
{
    MsgCapture cap;
    qInfo("v=%d s=%s", 42, "str");
    CHECK_EQ(g_callCount, 1);
    CHECK_EQ(int(g_lastLevel), int(Qt34Info));
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
    // 回归点一：实现先用 1024 字节栈缓冲，need >= 1024 时改走堆分配并重跑一次
    //   vsnprintf。截断是最容易犯也最难发现的错：日志看着正常，尾部静默丢失。
    // 回归点二（Qt3 库缺陷）：8KB 栈缓冲上限，故超长输出**分段**送出
    //   （每段 ≤ 4096）。20000 字符会被拆成多条消息，打印条数 > 1 是预期行为。
    MsgCapture cap;
    QString big;
    for (int i = 0; i < 2000; ++i) big += "0123456789";   // 20000 字符
    qInfo("[%s]", big.utf8().data());

    CHECK(g_callCount > 1);                     // 确实分段了
    for (int i = 0; i < g_callCount; ++i)
        CHECK(g_allMsgs[i].length() <= 4096);   // 每段不超阈值

    const QString all = joinAll();
    CHECK_EQ(all.length(), big.length() + 2);
    CHECK(all.startsWith("[0123"));
    CHECK(all.endsWith("]"));
}

TEST_CASE("qInfo(fmt, ...) 变参: 恰好在分段阈值两侧都不断内容")
{
    // 锁住 4096 边界：total <= 4096 时走单条，刚过 4096 就切两段。
    // 边界写错（用 < 写成 <=、或 off 步进算错）会让某一边丢/重一个字。
    const int kLimit = 4096;      // 与 qdebug_shim.h 的 kQt34LogChunkLimit 对齐
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
    // 零参流析构同样经 qt34EmitMessage，所以**也有** 8KB 崩栈风险。
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
    MsgCapture cap;
    qInfo("[%s]", "中文");
    CHECK(g_lastMsg == S("[中文]"));
}

TEST_CASE("qt34ShortFunc: 从 __PRETTY_FUNCTION__ 裁出限定函数名")
{
    char buf[256];
    CHECK_EQ(QString::fromUtf8(qt34ShortFunc(
                 "size_t {anonymous}::headerCb(char*, size_t, size_t, void*)",
                 buf, sizeof(buf))), S("headerCb"));
    CHECK_EQ(QString::fromUtf8(qt34ShortFunc(
                 "virtual void {anonymous}::Pump::run()", buf, sizeof(buf))), S("Pump::run"));
    CHECK_EQ(QString::fromUtf8(qt34ShortFunc("int main()", buf, sizeof(buf))), S("main"));
    CHECK_EQ(QString::fromUtf8(qt34ShortFunc("Pump::Pump()", buf, sizeof(buf))), S("Pump::Pump"));
    CHECK_EQ(QString::fromUtf8(qt34ShortFunc(
                 "int ns::Cls::method(int) const", buf, sizeof(buf))), S("ns::Cls::method"));
    CHECK_EQ(QString::fromUtf8(qt34ShortFunc("char *foo()", buf, sizeof(buf))), S("foo"));
    CHECK(QString::fromUtf8(qt34ShortFunc(0, buf, sizeof(buf))).isEmpty());
}

TEST_CASE("qt34BaseName: 取路径最后一段")
{
    CHECK_EQ(QString::fromUtf8(qt34BaseName("../../stikcommon/qwebdavtransport.cpp")),
             S("qwebdavtransport.cpp"));
    CHECK_EQ(QString::fromUtf8(qt34BaseName("/abs/path/file.cpp")), S("file.cpp"));
    CHECK_EQ(QString::fromUtf8(qt34BaseName("plain.cpp")), S("plain.cpp"));
    CHECK_EQ(QString::fromUtf8(qt34BaseName("dir\\win.cpp")), S("win.cpp"));
    CHECK(QString::fromUtf8(qt34BaseName(0)).isEmpty());
}

TEST_CASE("消息通道: 级别标志真的被析构函数用上")
{
    // 确认 Debug 与 Warning 各自落到正确级别；曾有风险是构造参数传反。
    // ⚠ 同理需观察析构前的 g_callCount，故显式具名构造。
    MsgCapture cap;
    {
        Qt34LogStream s(Qt34Debug, __FILE__, __LINE__, Q_FUNC_INFO);
        s << "i";
        CHECK_EQ(g_callCount, 0);   // 尚未析构 → 无输出
    }
    CHECK_EQ(g_callCount, 1);
    CHECK_EQ(int(g_lastLevel), int(Qt34Debug));
    CHECK(g_lastMsg == S("i"));

    {
        Qt34LogStream s(Qt34Warning, __FILE__, __LINE__, Q_FUNC_INFO);
        s << "w";
        CHECK_EQ(g_callCount, 1);   // 仍无新输出
    }
    CHECK_EQ(g_callCount, 2);
    CHECK_EQ(int(g_lastLevel), int(Qt34Warning));
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
    {
        MsgCapture cap2;
        qDebug() << "after";
        CHECK_EQ(g_callCount, 1);
    }
}
