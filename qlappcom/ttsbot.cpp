#include "ttsbot.h"
#include "edgedrm.h"
#include "eventpoller.h"
#include "netproxy.h"
#include "webcreds.h"
#include "compatcore34.h"

// #include <qapplication.h>
#include <qobject.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>
#include <vector>
#include <map>
#include <curl/curl.h>
#include <curl/websockets.h>
#include <unistd.h>
#include <sys/stat.h>

namespace {

const TtsbotProvider kTtsbotProv[] = {
    { kTtsbotEdge,       "edge",       false, false, "" },
    { kTtsbotGoogle,     "google",     false, true,  "https://translate.google.com/translate_tts" },
    { kTtsbotOpenai,     "openai",     true,  true,  "https://api.openai.com/v1/audio/speech" },
    { kTtsbotElevenLabs, "elevenlabs", true,  true,  "https://api.elevenlabs.io/v1/text-to-speech" },
    { kTtsbotDashScope,  "dashscope",  true,  false, "https://dashscope.aliyuncs.com/api/v1/services/aigc/multimodal-generation/generation" },
    { kTtsbotEspeak,     "espeak",     false, false, "" },
};
const int kProvCount = int(sizeof(kTtsbotProv) / sizeof(kTtsbotProv[0]));

// 默认参数
const char* kEdgeDefaultVoice = "zh-CN-XiaoxiaoNeural";
const char* kOpenaiDefaultVoice = "alloy";
const char* kElevenDefaultVoice = "21m00Tcm4TlvDq8ikWAM";  // Rachel
const char* kDashscopeDefaultVoice = "Cherry";

const char* kBrowserUA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                         "(KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36";

struct Session {
    TtsbotRequest req;
    QObject* target = nullptr;
    std::vector<const TtsbotProvider*> cands;
    std::string keyOpenai;
    std::string keyEleven;
    std::string keyDashscope;
};

// ── 小工具 ──
std::string trimStr(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) { return std::string(); }
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string toLowerStr(std::string s) {
    for (size_t i = 0; i < s.size(); i++) {
        s[i] = (char)tolower((unsigned char)s[i]);
    }
    return s;
}

bool startsWith(const std::string& s, const char* pre) {
    size_t n = std::strlen(pre);
    return s.size() >= n && s.compare(0, n, pre) == 0;
}

std::string randomHex(int nbytes) {
    unsigned char raw[64];
    if (nbytes > (int)sizeof(raw)) { nbytes = (int)sizeof(raw); }
    FILE* f = fopen("/dev/urandom", "rb");
    if (f) {
        if (fread(raw, 1, nbytes, f) != (size_t)nbytes) {
            for (int i = 0; i < nbytes; i++) { raw[i] = (unsigned char)(rand() & 0xFF); }
        }
        fclose(f);
    } else {
        for (int i = 0; i < nbytes; i++) { raw[i] = (unsigned char)(rand() & 0xFF); }
    }
    static const char* hx = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < nbytes; i++) {
        out.push_back(hx[(raw[i] >> 4) & 0xF]);
        out.push_back(hx[raw[i] & 0xF]);
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

std::string xmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size(); i++) {
        const unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default:
            if ((c >= 0 && c <= 8) || (c >= 11 && c <= 12) || (c >= 14 && c <= 31)) {
                out += ' ';   // 服务端不兼容的控制字符
            } else {
                out += (char)c;
            }
        }
    }
    return out;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size(); i++) {
        const unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
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

// ── 阻塞式 HTTP ──
struct EasyResult {
    long code = 0;
    std::string body;
    std::string error;
    std::string dateHeader;
};

size_t easyWrite(void* p, size_t sz, size_t nm, void* ud) {
    size_t total = sz * nm;
    static_cast<std::string*>(ud)->append((const char*)p, total);
    return total;
}

size_t easyHeader(char* p, size_t sz, size_t nm, void* ud) {
    size_t total = sz * nm;
    EasyResult* r = static_cast<EasyResult*>(ud);
    std::string line(p, total);
    size_t c = line.find(':');
    if (c != std::string::npos) {
        std::string k = toLowerStr(trimStr(line.substr(0, c)));
        std::string v = trimStr(line.substr(c + 1));
        if (k == "date") { r->dateHeader = v; }
    }
    return total;
}

void easyPerform(const std::string& url, bool post, const std::string& data,
                 const std::vector<std::string>& headers, const std::string& proxy,
                 int timeoutSec, EasyResult& out) {
    CURL* h = curl_easy_init();
    if (!h) { out.error = "curl init failed"; return; }
    struct curl_slist* sl = nullptr;
    for (size_t i = 0; i < headers.size(); i++) { sl = curl_slist_append(sl, headers[i].c_str()); }
    curl_easy_setopt(h, CURLOPT_URL, url.c_str());
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, easyWrite);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &out.body);
    curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, easyHeader);
    curl_easy_setopt(h, CURLOPT_HEADERDATA, &out);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, (long)timeoutSec);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(h, CURLOPT_USERAGENT, kBrowserUA);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    if (sl) { curl_easy_setopt(h, CURLOPT_HTTPHEADER, sl); }
    if (!proxy.empty()) { curl_easy_setopt(h, CURLOPT_PROXY, proxy.c_str()); }
    if (post) {
        curl_easy_setopt(h, CURLOPT_POST, 1L);
        curl_easy_setopt(h, CURLOPT_POSTFIELDS, data.data());
        curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)data.size());
    }
    CURLcode rc = curl_easy_perform(h);
    if (rc != CURLE_OK) {
        out.error = curl_easy_strerror(rc);
    } else {
        curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &out.code);
    }
    if (sl) { curl_slist_free_all(sl); }
    curl_easy_cleanup(h);
}

