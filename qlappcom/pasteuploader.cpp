#include "pasteuploader.h"
#include "compatcore34.h"
#include "eventpoller.h"
#include "netproxy.h"
#include "cJSON.h"

//#include <qapplication.h>
#include <qobject.h>
#include <qmutex.h>
#include <qglobal.h>
#include <qdatetime.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace {

// ── 服务静态表（顺序 = provider="all" 的固定尝试序）──
// 末位 needsProxy：境外服务走代理（mhimg.cn 为国内，直连）
const PasteHost kHosts[] = {
    { kPasteHostDpaste,      "dpaste.com",   true,  false, false, false,
      kPasteExp1d | kPasteExp1w | kPasteExp1m | kPasteExp1y, false, false, true },
    { kPasteHostCatbox,      "catbox",       true,  true,  true,  true,  kPasteExpNever, false, true, true },
    { kPasteHost0x0,         "0x0.st",       true,  true,  true,  true,
      kPasteExp1h | kPasteExp1d | kPasteExp1w | kPasteExp1m | kPasteExp1y, false, true, true },
    { kPasteHostTransferSh,  "transfer.sh",  true,  true,  true,  true,
      kPasteExp1d | kPasteExp1w | kPasteExp1m | kPasteExp1y, false, true, true },
    { kPasteHostMhimg,       "mhimg.cn",     false, true,  false, false, kPasteExp1h, false, false, false },
    { kPasteHostScdnIo,      "img.scdn.io",  false, true,  true,  false, kPasteExpNever, false, true, true },
    { kPasteHostTmpfileLink, "tmpfile.link", true,  true,  true,  true,  kPasteExpNever, false, false, true },
    { kPasteHostTempfileOrg, "tempfile.org", true,  true,  true,  true,  kPasteExpNever, false, false, true },
    { kPasteHostStorageTo,   "storage.to",   true,  true,  true,  true,  kPasteExpNever, false, false, true },
    { kPasteHostLitterbox,   "litterbox",    true,  true,  true,  true,  kPasteExp1h, false, false, true },
    { kPasteHostPastebinCom, "pastebin.com", false, false, false, false, 0, true, false, true },
};
const int kHostCount = int(sizeof(kHosts) / sizeof(kHosts[0]));

struct Session {
    PasteRequest req;
    QObject* target = nullptr;
    std::vector<const PasteHost*> cands;
    int index = 0;
    unsigned int seq = 0;
    std::string boundary;
    std::string lastError;
    std::vector<std::string> tried;
    int storageStep = 0;   // storage.to 三段流：0 init / 1 put / 2 confirm
    std::string storageUploadUrl;
    std::string storageR2Key;
};

QMutex g_mutex;
std::vector<Session*> g_sessions;
unsigned int g_seq = 0;
bool g_seeded = false;

std::string trimStr(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) { return std::string(); }
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool looksUrl(const std::string& s) { return s.rfind("http", 0) == 0; }

std::string jsonStr(cJSON* obj, const char* key) {
    if (!obj) { return std::string(); }
    cJSON* it = cJSON_GetObjectItem(obj, key);
    if (!it || !cJSON_IsString(it)) { return std::string(); }
    const char* v = cJSON_GetStringValue(it);
    return v ? std::string(v) : std::string();
}

bool jsonTrue(cJSON* obj, const char* key) {
    if (!obj) { return false; }
    cJSON* it = cJSON_GetObjectItem(obj, key);
    return it && cJSON_IsTrue(it);
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        const unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += (char)c;
            }
        }
    }
    return out;
}

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        const unsigned char c = (unsigned char)s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

std::string mimeOf(const std::string& name) {
    const size_t dot = name.rfind('.');
    std::string ext;
    if (dot != std::string::npos) {
        ext = name.substr(dot + 1);
        for (size_t i = 0; i < ext.size(); i++) {
            ext[i] = (char)std::tolower((unsigned char)ext[i]);
        }
    }
    if (ext == "png")  { return "image/png"; }
    if (ext == "jpg" || ext == "jpeg") { return "image/jpeg"; }
    if (ext == "gif")  { return "image/gif"; }
    if (ext == "webp") { return "image/webp"; }
    if (ext == "bmp")  { return "image/bmp"; }
    if (ext == "svg")  { return "image/svg+xml"; }
    if (ext == "mp4")  { return "video/mp4"; }
    if (ext == "webm") { return "video/webm"; }
    if (ext == "mov")  { return "video/quicktime"; }
    if (ext == "mkv")  { return "video/x-matroska"; }
    if (ext == "txt" || ext == "log") { return "text/plain"; }
    if (ext == "json") { return "application/json"; }
    if (ext == "pdf")  { return "application/pdf"; }
    return "application/octet-stream";
}

