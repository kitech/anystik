// stikcommon/test_qconnect_slots.cpp —— qconnect_slots 垫片契约（Qt3 单端，零网络）
//
// 被测对象为 stikcommon/qconnect_slots.h：它把新式
//     connect(sender, &Sender::sig, ctx, lambda_or_PMF)
// 转成 Qt3 下"往 QNetworkReply / QNetworkAccessManager 内部槽注册表里 push 一个
// std::function"。这些 emit* 全部是纯内存调用，不碰 socket，测试零网络。
//
// ★ 必须用 `::connect` / `::disconnect` 限定调用
//
//   Qt3 的 QObject 有静态成员 connect(const QObject*, const char*,
//   const QObject*, const char*)（qobject.h:129），但**本 Qt 3.3.8 没有 connect 宏**。
//   于是在 QObject 派生类的成员函数里写非限定 connect(...) 时，名字查找在类作用域
//   命中 QObject::connect 就**停止**，垫片的全局模板连候选都进不去：
//       error: no matching function for call to 'Host::connect(...)'
//       note: there are 2 candidates        ← 全是 QObject 的，垫片的不在其中
//   两种绕法都实测通过：
//     · `::connect(...)`            —— 本文件统一用这个
//     · 函数块内 `using ::connect;` —— qwebdavlite.cpp:25,290 生产用的写法
//   qwebdavlite.h:186-193 记的正是这件事。本文件一律写 `::connect`，既是生产写法，
//   也把这条绕过锁进测试：若哪天垫片被改坏，这里会直接编译失败而不是静默退化。

#include <cstdio>
#include <string>
#include <vector>

#include "doctest/doctest.h"

#include "qconnect_slots.h"

// 槽记录用字符串，便于断言"落到哪张表、被调了几次、参数是什么"
namespace {

struct Recorder {
    std::vector<std::string> log;
    void note(const char* what) { log.push_back(what); }
};

// 接收者：刻意**不**继承 QObject。PMF 槽只需要一个可调用 ctx，
// 继承 QObject 反而会让类作用域遮蔽垫片（见文件头说明）。
struct Sink {
    Recorder* rec;
    explicit Sink(Recorder* r) : rec(r) {}

    void onVoid() { rec->note("void"); }
    void onProgress(qint64 a, qint64 b)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "progress:%lld:%lld",
                      (long long)a, (long long)b);
        rec->note(buf);
    }
    void onError(QNetworkReply::NetworkError e)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "error:%d", (int)e);
        rec->note(buf);
    }
    void onRedirect(const QUrl& u)
    {
        rec->note(std::string("redirect:") + u.toString().utf8());
    }
    void onReply(QNetworkReply* r)
    {
        rec->note(r ? "reply:ptr" : "reply:null");
    }
    void onAuth(QNetworkReply* r, QAuthenticator* a)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "auth:%s",
                      (r && a) ? "both" : "one");
        rec->note(buf);
    }
};

} // namespace

// ── 无参信号：按 sig 取址分表 ──────────────────────────────────────

TEST_CASE("qconnect_slots: finished 与 readyRead 落不同的表")
{
    Recorder rec;
    QNetworkReply reply;

    ::connect(&reply, &QNetworkReply::finished, (void*)0,
              [&rec]() { rec.note("fin"); });
    ::connect(&reply, &QNetworkReply::readyRead, (void*)0,
              [&rec]() { rec.note("rr"); });

    // 分表成功：各表各一个，互不串台
    CHECK(reply.slots_finished().size() == 1);
    CHECK(reply.slots_readyRead().size() == 1);

    reply.emitFinished();
    CHECK(rec.log.size() == 1);
    CHECK(rec.log[0] == "fin");

    reply.emitReadyRead();
    CHECK(rec.log.size() == 2);
    CHECK(rec.log[1] == "rr");
}

TEST_CASE("qconnect_slots: metaDataChanged 被忽略（无对应槽表）")
{
    Recorder rec;
    QNetworkReply reply;

    ::connect(&reply, &QNetworkReply::metaDataChanged, (void*)0,
              [&rec]() { rec.note("mdc"); });

    // qconnect_slots.h 无参重载的 else 分支注明"metaDataChanged 无专门槽表，忽略"
    CHECK(reply.slots_finished().size() == 0);
    CHECK(reply.slots_readyRead().size() == 0);
    // 没有可触发的 metaDataChanged 发射器，这里只断言它没被误投到 finished
    CHECK(rec.log.empty());
}

