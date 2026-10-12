#include "websearchbot.h"
#include "eventpoller.h"
#include "netproxy.h"
#include "restapi.h"

// #include <qapplication.h>
#include <qobject.h>
#include <qmutex.h>
#include <qglobal.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <map>

namespace {

// ── 后端静态表（零认证：直连匿名公共搜索页；顺序 = "any" shuffle 前的初始顺序）──
const WebsearchBackend kBackends[] = {
    { kWebsearchGoogle,    "google",     true },
    { kWebsearchBing,      "bing",       true },
    { kWebsearchDuckduckgo,"duckduckgo", true },
    { kWebsearchYandex,    "yandex",     true },
    { kWebsearchSogou,     "sogou",      false },
};
const int kBackendCount = int(sizeof(kBackends) / sizeof(kBackends[0]));

// 语言参数表（sogou 无语言参数，固定中文）
const WebsearchLangParam kLangParams[] = {
    { "zh", "中文",   "zh-CN", "cn", "zh-CN", "cn-zh",  "",    ""   },
    { "ja", "日本語", "ja",    "JP", "ja-JP", "jp-jp",  "",    ""   },
    { "ko", "한국어", "ko",    "KR", "ko-KR", "kr-kr",  "",    ""   },
    { "en", "English","en",    "US", "en-US", "us-en",  "en",  "84" },
    { "ru", "Русский","ru",    "RU", "ru-RU", "ru-ru",  "ru",  "225"},
    { "de", "Deutsch", "de",   "DE", "de-DE", "de-de",  "de",  "102"},
    { "fr", "Français","fr",   "FR", "fr-FR", "fr-fr",  "",    ""   },
    { "ar", "العربية", "ar",   "SA", "ar-SA", "xa-ar",  "",    ""   },
};
const int kLangParamCount = int(sizeof(kLangParams) / sizeof(kLangParams[0]));

const WebsearchLangParam* websearchLangParamImpl(const std::string& code) {
    for (int i = 0; i < kLangParamCount; ++i) {
        if (code == kLangParams[i].code) { return &kLangParams[i]; }
    }
    return nullptr;
}

// ── 通用文本小工具 ──

std::string trimStr(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) { return std::string(); }
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string collapseWs(const std::string& in) {
    std::string out;
    bool ws = false;
    for (size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ws = true;
        } else {
            if (ws && !out.empty()) { out += ' '; }
            ws = false;
            out += c;
        }
    }
    return out;
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

void appendUtf8(std::string& out, unsigned int cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

struct NamedEntity {
    const char* ent;
    const char* rep;
};

const NamedEntity kEntities[] = {
    { "amp",  "&"  },
    { "lt",   "<"  },
    { "gt",   ">"  },
    { "quot", "\"" },
    { "apos", "'"  },
    { "nbsp", " "  },
    { "hellip", "…" },
};
const int kEntityCount = int(sizeof(kEntities) / sizeof(kEntities[0]));

std::string htmlUnescape(const std::string& in) {
    std::string out;
    size_t i = 0;
    while (i < in.size()) {
        const char c = in[i];
        if (c == '&') {
            const size_t sc = in.find(';', i);
            if (sc != std::string::npos && sc - i <= 12) {
                const std::string ent = in.substr(i + 1, sc - i - 1);
                bool replaced = false;
                if (!ent.empty() && ent[0] == '#') {
                    const bool isHex = (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X'));
                    const size_t d = isHex ? 2 : 1;
                    unsigned int cp = 0;
                    bool valid = true;
                    for (size_t k = d; k < ent.size(); ++k) {
                        const int v = hexVal(ent[k]);
                        if (v < 0) { valid = false; break; }
                        cp = cp * (isHex ? 16u : 10u) + (unsigned int)v;
                    }
                    if (valid && cp > 0) {
                        appendUtf8(out, cp);
                        replaced = true;
                    }
                } else {
                    for (int e = 0; e < kEntityCount; ++e) {
                        if (ent == kEntities[e].ent) {
                            out += kEntities[e].rep;
                            replaced = true;
                            break;
                        }
                    }
                }
                if (replaced) {
                    i = sc + 1;
                    continue;
                }
            }
        }
        out += c;
        ++i;
    }
    return out;
}

// 剥除 script/style 整块（其内部 < > 会干扰标签扫描）
std::string removeScriptStyle(const std::string& in) {
    std::string out;
    size_t i = 0;
    while (i < in.size()) {
        const size_t lt = in.find('<', i);
        if (lt == std::string::npos) {
            out += in.substr(i);
            break;
        }
        out += in.substr(i, lt - i);
        std::string name;
        size_t sp = lt + 1;
        while (sp < in.size() && (isalnum((unsigned char)in[sp]) || in[sp] == '/')) {
            name += in[sp];
            ++sp;
        }
        if (name == "script" || name == "style") {
            const std::string closeTag = "</" + name;
            const size_t ce = in.find(closeTag, sp);
            if (ce == std::string::npos) {
                i = in.size();
                break;
            }
            const size_t gt = in.find('>', ce);
            i = (gt == std::string::npos) ? in.size() : gt + 1;
        } else {
            i = lt + 1;
        }
    }
    return out;
}

std::string stripTags(const std::string& in) {
    std::string out;
    bool inTag = false;
    for (size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c == '<') { inTag = true; continue; }
        if (c == '>') { inTag = false; continue; }
        if (!inTag) { out += c; }
    }
    out = collapseWs(htmlUnescape(out));
    return trimStr(out);
}

std::string percentDecode(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c == '%' && i + 2 < in.size()) {
            const int h = hexVal(in[i + 1]);
            const int l = hexVal(in[i + 2]);
            if (h >= 0 && l >= 0) {
                out += (char)((h << 4) | l);
                i += 2;
                continue;
            }
        }
        out += c;
    }
    return out;
}