// ── 音色选择默认值 ──
std::string edgeVoice(const TtsbotRequest& r) {
    if (!r.voice.empty()) { return r.voice; }
    return kEdgeDefaultVoice;
}

// ── Google TTS ──
bool synthGoogle(const TtsbotRequest& req, const std::string& proxy,
                 std::vector<uint8_t>& audio, std::string& mime, std::string& err) {
    std::string text = req.text;
    if (text.size() > 190) { text = text.substr(0, 190); }   // 免费端点限制
    std::string tl = req.lang.empty() ? std::string("zh-CN") : req.lang;
    std::string url = "https://translate.google.com/translate_tts?ie=UTF-8&client=tw-ob&tl="
                    + urlEncode(tl) + "&q=" + urlEncode(text);
    EasyResult res;
    std::vector<std::string> hdrs;
    hdrs.push_back("Referer: https://translate.google.com/");
    easyPerform(url, false, "", hdrs, proxy, 30, res);
    if (res.code != 200 || res.body.empty()) {
        err = "google HTTP " + std::to_string(res.code) + (res.error.empty() ? "" : " " + res.error);
        return false;
    }
    audio.assign(res.body.begin(), res.body.end());
    mime = "audio/mpeg";
    return true;
}

// ── OpenAI TTS ──
bool synthOpenai(const std::string& key, const TtsbotRequest& req, const std::string& proxy,
                 std::vector<uint8_t>& audio, std::string& mime, std::string& err) {
    std::string voice = req.voice.empty() ? std::string(kOpenaiDefaultVoice) : req.voice;
    std::string body = std::string("{\"model\":\"tts-1\",\"input\":\"")
                     + jsonEscape(req.text) + "\",\"voice\":\"" + jsonEscape(voice)
                     + "\",\"response_format\":\"mp3\"}";
    std::vector<std::string> hdrs;
    hdrs.push_back("Authorization: Bearer " + key);
    hdrs.push_back("Content-Type: application/json");
    EasyResult res;
    easyPerform("https://api.openai.com/v1/audio/speech", true, body, hdrs, proxy, 60, res);
    if (res.code != 200 || res.body.empty()) {
        err = "openai HTTP " + std::to_string(res.code) + (res.error.empty() ? "" : " " + res.error);
        return false;
    }
    audio.assign(res.body.begin(), res.body.end());
    mime = "audio/mpeg";
    return true;
}

