#include "fanyibot.h"
#include "compatcore34.h"
#include "eventpoller.h"
#include "netproxy.h"
#include "cJSON.h"
#include "stdcpputil.hpp"

//#include <qapplication.h>
#include <qobject.h>
#include <qmutex.h>
#include <qglobal.h>

#include <algorithm>
#include <utility>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <sys/time.h>

extern "C" {
#include "md5.h"
}

namespace {

// ── 引擎静态表（每次请求随机取序；google 暂禁，保留条目）──
// needsProxy：境外引擎走代理（google/yandex/deepl 系）
const FanyibotEngine kEngines[] = {
    { kFanyibotMsedge, "msedge", false, false },
    { kFanyibotGoogle, "google", true,  true  },
    { kFanyibotYoudao, "youdao", false, false },
    { kFanyibotYandex, "yandex", false, true  },
    { kFanyibotDeepl,  "deepl",  true,  true  },
    { kFanyibotDeeplWeb, "deepl-web", false, true },
};
const int kEngineCount = int(sizeof(kEngines) / sizeof(kEngines[0]));

// msedge 取 token 必须带 msie UA（有道/雅翻走浏览器 UA）
const char* kMsedgeUA = "msie 6";
const char* kYandexUA = "msie 6";
const char* kBrowserUA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                         "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
const char* kDeepLWebUA = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
                          "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

// ── 有道：与 touse/oai 完全一致的 URL/头/参数 ──
const char* kYoudaoUA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                        "(KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36";
const char* kYoudaoKeyURL  = "https://dict.youdao.com/webtranslate/key";
const char* kYoudaoAPIURL  = "https://dict.youdao.com/webtranslate";
const char* kYoudaoReferer = "https://fanyi.youdao.com/";
const char* kYoudaoClient  = "fanyideskweb";
const char* kYoudaoProduct = "webfanyi";
const char* kYoudaoAppVer  = "1.0.0";   // 现接口带 12.0.0 会 500
const char* kYoudaoUserID  = "abcdefg";
const char* kYoudaoFakeCookie = "OUTFOX_SEARCH_USER_ID=0@127.0.0.1";

const char* kYoudaoKeyParam = "client,mysticTime,product";   // 现接口带 keyid 会 500

struct YoudaoKeyPair {
    const char* id;
    const char* secret;
};
const YoudaoKeyPair kYoudaoKeyPairs[] = {
    { "webfanyi-key-getter-2025", "yU5nT5dK3eZ1pI4j" },
    { "webfanyi-key-getter",       "asdjnjfenknafdfsdfsd" },
};
const int kYoudaoPairCount = int(sizeof(kYoudaoKeyPairs) / sizeof(kYoudaoKeyPairs[0]));

// 目标语言显示名 → 各家语言码
struct LangRow {
    const char* display;
    const char* msedge;
    const char* google;
    const char* youdao;
    const char* yandex;
    const char* deeplweb;
};
const LangRow kLangs[] = {
    { "中文",     "zh-Hans", "zh-CN", "zh-CHS", "zh", "zh-Hans" },
    { "繁體中文", "zh-Hant", "zh-TW",  "zh-CHT", "zh", "zh-Hant" },
    { "日本語",   "ja",      "ja",     "ja",     "ja", "ja"    },
    { "한국어",   "ko",      "ko",     "ko",     "ko", "ko"    },
    { "English",  "en",      "en",     "en",     "en", "en-US" },
    { "Français", "fr",      "fr",     "fr",     "fr", "fr"    },
    { "Deutsch",  "de",      "de",     "de",     "de", "de"    },
    { "Русский",  "ru",      "ru",     "ru",     "ru", "ru"    },
    { "العربية",  "ar",      "ar",     "ar",     "ar", "ar"    },
    { "地球语",   "eo",      "eo",     "eo",     "eo", "eo"    },
};
const int kLangCount = int(sizeof(kLangs) / sizeof(kLangs[0]));

struct Session {
    FanyibotRequest req;
    QObject* target = nullptr;
    std::vector<const FanyibotEngine*> cands;
    int index = 0;
    unsigned int seq = 0;
    std::string lastError;
    std::vector<std::pair<std::string, std::string> > tried;   // (engine, 失败原因)
    std::string cred;    // msedge JWT / yandex SID
    int step = 0;        // msedge: 恒 0(单步); yandex: 0=取 SID 1=发翻译; youdao: 0=取 key 1=发翻译
    int credRetry = 0;   // 凭据失效重取次数（上限 1，防死循环）
    std::string ydCookie;    // 有道 cookie（warm 取得）
    std::string ydSecret;    // 有道 data.secretKey
    std::string ydAesKey;    // 有道 data.aesKey
    std::string ydAesIv;     // 有道 data.aesIv
    int ydPair = 0;          // 有道 keyid 配对下标（0=2025 1=legacy）
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

std::string jsonStr(cJSON* obj, const char* key) {
    if (!obj) { return std::string(); }
    cJSON* it = cJSON_GetObjectItem(obj, key);
    if (!it || !cJSON_IsString(it)) { return std::string(); }
    const char* v = cJSON_GetStringValue(it);
    return v ? std::string(v) : std::string();
}

std::string md5Hex(const std::string& s) {
    MD5_CTX ctx;
    uint8_t digest[16];
    MD5_Init(&ctx);
    MD5_Update(&ctx, (const uint8_t*)s.data(), s.size());
    MD5_Final(digest, &ctx);
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 16; i++) {
        out += hex[(digest[i] >> 4) & 0xF];
        out += hex[digest[i] & 0xF];
    }
    return out;
}

std::string md5Raw(const std::string& s) {
    MD5_CTX ctx;
    uint8_t digest[16];
    MD5_Init(&ctx);
    MD5_Update(&ctx, (const uint8_t*)s.data(), s.size());
    MD5_Final(digest, &ctx);
    return std::string((const char*)digest, 16);
}

long long nowMs() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (long long)tv.tv_sec * 1000LL + (long long)tv.tv_usec / 1000LL;
}