// 取查询串中 key= 的参数值（要求前导为 ? 或 &）
std::string queryParam(const std::string& s, const std::string& key) {
    const std::string needle = key + "=";
    size_t pos = 0;
    while ((pos = s.find(needle, pos)) != std::string::npos) {
        bool prevOk = (pos == 0);
        if (!prevOk) {
            const char pc = s[pos - 1];
            prevOk = (pc == '?' || pc == '&');
        }
        if (prevOk) {
            const size_t e = s.find('&', pos + needle.size());
            if (e == std::string::npos) { return s.substr(pos + needle.size()); }
            return s.substr(pos + needle.size(), e - pos - needle.size());
        }
        pos += needle.size();
    }
    return std::string();
}

int b64Val(char c) {
    if (c >= 'A' && c <= 'Z') { return c - 'A'; }
    if (c >= 'a' && c <= 'z') { return c - 'a' + 26; }
    if (c >= '0' && c <= '9') { return c - '0' + 52; }
    if (c == '+') { return 62; }
    if (c == '/') { return 63; }
    return -1;
}

std::string base64Decode(const std::string& in) {
    std::string out;
    int val = 0;
    int nbits = 0;
    for (size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c == '=') { break; }
        const int d = b64Val(c);
        if (d < 0) { continue; }
        val = (val << 6) | d;
        nbits += 6;
        if (nbits >= 8) {
            nbits -= 8;
            out += (char)((val >> nbits) & 0xff);
        }
    }
    return out;
}

// ── HTML 锚点扫描 ──

