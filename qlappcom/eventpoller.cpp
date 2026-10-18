#include "eventpoller.h"
#include "limelog.h"
#include <unistd.h>

EventPoller* EventPoller::s_instance = nullptr;

std::string webUrlEncode(const std::string& str) {
    CURL* curl = curl_easy_init();
    if (!curl) { return str; }
    char* encoded = curl_easy_escape(curl, str.c_str(), (int)str.length());
    std::string result = encoded ? encoded : str;
    if (encoded) { curl_free(encoded); }
    curl_easy_cleanup(curl);
    return result;
}

static size_t writeCb(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), total);
    return total;
}

static size_t headerCb(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    auto* headers = static_cast<std::map<std::string, std::string>*>(userp);
    std::string line(static_cast<char*>(contents), total);
    size_t colon = line.find(':');
    if (colon == std::string::npos || colon == 0) { return total; }
    std::string name = line.substr(0, colon);
    size_t vStart = colon + 1;
    while (vStart < line.size() && (line[vStart] == ' ' || line[vStart] == '\t')) { vStart++; }
    size_t vEnd = line.size();
    while (vEnd > vStart && (line[vEnd - 1] == '\r' || line[vEnd - 1] == '\n')) vEnd--;
    if (vEnd <= vStart) { return total; }
    for (char& c : name) { c = static_cast<char>(tolower(static_cast<unsigned char>(c))); }
    std::string& dst = (*headers)[name];
    if (name == "set-cookie" && !dst.empty()) {
        dst += "\n";
        dst += line.substr(vStart, vEnd - vStart);
    } else {
        dst = line.substr(vStart, vEnd - vStart);
    }
    return total;
}

// 统一失败终态：请求无法被调度时立即回调一次，保证 done() 恰好被调用一次
static void curlFailNow(const char* why,
                        void (*done)(const HttpResponse& resp, void* udata),
                        void* udata) {
    if (!done) { return; }
    HttpResponse resp;
    resp.httpCode = 0;
    resp.curlErrStr = why;
    done(resp, udata);
}

// 下载进度回调（curl_multi pump 线程内）：≥100ms 或传输完成才转发一次
static int xferinfoCb(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                      curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal; (void)ulnow;
    auto* ctx = static_cast<HttpCtx*>(clientp);
    if (!ctx || !ctx->progress) { return 0; }
    bool done = (dltotal > 0 && dlnow >= dltotal);
    long long dMs = elapsedMs(ctx->lastEmitTp);
    if (done || dMs >= 100) {
        long long dByt = (long long)dlnow - ctx->lastEmitBytes;
        long long speed = 0;
        if (dMs > 0 && dByt >= 0) { speed = dByt * 1000 / dMs; }
        ctx->lastEmitBytes = (long long)dlnow;
        ctx->lastEmitTp = timeNow();
        // 看门狗"活动"只看真实字节推进：零字节发射（zombie 连接/挂起上游）不得重置停滞计时
        if (dByt > 0) { ctx->lastActiveTp = timeNowBoot(); }
        // 零字节且未完成时不发进度事件（避免无效重绘）；done 仍照发
        if (done || dByt > 0) {
            ctx->progress((long long)dlnow, (long long)dltotal, speed, ctx->udata);
        }
    }
    return 0;
}

EventPoller::EventPoller()
    : QThread(), running(true), multi(nullptr) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    multi = curl_multi_init();
    s_instance = this;
}

void EventPoller::start() {
    if (!s_instance) {
        auto* ep = new EventPoller();
        ep->QThread::start();
    }
}

void EventPoller::stop() {
    if (!s_instance) { return; }
    s_instance->running = false;
    s_instance->wait();
    // 停机时仍未进入 multi 的请求：补一次失败终态，杜绝"受理后无回调"的悬挂
    std::deque<CURL*> orphanHandles;
    std::deque<DelayedReq> orphanDelayed;
    {
        QMutexLocker lock(&s_instance->pendingMutex);
        orphanHandles.swap(s_instance->pendingHandles);
        orphanDelayed.swap(s_instance->delayedReqs);
    }
    for (CURL* easy : orphanHandles) {
        HttpCtx* c = nullptr;
        curl_easy_getinfo(easy, CURLINFO_PRIVATE, &c);
        curl_easy_cleanup(easy);
        if (c) {
            if (c->requestHeaders) { curl_slist_free_all(c->requestHeaders); }
            curlFailNow("poller stopped", c->done, c->udata);
            delete c;
        }
    }
    for (DelayedReq& d : orphanDelayed) {
        curlFailNow("poller stopped", d.done, d.udata);
    }
    if (s_instance->multi) {
        curl_multi_cleanup(s_instance->multi);
        s_instance->multi = nullptr;
    }
    delete s_instance;
    s_instance = nullptr;
}