std::string makeBoundary() {
    static const char* hex = "0123456789abcdef";
    std::string b = "----qltoxPasteBoundary";
    for (int i = 0; i < 16; i++) {
        b += hex[rand() & 0xF];
    }
    return b;
}

void appendPart(std::string& body, const std::string& boundary,
                const std::string& name, const std::string& value) {
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"" + name + "\"\r\n\r\n";
    body += value;
    body += "\r\n";
}

void appendFilePart(std::string& body, const std::string& boundary,
                    const std::string& name, const std::string& fileName,
                    const std::string& data, const std::string& mime) {
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"" + name + "\"; filename=\""
          + fileName + "\"\r\n";
    body += "Content-Type: " + mime + "\r\n\r\n";
    body += data;
    body += "\r\n";
}

// dpaste.com 只接受 expiry_days（1–365 天），无「1 小时」与「永不过期」
long long dpasteDays(const std::string& e) {
    if (e == "1w") { return 7LL; }
    if (e == "1m") { return 30LL; }
    if (e == "1y") { return 365LL; }
    return 1LL;
}

// transfer.sh 保留期用 Max-Days 头（服务端可能对上限做 clamp）
long long transferDays(const std::string& e) {
    if (e == "1w") { return 7LL; }
    if (e == "1m") { return 30LL; }
    if (e == "1y") { return 365LL; }
    return 1LL;
}

// 0x0.st 的 expires 单位是小时（也接受 epoch 毫秒，这里统一用小时）
long long zeroHours(const std::string& e) {
    if (e == "1h") { return 1LL; }
    if (e == "1d") { return 24LL; }
    if (e == "1w") { return 168LL; }
    if (e == "1m") { return 720LL; }
    if (e == "1y") { return 8760LL; }
    return 24LL;
}

// mhimg.cn 的 expired_at 要 "yyyy-MM-dd hh:mm:ss"（本表只开放 1h）
std::string mhimgExpiredAt() {
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime until(now.date(), now.time().addSecs(3600));
    return std::string(qToUtf8(until.toString("yyyy-MM-dd hh:mm:ss")).data());
}

void removeSession(Session* s) {
    {
        QMutexLocker lock(&g_mutex);
        for (size_t i = 0; i < g_sessions.size(); i++) {
            if (g_sessions[i] == s) {
                g_sessions.erase(g_sessions.begin() + i);
                break;
            }
        }
    }
    delete s;
}

// 同一消息（chatId+chatType+localId）已有更新的会话时，本次回包作废
bool isCurrentSeq(const Session* s) {
    QMutexLocker lock(&g_mutex);
    for (size_t i = 0; i < g_sessions.size(); i++) {
        const Session* o = g_sessions[i];
        if (o->req.chatId == s->req.chatId && o->req.chatType == s->req.chatType
            && o->req.localId == s->req.localId && o->seq > s->seq) {
            return false;
        }
    }
    return true;
}

void postResult(Session* s, bool ok, const std::string& url,
                const std::string& providerUsed, const std::string& err) {
    PasteResultEvent* ev = new PasteResultEvent();
    ev->chatId       = s->req.chatId;
    ev->chatType     = s->req.chatType;
    ev->localId      = s->req.localId;
    ev->success      = ok;
    ev->url          = url;
    ev->providerUsed = providerUsed;
    ev->errorMsg     = err;
    QObject* target  = s->target;
    removeSession(s);
    if (target) { 
		//QApplication::postEvent(target, ev);
		qAppPostEvent(target, ev);
	}
    else { delete ev; }
}