// 找下一个 <a ... href="..." ...> 内容 ... </a>，返回 href 起止与内容区间；失败 false
bool nextAnchor(const std::string& body, size_t start,
                size_t* hrefB, size_t* hrefE, size_t* contentB, size_t* contentE) {
    size_t pos = start;
    while (true) {
        const size_t a = body.find("<a", pos);
        if (a == std::string::npos) { return false; }
        if (a + 2 < body.size()
                && body[a + 2] != ' ' && body[a + 2] != '\t'
                && body[a + 2] != '\n' && body[a + 2] != '\r' && body[a + 2] != '>') {
            pos = a + 2;
            continue;
        }
        const size_t gt = body.find('>', a);
        if (gt == std::string::npos) { return false; }
        size_t h = body.find("href", a);
        if (h == std::string::npos || h > gt) {
            pos = gt;
            continue;
        }
        h += 4;
        while (h < gt && (body[h] == ' ' || body[h] == '\t')) { ++h; }
        if (h >= gt || body[h] != '=') {
            pos = gt;
            continue;
        }
        ++h;
        while (h < gt && (body[h] == ' ' || body[h] == '\t')) { ++h; }
        if (h >= gt) {
            pos = gt;
            continue;
        }
        const char q = body[h];
        if (q != '"' && q != '\'') {
            pos = gt;
            continue;
        }
        const size_t close = body.find(q, h + 1);
        if (close == std::string::npos || close > gt) {
            pos = gt;
            continue;
        }
        const size_t closing = body.find("</a>", gt + 1);
        if (closing == std::string::npos) { return false; }
        *hrefB = h + 1;
        *hrefE = close;
        *contentB = gt + 1;
        *contentE = closing;
        return true;
    }
}

// 锚点内容里取 h2/h3 标题文本；无标题用整段文本
std::string titleFromContent(const std::string& seg) {
    const size_t h2 = seg.find("<h2");
    const size_t h3 = seg.find("<h3");
    size_t p = std::string::npos;
    if (h2 != std::string::npos && h3 != std::string::npos) {
        p = (h2 < h3) ? h2 : h3;
    } else if (h2 != std::string::npos) {
        p = h2;
    } else if (h3 != std::string::npos) {
        p = h3;
    }
    if (p != std::string::npos) {
        const size_t gt = seg.find('>', p);
        if (gt != std::string::npos) {
            const size_t close = seg.find("</h", gt);
            if (close != std::string::npos) {
                return stripTags(seg.substr(gt + 1, close - gt - 1));
            }
        }
    }
    return stripTags(seg);
}

// 结果锚点之后寻找摘要容器（启发式：常见 snippet class 名）
std::string snippetAfter(const std::string& body, size_t from) {
    static const char* const kTokens[] = {
        "result__snippet", "b_caption", "VwiC3b", "organic__snippet",
        "text-layout", "fz-mid", "str_info"
    };
    const size_t limit = std::min(body.size(), from + 4000);
    size_t p = from;
    while (p < limit) {
        const size_t lt = body.find('<', p);
        if (lt == std::string::npos || lt >= limit) { break; }
        const size_t gt = body.find('>', lt + 1);
        if (gt == std::string::npos || gt >= limit) { break; }
        const std::string tagStr = body.substr(lt + 1, gt - lt - 1);
        std::string tagName;
        size_t k = 0;
        while (k < tagStr.size() && isalnum((unsigned char)tagStr[k])) {
            tagName += tagStr[k];
            ++k;
        }
        bool hit = false;
        if (tagName == "div" || tagName == "p" || tagName == "span") {
            for (size_t t = 0; t < sizeof(kTokens) / sizeof(kTokens[0]); ++t) {
                if (tagStr.find(kTokens[t]) != std::string::npos) { hit = true; break; }
            }
        }
        if (!hit) {
            p = lt + 1;
            continue;
        }
        const std::string closeTag = "</" + tagName;
        const size_t ce = body.find(closeTag, gt);
        if (ce == std::string::npos || ce >= limit) { break; }
        const std::string txt = stripTags(body.substr(gt + 1, ce - gt - 1));
        if (txt.size() >= 4) { return txt; }
        p = ce + closeTag.size();
    }
    return std::string();
}

// ── 风控/验证码页检测（命中即视为本次失败）──
bool looksLikeBotPage(const std::string& body) {
    static const char* const markers[] = {
        "Enable JavaScript", "enable JavaScript",
        "unusual traffic", "Unusual traffic",
        "anomaly", "Anomaly Detection",
        "SmartCaptcha", "captcha", "CAPTCHA",
        "antispider", "Anti-Spider",
        "请输入验证码", "访问频率过高", "异常流量", "频繁",
        "please verify", "Please verify",
    };
    for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); ++i) {
        if (body.find(markers[i]) != std::string::npos) { return true; }
    }
    return false;
}