// ── 两参信号：upload / download 必须按 sig 区分 ────────────────────

TEST_CASE("qconnect_slots: lambda 版 uploadProgress/downloadProgress 分表")
{
    Recorder rec;
    QNetworkReply reply;

    // 两者签名完全相同，只能靠成员函数取址区分 —— 这正是垫片存在的原因
    ::connect(&reply, &QNetworkReply::uploadProgress, (void*)0,
              [&rec](qint64, qint64) { rec.note("up"); });
    ::connect(&reply, &QNetworkReply::downloadProgress, (void*)0,
              [&rec](qint64, qint64) { rec.note("down"); });

    CHECK(reply.slots_uploadProgress().size() == 1);
    CHECK(reply.slots_downloadProgress().size() == 1);

    reply.emitUpload(10, 100);
    CHECK(rec.log.size() == 1);
    CHECK(rec.log[0] == "up");

    reply.emitDownload(20, 200);
    CHECK(rec.log.size() == 2);
    CHECK(rec.log[1] == "down");
}

TEST_CASE("qconnect_slots: ⚠ PMF 版两参信号忽略 sig，一律落 downloadProgress")
{
    Recorder rec;
    QNetworkReply reply;
    Sink sink(&rec);

    // 与上面的 lambda 版对照：这里传 uploadProgress，PMF 版却按 downloadProgress 处理
    ::connect(&reply, &QNetworkReply::uploadProgress, &sink,
              &Sink::onProgress);

    // ⚠ 缺陷：qconnect_slots.h 的两参 PMF 重载里写的是
    //     sender->slots_downloadProgress().push_back(...)
    //   完全没有 if (sig == &QNetworkReply::uploadProgress) 分支，sig 被 (void) 掉。
    //   同文件里 lambda 版是有区分的（upload→slots_uploadProgress），两者不一致。
    //   后果：把 uploadProgress 挂到成员函数槽上时，槽被塞进下载表，
    //   于是 emitUpload 不触发、emitDownload 反而触发。
    //   生产影响：qwebdavlite.cpp 只连 error / authenticationRequired，
    //   两参进度信号没有走 PMF 版，故当前无实际损害。
    CHECK(reply.slots_uploadProgress().size() == 0);
    CHECK(reply.slots_downloadProgress().size() == 1);

    reply.emitUpload(1, 2);
    CHECK(rec.log.empty());            // 上传触发器不响 —— 印证缺陷
    reply.emitDownload(3, 4);
    CHECK(rec.log.size() == 1);        // 下载触发器才响
    CHECK(rec.log[0] == "progress:3:4");
}

// ── 一参信号 ───────────────────────────────────────────────────────

TEST_CASE("qconnect_slots: error 落 slots_error")
{
    Recorder rec;
    QNetworkReply reply;

    ::connect(&reply, &QNetworkReply::error, (void*)0,
              [&rec](QNetworkReply::NetworkError) { rec.note("e1"); });
    CHECK(reply.slots_error().size() == 1);

    reply.emitError(static_cast<QNetworkReply::NetworkError>(42));
    CHECK(rec.log.size() == 1);
    CHECK(rec.log[0] == "e1");

    // ⚠ errorOccurred 在 qnam_shim.h:265 有声明，但 qnam_shim.cpp 的信号桩区
    //   （:221-227）只定义了 finished/readyRead/uploadProgress/downloadProgress/
    //   error/redirected/metaDataChanged 七个，**没有 errorOccurred 的定义**。
    //   取址一个无定义的成员函数会直接链接失败，所以本套件无法为它写用例，
    //   生产也连不了它。qwebdavlite.cpp 正是靠这点安全：它把 errorOccurred 放在
    //   `#else`（Qt4+）分支里，Qt3 分支连的是有定义的 error。
    //   签名完全相同的两个信号因此在 Qt3 侧只有 error 可用。
    CHECK(reply.slots_error().size() == 1);   // 再确认一次表未被旁路改动
}

TEST_CASE("qconnect_slots: redirected / redirectAllowed 都落 slots_redirected")
{
    Recorder rec;
    QNetworkReply reply;
    const QUrl target("http://example.com/moved");

    ::connect(&reply, &QNetworkReply::redirected, (void*)0,
              [&rec](const QUrl& u) {
                  rec.note(std::string("rd:") + u.toString().utf8());
              });
    CHECK(reply.slots_redirected().size() == 1);

    reply.emitRedirected(target);
    CHECK(rec.log.size() == 1);
    CHECK(rec.log[0] == "rd:http://example.com/moved");

    // emitRedirectAllowed 是 emitRedirected 的别名
    rec.log.clear();
    reply.emitRedirectAllowed(target);
    CHECK(rec.log.size() == 1);
    CHECK(rec.log[0] == "rd:http://example.com/moved");
}

