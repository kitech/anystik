// stikcommon/test_qconcurrent_shim.cpp —— QtConcurrent::run/QFuture 垫片契约（Qt3 单端）
//
// 被测对象：qconcurrent_shim.h —— Qt3.5 无 QtConcurrent/QFuture，垫片提供
//   run() + QFuture::then()/waitForFinished()/result()/isFinished()，
//   续延回投 context 所在线程（本仓恒为 GUI 线程）。
//
// ── 环境事实（与 test_qclipboard_shim.cpp 同源）────────────────────────────
// ⚠【需要 X11】Qt3 的 QApplication 构造依赖 DISPLAY，无 DISPLAY 时 qFatal abort。
//   故 appReady() 先查 DISPLAY，缺失则整例跳过（否则整个 run_tests 起不来）。
//   本机实测可跑：DISPLAY=:0.0。
// ⚠【只能有一个 QApplication】整个 run_tests 一个 main；Qt3 不许多实例。
//   ensureApp() 先看全局 qApp（qapplication.h:71）——若其它测试文件已建则复用，
//   否则惰性单例新建且**故意不 delete**（避开 static 析构序与 Qt 全局态打架）。
// ⚠【事件泵】postEvent 需应用实例且事件被派发；本文件用 app->processEvents()
//   （Qt3 该函数是**非静态成员**，qapplication.h:150）轮询，不依赖 exec()。

#include <stdlib.h>
#include <unistd.h>
#include <memory>

#include <qstring.h>
#include <qapplication.h>
#include <qobject.h>
#include <qthread.h>
#include <qmutex.h>

#include "doctest/doctest.h"

#include "qconcurrent_shim.h"

namespace {

QApplication* ensureApp()
{
    if (qApp)                              // 可能已由其它测试文件创建
        return qApp;
    static int           s_argc = 1;
    static char          s_arg0[] = "run_tests";
    static char*         s_argv[2] = { s_arg0, 0 };
    static QApplication* s_app = new QApplication(s_argc, s_argv);
    return s_app;
}

// 有 DISPLAY 且应用可用才返回非空；否则用例跳过。
QApplication* appReady()
{
    const char* d = getenv("DISPLAY");
    if (!d || !*d)
        return 0;
    return ensureApp();
}

// 毫秒级睡眠（Qt3 的 QThread::msleep 是 protected，子类外不可用；用 POSIX usleep）。
void sleepMs(int ms) { usleep((unsigned int)ms * 1000u); }

// 泵事件直到 *flag 为真或超时（毫秒）。
bool pumpUntil(QApplication* app, volatile bool* flag, int timeoutMs)
{
    for (int waited = 0; waited < timeoutMs; waited += 2) {
        app->processEvents();
        if (*flag)
            return true;
        sleepMs(2);
    }
    app->processEvents();
    return *flag;
}

// then 的 context（QObject 派生，作为回投目标；无信号槽，无需 moc）。
struct Ctx : public QObject {};

} // namespace

TEST_CASE("qconcurrent: run + then 结果正确且续延回到 context(GUI) 线程")
{
    QApplication* app = appReady();
    if (!app) { MESSAGE("skip: no DISPLAY"); return; }
    const Qt::HANDLE mainThread = QThread::currentThread();

    volatile bool ran = false;
    int got = -1;
    Qt::HANDLE ranOn = 0;

    Ctx ctx;
    QFuture<int> f = QtConcurrent::run([]() -> int { return 42; });
    f.then(&ctx, [&](int v) {
        got = v;
        ranOn = QThread::currentThread();
        ran = true;
    });

    CHECK(pumpUntil(app, &ran, 5000));
    CHECK_EQ(got, 42);
    CHECK(ranOn == mainThread);
    f.waitForFinished();
}

TEST_CASE("qconcurrent: result() 返回值 / isFinished() 反映完成")
{
    QApplication* app = appReady();
    if (!app) { MESSAGE("skip: no DISPLAY"); return; }
    QFuture<int> f = QtConcurrent::run([]() -> int { return 7; });
    CHECK_EQ(f.result(), 7);
    CHECK(f.isFinished());
}