const char* engineHost(const FanyibotEngine& e, int step) {
    switch (e.id) {
    case kFanyibotMsedge: return "edge.microsoft.com";   // 免 token 新端点单步
    case kFanyibotGoogle: return "translate.googleapis.com";
    case kFanyibotYoudao: return step == 0 ? "fanyi.youdao.com" : "dict.youdao.com";
    case kFanyibotYandex: return "translate.yandex.com";
    case kFanyibotDeepl:  return "-";
    case kFanyibotDeeplWeb: return "oneshot-free.www.deepl.com";
    }
    return "-";
}

std::string briefOf(const std::string& body) {
    std::string b = body;
    const std::string::size_type nl = b.find('\n');
    if (nl != std::string::npos) { b = b.substr(0, nl); }
    if (b.size() > 64) { b = b.substr(0, 64); }
    return b;
}

// ── 临时缓存：$TMPDIR/$TMP/<name>，与 touse/oai os.TempDir() 一致可互通 ──
std::string tmpDir() {
    const char* d = std::getenv("TMPDIR");
    if (d && *d) { return std::string(d); }
    return "/tmp";
}

std::string tmpPath(const char* name) {
    return tmpDir() + "/" + name;
}

std::string loadTmp(const char* name) {
    const std::string path = tmpPath(name);
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { return std::string(); }
    std::string out;
    char buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) { out.append(buf, n); }
    std::fclose(f);
    return trimStr(out);
}

void saveTmp(const char* name, const std::string& val) {
    if (val.empty()) { return; }
    const std::string path = tmpPath(name);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { return; }
    std::fwrite(val.data(), 1, val.size(), f);
    std::fclose(f);
}