// ── ElevenLabs TTS ──
bool synthEleven(const std::string& key, const TtsbotRequest& req, const std::string& proxy,
                 std::vector<uint8_t>& audio, std::string& mime, std::string& err) {
    std::string voice = req.voice.empty() ? std::string(kElevenDefaultVoice) : req.voice;
    std::string url = std::string("https://api.elevenlabs.io/v1/text-to-speech/") + voice;
    std::string body = std::string("{\"text\":\"") + jsonEscape(req.text)
                     + "\",\"model_id\":\"eleven_multilingual_v2\"}";
    std::vector<std::string> hdrs;
    hdrs.push_back("xi-api-key: " + key);
    hdrs.push_back("Content-Type: application/json");
    hdrs.push_back("Accept: audio/mpeg");
    EasyResult res;
    easyPerform(url, true, body, hdrs, proxy, 60, res);
    if (res.code != 200 || res.body.empty()) {
        err = "elevenlabs HTTP " + std::to_string(res.code) + (res.error.empty() ? "" : " " + res.error);
        return false;
    }
    audio.assign(res.body.begin(), res.body.end());
    mime = "audio/mpeg";
    return true;
}

// 从 JSON 中粗暴提取 "url":"..." 的值（避免为 dashscope 引入解析依赖）
bool extractJsonUrl(const std::string& body, std::string& out) {
    const std::string key = "\"url\"";
    size_t p = body.find(key);
    if (p == std::string::npos) { return false; }
    p = body.find(':', p + key.size());
    if (p == std::string::npos) { return false; }
    p++;
    while (p < body.size() && (body[p] == ' ' || body[p] == '\t')) { p++; }
    if (p >= body.size() || body[p] != '"') { return false; }
    p++;
    std::string v;
    while (p < body.size() && body[p] != '"') {
        if (body[p] == '\\' && p + 1 < body.size()) { p++; }
        v += body[p++];
    }
    out = v;
    return !out.empty();
}

// ── DashScope (qwen-tts) 返回远程音频 URL ──
bool synthDashscope(const std::string& key, const TtsbotRequest& req, const std::string& proxy,
                    std::string& audioUrl, std::string& mime, std::string& err) {
    std::string voice = req.voice.empty() ? std::string(kDashscopeDefaultVoice) : req.voice;
    std::string body = std::string("{\"model\":\"qwen-tts\",\"input\":{\"text\":\"")
                     + jsonEscape(req.text) + "\",\"voice\":\"" + jsonEscape(voice) + "\"}}";
    std::vector<std::string> hdrs;
    hdrs.push_back("Authorization: Bearer " + key);
    hdrs.push_back("Content-Type: application/json");
    EasyResult res;
    easyPerform("https://dashscope.aliyuncs.com/api/v1/services/aigc/multimodal-generation/generation",
                true, body, hdrs, proxy, 60, res);
    if (res.code != 200 || res.body.empty()) {
        err = "dashscope HTTP " + std::to_string(res.code) + (res.error.empty() ? "" : " " + res.error);
        return false;
    }
    if (!extractJsonUrl(res.body, audioUrl)) {
        err = "dashscope 响应无音频 URL";
        return false;
    }
    mime = "audio/wav";
    return true;
}