TEST_CASE("qconcurrent: void 返回 + 无参续延")
{
    QApplication* app = appReady();
    if (!app) { MESSAGE("skip: no DISPLAY"); return; }
    volatile bool ran = false;
    int side = 0;
    Ctx ctx;
    QFuture<void> f = QtConcurrent::run([&]() { side = 1; });
    f.then(&ctx, [&]() { ran = true; });
    CHECK(pumpUntil(app, &ran, 5000));
    CHECK_EQ(side, 1);
    f.waitForFinished();
}

TEST_CASE("qconcurrent: 任务已完成后再登记 then，续延仍执行")
{
    QApplication* app = appReady();
    if (!app) { MESSAGE("skip: no DISPLAY"); return; }
    volatile bool ran = false;
    int got = -1;
    Ctx ctx;
    QFuture<int> f = QtConcurrent::run([]() -> int { return 5; });
    f.waitForFinished();
    f.then(&ctx, [&](int v) { got = v; ran = true; });
    CHECK(pumpUntil(app, &ran, 5000));
    CHECK_EQ(got, 5);
}

TEST_CASE("qconcurrent: 工作体抛异常 → result() 重抛、then 被跳过")
{
    QApplication* app = appReady();
    if (!app) { MESSAGE("skip: no DISPLAY"); return; }
    struct Boom {};

    QFuture<int> f = QtConcurrent::run([]() -> int { throw Boom(); });
    bool rethrown = false;
    try { f.result(); } catch (const Boom&) { rethrown = true; }
    CHECK(rethrown);

    volatile bool ran = false;
    Ctx ctx;
    f.then(&ctx, [&](int) { ran = true; });
    for (int i = 0; i < 50; ++i) { app->processEvents(); sleepMs(2); }
    CHECK_FALSE(ran);
}

TEST_CASE("qconcurrent: 生命周期——context 先析构，工作线程随后完成，不崩")
{
    QApplication* app = appReady();
    if (!app) { MESSAGE("skip: no DISPLAY"); return; }
    QFuture<int> f = QtConcurrent::run([]() -> int {
        sleepMs(150);
        return 1;
    });
    {
        Ctx ctx;
        f.then(&ctx, [](int) {});
    }   // ctx（及其子 Notifier）在此析构，早于工作线程完成
    CHECK_EQ(f.result(), 1);
    app->processEvents();
}

TEST_CASE("qconcurrent: 并发多个 run+then 各自结果正确")
{
    QApplication* app = appReady();
    if (!app) { MESSAGE("skip: no DISPLAY"); return; }
    const int N = 8;
    volatile bool all = false;
    int count = 0;
    long sum = 0;
    Ctx ctx;
    for (int i = 0; i < N; ++i) {
        QtConcurrent::run([i]() -> int { return i * i; })
            .then(&ctx, [&, i](int v) { (void)i; sum += v; if (++count == N) all = true; });
    }
    CHECK(pumpUntil(app, &all, 8000));
    CHECK_EQ((int)sum, 140);   // 0+1+4+...+49
}

TEST_CASE("qconcurrent: 链式 then run→then(→int)→then(→void)，全程 GUI 线程")
{
    QApplication* app = appReady();
    if (!app) { MESSAGE("skip: no DISPLAY"); return; }
    const Qt::HANDLE mainThread = QThread::currentThread();
    volatile bool ran = false;
    int mid = -1, fin = -1;
    Qt::HANDLE secondThread = 0;
    Ctx a, b;
    QtConcurrent::run([]() -> int { return 10; })
        .then(&a, [&](int v) -> int { mid = v + 1; return mid; })
        .then(&b, [&](int v) { fin = v; secondThread = QThread::currentThread(); ran = true; });
    CHECK(pumpUntil(app, &ran, 5000));
    CHECK_EQ(mid, 11);
    CHECK_EQ(fin, 11);
    CHECK(secondThread == mainThread);
}

TEST_CASE("qconcurrent: 链式 then 中首续延抛异常 → 末续延跳过")
{
    QApplication* app = appReady();
    if (!app) { MESSAGE("skip: no DISPLAY"); return; }
    struct Boom {};
    volatile bool ran = false;
    Ctx a, b;
    QtConcurrent::run([]() -> int { return 1; })
        .then(&a, [](int) -> int { throw Boom(); })
        .then(&b, [&](int) { ran = true; });
    for (int i = 0; i < 100; ++i) { app->processEvents(); sleepMs(2); }
    CHECK_FALSE(ran);
}