bool EventPoller::isReady() { return s_instance && s_instance->multi; }

void EventPoller::addRequest(const HttpRequest& req,
                              void (*done)(const HttpResponse& resp, void* udata),
                              void* udata) {
    if (!s_instance || !s_instance->multi) {
        ALOG_WARN("addRequest dropped: EventPoller not ready", req.method, req.url);
        curlFailNow("poller not ready", done, udata);
        return;
    }

    ALOG_INFO(">>", req.method, req.url);

    QMutexLocker lock(&s_instance->pendingMutex);

    // 非阻塞延迟调度：仅入队，pump 线程到点后才真正发出（不阻塞任何线程）
    if (req.delayMs > 0) {
        DelayedReq d;
        d.req = req;
        d.done = done;
        d.udata = udata;
        d.readyAt = timeFromNowBoot(req.delayMs);
        s_instance->delayedReqs.push_back(d);
        ALOG_INFO("delayed", req.delayMs, "ms");
        return;
    }

    CURL* easy = buildHandle(req, done, udata);
    if (easy) { s_instance->pendingHandles.push_back(easy); }
    else { curlFailNow("easy handle init failed", done, udata); }
}

// 构造 curl easy + HttpCtx（全程不碰 multi，可在 GUI/泵线程安全调用）
CURL* EventPoller::buildHandle(const HttpRequest& req,
                                void (*done)(const HttpResponse& resp, void* udata),
                                void* udata) {
    auto t0 = timeNow();

    CURL* easy = curl_easy_init();
    if (!easy) {
        ALOG_WARN("addRequest dropped: curl_easy_init failed", req.method, req.url);
        return nullptr;
    }

    auto* ctx = new HttpCtx{req.url, req.data,
                             std::string(), std::map<std::string, std::string>(),
                             nullptr, done, udata, req.progress,
                             timeNow(), timeNow(), 0, 0, timeNowBoot(), req.stallSec};

    curl_easy_setopt(easy, CURLOPT_URL, ctx->urlStr.c_str());
    curl_easy_setopt(easy, CURLOPT_PRIVATE, ctx);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &ctx->body);
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, headerCb);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, &ctx->headers);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, (long)req.timeoutSec);
    if (req.connectTimeoutSec > 0) {
        curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, (long)req.connectTimeoutSec);
    }
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(easy, CURLOPT_FORBID_REUSE, 1L);
    curl_easy_setopt(easy, CURLOPT_FRESH_CONNECT, 1L);
    curl_easy_setopt(easy, CURLOPT_SSL_SESSIONID_CACHE, 0L);

    // 代理：严格只认调用方设定（req.proxy）。
    // 无条件设置（含空串）以关闭 libcurl 隐式环境变量代理回退；空串 = 直连。
    // NO_PROXY/no_proxy 仍由 libcurl 原生生效（命中可绕过此处设定）。
    curl_easy_setopt(easy, CURLOPT_PROXY, req.proxy.c_str());

    if (req.lowSpeedLimit > 0 && req.lowSpeedTime > 0) {
        curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, req.lowSpeedLimit);
        curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, req.lowSpeedTime);
    }

    // 下载进度：≥100ms 或传输完成才转发一次，避免高频重绘刷新
    if (req.progress) {
        curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, xferinfoCb);
        curl_easy_setopt(easy, CURLOPT_XFERINFODATA, ctx);
    }

    if (req.method == "POST") {
        curl_easy_setopt(easy, CURLOPT_POSTFIELDS, ctx->postData.c_str());
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, (long)ctx->postData.size());
    }

    // PUT 走原始字节体（storage.to 三段流的中间上传）；其余方法保持 libcurl 默认
    if (req.method == "PUT") {
        curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(easy, CURLOPT_POSTFIELDS, ctx->postData.c_str());
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, (long)ctx->postData.size());
    }

    // 默认不跟随重定向；仅显式请求的服务开启（既有调用方行为零变化）
    if (req.followRedirects) {
        curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 5L);
    }

    if (!req.extraHeaders.empty()) {
        for (const auto& h : req.extraHeaders) {
            std::string hv = h.first + ": " + h.second;
            ctx->requestHeaders = curl_slist_append(ctx->requestHeaders, hv.c_str());
        }
        curl_easy_setopt(easy, CURLOPT_HTTPHEADER, ctx->requestHeaders);
    }

    ALOG_INFO("addRequest setup took", timeSince(t0), "for", ctx->urlStr);
    return easy;
}