// ── espeak 本地合成（写 WAV 临时文件） ──
bool synthEspeak(const TtsbotRequest& req, std::vector<uint8_t>& audio,
                 std::string& mime, std::string& err) {
    const char* tmpdir = std::getenv("TMPDIR");
    std::string dir = (tmpdir && *tmpdir) ? std::string(tmpdir) : std::string("/tmp");
    char nameBuf[64];
    std::snprintf(nameBuf, sizeof(nameBuf), "/qltox_tts_%d_%s.wav",
                  (int)getpid(), randomHex(4).c_str());
    std::string path = dir + nameBuf;
    std::string lang = req.lang;
    std::string voice = req.voice;
    if (voice.empty()) { voice = (lang.compare(0, 2, "zh") == 0) ? "cmn" : "en"; }
    QStringList args;
    args << "-v" << qFromUtf8(voice) << "-w" << qFromUtf8(path) << qFromUtf8(req.text);
    QString errOut;
    int rc = qRuncmdCaptureOuterr("espeak-ng", args, &errOut);
    if (rc != 0) { rc = qRuncmdCaptureOuterr("espeak", args, &errOut); }
    if (rc != 0) {
        err = "espeak 运行失败: " + std::string(qToUtf8(errOut).data());
        ::unlink(path.c_str());
        return false;
    }
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { err = "espeak 输出文件打开失败"; return false; }
    char buf[8192];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        audio.insert(audio.end(), (uint8_t*)buf, (uint8_t*)buf + n);
    }
    fclose(f);
    ::unlink(path.c_str());
    if (audio.empty()) { err = "espeak 无输出"; return false; }
    mime = "audio/wav";
    return true;
}

// ── Edge WSS ──
struct WsOut {
    bool ok = false;
    std::string error;
    std::vector<uint8_t> audio;
    std::string dateHeader;
    bool got403 = false;
};

bool wsWait(CURL* h, bool wantRead, int timeoutMs) {
    curl_socket_t s = CURL_SOCKET_BAD;
    if (curl_easy_getinfo(h, CURLINFO_ACTIVESOCKET, &s) != CURLE_OK) { return false; }
    if (s == CURL_SOCKET_BAD) { return false; }
    fd_set fd;
    FD_ZERO(&fd);
    FD_SET(s, &fd);
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    int r;
    if (wantRead) { r = select(s + 1, &fd, NULL, NULL, &tv); }
    else { r = select(s + 1, NULL, &fd, NULL, &tv); }
    return r > 0;
}

bool wsSendAll(CURL* h, const std::string& data, unsigned flags) {
    size_t off = 0;
    while (off < data.size()) {
        size_t sent = 0;
        CURLcode rc = curl_ws_send(h, data.data() + off, data.size() - off, &sent, 0, flags);
        if (rc == CURLE_AGAIN) {
            if (!wsWait(h, false, 3000)) { return false; }
            continue;
        }
        if (rc != CURLE_OK) { return false; }
        off += sent;
    }
    return true;
}

size_t wsHeaderCb(char* p, size_t sz, size_t nm, void* ud) {
    size_t total = sz * nm;
    WsOut* r = static_cast<WsOut*>(ud);
    std::string line(p, total);
    size_t c = line.find(':');
    if (c != std::string::npos) {
        std::string k = toLowerStr(trimStr(line.substr(0, c)));
        std::string v = trimStr(line.substr(c + 1));
        if (k == "date") { r->dateHeader = v; }
    }
    if (startsWith(trimStr(line), "HTTP/")) {
        size_t sp = line.find(' ');
        if (sp != std::string::npos) {
            long code = atol(line.c_str() + sp + 1);
            if (code == 403) { r->got403 = true; }
        }
    }
    return total;
}

// 处理一个二进制帧：2 字节头长 + 头 + 音频
void handleEdgeBinary(const std::string& frame, std::vector<uint8_t>& audio) {
    if (frame.size() < 2) { return; }
    size_t hlen = ((unsigned char)frame[0] << 8) | (unsigned char)frame[1];
    if (hlen + 2 > frame.size()) { return; }
    std::string hdrs = frame.substr(2, hlen);
    if (hdrs.find("Path:audio") == std::string::npos) { return; }
    const char* p = frame.data() + 2 + hlen;
    size_t n = frame.size() - 2 - hlen;
    audio.insert(audio.end(), (const uint8_t*)p, (const uint8_t*)p + n);
}