TEST_CASE("qconnect_slots: 可连接的信号面只有 7 个（sslErrors 无桩定义，连不上）")
{
    // qnam_shim.cpp:221-227 的信号桩只给了 7 个定义：
    //   finished / readyRead / uploadProgress / downloadProgress / error /
    //   redirected / metaDataChanged
    // 另三个只有声明没有定义，取址即链接失败：
    //   errorOccurred / redirectAllowed / sslErrors
    // qconnect_slots.h 仍为 sslErrors 写了两个重载（lambda 版 + PMF 版），
    // 在当前 qnam_shim.cpp 下是无法使用的死代码。生产无调用方（已 grep 确认），
    // 故此处只把"可连接面"逐个验证一遍并记录缺口。
    Recorder rec;
    QNetworkReply reply;
    Sink sink(&rec);

    // 7 个信号逐个接上，各落各表
    ::connect(&reply, &QNetworkReply::finished, &sink, &Sink::onVoid);
    ::connect(&reply, &QNetworkReply::readyRead, &sink, &Sink::onVoid);
    ::connect(&reply, &QNetworkReply::uploadProgress, (void*)0,
              [](qint64, qint64) {});
    ::connect(&reply, &QNetworkReply::downloadProgress, (void*)0,
              [](qint64, qint64) {});
    ::connect(&reply, &QNetworkReply::error, &sink, &Sink::onError);
    ::connect(&reply, &QNetworkReply::redirected, &sink, &Sink::onRedirect);
    ::connect(&reply, &QNetworkReply::metaDataChanged, (void*)0,
              [&rec]() { rec.note("mdc"); });

    CHECK(reply.slots_finished().size() == 1);
    CHECK(reply.slots_readyRead().size() == 1);
    CHECK(reply.slots_uploadProgress().size() == 1);
    CHECK(reply.slots_downloadProgress().size() == 1);
    CHECK(reply.slots_error().size() == 1);
    CHECK(reply.slots_redirected().size() == 1);
    // metaDataChanged 无表 → 忽略
    CHECK(reply.slots_sslErrors().size() == 0);
    CHECK(rec.log.empty());
}

// ── 多槽与顺序 ─────────────────────────────────────────────────────

TEST_CASE("qconnect_slots: 同表多槽按注册顺序全部触发")
{
    Recorder rec;
    QNetworkReply reply;

    ::connect(&reply, &QNetworkReply::finished, (void*)0,
              [&rec]() { rec.note("a"); });
    ::connect(&reply, &QNetworkReply::finished, (void*)0,
              [&rec]() { rec.note("b"); });
    ::connect(&reply, &QNetworkReply::finished, (void*)0,
              [&rec]() { rec.note("c"); });

    CHECK(reply.slots_finished().size() == 3);
    reply.emitFinished();
    CHECK(rec.log.size() == 3);
    CHECK(rec.log[0] == "a");
    CHECK(rec.log[1] == "b");
    CHECK(rec.log[2] == "c");
}

// ── 成员函数指针槽 ─────────────────────────────────────────────────

TEST_CASE("qconnect_slots: PMF 版 void 槽按 sig 分表并绑定 ctx")
{
    Recorder rec;
    QNetworkReply reply;
    Sink sink(&rec);

    ::connect(&reply, &QNetworkReply::readyRead, &sink, &Sink::onVoid);
    CHECK(reply.slots_readyRead().size() == 1);
    CHECK(reply.slots_finished().size() == 0);
    reply.emitReadyRead();
    CHECK(rec.log.size() == 1);
    CHECK(rec.log[0] == "void");

    // 再挂 finished，应进另一张表
    ::connect(&reply, &QNetworkReply::finished, &sink, &Sink::onVoid);
    CHECK(reply.slots_finished().size() == 1);
    rec.log.clear();
    reply.emitFinished();
    CHECK(rec.log.size() == 1);
    CHECK(rec.log[0] == "void");
}