void removeTmp(const char* name) {
    std::remove(tmpPath(name).c_str());
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

void postResult(Session* s, bool ok, const std::string& text,
                const std::string& engineUsed, const std::string& err) {
    if (ok) {
        qWarning("fanyibot: engine=%s 成功 textlen=%d B", engineUsed.c_str(), (int)text.size());
    }
    FanyibotDoneEvent* ev = new FanyibotDoneEvent();
    ev->chatId         = s->req.chatId;
    ev->chatType       = s->req.chatType;
    ev->localId        = s->req.localId;
    ev->success        = ok;
    ev->translatedText = text;
    ev->engineUsed     = engineUsed;
    ev->errorMsg       = err;
    QObject* target    = s->target;
    removeSession(s);
    if (target) { 
		// QApplication::postEvent(target, ev);
		qAppPostEvent(target, ev);
	 }
    else { delete ev; }
}

std::string httpErr(const HttpResponse& resp) {
    std::string err = "HTTP " + std::to_string(resp.httpCode);
    if (!resp.curlErrStr.empty()) { err += " " + resp.curlErrStr; }
    return err;
}

// msedge: [{"translations":[{"text":"…"}]}]（touse/oai 的真实返回为最外层数组）
std::string parseMsedge(const std::string& body) {
    cJSON* root = cJSON_Parse(body.c_str());
    std::string out;
    if (root) {
        cJSON* first = cJSON_IsArray(root) ? cJSON_GetArrayItem(root, 0) : root;
        cJSON* arr = first ? cJSON_GetObjectItem(first, "translations") : nullptr;
        cJSON* tr0 = arr ? cJSON_GetArrayItem(arr, 0) : nullptr;
        out = tr0 ? jsonStr(tr0, "text") : std::string();
        cJSON_Delete(root);
    }
    return out;
}

// google: [[["译文","原文",…],…
std::string parseGoogle(const std::string& body) {
    const size_t head = body.find("[[\"");
    if (head == std::string::npos) { return std::string(); }
    size_t start = head + 3;
    if (start >= body.size()) { return std::string(); }
    if (body[start] == '"') { start++; }
    const size_t b = body.find('"', start);
    if (b == std::string::npos) { return std::string(); }
    return body.substr(start, b - start);
}

// 有道（v2 接口）：translateResult[][].tgt，回退 v1 .t；多段 / 多行以 "" 拼接
std::string parseYoudaoV2(const std::string& body) {
    cJSON* root = cJSON_Parse(body.c_str());
    std::string out;
    if (root) {
        cJSON* tr = cJSON_GetObjectItem(root, "translateResult");
        if (cJSON_IsArray(tr)) {
            const int rows = cJSON_GetArraySize(tr);
            for (int i = 0; i < rows; i++) {
                cJSON* segArr = cJSON_GetArrayItem(tr, i);
                const int segs = cJSON_IsArray(segArr) ? cJSON_GetArraySize(segArr) : 0;
                for (int j = 0; j < segs; j++) {
                    std::string t = jsonStr(cJSON_GetArrayItem(segArr, j), "tgt");
                    if (t.empty()) { t = jsonStr(cJSON_GetArrayItem(segArr, j), "t"); }
                    out += t;
                }
            }
        }
        if (out.empty()) {
            cJSON* tl = cJSON_GetObjectItem(root, "translation");
            cJSON* t0 = tl ? cJSON_GetArrayItem(tl, 0) : nullptr;
            if (t0) { out = jsonStr(t0, "t"); }
        }
        cJSON_Delete(root);
    }
    return out;
}

int youdaoResultCode(const std::string& body) {
    cJSON* root = cJSON_Parse(body.c_str());
    int code = 0;
    if (root) {
        cJSON* c = cJSON_GetObjectItem(root, "code");
        if (c && cJSON_IsNumber(c)) { code = (int)c->valuedouble; }
        cJSON_Delete(root);
    }
    return code;
}

// 雅翻：{"code":200,"text":["译文",…]}
std::string parseYandex(const std::string& body) {
    cJSON* root = cJSON_Parse(body.c_str());
    std::string out;
    if (root) {
        cJSON* arr = cJSON_GetObjectItem(root, "text");
        cJSON* first = arr ? cJSON_GetArrayItem(arr, 0) : nullptr;
        if (first && cJSON_IsString(first)) {
            const char* v = cJSON_GetStringValue(first);
            if (v) { out = v; }
        }
        cJSON_Delete(root);
    }
    return out;
}

// deepl-web 实例 id（app_information.instance_id，进程内固定 32 hex）
std::string deeplWebInstanceId() {
    static std::string id;
    if (id.empty()) {
        char buf[33];
        for (int i = 0; i < 32; i++) {
            const int v = rand() & 0xF;
            buf[i] = (char)(v < 10 ? '0' + v : 'a' + v - 10);
        }
        buf[32] = 0;
        id = buf;
    }
    return id;
}

// deepl-web（oneshot）：{"translations":[{"detected_source_language":"en","text":"…"}]}
std::string parseDeeplWeb(const std::string& body) {
    cJSON* root = cJSON_Parse(body.c_str());
    std::string out;
    if (root) {
        cJSON* arr = cJSON_GetObjectItem(root, "translations");
        cJSON* t0 = arr ? cJSON_GetArrayItem(arr, 0) : nullptr;
        out = t0 ? jsonStr(t0, "text") : std::string();
        cJSON_Delete(root);
    }
    return out;
}

// 雅翻 SID 抓取（touse/oai 同款 esc-js 正则）：sid\:\s'([0-9a-f.]+)'
std::string scrapeYandexSid(const std::string& html) {
    const std::string tag = "sid: '";
    const size_t at = html.find(tag);
    if (at == std::string::npos) { return std::string(); }
    const size_t b = at + tag.size();
    size_t e = b;
    while (e < html.size()) {
        const char c = html[e];
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '.';
        if (!ok) { break; }
        e++;
    }
    return html.substr(b, e - b);
}

// ── 有道 key 缓存（$TMP/youdao_web_key.txt，JSON 与 touse/oai 互通）──
bool youdaoKeyParse(const std::string& json, std::string& secretKey,
                    std::string& aesKey, std::string& aesIv, std::string& cookie) {
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) { return false; }
    cJSON* body = cJSON_GetObjectItem(root, "data");   // 现接口返回 data.{...}；旧缓存为顶层
    if (!cJSON_IsObject(body)) { body = root; }
    secretKey = jsonStr(body, "secretKey");
    aesKey    = jsonStr(body, "aesKey");
    aesIv     = jsonStr(body, "aesIv");
    cookie    = jsonStr(body, "cookie");
    const bool ok = !secretKey.empty() && !aesKey.empty() && !aesIv.empty();
    cJSON_Delete(root);
    return ok;
}