// ── 结果链接解析 ──

bool isShellUrl(const std::string& href) {
    if (href.compare(0, 4, "link") == 0) { return true; }
    if (href.compare(0, 5, "/link") == 0) { return true; }
    if (href.find("sogou.com/link") != std::string::npos) { return true; }
    return false;
}

std::string resolveItemUrl(const WebsearchBackend& b, const std::string& href) {
    switch (b.id) {
    case kWebsearchGoogle: {
        if (href.compare(0, 8, "/url?q=") == 0) {
            const std::string real = percentDecode(queryParam(href, "q"));
            if (real.compare(0, 8, "https://") == 0 || real.compare(0, 7, "http://") == 0) {
                return real;
            }
            return std::string();
        }
        if (href.compare(0, 2, "//") == 0) { return "https:" + href; }
        if ((href.compare(0, 8, "https://") == 0 || href.compare(0, 7, "http://") == 0)
                && href.find("google.com") == std::string::npos) {
            return href;
        }
        return std::string();
    }
    case kWebsearchBing: {
        if (href.find("/ck/a") != std::string::npos) {
            std::string u = percentDecode(queryParam(href, "u"));
            if (u.size() >= 2 && u[0] == 'a' && u[1] >= '0' && u[1] <= '9') {
                const std::string dec = base64Decode(u.substr(2));
                if (dec.compare(0, 4, "http") == 0) { return dec; }
            }
            return std::string();
        }
        if (href.find("bing.com") != std::string::npos) { return std::string(); }
        if (href.compare(0, 8, "https://") == 0 || href.compare(0, 7, "http://") == 0) {
            return href;
        }
        return std::string();
    }
    case kWebsearchDuckduckgo: {
        if (href.find("uddg=") != std::string::npos) {
            const std::string real = percentDecode(queryParam(href, "uddg"));
            if (real.compare(0, 4, "http") == 0) { return real; }
            return std::string();
        }
        if (href.compare(0, 2, "//") == 0) { return "https:" + href; }
        if ((href.compare(0, 8, "https://") == 0 || href.compare(0, 7, "http://") == 0)
                && href.find("duckduckgo.com") == std::string::npos) {
            return href;
        }
        return std::string();
    }
    case kWebsearchYandex: {
        if ((href.compare(0, 8, "https://") == 0 || href.compare(0, 7, "http://") == 0)
                && href.find("yandex") == std::string::npos) {
            return href;
        }
        return std::string();
    }
    case kWebsearchSogou: {
        if (isShellUrl(href)) {
            if (href.compare(0, 4, "http") == 0) { return href; }
            if (href[0] == '/') { return "https://www.sogou.com" + href; }
            return "https://www.sogou.com/" + href;
        }
        if (href.find("sogou.com") != std::string::npos) { return std::string(); }
        if (href.compare(0, 8, "https://") == 0 || href.compare(0, 7, "http://") == 0) {
            return href;
        }
        return std::string();
    }
    }
    return std::string();
}

void parseResults(const WebsearchBackend& b, const std::string& body,
                  std::vector<WebSearchItem>& items, std::vector<char>& needResolve) {
    items.clear();
    needResolve.clear();
    const std::string clean = removeScriptStyle(body);
    size_t pos = 0;
    int guard = 0;
    while (items.size() < 3 && guard < 400) {
        size_t hrefB = 0, hrefE = 0, cB = 0, cE = 0;
        if (!nextAnchor(clean, pos, &hrefB, &hrefE, &cB, &cE)) { break; }
        pos = cE + 4;
        ++guard;
        const std::string href = clean.substr(hrefB, hrefE - hrefB);
        const std::string url = resolveItemUrl(b, href);
        if (url.empty()) { continue; }
        const std::string title = titleFromContent(clean.substr(cB, cE - cB));
        if (title.empty()) { continue; }
        bool dup = false;
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].url == url) { dup = true; break; }
        }
        if (dup) { continue; }
        WebSearchItem it;
        it.title = title;
        it.url = url;
        it.snippet = snippetAfter(clean, cE + 4);
        items.push_back(it);
        needResolve.push_back((b.id == kWebsearchSogou && isShellUrl(href)) ? 1 : 0);
    }
}