std::string parseUrl(PasteHostId id, const std::string& body, std::string& err) {
    const std::string text = trimStr(body);
    if (looksUrl(text)) { return text; }
    if (id == kPasteHostCatbox || id == kPasteHostLitterbox) {
        err = "响应不是 URL";
        return std::string();
    }

    cJSON* root = cJSON_Parse(text.c_str());
    if (!root) {
        err = "响应既非 URL 也非 JSON";
        return std::string();
    }
    std::string url;
    if (id == kPasteHostTempfileOrg) {
        if (jsonTrue(root, "success")) {
            cJSON* files = cJSON_GetObjectItem(root, "files");
            cJSON* first = files ? cJSON_GetArrayItem(files, 0) : nullptr;
            const std::string fid = first ? jsonStr(first, "id") : std::string();
            if (!fid.empty()) { url = "https://tempfile.org/" + fid + "/download"; }
        }
    } else if (id == kPasteHostTmpfileLink) {
        url = jsonStr(root, "downloadLink");
    } else if (id == kPasteHostMhimg) {
        cJSON* data = cJSON_GetObjectItem(root, "data");
        cJSON* links = data ? cJSON_GetObjectItem(data, "links") : nullptr;
        url = links ? jsonStr(links, "url") : std::string();
    } else {
        url = jsonStr(root, "url");
        if (url.empty()) {
            cJSON* data = cJSON_GetObjectItem(root, "data");
            url = data ? jsonStr(data, "url") : std::string();
        }
    }
    cJSON_Delete(root);
    if (!looksUrl(url)) {
        err = "未从响应中取到 URL";
        return std::string();
    }
    return url;
}

std::vector<const PasteHost*> buildCandidates(const PasteRequest& req,
                                              std::string& reason) {
    std::vector<const PasteHost*> out;
    // expire=auto/空串：不按有效期过滤（由服务能力决定），最大化候选
    const bool expireAuto = (req.expire.empty() || req.expire == "auto");
    const unsigned int mask = expireAuto ? ~0u : pasteExpireMask(req.expire);
    if (req.provider == "all" || req.provider == "any" || req.provider.empty()) {
        for (int i = 0; i < kHostCount; i++) {
            const PasteHost& h = kHosts[i];
            if (!h.unsupported && !h.disabled && pasteKindSupported(h, req.kind)
                && (h.expireMask & mask)) {
                out.push_back(&h);
            }
        }
        if (req.provider != "all" && !out.empty()) {   // any/空串：随机取序
            for (size_t i = out.size() - 1; i > 0; i--) {
                const size_t j = (size_t)rand() % (i + 1);
                std::swap(out[i], out[j]);
            }
        }
        if (out.empty()) { reason = "没有可用服务：内容类型/有效期/可用性过滤后候选为空"; }
        return out;
    }
    for (int i = 0; i < kHostCount; i++) {
        const PasteHost& h = kHosts[i];
        if (req.provider != h.provider) { continue; }
        if (h.unsupported) {
            reason = std::string(h.provider) + " 需官方 api_dev_key，暂不可用";
        } else if (h.disabled) {
            reason = std::string(h.provider) + " 当前暂不可用";
        } else if (!pasteKindSupported(h, req.kind)) {
            reason = std::string(h.provider) + " 不支持该内容类型";
        } else if (!(h.expireMask & mask)) {
            reason = std::string(h.provider) + " 不支持该有效期";
        } else {
            out.push_back(&h);
        }
        return out;
    }
    reason = "未知服务：" + req.provider;
    return out;
}

void onDone(const HttpResponse& resp, void* udata);

void sendHttp(Session* s, const std::string& url, const std::string& method,
              const std::string& body,
              const std::map<std::string, std::string>& headers) {
    HttpRequest req(url, method, body, 30, headers);
    req.followRedirects = true;
    if (!s->cands.empty() && s->index >= 0 && s->index < (int)s->cands.size()) {
        req.proxy = netProxyFor(s->cands[s->index]->needsProxy);
    }
    EventPoller::addRequest(req, onDone, s);
}

void startStorageTo(Session* s, int step) {
    std::map<std::string, std::string> headers;
    headers["User-Agent"] = "qltox/1.0";
    headers["Expect"] = "";
    const bool isText = (s->req.kind == kPasteKindText);
    const std::string& payload = isText ? s->req.text : s->req.fileData;
    std::string fileName = isText ? std::string("paste.txt") : s->req.fileName;
    if (fileName.empty()) { fileName = "paste.bin"; }

    if (step == 1) {
        headers["Content-Type"] = "application/octet-stream";
        sendHttp(s, s->storageUploadUrl, "PUT", payload, headers);
        return;
    }
    const long long size = (long long)payload.size();
    headers["Content-Type"] = "application/json";
    std::string body = "{\"filename\":\"" + jsonEscape(fileName) + "\"";
    if (step == 2) {
        body += ",\"r2_key\":\"" + jsonEscape(s->storageR2Key) + "\"";
    }
    body += ",\"content_type\":\"application/octet-stream\"";
    body += ",\"size\":" + std::to_string(size) + "}";
    sendHttp(s, step == 0 ? "https://storage.to/api/upload/init"
                          : "https://storage.to/api/upload/confirm",
             "POST", body, headers);
}

