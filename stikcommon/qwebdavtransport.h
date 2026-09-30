#ifndef QWEBDAVTRANSPORT_H
#define QWEBDAVTRANSPORT_H

// ─────────────────────────────────────────────────────────────────────────────
// qwebdavtransport —— QWebdavLite 专用的 verb-aware 异步传输层
//
// 存在的原因：qldox/eventpoller.cpp 是只读依赖，且它只在
// `if (req.method == "POST")` 分支里设 POSTFIELDS，全文没有
// CURLOPT_CUSTOMREQUEST / CURLOPT_UPLOAD。故经 EventPoller 发出的任何非
// POST 请求都会退回 curl 默认行为——一律 GET，且请求体被丢弃。
// e2e 实测：MKCOL/MOVE/DELETE/OPTIONS/HEAD/PROPFIND 全部到达服务端时
// 变成 GET，PUT 的 9 字节 body 变成 0。
//
// 因不能改 qldox，这里自带一套 curl_multi 泵：真正把 method 传给 curl
// （CURLOPT_CUSTOMREQUEST），有请求体时走 CURLOPT_UPLOAD + READFUNCTION。
// 结果仍按 QNAM 垫片既有的 QNetworkReplyEvent 契约 postEvent 回投，
// QNetworkReply::event() 一行都不用改。
//
// 只服务 QWebdavLite / QNetworkAccessManager::issue() 的分流分支；
// GET/POST 仍由 EventPoller 处理（那边本来就对），不重复造。
// ─────────────────────────────────────────────────────────────────────────────

#include <qobject.h>
#include <qthread.h>
#include <qmutex.h>
#include <string>
#include <vector>
#include <map>

class QWebdavTransport
{
public:
    // 一次请求的完成回调（在 QWebdavTransport 自己的泵线程里被调用，
    // 实现方负责把结果 postEvent 回目标线程）。
    // aborted=true 表示传输期间被取消/超时等 CURLcode 非 0 情况。
    typedef void (*DoneCb)(void* userdata,
                           int httpCode,
                           const std::string& curlErr,
                           const std::string& body,
                           const std::map<std::string, std::string>& headers,
                           bool aborted);

    // 起泵线程（幂等）。与 EventPoller::start() 同样需要显式调用。
    //
    // ★ 一次性语义：**同一进程内 start() 之后不可再 start()**。
    //   Pump 是 QThread 单例，stop() 里的 wait() 一旦 join 过，QThread 就不可能
    //   第二次 start（Qt 明确不支持重启线程）。故 stop() 是终态：调过之后
    //   isReady() 恒为 false，后续 start() 静默无操作（不是错误）。
    //   现有唯一调用方 qlstik/src/main.cpp 就是「开机 start、退出 stop」，
    //   不需要重启；真要重启只能把 Pump 改成可重建的指针，属另一件事。
    static void start();

    // 停泵线程并 join（幂等）。进程退出前调用。
    //
    // ★ 不会阻塞：即便有在途请求（服务端收下连接却永不响应），也会主动把
    //   easy handle 从 multi 摘除后清理，毫秒级返回。见 .cpp run() 里的
    //   「停机必须主动拆掉在途 easy」注释——那里记着只置 cancel 标志会导致
    //   wait() 永久阻塞的实测事故。
    static void stop();

    static bool isReady();

    // 提交请求。立刻返回，绝不阻塞。headers 为额外请求头（会覆盖同名默认头）。
    // timeoutMsecs：**毫秒**（与 QNAM 垫片 setTransferTimeout 同语义；
    // 注意 qldox/eventpoller.h 的 HttpRequest::timeoutSec 是「秒」，两者差
    // 1000 倍，直接透传会把 60s 超时变成 60000s ≈ 16.7 小时）。
    // body 为空时不启用 UPLOAD（GET/HEAD/DELETE/MKCOL/MOVE/PROPFIND 无体场景）。
    static void send(const std::string& method,
                     const std::string& url,
                     const std::map<std::string, std::string>& headers,
                     const std::string& body,
                     int timeoutMsecs,
                     DoneCb cb,
                     void* userdata,
                     volatile bool* cancel);

private:
    QWebdavTransport();
    ~QWebdavTransport();
};

#endif // QWEBDAVTRANSPORT_H