// ── 会话管理 ──

struct Session {
    WebsearchRequest req;
    QObject* target = nullptr;
    std::vector<const WebsearchBackend*> cands;
    int index = 0;
    int retries = 0;        // 当前后端已重试次数（0=首试）
    std::string cookies;    // 反爬/consent cookie 回放（非凭据）
    std::string lastError;
    std::vector<std::string> tried;
    int phase = 0;          // 0=搜索页；1=sogou 壳链接 follow 解析
    std::vector<WebSearchItem> resolveItems;
    std::vector<char> resolveNeeded;
    int resolveFired = 0;
    int resolveDone = 0;
    std::string backendUsed;
};

struct ResolveCtx {
    Session* s;
    int itemIndex;
};

QMutex g_mutex;
std::vector<Session*> g_sessions;
bool g_seeded = false;

void ensureSeed() {
    QMutexLocker lock(&g_mutex);
    if (g_seeded) { return; }
    g_seeded = true;
    srand((unsigned int)time(nullptr));
}

void removeSession(Session* s) {
    if (!s) { return; }
    {
        QMutexLocker lock(&g_mutex);
        for (size_t i = 0; i < g_sessions.size(); ++i) {
            if (g_sessions[i] == s) {
                g_sessions.erase(g_sessions.begin() + i);
                break;
            }
        }
    }
    delete s;
}

void postResult(Session* s, bool ok, const std::string& err = std::string()) {
    if (!s) { return; }
    WebsearchDoneEvent* ev = new WebsearchDoneEvent();
    ev->chatId = s->req.chatId;
    ev->chatType = s->req.chatType;
    ev->localId = s->req.localId;
    ev->success = ok;
    if (ok) {
        ev->items = s->resolveItems;
        ev->backendUsed = s->backendUsed;
    } else {
        std::string e = err.empty() ? s->lastError : err;
        if (e.empty()) { e = "所有搜索后端均失败"; }
        ev->errorMsg = e;
    }
    QObject* target = s->target;
    removeSession(s);
    if (target) {
        // QApplication::postEvent(target, ev);
        qAppPostEvent(target, ev);
    } else {
        delete ev;
    }
}

// ── HTTP ──

std::map<std::string, std::string> browserHeaders(const std::string& cookies) {
    std::map<std::string, std::string> h;
    h["User-Agent"] =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36";
    h["Accept"] = "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8";
    h["Accept-Language"] = "zh-CN,zh;q=0.9,en;q=0.4";
    h["Sec-Fetch-Dest"] = "document";
    h["Sec-Fetch-Mode"] = "navigate";
    h["Sec-Fetch-Site"] = "none";
    h["Upgrade-Insecure-Requests"] = "1";
    if (!cookies.empty()) { h["Cookie"] = cookies; }
    return h;
}

std::string buildSearchUrl(const WebsearchBackend& b, const WebsearchLangParam& lp,
                           const std::string& text) {
    const std::string enc = webUrlEncode(text); // DoxAPI::urlEncode(text);
    switch (b.id) {
    case kWebsearchGoogle: {
        return "https://www.google.com/search?gbv=1&q=" + enc
            + "&hl=" + lp.hl + "&gl=" + lp.gl + "&ie=UTF-8";
    }
    case kWebsearchBing: {
        return "https://www.bing.com/search?q=" + enc + "&mkt=" + lp.mkt + "&setlang=" + lp.hl;
    }
    case kWebsearchDuckduckgo: {
        return "https://html.duckduckgo.com/html/?q=" + enc + "&kl=" + lp.kl;
    }
    case kWebsearchYandex: {
        std::string u = "https://yandex.com/search/?text=" + enc;
        if (lp.yalang[0]) {
            u += "&lang=" + std::string(lp.yalang) + "&lr=" + std::string(lp.yalr);
        }
        return u;
    }
    case kWebsearchSogou: {
        return "https://www.sogou.com/web?query=" + enc + "&ie=utf8";
    }
    }
    return std::string();
}