bool youdaoLoadCache(Session* s) {
    std::string secretKey, aesKey, aesIv, cookie;
    if (!youdaoKeyParse(loadTmp("youdao_web_key.txt"), secretKey, aesKey, aesIv, cookie)) {
        return false;
    }
    s->ydSecret = secretKey;
    s->ydAesKey = aesKey;
    s->ydAesIv  = aesIv;
    s->ydCookie = cookie;
    return true;
}

void youdaoSaveKeyCache(Session* s) {
    std::string cookie = s->ydCookie;
    if (cookie.empty()) { cookie = kYoudaoFakeCookie; }
    const std::string json =
        "{\"secretKey\":\"" + jsonEscape(s->ydSecret)
        + "\",\"aesKey\":\"" + jsonEscape(s->ydAesKey)
        + "\",\"aesIv\":\"" + jsonEscape(s->ydAesIv)
        + "\",\"cookie\":\"" + jsonEscape(cookie) + "\"}";
    saveTmp("youdao_web_key.txt", json);
}

// ── base64url 解码（- _ 转 + /，容忍尾部 =）──
static const char* kBase64Chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64UrlDecode(const std::string& s) {
    static int8_t rev[256];
    static bool init = false;
    if (!init) {
        std::memset(rev, -1, sizeof(rev));
        for (int i = 0; kBase64Chars[i]; i++) { rev[(uint8_t)kBase64Chars[i]] = (int8_t)i; }
        init = true;
    }
    std::string out;
    uint32_t bits = 0;
    int nbits = 0;
    for (size_t i = 0; i < s.size(); i++) {
        const unsigned char c = (unsigned char)s[i];
        if (c == '=') { break; }
        int v = -1;
        if (c == '-') { v = 62; }
        else if (c == '_') { v = 63; }
        else if (c < 128) { v = rev[c]; }
        if (v < 0) { continue; }
        bits = (bits << 6) | (uint32_t)v;
        nbits += 6;
        if (nbits >= 8) {
            nbits -= 8;
            out += (char)((bits >> nbits) & 0xFF);
        }
    }
    return out;
}

// ── AES-128（Sbox/InvSbox/密钥扩展/解密），仅 CBC 解密用到 ──
static const uint8_t kAesSbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
    0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
    0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
    0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
    0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
    0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b,
    0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
    0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
    0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17,
    0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
    0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
    0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9,
    0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
    0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
    0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94,
    0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
    0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

static const uint8_t kAesInvSbox[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38,
    0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87,
    0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d,
    0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2,
    0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16,
    0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda,
    0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a,
    0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02,
    0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea,
    0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85,
    0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89,
    0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20,
    0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31,
    0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d,
    0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0,
    0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26,
    0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d,
};

static uint8_t aesMul(uint8_t a, uint8_t b) {
    uint8_t p = 0;
    for (int i = 0; i < 8; i++) {
        if (b & 1) { p ^= a; }
        const bool hi = (a & 0x80) != 0;
        a <<= 1;
        if (hi) { a ^= 0x1b; }
        b >>= 1;
    }
    return p;
}

static void aesExpandKey(const uint8_t* key, uint8_t* rk) {
    static const uint8_t rcon[11] = {
        0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36,
    };
    for (int i = 0; i < 16; i++) { rk[i] = key[i]; }
    for (int i = 4; i < 44; i++) {
        uint8_t t[4];
        for (int j = 0; j < 4; j++) { t[j] = rk[(i - 1) * 4 + j]; }
        if (i % 4 == 0) {
            const uint8_t first = t[0];
            for (int j = 0; j < 3; j++) { t[j] = t[j + 1]; }
            t[3] = first;
            for (int j = 0; j < 4; j++) { t[j] = kAesSbox[t[j]]; }
            t[0] ^= rcon[i / 4];
        }
        for (int j = 0; j < 4; j++) { rk[i * 4 + j] = rk[(i - 4) * 4 + j] ^ t[j]; }
    }
}

static void aesAddRoundKey(uint8_t* s, const uint8_t* rk, int round) {
    for (int i = 0; i < 16; i++) { s[i] ^= rk[round * 16 + i]; }
}

static void aesInvShiftRows(uint8_t* s) {
    uint8_t tmp[16];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) { tmp[c * 4 + r] = s[((c + 4 - r) % 4) * 4 + r]; }
    }
    std::memcpy(s, tmp, 16);
}

static void aesInvSubBytes(uint8_t* s) {
    for (int i = 0; i < 16; i++) { s[i] = kAesInvSbox[s[i]]; }
}