void sendHost(Session* s, const PasteHost& h) {
    s->tried.push_back(h.provider);
    if (h.id == kPasteHostStorageTo) {
        startStorageTo(s, 0);
        return;
    }
    std::map<std::string, std::string> headers;
    headers["User-Agent"] = "qltox/1.0";
    headers["Expect"] = "";
    std::string url;
    std::string body;

    const bool isText = (s->req.kind == kPasteKindText);
    const std::string& payload = isText ? s->req.text : s->req.fileData;
    std::string fileName = isText ? std::string("paste.txt") : s->req.fileName;
    if (fileName.empty()) { fileName = "paste.bin"; }
    const std::string mime = isText ? std::string("text/plain; charset=utf-8")
                                    : pasteMimeOf(fileName);

    // transfer.sh 只收 PUT 原始字节（目标 URL 携带文件名），保留期用 Max-Days 头
    if (h.id == kPasteHostTransferSh) {
        headers["Max-Days"] = std::to_string(transferDays(s->req.expire));
        sendHttp(s, "https://transfer.sh/" + urlEncode(fileName),
                 "PUT", payload, headers);
        return;
    }

    const std::string& boundary = s->boundary;

    if (h.id == kPasteHostDpaste) {
        url = "https://dpaste.com/api/v2/";
        appendPart(body, boundary, "expiry_days",
                   std::to_string(dpasteDays(s->req.expire)));
        appendFilePart(body, boundary, "content", fileName, payload, mime);
    } else if (h.id == kPasteHost0x0) {
        url = "https://0x0.st";
        appendFilePart(body, boundary, "file", fileName, payload, mime);
        appendPart(body, boundary, "expires",
                   std::to_string(zeroHours(s->req.expire)));
    } else if (h.id == kPasteHostCatbox || h.id == kPasteHostLitterbox) {
        url = (h.id == kPasteHostCatbox)
                  ? "https://catbox.moe/user/api.php"
                  : "https://litterbox.catbox.moe/resources/internals/api.php";
        appendPart(body, boundary, "reqtype", "fileupload");
        if (h.id == kPasteHostLitterbox) { appendPart(body, boundary, "time", "1h"); }
        appendFilePart(body, boundary, "fileToUpload", fileName, payload, mime);
    } else if (h.id == kPasteHostMhimg) {
        url = "https://mhimg.cn/api/v1/upload";
        appendPart(body, boundary, "expired_at", mhimgExpiredAt());
        appendFilePart(body, boundary, "file", fileName, payload, mime);
    } else if (h.id == kPasteHostScdnIo) {
        url = "https://img.scdn.io/api/v1.php";
        appendPart(body, boundary, "outputFormat", "auto");
        appendFilePart(body, boundary, "image", fileName, payload, mime);
    } else if (h.id == kPasteHostTmpfileLink) {
        url = "https://tmpfile.link/api/upload";
        appendFilePart(body, boundary, "file", fileName, payload, mime);
    } else if (h.id == kPasteHostTempfileOrg) {
        url = "https://tempfile.org/api/upload/local";
        appendFilePart(body, boundary, "files", fileName, payload, mime);
    } else {
        return;
    }
    body += "--" + boundary + "--\r\n";
    headers["Content-Type"] = "multipart/form-data; boundary=" + boundary;
    sendHttp(s, url, "POST", body, headers);
}

void startNext(Session* s) {
    if (s->index < (int)s->cands.size()) {
        sendHost(s, *s->cands[s->index]);
        return;
    }
    std::string err = s->lastError.empty() ? std::string("所有候选服务均失败") : s->lastError;
    std::string tried;
    for (size_t i = 0; i < s->tried.size(); i++) {
        if (i) { tried += ", "; }
        tried += s->tried[i];
    }
    if (!tried.empty()) { err += "（已尝试：" + tried + "）"; }
    postResult(s, false, std::string(), std::string(), err);
}