bool synthEdgeOnce(const TtsbotRequest& req, const std::string& proxy,
                   const char* host, bool legacy, int tokVer, WsOut& out) {
    std::string connId = randomHex(16);
    std::string token = (tokVer == 2) ? edgedrm::secMsGecV2(edgedrm::nowUnix())
                                      : edgedrm::secMsGecV1(edgedrm::nowUnix());
    std::string ver = (tokVer == 2) ? edgedrm::versionV2() : edgedrm::versionV1();
    if (token.empty()) { out.error = "edge token 生成失败"; return false; }

    std::string url;
    if (legacy) {
        url = std::string("wss://") + host
            + "/consumer/speech/synthesize/readaloud/edge/v1?TrustedClientToken="
            + edgedrm::kTrustedClientToken + "&ConnectionId=" + connId
            + "&Sec-MS-GEC=" + token + "&Sec-MS-GEC-Version=" + ver;
    } else {
        url = std::string("wss://") + host
            + "/tts/cognitiveservices/websocket/v1?Ocp-Apim-Subscription-Key="
            + edgedrm::kTrustedClientToken + "&ConnectionId=" + connId
            + "&Sec-MS-GEC=" + token + "&Sec-MS-GEC-Version=" + ver;
    }

    CURL* h = curl_easy_init();
    if (!h) { out.error = "curl init failed"; return false; }

    std::vector<std::string> hdrs;
    hdrs.push_back("Pragma: no-cache");
    hdrs.push_back("Cache-Control: no-cache");
    hdrs.push_back("Origin: chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold");
    hdrs.push_back("Accept-Language: en-US,en;q=0.9");
    hdrs.push_back(std::string("User-Agent: ") + kBrowserUA + " Edg/143.0.0.0");
    hdrs.push_back("Cookie: muid=" + edgedrm::generateMuid() + ";");

    struct curl_slist* sl = nullptr;
    for (size_t i = 0; i < hdrs.size(); i++) { sl = curl_slist_append(sl, hdrs[i].c_str()); }

    curl_easy_setopt(h, CURLOPT_URL, url.c_str());
    curl_easy_setopt(h, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, sl);
    curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, wsHeaderCb);
    curl_easy_setopt(h, CURLOPT_HEADERDATA, &out);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 40L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    if (!proxy.empty()) { curl_easy_setopt(h, CURLOPT_PROXY, proxy.c_str()); }

    CURLcode rc = curl_easy_perform(h);
    if (rc != CURLE_OK) {
        out.error = std::string("edge 握手失败: ") + curl_easy_strerror(rc);
        if (sl) { curl_slist_free_all(sl); }
        curl_easy_cleanup(h);
        return false;
    }

    // speech.config
    std::string cfg = std::string("X-Timestamp:") + edgedrm::dateToString()
        + "\r\nContent-Type:application/json; charset=utf-8\r\nPath:speech.config\r\n\r\n"
        + "{\"context\":{\"synthesis\":{\"audio\":{\"metadataoptions\":{"
          "\"sentenceBoundaryEnabled\":\"false\",\"wordBoundaryEnabled\":\"true\"},"
          "\"outputFormat\":\"audio-24khz-48kbitrate-mono-mp3\"}}}}\r\n";
    // SSML
    std::string voice = edgeVoice(req);
    std::string rate = req.rate.empty() ? std::string("+0%") : req.rate;
    std::string ssml = std::string("<speak version='1.0' xmlns='http://www.w3.org/2001/10/synthesis' xml:lang='en-US'>")
        + "<voice name='" + xmlEscape(voice) + "'>"
        + "<prosody pitch='+0Hz' rate='" + xmlEscape(rate) + "' volume='+0%'>"
        + xmlEscape(req.text) + "</prosody></voice></speak>";
    std::string ssmlFrame = std::string("X-RequestId:") + randomHex(16)
        + "\r\nContent-Type:application/ssml+xml\r\nX-Timestamp:"
        + edgedrm::dateToString() + "Z\r\nPath:ssml\r\n\r\n" + ssml;

    bool sent = wsSendAll(h, cfg, CURLWS_TEXT) && wsSendAll(h, ssmlFrame, CURLWS_TEXT);
    if (!sent) {
        out.error = "edge 发送失败";
        if (sl) { curl_slist_free_all(sl); }
        curl_easy_cleanup(h);
        return false;
    }

    bool done = false;
    bool pending = false;
    int isBinary = 0;
    std::string frame;
    time_t deadline = time(NULL) + 40;
    while (!done) {
        if (time(NULL) > deadline) { out.error = "edge 接收超时"; break; }
        char buf[65536];
        size_t nread = 0;
        const struct curl_ws_frame* meta = NULL;
        CURLcode r = curl_ws_recv(h, buf, sizeof(buf), &nread, &meta);
        if (r == CURLE_AGAIN) {
            if (!wsWait(h, true, 20000)) { out.error = "edge 接收等待超时"; break; }
            continue;
        }
        if (r != CURLE_OK) { out.error = std::string("edge 接收失败: ") + curl_easy_strerror(r); break; }
        if (meta && (meta->flags & CURLWS_CLOSE)) { done = true; break; }
        if (!pending) {
            isBinary = (meta && (meta->flags & CURLWS_BINARY)) ? 1 : 0;
            frame.clear();
            pending = true;
        }
        frame.append(buf, nread);
        if (!meta || meta->bytesleft == 0) {
            if (isBinary) {
                handleEdgeBinary(frame, out.audio);
            } else {
                if (frame.find("Path:turn.end") != std::string::npos) { done = true; }
            }
            pending = false;
        }
    }

    if (sl) { curl_slist_free_all(sl); }
    curl_easy_cleanup(h);

    if (!done && out.error.empty()) { out.error = "edge 未收到 turn.end"; }
    if (!out.error.empty()) { return false; }
    if (out.audio.empty()) { out.error = "edge 无音频数据"; return false; }
    out.ok = true;
    return true;
}

