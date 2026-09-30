#include "qwebdavtransport.h"

#include <qwaitcondition.h>
#include <curl/curl.h>
#include <cstdlib>
#include <cstring>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// 泵线程与任务表
//
// 参照 移植计划 §5.4「必须 qlstik 自行解决的点（不改 qltox）」的 a. 取消 一节：
// addRequest 无句柄可取消，故 udata 内放取消标志，置位后泵自行收尾并静默
// 丢弃数据（不重复投递结果）。本传输层直接持有自己的 easy handle，
// 取消更直接：置 cancel 标志 + curl_multi_remove。
// ─────────────────────────────────────────────────────────────────────────────

namespace {

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

    Task() : easy(0), status(0), uploadPos(0), cancel(0), reported(false),
             cb(0), ud(0), hdrs(0) {}
};

size_t writeCb(char* ptr, size_t sz, size_t nm, void* ud)
{
    Task* t = (Task*)ud;
    if (t != 0 && sz * nm > 0) {
        t->respBody.append(ptr, sz * nm);
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
    return n;
}

size_t headerCb(char* ptr, size_t sz, size_t nm, void* ud)
{
    Task* t = (Task*)ud;
    if (t == 0) {
        return sz * nm;
    }
    const size_t total = sz * nm;
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
                // 还有在途任务：先把它们中止掉（cancel 置位）
                QMutexLocker lock(&m_mutex);
                for (size_t i = 0; i < m_live.size(); ++i) {
                    if (m_live[i]->cancel != 0) {
                        *m_live[i]->cancel = true;
                    }
                }
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

                const bool aborted = (t->cancel != 0 && *t->cancel)
                                     || (msg->data.result != CURLE_OK);
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
        // 与 EventPoller 一致的失败语义：立刻回调，让上层照常走错误路径
        if (cb != 0) {
            cb(userdata, 0, "transport not ready", std::string(),
               std::map<std::string, std::string>(), true);
        }
        return;
    }

    Task* t = new Task;
    t->key = method + " " + url;
    t->uploadBuf = body;
    t->cb = cb;
    t->ud = userdata;
    t->cancel = cancel;

    t->easy = curl_easy_init();
    if (t->easy == 0) {
        delete t;
        if (cb != 0) {
            cb(userdata, 0, "curl_easy_init failed", std::string(),
               std::map<std::string, std::string>(), true);
        }
        return;
    }

    curl_easy_setopt(t->easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(t->easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(t->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(t->easy, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(t->easy, CURLOPT_SSL_SESSIONID_CACHE, 0L);
    if (timeoutMsecs > 0) {
        // 毫秒 → 秒（向上取整，至少 1s；0 视为不限）
        long secs = (long)((timeoutMsecs + 999) / 1000);
        if (secs < 1) {
            secs = 1;
        }
        curl_easy_setopt(t->easy, CURLOPT_TIMEOUT, secs);
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