TEST_CASE("qconnect_slots: PMF 版一参槽（error / redirect）")
{
    Recorder rec;
    QNetworkReply reply;
    Sink sink(&rec);

    ::connect(&reply, &QNetworkReply::error, &sink, &Sink::onError);
    CHECK(reply.slots_error().size() == 1);
    reply.emitError(static_cast<QNetworkReply::NetworkError>(7));
    CHECK(rec.log.size() == 1);
    CHECK(rec.log[0] == "error:7");

    rec.log.clear();
    ::connect(&reply, &QNetworkReply::redirected, &sink, &Sink::onRedirect);
    CHECK(reply.slots_redirected().size() == 1);
    reply.emitRedirected(QUrl("http://a.b/c"));
    CHECK(rec.log.size() == 1);
    CHECK(rec.log[0] == "redirect:http://a.b/c");
}

// ── QNetworkAccessManager 发送者 ───────────────────────────────────

TEST_CASE("qconnect_slots: manager finished / authenticationRequired 分表")
{
    Recorder rec;
    QNetworkAccessManager nam;
    Sink sink(&rec);
    QNetworkReply reply;

    ::connect(&nam, &QNetworkAccessManager::finished, &sink, &Sink::onReply);
    ::connect(&nam, &QNetworkAccessManager::authenticationRequired, &sink,
              &Sink::onAuth);

    CHECK(nam.slots_finished().size() == 1);
    CHECK(nam.slots_authRequired().size() == 1);

    nam.emitFinished(&reply);
    CHECK(rec.log.size() == 1);
    CHECK(rec.log[0] == "reply:ptr");

    QAuthenticator auth;
    nam.emitAuthenticationRequired(&reply, &auth);
    CHECK(rec.log.size() == 2);
    CHECK(rec.log[1] == "auth:both");

    // ⚠ manager 侧无 disconnect 垫片：qconnect_slots.h 只为 QNetworkReply 提供了
    //   disconnect，manager 收尾清理得靠 QNetworkAccessManager 自己的方法。
    //   qwebdavlite 每轮新建实例，不受影响，故此处只作记录，不断言。
}

// ── disconnect ─────────────────────────────────────────────────────

TEST_CASE("qconnect_slots: disconnect 清空整张表（无连接 id）")
{
    Recorder rec;
    QNetworkReply reply;
    Sink sink(&rec);

    ::connect(&reply, &QNetworkReply::finished, (void*)0,
              [&rec]() { rec.note("f1"); });
    ::connect(&reply, &QNetworkReply::finished, &sink, &Sink::onVoid);
    CHECK(reply.slots_finished().size() == 2);

    // 垫片不返回连接 id，disconnect 一律"清空对应槽表"
    // （qconnect_slots.h 末尾注释：QWebdav 每轮新建实例，语义等价）
    ::disconnect(&reply, &QNetworkReply::finished, &sink, &Sink::onVoid);
    CHECK(reply.slots_finished().size() == 0);

    rec.log.clear();
    reply.emitFinished();
    CHECK(rec.log.empty());        // 连同另一个 lambda 槽一起被清掉了

    // readyRead 走另一张表，不受 finished 的 disconnect 影响
    ::connect(&reply, &QNetworkReply::readyRead, (void*)0,
              [&rec]() { rec.note("rr"); });
    ::disconnect(&reply, &QNetworkReply::finished, (void*)0, (void*)0);
    CHECK(reply.slots_readyRead().size() == 1);
    reply.emitReadyRead();
    CHECK(rec.log.size() == 1);
    CHECK(rec.log[0] == "rr");
}

TEST_CASE("qconnect_slots: disconnect error / redirected 亦清整表")
{
    Recorder rec;
    QNetworkReply reply;
    Sink sink(&rec);

    ::connect(&reply, &QNetworkReply::error, &sink, &Sink::onError);
    ::connect(&reply, &QNetworkReply::redirected, &sink, &Sink::onRedirect);
    CHECK(reply.slots_error().size() == 1);
    CHECK(reply.slots_redirected().size() == 1);

    ::disconnect(&reply, &QNetworkReply::error, &sink, &Sink::onError);
    ::disconnect(&reply, &QNetworkReply::redirected, &sink, &Sink::onRedirect);
    CHECK(reply.slots_error().size() == 0);
    CHECK(reply.slots_redirected().size() == 0);

    rec.log.clear();
    reply.emitError(static_cast<QNetworkReply::NetworkError>(1));
    reply.emitRedirected(QUrl("http://x/"));
    CHECK(rec.log.empty());
}