bool synthEdge(const TtsbotRequest& req, const std::string& proxy, WsOut& out) {
    struct Cand { const char* host; bool legacy; int ver; };
    const Cand cands[] = {
        { edgedrm::kWssHostLegacy, true,  2 },
        { edgedrm::kWssHostNew,    false, 2 },
        { edgedrm::kWssHostNew,    false, 1 },
        { edgedrm::kWssHostLegacy, true,  1 },
    };
    std::string lastErr;
    for (int i = 0; i < int(sizeof(cands) / sizeof(cands[0])); i++) {
        out = WsOut();
        if (synthEdgeOnce(req, proxy, cands[i].host, cands[i].legacy, cands[i].ver, out)) {
            return true;
        }
        lastErr = out.error;
        // 403：用响应 Date 做一次时钟校正后重试同候选
        if (out.got403 && !out.dateHeader.empty()) {
            double serverDate = 0;
            if (edgedrm::parseRfc2616Date(out.dateHeader, serverDate)) {
                double skew = serverDate - (double)time(NULL);
                if (skew > 3600) { skew = 3600; }
                if (skew < -3600) { skew = -3600; }
                edgedrm::setClockSkew(skew);
                WsOut retry;
                if (synthEdgeOnce(req, proxy, cands[i].host, cands[i].legacy, cands[i].ver, retry)) {
                    out = retry;
                    return true;
                }
                lastErr = retry.error;
            }
        }
    }
    out.error = lastErr.empty() ? "edge 全部候选失败" : lastErr;
    return false;
}

// ── 单 provider 合成 ──
// 返回 true=成功（audio 或 audioUrl 已填）；false=失败（err 填原因）
bool synthProvider(const TtsbotProvider& p, const Session& s,
                   std::vector<uint8_t>& audio, std::string& audioUrl,
                   std::string& mime, std::string& err) {
    std::string proxy = netProxyFor(p.needsProxy);
    const TtsbotRequest& req = s.req;
    switch (p.id) {
    case kTtsbotEdge: {
        WsOut w;
        if (synthEdge(req, proxy, w)) {
            audio = w.audio;
            mime = "audio/mpeg";
            return true;
        }
        err = w.error.empty() ? "edge 失败" : w.error;
        return false;
    }
    case kTtsbotGoogle:
        return synthGoogle(req, proxy, audio, mime, err);
    case kTtsbotOpenai:
        return synthOpenai(s.keyOpenai, req, proxy, audio, mime, err);
    case kTtsbotElevenLabs:
        return synthEleven(s.keyEleven, req, proxy, audio, mime, err);
    case kTtsbotDashScope:
        return synthDashscope(s.keyDashscope, req, proxy, audioUrl, mime, err);
    case kTtsbotEspeak:
        return synthEspeak(req, audio, mime, err);
    }
    err = "未知 provider";
    return false;
}