void onDone(const HttpResponse& resp, void* udata);
void onFollowDone(const HttpResponse& resp, void* udata);
void sendSearch(Session* s);
void sogouResolve(Session* s);
void finalizeSuccess(Session* s);
void nextCandidate(Session* s);

void sendSearch(Session* s) {
    if (!s || s->index < 0 || s->index >= (int)s->cands.size()) { return; }
    const WebsearchBackend& b = *s->cands[s->index];
    const WebsearchLangParam* lp = websearchLangParamImpl(s->req.lang);
    if (!lp) { lp = websearchLangParamImpl("zh"); }
    const std::string url = buildSearchUrl(b, *lp, s->req.text);
    if (url.empty()) {
        s->lastError = std::string(b.name) + ": 内部错误（URL 构造失败）";
        nextCandidate(s);
        return;
    }
    HttpRequest rq(url, "GET", "", 40, browserHeaders(s->cookies), 35);
    rq.followRedirects = true;   // 跟随 consent/地区/落地 302
    rq.proxy = netProxyFor(b.needsProxy);
    if (s->retries > 0) { rq.delayMs = (int)(rand() % 800); }
    s->tried.push_back(b.name);
    qWarning("websearch: 候选[%d/%d] name=%s lang=%s retry=%d url=%s",
             s->index, (int)s->cands.size(), b.name, lp->code, s->retries, url.c_str());
    EventPoller::addRequest(rq, onDone, s);
}

// 失败：同后端重试 1 次（随机延迟 0-800ms 绕过限流/抽风），否则切下一候选
void onBackendFail(Session* s, const std::string& err) {
    s->lastError = err;
    qWarning("websearch: 后端=%s 失败（retry=%d）: %s",
             s->cands[s->index]->name, s->retries, err.c_str());
    if (s->retries == 0) {
        s->retries = 1;
        sendSearch(s);
        return;
    }
    nextCandidate(s);
}

void onSearchDone(Session* s, const HttpResponse& resp) {
    if (!s || s->index < 0 || s->index >= (int)s->cands.size()) { return; }
    const WebsearchBackend& b = *s->cands[s->index];
    // 回放服务端下发的 cookie（consent/反爬会话；非凭据）
    const std::map<std::string, std::string>::const_iterator cit = resp.headers.find("set-cookie");
    if (cit != resp.headers.end() && !cit->second.empty()) {
        const std::string& v = cit->second;
        const size_t semi = v.find(';');
        s->cookies = (semi == std::string::npos) ? v : v.substr(0, semi);
        qWarning("websearch: 后端=%s 已回放 cookie=%d B", b.name, (int)s->cookies.size());
    }
    if (!resp.curlErrStr.empty() || resp.httpCode != 200) {
        onBackendFail(s, std::string(b.name) + ": HTTP " + std::to_string(resp.httpCode)
                      + (resp.curlErrStr.empty() ? "" : " " + resp.curlErrStr));
        return;
    }
    if (looksLikeBotPage(removeScriptStyle(resp.body))) {
        onBackendFail(s, std::string(b.name) + ": 命中风控/验证码页（需浏览器人工验证）");
        return;
    }
    std::vector<WebSearchItem> items;
    std::vector<char> needResolve;
    parseResults(b, resp.body, items, needResolve);
    if (items.empty()) {
        onBackendFail(s, std::string(b.name) + ": 未解析到结果");
        return;
    }
    qWarning("websearch: 后端=%s 命中 %d 条结果 body=%d B elapsed=%lld ms",
             b.name, (int)items.size(), (int)resp.body.size(), (long long)resp.elapsedMs);
    s->backendUsed = b.name;
    s->resolveItems = items;
    s->resolveNeeded = needResolve;
    if (b.id == kWebsearchSogou) {
        sogouResolve(s);
        return;
    }
    finalizeSuccess(s);
}