static void aesInvMixColumns(uint8_t* s) {
    for (int c = 0; c < 4; c++) {
        const int o = c * 4;
        const uint8_t a = s[o], b = s[o + 1], e = s[o + 2], d = s[o + 3];
        s[o]     = aesMul(a, 14) ^ aesMul(b, 11) ^ aesMul(e, 13) ^ aesMul(d, 9);
        s[o + 1] = aesMul(a, 9)  ^ aesMul(b, 14) ^ aesMul(e, 11) ^ aesMul(d, 13);
        s[o + 2] = aesMul(a, 13) ^ aesMul(b, 9)  ^ aesMul(e, 14) ^ aesMul(d, 11);
        s[o + 3] = aesMul(a, 11) ^ aesMul(b, 13) ^ aesMul(e, 9)  ^ aesMul(d, 14);
    }
}

static void aesDecryptBlock(const uint8_t* key, const uint8_t* in, uint8_t* out) {
    uint8_t rk[176];
    aesExpandKey(key, rk);
    uint8_t s[16];
    std::memcpy(s, in, 16);
    aesAddRoundKey(s, rk, 10);
    for (int round = 9; round >= 1; round--) {
        aesInvShiftRows(s);
        aesInvSubBytes(s);
        aesAddRoundKey(s, rk, round);
        aesInvMixColumns(s);
    }
    aesInvShiftRows(s);
    aesInvSubBytes(s);
    aesAddRoundKey(s, rk, 0);
    std::memcpy(out, s, 16);
}

// key/iv 各 16 字节（调用方传 md5 原字节），PKCS7 去填充
std::string aes128CbcDecrypt(const std::string& key, const std::string& iv,
                             const std::string& data) {
    std::string out;
    if (key.size() != 16 || iv.size() != 16 || data.empty() || data.size() % 16 != 0) {
        return out;
    }
    const uint8_t* k = (const uint8_t*)key.data();
    uint8_t prev[16];
    std::memcpy(prev, iv.data(), 16);
    for (size_t off = 0; off < data.size(); off += 16) {
        uint8_t blk[16];
        aesDecryptBlock(k, (const uint8_t*)data.data() + off, blk);
        for (int i = 0; i < 16; i++) {
            blk[i] ^= prev[i];
            prev[i] = (uint8_t)data[off + i];
        }
        out.append((const char*)blk, 16);
    }
    if (out.empty()) { return out; }
    const uint8_t pad = (uint8_t)out[out.size() - 1];
    if (pad < 1 || pad > 16) { return std::string(); }
    for (int i = 0; i < pad; i++) {
        if ((uint8_t)out[out.size() - 1 - i] != pad) { return std::string(); }
    }
    out.erase(out.size() - pad);
    return out;
}

std::vector<const FanyibotEngine*> buildCandidates(const FanyibotRequest& req,
                                                   std::string& reason) {
    std::vector<const FanyibotEngine*> out;
    if (req.engine == "all" || req.engine.empty() || req.engine == "any") {
        for (int i = 0; i < kEngineCount; i++) {
            if (!kEngines[i].unsupported) { out.push_back(&kEngines[i]); }
        }
        if (out.empty()) {
            reason = "没有可用引擎：候选表全为不可用";
            return out;
        }
        randomShuffle(out.begin(), out.end());   // all/any/空：每次请求随机取序
        return out;
    }
    for (int i = 0; i < kEngineCount; i++) {
        if (req.engine != kEngines[i].name) { continue; }
        if (kEngines[i].unsupported) {
            reason = std::string(kEngines[i].name) + " 需官方 auth key，暂不可用";
        } else {
            out.push_back(&kEngines[i]);
        }
        return out;
    }
    reason = "未知引擎：" + req.engine;
    return out;
}

void onDone(const HttpResponse& resp, void* udata);
void startNext(Session* s);

void failEngine(Session* s, const FanyibotEngine& e, const std::string& why) {
    s->lastError = std::string(e.name) + ": " + why;
    if (s->tried.empty() || s->tried.back().first != e.name) {
        s->tried.push_back(std::make_pair(std::string(e.name), why));
    } else {
        s->tried.back().second = why;
    }
    qWarning("fanyibot: engine=%s 失败: %s → 切换候选", e.name, why.c_str());
    s->cred.clear();
    s->step = 0;
    s->credRetry = 0;
    s->ydCookie = s->ydSecret = s->ydAesKey = s->ydAesIv = std::string();
    s->ydPair = 0;
    ++s->index;
    startNext(s);
}

void sendHttp(Session* s, const std::string& url, const std::string& method,
              const std::string& body,
              const std::map<std::string, std::string>& headers,
              bool follow = true) {
    HttpRequest req(url, method, body, 20, headers);
    req.followRedirects = follow;
    if (!s->cands.empty() && s->index >= 0 && s->index < (int)s->cands.size()) {
        req.proxy = netProxyFor(s->cands[s->index]->needsProxy);
    }
    EventPoller::addRequest(req, onDone, s);
}

