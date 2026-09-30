#include "qwebdavtransport.h"

#include <qwaitcondition.h>
#include <curl/curl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <algorithm>

// qint64：Qt 3.5 的 qglobal.h **没有** typedef（Qt4 起才补上），项目统一由
// qglobaltype_shim.h 在 QT_VERSION < 0x040000 分支里补。空闲计时要用它。
#include "qglobaltype_shim.h"

// ─────────────────────────────────────────────────────────────────────────────
// 泵线程与任务表
//
// 参照 移植计划 §5.4「必须 qlstik 自行解决的点（不改 qltox）」的 a. 取消 一节：
// addRequest 无句柄可取消，故 udata 内放取消标志，置位后泵自行收尾并静默
// 丢弃数据（不重复投递结果）。本传输层直接持有自己的 easy handle，
// 取消更直接：置 cancel 标志 + curl_multi_remove。
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// 单调时钟（毫秒）。不能用 time(NULL)/gettimeofday：前者秒粒度、后者会被
// 调时（夏令时/NTP 回拨）打断，导致「两次进度间隔」算出负数或超大值。
// CLOCK_MONOTONIC 不受调时影响，正是空闲计时需要的语义。
qint64 nowMs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (qint64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

struct Task
{
    CURL* easy;
    std::string key;
    std::string respBody;
    std::map<std::string, std::string> respHeaders;  // 头名统一小写
    int status;
    std::string uploadBuf;     // 请求体
    size_t uploadPos;
    volatile bool* cancel;     // 调用方持有；非 0 且置位则中止
    bool reported;             // 防止重复投递
    QWebdavTransport::DoneCb cb;
    void* ud;
    curl_slist* hdrs;
    // ── 空闲超时 ──
    // 语义：**连续无字节流动**的毫秒数；<=0 关闭。任何方向的字节流动
    // （响应头/响应体/上传体）都重置 lastProgressMs。
    //
    // ★ 为什么不用 CURLOPT_TIMEOUT：那是「总时长」上限，与 davbisync 的需求
    //   相反。davbisync.cpp:102 写的是「空闲超时：仅在连续无字节传输时计时，
    //   且随上传/下载进度重置；大图留余量」，它传 90000ms 是为了让慢速大图
    //   传完，不是给整个传输 90s 硬上限。按总时长实现，6 秒慢速吐 30 字节
    //   的传输会被 2s 限额误杀（探针 /dribble 就是这个判别式）。
    int idleTimeoutMs;
    qint64 lastProgressMs;

    Task() : easy(0), status(0), uploadPos(0), cancel(0), reported(false),
             cb(0), ud(0), hdrs(0), idleTimeoutMs(0), lastProgressMs(0) {}
};

size_t writeCb(char* ptr, size_t sz, size_t nm, void* ud)
{
    Task* t = (Task*)ud;
    if (t != 0 && sz * nm > 0) {
        t->respBody.append(ptr, sz * nm);
        t->lastProgressMs = nowMs();   // 下行有字节 → 重置空闲计时
    }
    return sz * nm;
}

// UPLOAD（PUT 等带体动词）的请求体数据源
size_t readCb(char* buf, size_t sz, size_t nm, void* ud)
{
    Task* t = (Task*)ud;
    if (t == 0) {
        return 0;
    }
    const size_t want = sz * nm;
    const size_t left = t->uploadBuf.size() - t->uploadPos;
    if (left == 0) {
        return 0;   // 供完，curl 结束上传
    }
    const size_t n = (left < want) ? left : want;
    memcpy(buf, t->uploadBuf.data() + t->uploadPos, n);
    t->uploadPos += n;
    t->lastProgressMs = nowMs();       // 上行有字节 → 重置空闲计时
    return n;
}

size_t headerCb(char* ptr, size_t sz, size_t nm, void* ud)
{
    Task* t = (Task*)ud;
    if (t == 0) {
        return sz * nm;
    }
    const size_t total = sz * nm;
    // 响应头（含状态行）到达也算有进展：服务端可能先吐头再慢慢吐体，
    // 此时 body 尚无字节，但链路是活的，不该判空闲。
    t->lastProgressMs = nowMs();
    size_t len = total;
    while (len > 0 && (ptr[len - 1] == '\r' || ptr[len - 1] == '\n')) {
        --len;
    }
    if (len == 0) {
        return total;   // 空行 = 头结束
    }
    // 每轮响应都会重发状态行（100-continue、重定向等），以它刷新 status
    if (len > 5 && strncmp(ptr, "HTTP/", 5) == 0) {
        const char* sp1 = (const char*)memchr(ptr, ' ', len);
        if (sp1 != 0) {
            const char* sp2 = (const char*)memchr(sp1 + 1, ' ',
                                                len - (size_t)(sp1 + 1 - ptr));
            if (sp2 != 0) {
                t->status = atoi(std::string(sp1 + 1, sp2 - sp1 - 1).c_str());
                return total;
            }
        }
        t->status = 0;
        return total;
    }
    const char* colon = (const char*)memchr(ptr, ':', len);
    if (colon != 0) {
        std::string name(ptr, colon - ptr);
        const char* v = colon + 1;
        while (v < ptr + len && (*v == ' ' || *v == '\t')) {
            ++v;
        }
        std::string value(v, (ptr + len) - v);
        // HTTP 头名大小写不敏感 → 统一小写存，调用方不必为 dav/DASL 之类
        // 大小写差异各写一份查找
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        t->respHeaders[name] = value;
    }
    return total;
}

class Pump : public QThread
{
public:
    static Pump& inst()
    {
        static Pump p;
        return p;
    }

    void enqueue(Task* t)
    {
        QMutexLocker lock(&m_mutex);
        m_pending.push_back(t);
        m_wakeup.wakeOne();
    }

    void remove(Task* t)
    {
        QMutexLocker lock(&m_mutex);
        std::vector<Task*>::iterator it = std::find(m_live.begin(), m_live.end(), t);
        if (it != m_live.end()) {
            m_live.erase(it);
        }
    }

protected:
    void run()
    {
        m_multi = curl_multi_init();
        while (true) {
            // 搬新任务进 multi
            {
                QMutexLocker lock(&m_mutex);
                while (!m_pending.empty()) {
                    Task* t = m_pending.front();
                    m_pending.erase(m_pending.begin());
                    m_live.push_back(t);
                    curl_multi_add_handle(m_multi, t->easy);
                }
                if (m_quit && m_live.empty()) {
                    break;
                }
            }
            if (m_quit) {
                // ★ 停机必须**主动拆掉**在途 easy，不能只置 cancel 标志。
                //   原先只做 `*cancel = true`，但这个标志 curl 根本不读，
                //   easy handle 会继续跑到底；而循环的退出条件是
                //   `m_quit && m_live.empty()`，m_live 只在 CURLMSG_DONE
                //   （传输真的结束）或空闲超时巡检时才排空。
                //   ⇒ 服务端「收下连接但永不响应」时，QWebdavTransport::stop()
                //   里的 p.wait() 会**永久阻塞**，应用退不出。
                //   （未设 setTransferTimeout 的请求连空闲巡检都兜不住。）
                //
                // 这里刻意**不回调** DoneCb：停机时上层（reply / davbisync）
                // 可能已销毁，回调会打进悬垂指针。回调与否对退出无影响，
                // 语义上「进程正在退出」也不是需要上报告警的故障。
                // 尾部的收尾清理块照常跑；m_live 已清空，不会二次释放。
                QMutexLocker lock(&m_mutex);
                for (size_t i = 0; i < m_live.size(); ++i) {
                    Task* t = m_live[i];
                    if (t->cancel != 0) {
                        *t->cancel = true;   // 告知调用方「是被停机中止的」
                    }
                    curl_multi_remove_handle(m_multi, t->easy);
                    curl_easy_cleanup(t->easy);
                    delete t;
                }
                m_live.clear();
            }
            // ★ 关键：poll 只负责「等」，真正驱动传输的是 perform。
            // 官方 libcurl-multi 文档原话：
            //   "Adding the easy handle to the multi handle does not start the
            //    transfer... You drive the transfers by invoking
            //    curl_multi_perform."
            // 只 poll 不 perform → 任务进了 multi 却永远不推进，表现为
            // 请求既不到服务端、也不回调。（官方示例就是
            //  curl_multi_poll(m, NULL, 0, 1000, NULL)，NULL 是合法的。）
            int still_running = 0;
            CURLMcode mc = curl_multi_perform(m_multi, &still_running);
            if (mc != CURLM_OK) {
                break;
            }
            if (still_running == 0) {
                // 无在途传输：poll 只会空等满超时。给泵一个让出点，
                // 避免新任务入队后要等一整个超时周期才被处理。
                msleep(10);
            } else {
                curl_waitfd wfds[16];
                int numfds = 0;
                mc = curl_multi_poll(m_multi, wfds, 16, 100, &numfds);
                if (mc != CURLM_OK) {
                    break;
                }
            }

            // ── 空闲超时巡检 ──
            // 放在 poll 之后、curl_multi_info_read 之前：超时的 easy 必须先
            // 从 multi 摘掉，否则它还会补一条 CURLMSG_DONE 上来，
            // 与这里的回调重复投递。
            //
            // 锁内只做「摘除 + 收集」，回调在锁外发（与 CURLMSG_DONE 路径一致，
            // 避免回调里若再碰 QWebdavTransport/QNAM 就自死锁）。
            std::vector<Task*> idleDead;
            if (!m_live.empty()) {
                const qint64 now = nowMs();
                QMutexLocker lock(&m_mutex);
                for (size_t i = 0; i < m_live.size(); ) {
                    Task* t = m_live[i];
                    if (t->idleTimeoutMs > 0 &&
                        now - t->lastProgressMs >= (qint64)t->idleTimeoutMs) {
                        curl_multi_remove_handle(m_multi, t->easy);
                        idleDead.push_back(t);
                        m_live.erase(m_live.begin() + i);
                        continue;   // erase 后本下标已是下一个元素，不能自增
                    }
                    ++i;
                }
            }
            for (size_t i = 0; i < idleDead.size(); ++i) {
                Task* t = idleDead[i];
                if (!t->reported) {
                    t->reported = true;
                    if (t->cb != 0) {
                        // 错误串必须含大写 "Timeout"：垫片的 mapCurlError 只认
                        // "timed out"（小写 t）与 "Timeout"（大写 T）两个子串，
                        // 写成小写 "idle timeout" 会落到 UnknownNetworkError。
                        char buf[128];
                        snprintf(buf, sizeof(buf),
                                 "Idle Timeout: no data for %d ms",
                                 t->idleTimeoutMs);
                        // aborted=false：空闲超时是**故障**不是用户中止。
                        // 传 true 会让 deliverResult 走 aborted 分支，把原因
                        // 覆盖成无意义的 "Request aborted"（见下 DONE 路径注释）。
                        t->cb(t->ud, t->status, std::string(buf), t->respBody,
                              t->respHeaders, false);
                    }
                }
                curl_easy_cleanup(t->easy);
                delete t;
            }

            int msgsLeft = 0;
            CURLMsg* msg = 0;
            while ((msg = curl_multi_info_read(m_multi, &msgsLeft)) != 0) {
                if (msg->msg != CURLMSG_DONE) {
                    continue;
                }
                CURL* e = msg->easy_handle;
                // 找回 Task：easy->private 用不了，改按 ourdata 关联表反查
                Task* t = findByEasy(e);
                if (t == 0) {
                    curl_multi_remove_handle(m_multi, e);
                    curl_easy_cleanup(e);
                    continue;
                }
                long code = 0;
                curl_easy_getinfo(e, CURLINFO_RESPONSE_CODE, &code);
                t->status = (int)code;

                curl_multi_remove_handle(m_multi, e);
                remove(t);

                // ★ aborted 只表示「**调用方主动取消**」，不再兼指 curl 失败。
                //   原先是 `cancel || result != CURLE_OK`，两者混在一起，后果是：
                //   任何 curl 层失败（连接被拒 / DNS 失败 / TLS 失败 / 空闲超时）
                //   都以 aborted=true 上报，而垫片的 deliverResult 见到
                //   aborted=true 就走 aborted 分支，直接把 errorString 定成
                //   "Request aborted" 并 return —— 传上来的真实 curl 错误串被
                //   **整个丢弃**，mapCurlError 根本没机会跑。
                //   实测症状：连接被拒、404、401 的 errorChanged 原因全都是
                //   一句 "Request aborted"，毫无信息量。
                //   现在 curl 失败只经 err 串上报（aborted=false），
                //   deliverResult 会走 `if (!curlErr.empty())` → mapCurlError。
                const bool aborted = (t->cancel != 0 && *t->cancel);
                if (!t->reported) {
                    t->reported = true;
                    const char* err = (msg->data.result == CURLE_OK)
                                      ? "" : curl_easy_strerror(msg->data.result);
                    if (t->cb != 0) {
                        t->cb(t->ud, t->status, std::string(err), t->respBody,
                              t->respHeaders, aborted);
                    }
                }
                curl_easy_cleanup(e);
                delete t;
            }
        }
        // 收尾：把还没跑的 pending 丢掉
        QMutexLocker lock(&m_mutex);
        while (!m_pending.empty()) {
            Task* t = m_pending.front();
            m_pending.erase(m_pending.begin());
            curl_easy_cleanup(t->easy);
            delete t;
        }
        for (size_t i = 0; i < m_live.size(); ++i) {
            curl_easy_cleanup(m_live[i]->easy);
            delete m_live[i];
        }
        m_live.clear();
        curl_multi_cleanup(m_multi);
        m_multi = 0;
    }

private:
    void setQuit() { m_quit = true; }
    friend class ::QWebdavTransport;
    Task* findByEasy(CURL* e)
    {
        QMutexLocker lock(&m_mutex);
        for (size_t i = 0; i < m_live.size(); ++i) {
            if (m_live[i]->easy == e) {
                return m_live[i];
            }
        }
        return 0;
    }

    CURLM* m_multi;
    QMutex m_mutex;
    QWaitCondition m_wakeup;
    std::vector<Task*> m_pending;
    std::vector<Task*> m_live;
    volatile bool m_quit;
    bool m_started;
    bool m_stopped;

    Pump() : m_multi(0), m_quit(false), m_started(false), m_stopped(false) {}
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
void QWebdavTransport::start()
{
    Pump& p = Pump::inst();
    if (p.m_started) {
        return;
    }
    p.m_started = true;
    p.start();
}

void QWebdavTransport::stop()
{
    Pump& p = Pump::inst();
    if (!p.m_started || p.m_stopped) {
        return;
    }
    p.m_stopped = true;
    p.setQuit();
    p.wait();
}

bool QWebdavTransport::isReady()
{
    Pump& p = Pump::inst();
    return p.m_started && !p.m_stopped;
}

void QWebdavTransport::send(const std::string& method,
                            const std::string& url,
                            const std::map<std::string, std::string>& headers,
                            const std::string& body,
                            int timeoutMsecs,
                            DoneCb cb,
                            void* userdata,
                            volatile bool* cancel)
{
        if (!isReady()) {
        // 与 EventPoller 一致的失败语义：立刻回调，让上层照常走错误路径。
        // aborted=false：这是故障不是用户取消，原因靠 err 串上报。
        // 传 true 会让 deliverResult 走 aborted 分支，原因被替换成
        // "Request aborted"，真实的 "transport not ready" 就丢了。
        if (cb != 0) {
            cb(userdata, 0, "transport not ready", std::string(),
               std::map<std::string, std::string>(), false);
        }
        return;
    }

    Task* t = new Task;
    t->key = method + " " + url;
    t->uploadBuf = body;
    t->cb = cb;
    t->ud = userdata;
    t->cancel = cancel;
    t->idleTimeoutMs = timeoutMsecs;
    // ★ 发包前就以当前时刻起算：连接建立 + TLS 握手这段时间里 readCb/headerCb
    //   都还没被调过，若把 lastProgressMs 留 0，第一次巡检就会把刚入队的
    //   任务误判成「已空闲 timeoutMs 毫秒」。
    t->lastProgressMs = nowMs();

    t->easy = curl_easy_init();
    if (t->easy == 0) {
        delete t;
        if (cb != 0) {
            cb(userdata, 0, "curl_easy_init failed", std::string(),
               std::map<std::string, std::string>(), false);
        }
        return;
    }

    curl_easy_setopt(t->easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(t->easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(t->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(t->easy, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(t->easy, CURLOPT_SSL_SESSIONID_CACHE, 0L);
    if (timeoutMsecs > 0) {
        // ★ 这里**不设** CURLOPT_TIMEOUT / CURLOPT_LOW_SPEED_*。
        //   空闲超时由 Pump 循环自己巡检 Task::lastProgressMs 实现（见
        //   run() 里的「空闲超时巡检」），因为语义是「字节流动即重置」，
        //   而 curl 现有的几个超时选项都给不出这个语义：
        //     CURLOPT_TIMEOUT        总时长上限（会误杀慢速大图）
        //     CURLOPT_CONNECTTIMEOUT 只管连接建立阶段
        //     CURLOPT_LOW_SPEED_*    按**平均速度**判定，且久未在 libcurl 使用
        //   保持 t->idleTimeoutMs 即可，这里只是留个记录。
    }

    // ★ 本传输层存在的核心：真正把 verb 传给 curl。
    // qldox/eventpoller.cpp 全文无 CURLOPT_CUSTOMREQUEST，故非 POST 一律变 GET。
    curl_easy_setopt(t->easy, CURLOPT_CUSTOMREQUEST, method.c_str());

    // HEAD 只需头，不要体
    if (method == "HEAD") {
        curl_easy_setopt(t->easy, CURLOPT_NOBODY, 1L);
    }

    curl_easy_setopt(t->easy, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(t->easy, CURLOPT_WRITEDATA, (void*)t);
    curl_easy_setopt(t->easy, CURLOPT_HEADERFUNCTION, headerCb);
    curl_easy_setopt(t->easy, CURLOPT_HEADERDATA, (void*)t);

    // 有体动词（PUT/POST/PROPPATCH…）走 UPLOAD + READFUNCTION；
    // 空体动词不要设 UPLOAD，否则 curl 会因 0 字节输入卡住。
    if (!body.empty()) {
        curl_easy_setopt(t->easy, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(t->easy, CURLOPT_READFUNCTION, readCb);
        curl_easy_setopt(t->easy, CURLOPT_READDATA, (void*)t);
        curl_easy_setopt(t->easy, CURLOPT_INFILESIZE_LARGE,
                         (curl_off_t)body.size());
    }

    // 请求头
    for (std::map<std::string, std::string>::const_iterator it = headers.begin();
         it != headers.end(); ++it) {
        if (it->first == "Content-Length") {
            continue;   // UPLOAD 模式由 curl 自己算，手工给会冲突
        }
        const std::string line = it->first + ": " + it->second;
        t->hdrs = curl_slist_append(t->hdrs, line.c_str());
    }
    if (t->hdrs != 0) {
        curl_easy_setopt(t->easy, CURLOPT_HTTPHEADER, t->hdrs);
    }

    Pump::inst().enqueue(t);
}