// multi 级错误恢复（泵线程）：libcurl 文档规定 curl_multi_perform 返回错误后，该
// multi 上所有传输状态不可继续保持，必须移除全部句柄后再重建；否则后续 perform
// 不会推进，在途句柄永不完成（连 CURLOPT_TIMEOUT 也不会被处理），事件长轮询链
// 会静默永久死亡。此处按文档顺序安全拆除（先 remove 全部句柄 → 释放 easy/头/ctx
// → multi_cleanup），重建 multi，并逐个终态化在途请求，让上层既有重试逻辑
// （events poll → schedulePoll(2000)）接管。句柄与内存均无泄漏。
void EventPoller::recoverMulti(const char* where, int mcode) {
    ALOG_ERROR("!! multi error:", where, curl_multi_strerror((CURLMcode)mcode),
               "code=", mcode, "active=", activeHandles.size());
    // 取出在途句柄并清空 active（此后不再有新成员进入本次拆除集合）
    std::vector<CURL*> dead = activeHandles;
    activeHandles.clear();
    // 1) 先逐个从旧 multi 摘除（curl_easy_cleanup 前必须 curl_multi_remove_handle）
    for (CURL* easy : dead) {
        CURLMcode rc = curl_multi_remove_handle(multi, easy);
        if (rc != CURLM_OK) {
            ALOG_WARN("!! recover: curl_multi_remove_handle failed:",
                      curl_multi_strerror(rc));
        }
    }
    // 2) 释放旧 multi 并重建（此刻旧 multi 已无挂载句柄，可安全 cleanup）
    if (multi) { curl_multi_cleanup(multi); }
    multi = curl_multi_init();
    if (!multi) {
        ALOG_ERROR("!! curl_multi_init failed after multi error");
    }
    // 3) 终态化每个在途请求：释放 easy + 请求头 + ctx，回调 done（带错误）
    for (CURL* easy : dead) {
        HttpCtx* c = nullptr;
        curl_easy_getinfo(easy, CURLINFO_PRIVATE, &c);
        curl_easy_cleanup(easy);
        if (c) {
            if (c->requestHeaders) { curl_slist_free_all(c->requestHeaders); }
            HttpResponse resp;
            resp.httpCode = 0;
            resp.curlErrStr = std::string("multi error: ")
                            + curl_multi_strerror((CURLMcode)mcode);
            c->done(resp, c->udata);
            delete c;
        }
    }
}