void onDone(const HttpResponse& resp, void* udata) {
    Session* s = static_cast<Session*>(udata);
    if (!s) { return; }
    if (!isCurrentSeq(s)) { removeSession(s); return; }   // 迟到回包：已被重试覆盖
    const PasteHost* h = (s->index < (int)s->cands.size()) ? s->cands[s->index] : nullptr;
    if (!h) { removeSession(s); return; }

    std::string parseErr;
    if (resp.httpCode >= 200 && resp.httpCode < 300 && resp.curlErrStr.empty()) {
        if (h->id != kPasteHostStorageTo) {
            const std::string url = parseUrl(h->id, resp.body, parseErr);
            if (!url.empty()) {
                postResult(s, true, url, h->provider, std::string());
                return;
            }
        } else if (s->storageStep == 0) {
            cJSON* root = cJSON_Parse(resp.body.c_str());
            const bool ok = root && jsonTrue(root, "success");
            const std::string up = root ? jsonStr(root, "upload_url") : std::string();
            const std::string key = root ? jsonStr(root, "r2_key") : std::string();
            if (root) { cJSON_Delete(root); }
            if (ok && !up.empty() && !key.empty()) {
                s->storageUploadUrl = up;
                s->storageR2Key = key;
                s->storageStep = 1;
                startStorageTo(s, 1);
                return;
            }
            parseErr = "storage.to: init 未返回 success/upload_url/r2_key";
        } else if (s->storageStep == 1) {
            s->storageStep = 2;
            startStorageTo(s, 2);
            return;
        } else {
            cJSON* root = cJSON_Parse(resp.body.c_str());
            cJSON* fileItem = root ? cJSON_GetObjectItem(root, "file") : nullptr;
            std::string raw = fileItem ? jsonStr(fileItem, "raw_url") : std::string();
            if (raw.empty() && fileItem) { raw = jsonStr(fileItem, "url"); }   // 接口已不返回 raw_url，回退 url
            if (root) { cJSON_Delete(root); }
            if (!raw.empty()) {
                postResult(s, true, raw, h->provider, std::string());
                return;
            }
            parseErr = "storage.to: confirm 未返回 raw_url/url";
        }
    }

    s->lastError = std::string(h->provider) + ": HTTP " + std::to_string(resp.httpCode);
    if (!resp.curlErrStr.empty()) { s->lastError += " " + resp.curlErrStr; }
    if (!parseErr.empty()) { s->lastError += "（" + parseErr + "）"; }
    ++s->index;
    startNext(s);
}

void ensureSeed() {
    QMutexLocker lock(&g_mutex);
    if (g_seeded) { return; }
    g_seeded = true;
    srand((unsigned int)time(nullptr));
}

} // namespace

// 按文件名（含扩展名）判定 MIME；宿主据此选 image/video/file 上传类别。
std::string pasteMimeOf(const std::string& fileName) {
    return mimeOf(fileName);
}

const std::vector<PasteHost>& pasteHosts() {
    static std::vector<PasteHost> hosts(kHosts, kHosts + kHostCount);
    return hosts;
}

bool pasteKindSupported(const PasteHost& host, PasteKind kind) {
    switch (kind) {
    case kPasteKindText:  return host.text;
    case kPasteKindImage: return host.image;
    case kPasteKindVideo: return host.video;
    case kPasteKindFile:  return host.file;
    }
    return false;
}

unsigned int pasteExpireMask(const std::string& expire) {
    if (expire == "1h") { return kPasteExp1h; }
    if (expire == "1d") { return kPasteExp1d; }
    if (expire == "1w") { return kPasteExp1w; }
    if (expire == "1m") { return kPasteExp1m; }
    if (expire == "1y") { return kPasteExp1y; }
    return kPasteExpNever;
}

bool pasteExpireSupported(const PasteHost& host, const std::string& expire) {
    return (host.expireMask & pasteExpireMask(expire)) != 0;
}

void pasteUploadStart(const PasteRequest& req, QObject* target) {
    if (!target) { return; }
    ensureSeed();
    Session* s = new Session();
    s->req = req;
    s->target = target;
    {
        QMutexLocker lock(&g_mutex);
        s->seq = ++g_seq;
        g_sessions.push_back(s);
    }
    std::string reason;
    s->cands = buildCandidates(req, reason);
    s->boundary = makeBoundary();
    if (!reason.empty()) { s->lastError = reason; }

    std::string order;
    for (size_t i = 0; i < s->cands.size(); i++) {
        if (i) { order += " → "; }
        order += s->cands[i]->provider;
    }
    qWarning("paste: provider=%s expire=%s 候选 %d 个: %s",
             req.provider.empty() ? "any" : req.provider.c_str(),
             (req.expire.empty() || req.expire == "auto") ? "auto" : req.expire.c_str(),
             (int)s->cands.size(), order.c_str());
    startNext(s);
}