void sendEngine(Session* s, const FanyibotEngine& e) {
    const std::string code = fanyibotLangCode(e.id, s->req.toLang);
    if (code.empty()) {
        failEngine(s, e, "不支持目标语言 " + s->req.toLang);
        return;
    }
    qWarning("fanyibot: 候选[%d/%d] engine=%s step=%d host=%s to=%s textlen=%d B",
         s->index, (int)s->cands.size(), e.name, (int)s->step,
         engineHost(e, s->step), code.c_str(), (int)s->req.text.size());
    const std::string& text = s->req.text;
    std::map<std::string, std::string> h;

    if (e.id == kFanyibotMsedge) {
        // 免鉴权新端点：POST /translate/translatetext，body 为字符串数组
        h["User-Agent"]   = kMsedgeUA;
        h["Content-Type"] = "application/json; charset=utf-8";
        sendHttp(s, "https://edge.microsoft.com/translate/translatetext?from=&to=" + code
                    + "&isEnterpriseClient=false",
                 "POST", "[\"" + jsonEscape(text) + "\"]", h);
        return;
    }

    if (e.id == kFanyibotGoogle) {
        h["User-Agent"] = kBrowserUA;
        sendHttp(s, "https://translate.googleapis.com/translate_a/single?client=gtx"
                    "&sl=auto&tl=" + code + "&dt=t&q=" + urlEncode(text),
                 "GET", "", h);
        return;
    }

    if (e.id == kFanyibotDeeplWeb) {
        // 免鉴权 oneshot 端点（DeepL 扩展/iOS App 同款）；Authorization 字面量 "None"
        h["User-Agent"]     = kDeepLWebUA;
        h["Content-Type"]   = "application/json";
        h["Accept"]         = "*/*";
        h["Authorization"]  = "None";
        h["Origin"]         = "chrome-extension://cofdbpoegempjloogbagkncekinflcnj";
        h["Sec-Fetch-Site"] = "cross-site";
        h["Sec-Fetch-Mode"] = "cors";
        h["Sec-Fetch-Dest"] = "empty";
        const std::string body =
            "{\"text\":[\"" + jsonEscape(text) + "\"],"
            "\"target_lang\":\"" + code + "\","
            "\"usage_type\":\"Translate\","
            "\"app_information\":{\"os\":\"brex_macOS\","
            "\"os_version\":\"brex_chrome_120.0.0.0\","
            "\"app_version\":\"1.86.0\",\"app_build\":\"chrome_web_store\","
            "\"instance_id\":\"" + deeplWebInstanceId() + "\"}}";
        sendHttp(s, "https://oneshot-free.www.deepl.com/v1/translate", "POST", body, h);
        return;
    }

    if (e.id == kFanyibotYoudao) {
        if (s->step == 0) {   // 取 key：磁盘缓存/会话命中，否则 key 接口（无需 warm cookie）
            if (s->ydAesKey.empty() || s->ydSecret.empty()) { youdaoLoadCache(s); }
            if (!s->ydAesKey.empty() && !s->ydSecret.empty()) {
                qWarning("fanyibot: youdao key 命中(缓存/会话)");
                s->step = 1;
                sendEngine(s, e);
                return;
            }
            if (s->ydCookie.empty()) { s->ydCookie = kYoudaoFakeCookie; }
            const long long ms = nowMs();
            const std::string mysticTime = std::to_string(ms);
            const std::string sign = md5Hex(std::string(kYoudaoClient) + "&mysticTime="
                                            + mysticTime + "&product=" + kYoudaoProduct
                                            + "&key=" + kYoudaoKeyPairs[s->ydPair].secret);
            const std::string url = std::string(kYoudaoKeyURL)
                + "?keyid=" + kYoudaoKeyPairs[s->ydPair].id
                + "&pointParam=" + kYoudaoKeyParam
                + "&client=" + kYoudaoClient
                + "&product=" + kYoudaoProduct
                + "&mysticTime=" + mysticTime
                + "&sign=" + sign;
            h["User-Agent"] = kYoudaoUA;
            h["Referer"]    = kYoudaoReferer;
            h["Cookie"]     = s->ydCookie;
            sendHttp(s, url, "GET", "", h);
            return;
        }
        if (s->step == 1) {   // 正式翻译
            const long long ms = nowMs();
            const std::string mysticTime = std::to_string(ms);
            const std::string sign = md5Hex(std::string(kYoudaoClient) + "&mysticTime="
                                            + mysticTime + "&product=" + kYoudaoProduct
                                            + "&key=" + s->ydSecret);
            const std::string body =
                "i=" + urlEncode(text)
                + "&from=auto&to=" + code
                + "&useTerm=false"
                + "&dictResult=true"
                + "&keyid=" + kYoudaoProduct
                + "&appVersion=" + kYoudaoAppVer
                + "&vendor=web"
                + "&pointParam=" + urlEncode(kYoudaoKeyParam)
                + "&keyfrom=fanyi.web"
                + "&client=" + kYoudaoClient
                + "&product=" + kYoudaoProduct
                + "&mysticTime=" + mysticTime
                + "&sign=" + sign
                + "&mid=1&screen=1&model=1&network=wifi&abtest=0&yduuid=" + kYoudaoUserID;
            h["User-Agent"]   = kYoudaoUA;
            h["Referer"]      = kYoudaoReferer;
            h["Content-Type"] = "application/x-www-form-urlencoded; charset=UTF-8";
            h["Cookie"]       = s->ydCookie.empty() ? kYoudaoFakeCookie : s->ydCookie;
            sendHttp(s, kYoudaoAPIURL, "POST", body, h);
            return;
        }
        failEngine(s, e, "状态机异常: step=" + std::to_string(s->step));
        return;
    }

    if (e.id == kFanyibotYandex) {
        if (s->step == 0) {   // 取 SID（$TMP/yandex_sid.txt 缓存优先）
            if (s->cred.empty()) { s->cred = loadTmp("yandex_sid.txt"); }
            if (!s->cred.empty()) {
                qWarning("fanyibot: yandex SID 缓存命中 len=%d B", (int)s->cred.size());
                s->step = 1;
                sendEngine(s, e);
                return;
            }
            qWarning("fanyibot: yandex SID 缓存未命中，重新抓取");
            h["User-Agent"] = kYandexUA;
            sendHttp(s, "https://translate.yandex.com/", "GET", "", h, false);   // 反爬 302 秒判失败
            return;
        }
        h["User-Agent"] = kYandexUA;
        sendHttp(s, "https://translate.yandex.com/api/v1/tr.json/translate?srv=tr-url-widget"
                    + std::string("&id=") + s->cred + "-0-0"
                    + "&format=text&lang=-" + code
                    + "&text=" + urlEncode(text),
                 "GET", "", h);
        return;
    }

    failEngine(s, e, "暂不可用");
}