void EventPoller::run() {
    TimePoint lastBeatTp = timeNowBoot();
    TimePoint lastDoneTp = timeNowBoot();
    while (running) {
        // 泵线程诊断心跳：每 30s 打印存活性/负载。
        // 若心跳随日志消失 → 泵线程自身卡死；若心跳照常但 active=0 → 轮询链死（无在途请求）
        if (elapsedMsBoot(lastBeatTp) >= 30000) {
            size_t pendingN = 0, delayedN = 0;
            {
                QMutexLocker lock(&pendingMutex);
                pendingN = pendingHandles.size();
                delayedN = delayedReqs.size();
            }
            ALOG_DEBUG("pump alive: active=", activeHandles.size(),
                      "pending=", pendingN, "delayed=", delayedN,
                      "since_last_done=", elapsedMsBoot(lastDoneTp), "ms");
            lastBeatTp = timeNowBoot();
        }
        // 1) 泵线程内统一 add_handle —— libcurl multi 非线程安全，
        //    所有 multi 操作只能在泵线程发生，GUI 线程只入挂起队列。
        //    先处理到点的非阻塞延迟重发（HttpRequest.delayMs）
        {
            QMutexLocker lock(&pendingMutex);
            while (!delayedReqs.empty() && elapsedMsBoot(delayedReqs.front().readyAt) >= 0) {
                DelayedReq d = delayedReqs.front();
                delayedReqs.pop_front();
                CURL* easy = buildHandle(d.req, d.done, d.udata);
                if (easy) { pendingHandles.push_back(easy); }
                else { curlFailNow("easy handle init failed", d.done, d.udata); }
            }
            while (!pendingHandles.empty()) {
                CURL* easy = pendingHandles.front();
                pendingHandles.pop_front();
                CURLMcode rc = curl_multi_add_handle(multi, easy);
                if (rc != CURLM_OK) {
                    HttpCtx* c = nullptr;
                    curl_easy_getinfo(easy, CURLINFO_PRIVATE, &c);
                    ALOG_ERROR("!! curl_multi_add_handle failed:",
                               curl_multi_strerror(rc),
                               c ? c->urlStr : std::string("(no ctx)"));
                    // 终态化该请求，避免 done 永不回调导致上层悬挂
                    curl_easy_cleanup(easy);
                    if (c) {
                        if (c->requestHeaders) { curl_slist_free_all(c->requestHeaders); }
                        HttpResponse resp;
                        resp.httpCode = 0;
                        resp.curlErrStr = std::string("add_handle: ")
                                        + curl_multi_strerror(rc);
                        c->done(resp, c->udata);
                        delete c;
                    }
                } else {
                    activeHandles.push_back(easy);
                }
            }
        }

        // 2) 阻塞等待 I/O：curl_multi_poll 官方推荐，无事件也会休眠（最多 20ms），
        //    避免对"永远活跃"的 socket 形成无节流忙循环。
        int numfds = 0;
        CURLMcode pollRc = curl_multi_poll(multi, NULL, 0, 20, &numfds);
        if (pollRc != CURLM_OK) {
            recoverMulti("curl_multi_poll", (int)pollRc);
            continue;
        }

        // 3) 每轮只调用一次 curl_multi_perform，绝不用废弃的
        //    `while (perform == CURLM_CALL_MULTI_PERFORM)` 忙循环。
        int stillRunning = 0;
        CURLMcode perfRc = curl_multi_perform(multi, &stillRunning);
        if (perfRc != CURLM_OK && perfRc != CURLM_CALL_MULTI_PERFORM) {
            recoverMulti("curl_multi_perform", (int)perfRc);
            continue;
        }

        // 3.5) 停滞看门狗：任意设置 stallSec 的请求（含 event poll）连续无收发活动
        //      （boottime 时钟，休眠时间照算）→ 取证真实网络状态后中断
        {
            std::vector<HttpCtx*> stalled;
            for (size_t i = 0; i < activeHandles.size(); ++i) {
                HttpCtx* c = nullptr;
                curl_easy_getinfo(activeHandles[i], CURLINFO_PRIVATE, &c);
                if (!c || c->stallSec <= 0) { continue; }
                if (elapsedMsBoot(c->lastActiveTp) <= (long long)c->stallSec * 1000) { continue; }
                stalled.push_back(c);
            }
            for (HttpCtx* c : stalled) {
                for (size_t idx = 0; idx < activeHandles.size(); ++idx) {
                    CURL* h = activeHandles[idx];
                    HttpCtx* cc = nullptr;
                    curl_easy_getinfo(h, CURLINFO_PRIVATE, &cc);
                    if (cc != c) { continue; }
                    // ── 取证真实网络状态（不凭猜测重置 UI）──
                    long respCode = 0; long long recv = 0, expect = 0;
                    double speed = 0, totalT = 0, connectT = 0, startT = 0;
                    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &respCode);
                    curl_easy_getinfo(h, CURLINFO_SIZE_DOWNLOAD_T, &recv);
                    curl_easy_getinfo(h, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &expect);
                    curl_easy_getinfo(h, CURLINFO_SPEED_DOWNLOAD, &speed); // dep
                    curl_easy_getinfo(h, CURLINFO_SPEED_DOWNLOAD_T, &speed);
                    curl_easy_getinfo(h, CURLINFO_TOTAL_TIME, &totalT);
                    curl_easy_getinfo(h, CURLINFO_CONNECT_TIME, &connectT);
                    curl_easy_getinfo(h, CURLINFO_STARTTRANSFER_TIME, &startT);
                    std::string phase = (connectT <= 0) ? "not_connected"
                        : (startT <= 0 ? "waiting_headers" : "in_body");
                    std::string reason = "stall: no data for " + std::to_string(c->stallSec) + "s"
                        + " http=" + std::to_string(respCode)
                        + " got=" + std::to_string(recv)
                        + "/" + std::to_string(expect)
                        + " speed=" + std::to_string((long)speed) + "B/s"
                        + " elapsed=" + std::to_string((long)totalT) + "s"
                        + " phase=" + phase;
                    ALOG_WARN("!! stall timeout", c->urlStr, reason,
                              "idx=" + std::to_string(idx) + "/" + std::to_string(activeHandles.size()));

                    HttpResponse resp;
                    resp.httpCode = (int)respCode;   // 0 = 尚未收到响应头，如实反映
                    resp.curlErrStr = reason;
                    CURLMcode rmRc = curl_multi_remove_handle(multi, h);
                    if (rmRc != CURLM_OK) {
                        ALOG_WARN("!! curl_multi_remove_handle failed:",
                                  curl_multi_strerror(rmRc), c->urlStr,
                                  "idx=" + std::to_string(idx));
                    }
                    activeHandles.erase(activeHandles.begin() + idx);
                    curl_easy_cleanup(h);
                    if (c->requestHeaders) { curl_slist_free_all(c->requestHeaders); }
                    c->done(resp, c->udata);
                    delete c;
                    break;
                }
            }
        }

        // 4) 兜底防空转：无 fd 可等且无在跑 handle 时主动休眠
        if (numfds == 0 && stillRunning == 0) {
            qSleepMs(10);
        }

        // 5) 无条件派发完成消息，防止被任意分支饿死
        CURLMsg* msg;
        int left;
        while ((msg = curl_multi_info_read(multi, &left))) {
            if (msg->msg == CURLMSG_DONE) {
                HttpCtx* ctx;
                curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &ctx);
                long httpCode = 0;
                curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &httpCode);
                int curlResult = msg->data.result;
                HttpResponse resp;
                resp.httpCode = (int)httpCode;
                if (curlResult != 0) {
                    resp.curlErrStr = curl_easy_strerror((CURLcode)curlResult);
                }
                resp.body = std::move(ctx->body);
                resp.headers = std::move(ctx->headers);
                double totalTime = 0.0;
                curl_easy_getinfo(msg->easy_handle, CURLINFO_TOTAL_TIME, &totalTime);
                resp.elapsedMs = (int64_t)(totalTime * 1000);
                char* effUrl = nullptr;
                if (curl_easy_getinfo(msg->easy_handle, CURLINFO_EFFECTIVE_URL, &effUrl) == CURLE_OK
                        && effUrl) {
                    resp.finalUrl = effUrl;
                }

                if (curlResult != 0) {
                    ALOG_WARN("!!", ctx->urlStr, resp.curlErrStr);
                }
                if (httpCode >= 400 && !resp.body.empty()) {
                    std::string snippet = resp.body.substr(0, 99);
                    ALOG_ERROR("<<", httpCode, ctx->urlStr,
                        resp.elapsedMs, "ms", resp.body.size(), "bytes",
                        "| body:", snippet);
                } else {
                    ALOG_INFO("<<", httpCode, ctx->urlStr,
                        resp.elapsedMs, "ms", resp.body.size(), "bytes");
                }

                CURLMcode rmRc = curl_multi_remove_handle(multi, msg->easy_handle);
                if (rmRc != CURLM_OK) {
                    ALOG_WARN("!! curl_multi_remove_handle failed:",
                              curl_multi_strerror(rmRc), ctx->urlStr);
                }
                for (auto it = activeHandles.begin(); it != activeHandles.end(); ++it) {
                    if (*it == msg->easy_handle) { activeHandles.erase(it); break; }
                }
                curl_easy_cleanup(msg->easy_handle);

                if (ctx->requestHeaders) {
                    curl_slist_free_all(ctx->requestHeaders);

                }

                ctx->done(resp, ctx->udata);
                delete ctx;
                lastDoneTp = timeNowBoot();
            }
        }
    }
    ALOG_INFO("EventPoller thread exited");
}