void postResult(const Session& s, bool ok, const std::string& providerUsed,
                const std::string& err, const std::string& voice, const std::string& mime,
                const std::string& audioUrl, std::vector<uint8_t> audio) {
    TtsbotDoneEvent* ev = new TtsbotDoneEvent();
    ev->chatId = s.req.chatId;
    ev->chatType = s.req.chatType;
    ev->localId = s.req.localId;
    ev->success = ok;
    ev->providerUsed = providerUsed;
    ev->errorMsg = err;
    ev->voice = voice;
    ev->text = s.req.text;
    ev->mime = mime;
    ev->audioUrl = audioUrl;
    ev->audioData.swap(audio);
    if (s.target) { qAppPostEvent(s.target, ev); }
    else { delete ev; }
}

void runSynth(Session s) {
    std::string voiceOut = s.req.voice;
    std::string tried;
    for (size_t i = 0; i < s.cands.size(); i++) {
        const TtsbotProvider& p = *s.cands[i];
        std::vector<uint8_t> audio;
        std::string audioUrl;
        std::string mime;
        std::string err;
        if (synthProvider(p, s, audio, audioUrl, mime, err)) {
            qWarning("ttsbot: provider=%s 成功 bytes=%d", p.name, (int)audio.size());
            postResult(s, true, p.name, std::string(), voiceOut, mime, audioUrl, audio);
            return;
        }
        if (!tried.empty()) { tried += "; "; }
        tried += std::string(p.name) + "=" + (err.empty() ? "失败" : err);
    }
    qWarning("ttsbot: 全部失败: %s", tried.c_str());
    postResult(s, false, std::string(), tried, voiceOut, std::string(), std::string(),
               std::vector<uint8_t>());
}

std::string credValue(const char* field) {
    WebCredResult r = webCredGet(field, "");
    return r.ok ? r.value : std::string();
}

} // anon

const std::vector<TtsbotProvider>& ttsbotProviders() {
    static std::vector<TtsbotProvider> v(kTtsbotProv, kTtsbotProv + kProvCount);
    return v;
}

void ttsbotSynthStart(const TtsbotRequest& req, QObject* target) {
    std::string text = trimStr(req.text);
    if (text.empty()) { return; }

    Session s;
    s.req = req;
    s.req.text = text;
    s.target = target;

    // 凭据在 GUI 线程预读
    s.keyOpenai = credValue("openai_api");
    s.keyEleven = credValue("elevenlabs_api");
    s.keyDashscope = credValue("dashscope_api");

    // 候选构建
    std::string sel = toLowerStr(trimStr(req.provider));
    for (int i = 0; i < kProvCount; i++) {
        const TtsbotProvider& p = kTtsbotProv[i];
        if (!sel.empty() && sel != "any" && sel != "all" && sel != p.name) { continue; }
        if (p.needsKey) {
            std::string k;
            if (p.id == kTtsbotOpenai) { k = s.keyOpenai; }
            else if (p.id == kTtsbotElevenLabs) { k = s.keyEleven; }
            else if (p.id == kTtsbotDashScope) { k = s.keyDashscope; }
            if (k.empty()) { continue; }   // 无 key 跳过
        }
        s.cands.push_back(&p);
    }
    if (s.cands.empty()) {
        s.cands.push_back(&kTtsbotProv[kProvCount - 1]);   // 兜底本地 espeak
    }

    std::thread worker([s]() { runSynth(s); });
    worker.detach();
}