void startNext(Session* s) {
    if (s->index < (int)s->cands.size()) {
        sendEngine(s, *s->cands[s->index]);
        return;
    }
    std::string err;
    for (size_t i = 0; i < s->tried.size(); i++) {
        if (i) { err += "; "; }
        err += s->tried[i].first + ": " + s->tried[i].second;
    }
    if (err.empty()) {
        err = s->lastError.empty() ? std::string("所有候选引擎均失败") : s->lastError;
    }
    qWarning("fanyibot: 全部候选失败: %s", err.c_str());
    postResult(s, false, std::string(), std::string(), err);
}

void onDone(const HttpResponse& resp, void* udata) {
    Session* s = static_cast<Session*>(udata);
    if (!s) { return; }
    if (!isCurrentSeq(s)) {
        qWarning("fanyibot: 迟到回包丢弃 seq=%u", (unsigned int)s->seq);
        removeSession(s);
        return;
    }
    const FanyibotEngine* e =
        (s->index < (int)s->cands.size()) ? s->cands[s->index] : nullptr;
    if (!e) { removeSession(s); return; }

    const bool httpOk = resp.httpCode >= 200 && resp.httpCode < 300
                        && resp.curlErrStr.empty();
    qWarning("fanyibot: resp engine=%s step=%d http=%d curl=[%s] body=[%s]",
         e->name, (int)s->step, resp.httpCode,
         resp.curlErrStr.c_str(), briefOf(resp.body).c_str());

    if (e->id == kFanyibotMsedge) {
        if (httpOk) {
            const std::string out = parseMsedge(resp.body);
            if (!out.empty()) {
                postResult(s, true, out, e->name, std::string());
                return;
            }
        }
        failEngine(s, *e, httpErr(resp));
        return;
    }

    if (e->id == kFanyibotGoogle) {
        if (httpOk) {
            const std::string out = parseGoogle(resp.body);
            if (!out.empty()) {
                postResult(s, true, out, e->name, std::string());
                return;
            }
        }
        failEngine(s, *e, httpErr(resp));
        return;
    }

    if (e->id == kFanyibotDeeplWeb) {
        if (httpOk) {
            const std::string out = parseDeeplWeb(resp.body);
            if (!out.empty()) {
                postResult(s, true, out, e->name, std::string());
                return;
            }
        }
        failEngine(s, *e, httpErr(resp));
        return;
    }

    if (e->id == kFanyibotYoudao) {
        if (httpOk && s->step == 0) {
            std::string secretKey, aesKey, aesIv, cookie;
            if (youdaoKeyParse(resp.body, secretKey, aesKey, aesIv, cookie)) {
                s->ydSecret = secretKey;
                s->ydAesKey = aesKey;
                s->ydAesIv  = aesIv;
                if (!cookie.empty()) { s->ydCookie = cookie; }
                youdaoSaveKeyCache(s);
                qWarning("fanyibot: youdao key 新取成功，写入缓存 pair=%d", s->ydPair);
                s->step = 1;
                sendEngine(s, *e);
                return;
            }
            if (s->ydPair < kYoudaoPairCount - 1) {   // 换 legacy keyid 再试
                ++s->ydPair;
                qWarning("fanyibot: youdao key 未解析成功，换 keyid[%d]", s->ydPair);
                sendEngine(s, *e);
                return;
            }
            failEngine(s, *e, "key 接口未解析到密钥");
            return;
        }
        if (httpOk && s->step == 1) {
            const int code = youdaoResultCode(resp.body);
            if (code == 40 || code == 50) {
                if (s->credRetry < 1) {
                    ++s->credRetry;
                    removeTmp("youdao_web_key.txt");
                    qWarning("fanyibot: youdao 密钥失效(code %d)，删除缓存重取", code);
                    s->ydCookie.clear();
                    s->ydSecret = s->ydAesKey = s->ydAesIv = std::string();
                    s->step = 0;
                    sendEngine(s, *e);
                    return;
                }
                failEngine(s, *e, "密钥失效(code " + std::to_string(code) + ")");
                return;
            }
            if (code != 0) {
                failEngine(s, *e, code == 20 ? "文本超长" : "有道返回码 " + std::to_string(code));
                return;
            }
            if (!resp.body.empty()) {
                const std::string plain =
                    aes128CbcDecrypt(md5Raw(s->ydAesKey), md5Raw(s->ydAesIv),
                                     base64UrlDecode(resp.body));
                if (!plain.empty()) {
                    const std::string out = parseYoudaoV2(plain);
                    if (!out.empty()) {
                        postResult(s, true, out, e->name, std::string());
                        return;
                    }
                }
                if (s->credRetry < 1) {   // 解密失败：key 不干净，重取一次
                    ++s->credRetry;
                    removeTmp("youdao_web_key.txt");
                    qWarning("fanyibot: youdao 解密失败，key 不干净，删除缓存重取");
                    s->ydSecret = s->ydAesKey = s->ydAesIv = std::string();
                    s->step = 0;
                    sendEngine(s, *e);
                    return;
                }
            }
        }
        failEngine(s, *e, httpErr(resp));
        return;
    }

    if (e->id == kFanyibotYandex) {
        if (s->step == 0) {
            const std::string sid = httpOk ? scrapeYandexSid(resp.body) : std::string();
            if (!sid.empty()) {
                saveTmp("yandex_sid.txt", sid);
                qWarning("fanyibot: yandex SID 新抓成功，写入缓存");
                s->cred = sid;
                s->step = 1;
                sendEngine(s, *e);
                return;
            }
            failEngine(s, *e, httpOk ? "首页未解析到 SID" : httpErr(resp));
            return;
        }
        if (httpOk) {
            const std::string out = parseYandex(resp.body);
            if (!out.empty()) {
                postResult(s, true, out, e->name, std::string());
                return;
            }
            if (s->credRetry < 1) {   // SID 失效：重抓一次
                ++s->credRetry;
                removeTmp("yandex_sid.txt");
                qWarning("fanyibot: yandex SID 失效，删除缓存重抓");
                s->step = 0;
                s->cred.clear();
                sendEngine(s, *e);
                return;
            }
        }
        failEngine(s, *e, httpErr(resp));
        return;
    }

    failEngine(s, *e, "暂不可用");
}