// sogou 壳链接 follow 解析（并行至多 3 条；失败保留壳链接可点击）
void sogouResolve(Session* s) {
    if (!s) { return; }
    s->phase = 1;
    const WebsearchBackend& b = *s->cands[s->index];
    const int n = std::min(3, (int)s->resolveItems.size());
    for (int i = 0; i < n; ++i) {
        if (i >= (int)s->resolveNeeded.size() || !s->resolveNeeded[i]) { continue; }
        HttpRequest rq(s->resolveItems[i].url, "GET", "", 25, browserHeaders(s->cookies), 25);
        rq.followRedirects = true;
        rq.proxy = netProxyFor(b.needsProxy);
        auto* rc = new ResolveCtx();
        rc->s = s;
        rc->itemIndex = i;
        EventPoller::addRequest(rq, onFollowDone, rc);
        ++s->resolveFired;
    }
    if (s->resolveFired == 0) {
        finalizeSuccess(s);
    }
}

void onFollowDone(const HttpResponse& resp, void* udata) {
    auto* rc = static_cast<ResolveCtx*>(udata);
    Session* s = rc->s;
    const int idx = rc->itemIndex;
    delete rc;
    if (!s) { return; }
    if (idx >= 0 && idx < (int)s->resolveItems.size() && !resp.finalUrl.empty()) {
        s->resolveItems[idx].url = resp.finalUrl;
    }
    ++s->resolveDone;
    if (s->resolveDone >= s->resolveFired) {
        finalizeSuccess(s);
    }
}

void finalizeSuccess(Session* s) {
    postResult(s, true);
}

void nextCandidate(Session* s) {
    ++s->index;
    if (s->index < (int)s->cands.size()) {
        s->retries = 0;
        s->cookies.clear();
        s->phase = 0;
        s->resolveFired = 0;
        s->resolveDone = 0;
        sendSearch(s);
        return;
    }
    std::string err = s->lastError.empty() ? std::string("所有搜索后端均失败") : s->lastError;
    std::string tried;
    for (size_t i = 0; i < s->tried.size(); ++i) {
        if (i) { tried += ", "; }
        tried += s->tried[i];
    }
    if (!tried.empty()) { err += "（已尝试：" + tried + "）"; }
    postResult(s, false, err);
}

void onDone(const HttpResponse& resp, void* udata) {
    Session* s = static_cast<Session*>(udata);
    if (!s) { return; }
    if (s->phase == 1) { onFollowDone(resp, udata); return; }
    onSearchDone(s, resp);
}

} // namespace

const std::vector<WebsearchBackend>& websearchBackends() {
    static const std::vector<WebsearchBackend> list(
            kBackends, kBackends + kBackendCount);
    return list;
}

const WebsearchLangParam* websearchLangParam(const std::string& code) {
    return websearchLangParamImpl(code);
}

const char* websearchLangDisplay(const std::string& code) {
    const WebsearchLangParam* lp = websearchLangParamImpl(code);
    return lp ? lp->display : "中文";
}

void websearchStart(const WebsearchRequest& req, QObject* target) {
    ensureSeed();
    auto* s = new Session();
    s->req = req;
    s->target = target;
    if (s->req.lang.empty()) { s->req.lang = "zh"; }
    const std::vector<WebsearchBackend>& list = websearchBackends();
    if (!req.backend.empty() && req.backend != "any") {
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i].name == req.backend) { s->cands.push_back(&list[i]); }
        }
        if (s->cands.empty()) {
            postResult(s, false, "未知搜索后端：" + req.backend);
            return;
        }
    } else {
        for (size_t i = 0; i < list.size(); ++i) { s->cands.push_back(&list[i]); }
        std::random_shuffle(s->cands.begin(), s->cands.end());
    }
    {
        QMutexLocker lock(&g_mutex);
        g_sessions.push_back(s);
    }
    s->index = 0;
    sendSearch(s);
}