void ensureSeed() {
    QMutexLocker lock(&g_mutex);
    if (g_seeded) { return; }
    g_seeded = true;
    srand((unsigned int)time(nullptr));
}

} // namespace

const std::vector<FanyibotEngine>& fanyibotEngines() {
    static std::vector<FanyibotEngine> engines(kEngines, kEngines + kEngineCount);
    return engines;
}

std::string fanyibotLangCode(FanyibotEngineId id, const std::string& toLang) {
    for (int i = 0; i < kLangCount; i++) {
        if (toLang != kLangs[i].display) { continue; }
        switch (id) {
        case kFanyibotMsedge: return kLangs[i].msedge;
        case kFanyibotGoogle: return kLangs[i].google;
        case kFanyibotYoudao: return kLangs[i].youdao;
        case kFanyibotYandex: return kLangs[i].yandex;
        case kFanyibotDeepl:  break;   // 暂不可用，无语言码
        case kFanyibotDeeplWeb: return kLangs[i].deeplweb;
        }
        return std::string();
    }
    return std::string();
}

void fanyibotTranslateStart(const FanyibotRequest& req, QObject* target) {
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
    if (!reason.empty()) { s->lastError = reason; }

    std::string order;
    for (size_t i = 0; i < s->cands.size(); i++) {
        if (i) { order += " → "; }
        order += s->cands[i]->name;
    }
    qWarning("fanyibot: engine=%s toLang=%s 候选 %d 个: %s",
             req.engine.empty() ? "any" : req.engine.c_str(),
             req.toLang.c_str(), (int)s->cands.size(), order.c_str());
    startNext(s);
}
