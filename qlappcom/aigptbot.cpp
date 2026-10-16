#include "aigptbot.h"
#include "compatcore34.h"
#include "eventpoller.h"
#include "netproxy.h"
#include "webcreds.h"
#include "aigpt_sessions.h"
#include "cJSON.h"
#include "obsd_sha2.h"
#include "dspow_solve.h"
#include "stdcpputil.hpp"

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
#include <string>
#include <vector>
#include <map>

namespace {

// ── web provider 凭据（用户手工填写；提取方法见 docs/aigptbot-web-sources.md §3）──
// 空串 = 未配置：any/all 跳过该候选，点名单选直接报“未配置凭据”。
// 填值后重编译（buildqt3.sh → buildqt4.sh，勿并行）。勿提交到公开仓库。
const char* const kDeepseekWebCred = "";
const char* const kGeminiWebCred   = "";
const char* const kGrokWebCred     = "";
const char* const kChatgptWebCred  = "";
const char* const kMetaWebCred     = "";

// ── 浏览器指纹对齐（可选；空 = 各 provider 内置默认 UA）──
// 从 cookie/token 来源浏览器 Copy as cURL 抓实际 UA 填入 kWebCredUA 可覆盖全部
// provider（含 deepseek，覆盖后需自行承担 AWS WAF 202 风险）。grok 的 Cloudflare
// cf_clearance/__cf_bm 绑定 UA+IP，UA 必须与 Cookie 源浏览器一致。
const char* const kWebCredUA         = "";   // 空：deepseek=App UA，Web 端=Chrome/126
const char* const kWebSecChUa        = "";   // 空：按 Chrome/126 内置
const char* const kWebSecChPlatform  = "";   // 空："Windows"
const char* const kWebAcceptLanguage = "";   // 空："zh-CN,zh;q=0.9,en;q=0.4"
// deepseek 可选：App 持久 X-Device-Id 与 AWS WAF 附加 cookie（见 docs §3.1/§5）
const char* const kDeepseekDeviceIdCred = ""; // 空：进程内随机 uuid
const char* const kDeepseekWebCookie    = ""; // 空：不带 Cookie 头
// grok x-statsig-id 挑战常量（浏览器控制台提取，见 docs §6.2；三项齐备才启用）
// 2026-06 起空 statsig 串会被 xAI 403（code:7 anti-bot rules）
const char* const kGrokChallengeHeaderHex = "";  // 49 字节 hex
const char* const kGrokChallengeSuffix    = "";
const char* const kGrokChallengeTrailer   = "3"; // 默认 3，随 grok 发版可换

const char* const kDefaultChromeUA =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36";
const char* const kDeepseekAppUA = "DeepSeek/2.5.0 Android/35";

// 统一 UA：全局 kWebCredUA 非空 → 覆盖；否则 deepseek=App UA，其余=Chrome
const char* webUA(bool deepseek) {
    if (kWebCredUA[0]) { return kWebCredUA; }
    return deepseek ? kDeepseekAppUA : kDefaultChromeUA;
}

const char* webSecChUA() {
    return kWebSecChUa[0] ? kWebSecChUa
        : "\"Chromium\";v=\"126\", \"Google Chrome\";v=\"126\", \"Not.A/Brand\";v=\"8\"";
}

const char* webSecChPlatform() {
    return kWebSecChPlatform[0] ? kWebSecChPlatform : "\"Windows\"";
}

const char* webAcceptLanguage() {
    return kWebAcceptLanguage[0] ? kWebAcceptLanguage : "zh-CN,zh;q=0.9,en;q=0.4";
}

// Web 端（gemini/grok/chatgpt）补 sec-ch-* / Accept-Language；deepseek 不调用
void webBrowserHeaders(std::map<std::string, std::string>& h) {
    h["sec-ch-ua"] = webSecChUA();
    h["sec-ch-ua-mobile"] = "?0";
    h["sec-ch-ua-platform"] = webSecChPlatform();
    h["Accept-Language"] = webAcceptLanguage();
}

// ── provider 静态表（顺序 = provider="all" 的固定尝试序）──
// 名称字段 name 原样拷贝自 imageaiutil.cpp（startOpenAiVision 第一个参数）
const AigptbotProvider kAigptProviders[] = {
    { kAigptbotPollinations,  "pollinations",    true,  false,   // 现已要求 key，any/all 跳过
      "https://gen.pollinations.ai/v1/chat/completions", "openai", kAigptbotWebNone, true },
    { kAigptbotPollinationsText,"pollinations(匿名)", false, true,
      "https://text.pollinations.ai/openai/chat/completions", "openai", kAigptbotWebNone, true },
    { kAigptbotZhipu,         "智谱",            true,  false,
      "https://open.bigmodel.cn/api/paas/v4/chat/completions", "", kAigptbotWebNone, false },
    { kAigptbotSiliconFlow,   "硅基流动",        true,  false,
      "https://api.siliconflow.cn/v1/chat/completions", "", kAigptbotWebNone, false },
    { kAigptbotNvidia,        "NVIDIA NIM",      true,  false,
      "https://integrate.api.nvidia.com/v1/chat/completions", "", kAigptbotWebNone, true },
    { kAigptbotOpenRouter,    "OpenRouter",      true,  false,
      "https://openrouter.ai/api/v1/chat/completions", "", kAigptbotWebNone, true },
    { kAigptbotBlockRun,      "BlockRun",        false, true,
      "https://blockrun.ai/api/v1/chat/completions", "nvidia/mistral-nemotron", kAigptbotWebNone, true },
    { kAigptbotLlm7,          "LLM7",            true,  false,
      "https://api.llm7.io/v1/chat/completions", "", kAigptbotWebNone, true },
    { kAigptbotCloudflare,    "Cloudflare",      true,  false,
      "https://api.cloudflare.com/client/v4/accounts/%1/ai/v1/chat/completions", "", kAigptbotWebNone, true },
    { kAigptbotDashScope,     "百炼",            true,  false,
      "https://dashscope.aliyun.com/compatible-mode/v1/chat/completions", "", kAigptbotWebNone, false },
    { kAigptbotOvh,           "OVH",             true, true,
      "https://oai.endpoints.kepler.ai.cloud.ovh.net/v1/chat/completions", "Qwen3.6-27B", kAigptbotWebNone, true },
    { kAigptbotVolcengine,    "豆包",            true,  false,
      "https://ark.cn-beijing.volces.com/api/v3/chat/completions", "", kAigptbotWebNone, false },
    { kAigptbotModelScope,    "ModelScope(国内)", true, false,
      "https://api-inference.modelscope.cn/v1/chat/completions", "", kAigptbotWebNone, false },
    { kAigptbotModelScopeIntl,"ModelScope(国际)", true, false,
      "https://api-inference.modelscope.ai/v1/chat/completions", "", kAigptbotWebNone, true },
    { kAigptbotGroq,          "Groq",            true,  false,
      "https://api.groq.com/openai/v1/chat/completions", "openai/gpt-oss-120b", kAigptbotWebNone, true },
    { kAigptbotHuggingFace,   "HuggingFace",     true,  false,
      "https://router.huggingface.co/v1/chat/completions", "", kAigptbotWebNone, true },
    { kAigptbotGemini,        "Gemini",          true,  false,
      "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions", "", kAigptbotWebNone, true },
    { kAigptbotOllama,        "Ollama 本地",     false, true,
      "http://localhost:11434/v1/chat/completions", "qwen2.5", kAigptbotWebNone, false },
    { kAigptbotZai,           "Z.ai(国际)",      true,  false,
      "https://api.z.ai/api/paas/v4/chat/completions", "", kAigptbotWebNone, true },
    { kAigptbotGroqViaCf,     "groq-viacf",      true,  false,
      "https://gateway.ai.cloudflare.com/v1/%1/%2/groq/chat/completions", "", kAigptbotWebNone, true },
    { kAigptbotGeminiViaCf,   "gemini-viacf",    true,  false,
      "https://gateway.ai.cloudflare.com/v1/%1/%2/compat/chat/completions", "", kAigptbotWebNone, true },
    { kAigptbotAiHorde,       "aihorde",         true,  false,
      "", "", kAigptbotWebNone, true }, // 不走 OpenAI 兼容
    { kAigptbotMetaApi,       "meta",            true,  false,
      "https://api.meta.ai/v1/chat/completions", "muse-spark-1.3", kAigptbotWebNone, true },
    // ── web 版直连（不走 OpenAI 协议；webKind 分派 sendWebHost）──
    { kAigptbotDeepseekWeb,   "deepseek-web",    false, true,
      "https://chat.deepseek.com", "", kAigptbotWebDeepseek, false },
    { kAigptbotGeminiWeb,     "gemini-web",      false, true,
      "https://gemini.google.com", "", kAigptbotWebGemini, true },
    { kAigptbotGrokWeb,       "grok-web",        false, true,
      "https://grok.com", "", kAigptbotWebGrok, true },
    { kAigptbotChatgptWeb,    "chatgpt-web",     false, true,
      "https://chatgpt.com", "", kAigptbotWebChatgpt, true },
    { kAigptbotMetaWeb,       "meta-web",        false, true,
      "https://www.meta.ai", "", kAigptbotWebMeta, true },
};
const int kAigptProviderCount = int(sizeof(kAigptProviders) / sizeof(kAigptProviders[0]));

// web provider 对应凭据结果（默认读 webcreds 侧车，源码常量为默认定底）
WebCredResult webCredResult(const AigptbotProvider& p) {
    switch (p.webKind) {
    case kAigptbotWebDeepseek: { return webCredGet("deepseek_web", kDeepseekWebCred); }
    case kAigptbotWebGemini:   { return webCredGet("gemini_web", kGeminiWebCred); }
    case kAigptbotWebGrok:     { return webCredGet("groq_web", kGrokWebCred); }
    case kAigptbotWebChatgpt:  { return webCredGet("chatgpt_web", kChatgptWebCred); }
    case kAigptbotWebMeta:     { return webCredGet("meta_web", kMetaWebCred); }
    case kAigptbotWebNone:     { break; }
    }
    return WebCredResult();
}

// 日志统一状态前缀 cred=<none|builtin|plain|token|pass|broken>
const char* webCredTag(WebCredStatus s) {
    switch (s) {
    case kWebCredNone:     { return "none"; }
    case kWebCredConstant: { return "builtin"; }
    case kWebCredPlain:    { return "plain"; }
    case kWebCredTokenEnc: { return "token"; }
    case kWebCredPassEnc:  { return "pass"; }
    case kWebCredBroken:   { return "broken"; }
    }
    return "none";
}

// non-web（OpenAI 兼容）provider 的 API key 侧车字段名：<slug>_api
const char* nonWebCredField(const AigptbotProvider& p) {
    switch (p.id) {
    case kAigptbotPollinations:     { return "pollinations_api"; }
    case kAigptbotPollinationsText: { return ""; } // 匿名（allowEmptyKey）
    case kAigptbotZhipu:            { return "zhipu_api"; }
    case kAigptbotSiliconFlow:      { return "siliconflow_api"; }
    case kAigptbotNvidia:           { return "nvidia_api"; }
    case kAigptbotOpenRouter:       { return "openrouter_api"; }
    case kAigptbotBlockRun:         { return ""; } // 匿名（allowEmptyKey）
    case kAigptbotLlm7:             { return "llm7_api"; }
    case kAigptbotCloudflare:       { return "cloudflare_api"; }
    case kAigptbotDashScope:        { return "dashscope_api"; }
    case kAigptbotOvh:              { return ""; } // 匿名（allowEmptyKey）
    case kAigptbotVolcengine:       { return "volcengine_api"; }
    case kAigptbotModelScope:       { return "modelscope_api"; }
    case kAigptbotModelScopeIntl:   { return "modelscope_intl_api"; }
    case kAigptbotGroq:             { return "groq_api"; }
    case kAigptbotHuggingFace:      { return "huggingface_api"; }
    case kAigptbotGemini:           { return "gemini_api"; }
    case kAigptbotOllama:           { return ""; } // 本地，无 key
    case kAigptbotZai:              { return "zai_api"; }
    case kAigptbotGroqViaCf:        { return "groq_viacf_api"; }
    case kAigptbotGeminiViaCf:      { return "gemini_viacf_api"; }
    case kAigptbotAiHorde:          { return "aihorde_api"; }
    case kAigptbotMetaApi:          { return "meta_api"; }
    default:                        { return ""; }
    }
}

// non-web provider 的 key 是否已配置（读侧车 <slug>_api）
bool nonWebKeyReady(const AigptbotProvider& p) {
    const char* f = nonWebCredField(p);
    if (!f || !f[0]) { return false; }
    const WebCredResult r = webCredGet(f, "");
    return r.ok && !r.value.empty();
}

struct Session {
    AigptbotRequest req;
    QObject* target = nullptr;
    std::vector<const AigptbotProvider*> cands;
    int index = 0;
    unsigned long long seq = 0;
    std::string lastError;
    std::vector<std::string> tried;
    // web 分步请求状态：step = 当前步骤序号，wv = 步骤间中间值
    // （deepseek: hif/pow/session；gemini: at/reqid；grok: conversationId；chatgpt: prepare_token；meta: lsd/datr/token/conv）
    int step = 0;
    std::map<std::string, std::string> wv;
    std::string sessKey;   // 会话缓存键（web provider 用）
};

// 会话缓存键：provider 名 + 凭据指纹（凭据变化则不复用旧会话）
std::string webSessionKey(const AigptbotProvider& p) {
    const WebCredResult cr = webCredResult(p);
    return aigptSessionKey(p.name, cr.value);
}

QMutex g_mutex;
std::vector<Session*> g_sessions;
unsigned long long g_seq = 0;
bool g_seeded = false;

std::string trimStr(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) { return std::string(); }
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool strEqual(const std::string& a, const std::string& b) {
    return a == b;
}

// 日志截断：取首行、上限 64 字符（同 fanyibot::briefOf）
std::string briefBody(const std::string& body) {
    std::string b = body;
    const std::string::size_type nl = b.find('\n');
    if (nl != std::string::npos) { b = b.substr(0, nl); }
    if (b.size() > 64) { b = b.substr(0, 64); }
    return b;
}

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

bool isCurrentSeq(const Session* s) {
    if (!s) { return false; }
    QMutexLocker lock(&g_mutex);
    for (size_t i = 0; i < g_sessions.size(); ++i) {
        const Session* o = g_sessions[i];
        if (o->req.chatId == s->req.chatId && o->req.chatType == s->req.chatType
            && o->req.localId == s->req.localId && o->seq > s->seq) {
            return false;
        }
    }
    return true;
}

void postResult(Session* s, bool ok, const std::string& reply,
                const std::string& providerUsed, const std::string& err) {
    if (!s) { return; }
    AigptbotDoneEvent* ev = new AigptbotDoneEvent();
    ev->chatId       = s->req.chatId;
    ev->chatType     = s->req.chatType;
    ev->localId      = s->req.localId;
    ev->success      = ok;
    ev->reply        = reply;
    ev->providerUsed = providerUsed;
    ev->errorMsg     = err;
    QObject* target  = s->target;
    removeSession(s);
    if (target) {
        // QCoreApplication::postEvent(target, ev);
        qAppPostEvent(target, ev);
    } else {
        delete ev;
    }
}

std::string jsonEscape(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        const unsigned char c = (unsigned char)in[i];
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
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

// JSON 简易解析（仅提取需要字段）
std::string parseOpenAiContent(const std::string& body, std::string& apiErr) {
    apiErr.clear();
    std::string text = trimStr(body);
    if (text.empty()) { return std::string(); }
    cJSON* root = cJSON_Parse(text.c_str());
    if (!root) { return std::string(); }
    // error.message
    cJSON* err = cJSON_GetObjectItem(root, "error");
    if (err && cJSON_IsObject(err)) {
        cJSON* msg = cJSON_GetObjectItem(err, "message");
        if (msg && cJSON_IsString(msg) && msg->valuestring) {
            apiErr = msg->valuestring;
        }
    }
    // choices[0].message.content
    std::string content;
    cJSON* choices = cJSON_GetObjectItem(root, "choices");
    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON* first = cJSON_GetArrayItem(choices, 0);
        if (first && cJSON_IsObject(first)) {
            cJSON* message = cJSON_GetObjectItem(first, "message");
            if (message && cJSON_IsObject(message)) {
                cJSON* c = cJSON_GetObjectItem(message, "content");
                if (c && cJSON_IsString(c) && c->valuestring) {
                    content = c->valuestring;
                }
            } else {
                cJSON* c = cJSON_GetObjectItem(first, "text");
                if (c && cJSON_IsString(c) && c->valuestring) {
                    content = c->valuestring;
                }
            }
        }
    }
    cJSON_Delete(root);
    if (content == std::string("[object Object]")) { return std::string(); }
    return trimStr(content);
}

void onDone(const HttpResponse& resp, void* udata);

void sendHttp(Session* s, const std::string& url, const std::string& body,
              const std::map<std::string, std::string>& headers) {
    HttpRequest req(url, "POST", body, 51, headers);
    req.followRedirects = true;
    if (!s->cands.empty() && s->index >= 0 && s->index < (int)s->cands.size()) {
        req.proxy = netProxyFor(s->cands[s->index]->needsProxy);
    }
    EventPoller::addRequest(req, onDone, s);
}

void startNext(Session* s);

// web provider 分派（实现分阶段加入：P1 deepseek → P2 gemini → P3 grok → P4 chatgpt → P5 meta-web）
void sendWebHost(Session* s, const AigptbotProvider& p);
void sendDeepseek(Session* s, const AigptbotProvider& p);
void sendGemini(Session* s, const AigptbotProvider& p);
void sendGrok(Session* s, const AigptbotProvider& p);
void sendChatgpt(Session* s, const AigptbotProvider& p);
// web 回包统一分发（对应 sendWebHttp 发出的请求；按 Session::step 推进状态机）
void onWebDone(const HttpResponse& resp, void* udata);

void sendHost(Session* s, const AigptbotProvider& p) {
    s->tried.push_back(p.name);
    // web provider：凭据检查 + 协议分派（不走 OpenAI 兼容请求体）
    if (p.webKind != kAigptbotWebNone) {
        const WebCredResult cr = webCredResult(p);
        qWarning("aigptbot: 候选[%d/%d] provider=%s host=%s webKind=%d cred=%s brief=%d textlen=%d B",
                 s->index, (int)s->cands.size(), p.name, p.baseUrl,
                 (int)p.webKind, webCredTag(cr.status), (int)s->req.brief, (int)s->req.text.size());
        if (!cr.ok) {
            std::string why;
            switch (cr.status) {
            case kWebCredPassEnc: {
                why = std::string(p.name) + ": 口令加密凭据未提供口令（配置时输入口令，或设 QTOX_WEB_CRED_PASS/PASS_COMMAND）";
                break;
            }
            case kWebCredBroken: {
                why = std::string(p.name) + ": 凭据解不开（密钥/口令错误或文件损坏，建议 工具→Web 凭据 中删除重建）";
                break;
            }
            default: {
                why = std::string(p.name) + ": 未配置凭据（工具→Web 凭据 或 docs/aigptbot-web-sources.md §3）";
                break;
            }
            }
            s->lastError = why;
            qWarning("aigptbot: provider=%s 凭据不可用(cred=%s) → 切换候选", p.name, webCredTag(cr.status));
            ++s->index;
            startNext(s);
            return;
        }
        sendWebHost(s, p);
        return;
    }
    // AI Horde：直接指定时立即失败
    if (p.id == kAigptbotAiHorde) {
        postResult(s, false, std::string(), p.name,
                   std::string("AI Horde 暂不支持文本对话"));
        return;
    }
    const std::string model = s->req.model.empty() ? p.modelDefault : s->req.model;
    qWarning("aigptbot: 候选[%d/%d] provider=%s host=%s model=%s brief=%d textlen=%d B keyready=%d",
             s->index, (int)s->cands.size(), p.name, p.baseUrl,
             model.empty() ? "(未指定)" : model.c_str(), (int)s->req.brief, (int)s->req.text.size(),
             p.webKind == kAigptbotWebNone ? (nonWebKeyReady(p) ? 1 : 0) : 0);
    // 构造 OpenAI 兼容请求体
    std::string body = "{";
    // model（UI 未指定时回落 provider 默认模型）
    if (!model.empty()) {
        body += "\"model\":\"" + jsonEscape(model) + "\",";
    }
    // 显式关闭流式：避免服务端 SSE/keep-alive 不关连接导致 200+超时误判
    body += "\"stream\":false,";
    // 简洁回复：max_tokens 兜底 + system 简洁指令引导
    if (s->req.brief) {
        body += "\"max_tokens\":512,";
    }
    // messages
    body += "\"messages\":[";
    if (s->req.brief) {
        body += "{\"role\":\"system\",\"content\":\""
                + jsonEscape("请尽量用最简洁的方式回答，简短直接，避免冗长。") + "\"},";
    }
    body += "{\"role\":\"user\",\"content\":\""
            + jsonEscape(s->req.text) + "\"}]";
    body += "}";
    // headers
    std::map<std::string, std::string> headers;
    headers["User-Agent"] = "qltox/1.0";
    headers["Accept-Language"] = "zh-CN,zh;q=0.9,en;q=0.4";
    headers["Content-Type"] = "application/json";
    // Authorization：非 allowEmptyKey 且已配置 key → 读侧车 <slug>_api
    if (!p.allowEmptyKey) {
        const char* kf = nonWebCredField(p);
        if (kf && kf[0]) {
            const WebCredResult key = webCredGet(kf, "");
            if (key.ok && !key.value.empty()) {
                headers["Authorization"] = "Bearer " + key.value;
            }
        }
    }
    sendHttp(s, p.baseUrl, body, headers);
}

void startNext(Session* s) {
    if (!s) { return; }
    if (s->index < (int)s->cands.size()) {
        sendHost(s, *s->cands[s->index]);
        return;
    }
    std::string err = s->lastError.empty()
            ? std::string("所有候选服务均失败")
            : s->lastError;
    std::string tried;
    for (size_t i = 0; i < s->tried.size(); ++i) {
        if (i) { tried += ", "; }
        tried += s->tried[i];
    }
    if (!tried.empty()) { err += "（已尝试：" + tried + "）"; }
    qWarning("aigptbot: 全部候选失败: %s", err.c_str());
    postResult(s, false, std::string(), std::string(), err);
}

// web 分支失败：记录错误并尝试下一个候选（点名单选时由 startNext 汇总报错）
void webFail(Session* s, const std::string& name, const std::string& err) {
    s->lastError = name + ": " + err;
    qWarning("aigptbot: provider=%s 失败: %s → 切换候选", name.c_str(), err.c_str());
    ++s->index;
    startNext(s);
}

// web 分支成功：聚合后单次返回
void webSucceed(Session* s, const std::string& name, const std::string& reply) {
    qWarning("aigptbot: provider=%s 成功 textlen=%d B",
             name.c_str(), (int)reply.size());
    postResult(s, true, reply, name, std::string());
}

// ── web 通用小工具 ──

std::string base64Encode(const std::string& in) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    while (i + 2 < in.size()) {
        unsigned int n = ((unsigned char)in[i] << 16)
                       | ((unsigned char)in[i + 1] << 8)
                       | ((unsigned char)in[i + 2]);
        out += tbl[(n >> 18) & 63];
        out += tbl[(n >> 12) & 63];
        out += tbl[(n >> 6) & 63];
        out += tbl[n & 63];
        i += 3;
    }
    if (i + 1 == in.size()) {
        unsigned int n = (unsigned char)in[i] << 16;
        out += tbl[(n >> 18) & 63];
        out += tbl[(n >> 12) & 63];
        out += "==";
    } else if (i + 2 == in.size()) {
        unsigned int n = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8);
        out += tbl[(n >> 18) & 63];
        out += tbl[(n >> 12) & 63];
        out += tbl[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

// base64 无 padding（grok x-statsig-id；70B → 94 字符）
std::string base64EncodeNoPad(const std::string& in) {
    std::string s = base64Encode(in);
    while (!s.empty() && s[s.size() - 1] == '=') { s.erase(s.size() - 1); }
    return s;
}

// 随机 UUID v4（设备 ID 等用；依赖 ensureSeed() 已 srand）
std::string uuidV4() {
    static const char* hexd = "0123456789abcdef";
    unsigned char b[16];
    for (int i = 0; i < 16; ++i) { b[i] = (unsigned char)(rand() & 0xff); }
    b[6] = (unsigned char)((b[6] & 0x0f) | 0x40);
    b[8] = (unsigned char)((b[8] & 0x3f) | 0x80);
    std::string out;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) { out += '-'; }
        out += hexd[b[i] >> 4];
        out += hexd[b[i] & 0x0f];
    }
    return out;
}

std::string toUpperAscii(const std::string& in) {
    std::string out = in;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] >= 'a' && out[i] <= 'z') { out[i] = (char)(out[i] - 'a' + 'A'); }
    }
    return out;
}

// 表单 percent-encoding（encodeURIComponent 语义：除 A-Za-z0-9-_.~ 外全编码）
std::string urlEncode(const std::string& in) {
    static const char* hexd = "0123456789ABCDEF";
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        const unsigned char c = (unsigned char)in[i];
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) {
            out += (char)c;
        } else {
            out += '%';
            out += hexd[c >> 4];
            out += hexd[c & 15];
        }
    }
    return out;
}

// cJSON 字段提取（缺省安全返回）
std::string jStr(cJSON* obj, const char* name) {
    if (!obj) { return std::string(); }
    cJSON* n = cJSON_GetObjectItem(obj, name);
    if (n && cJSON_IsString(n) && n->valuestring) { return n->valuestring; }
    return std::string();
}
long long jInt(cJSON* obj, const char* name) {
    if (!obj) { return 0; }
    cJSON* n = cJSON_GetObjectItem(obj, name);
    if (n && cJSON_IsNumber(n)) { return (long long)n->valuedouble; }
    return 0;
}
double jNum(cJSON* obj, const char* name) {
    if (!obj) { return 0.0; }
    cJSON* n = cJSON_GetObjectItem(obj, name);
    if (n && cJSON_IsNumber(n)) { return n->valuedouble; }
    return 0.0;
}

// web 专用 HTTP（timeout/stall 按流式与非流式分别设置）
void sendWebHttp(Session* s, const std::string& url, const std::string& method,
                 const std::string& body,
                 const std::map<std::string, std::string>& headers,
                 int timeoutSec, int stallSec) {
    HttpRequest req(url, method, body, timeoutSec, headers, stallSec);
    req.followRedirects = true;
    if (!s->cands.empty() && s->index >= 0 && s->index < (int)s->cands.size()) {
        req.proxy = netProxyFor(s->cands[s->index]->needsProxy);
    }
    EventPoller::addRequest(req, onWebDone, s);
}

// ── deepseek-web（协议见 docs/aigptbot-web-sources.md §6.1）──

std::string g_dsDeviceId;   // 进程内稳定设备 ID（首启生成后复用）

std::string dsDeviceId() {
    const WebCredResult r = webCredGet("deepseek_device_id", kDeepseekDeviceIdCred);
    if (r.ok && !r.value.empty()) { return r.value; }
    if (g_dsDeviceId.empty()) { g_dsDeviceId = uuidV4(); }
    return g_dsDeviceId;
}

std::map<std::string, std::string> dsHeaders(bool auth) {
    std::map<std::string, std::string> h;
    h["User-Agent"] = webUA(true);
    h["Accept"] = "*/*";
    h["Referer"] = "https://chat.deepseek.com/";
    const WebCredResult dsCookie = webCredGet("deepseek_cookie", kDeepseekWebCookie);
    if (!dsCookie.value.empty()) { h["Cookie"] = dsCookie.value; }
    h["X-Client-Version"] = "2.5.0";
    h["X-Client-Platform"] = "android";
    h["X-Client-Locale"] = "zh_CN";
    h["X-Client-Bundle-Id"] = "com.deepseek.chat";
    h["X-Device-Id"] = dsDeviceId();
    h["X-Device-Model"] = "";
    h["X-Client-Timezone-Offset"] = "28800";
    if (auth) {
        h["Content-Type"] = "application/json";
        h["Authorization"] = std::string("Bearer ") + webCredGet("deepseek_web", kDeepseekWebCred).value;
    }
    return h;
}

// 解析 DeepSeek 信封 {code,msg,data:{biz_code,biz_msg,biz_data}}。
// 成功返回 biz_data（借用，须连同 *rootOut 一起 Delete），失败返回 nullptr 并填 err。
cJSON* dsParseEnvelope(const std::string& body, cJSON** rootOut, std::string& err) {
    *rootOut = nullptr;
    cJSON* root = cJSON_Parse(trimStr(body).c_str());
    if (!root) {
        err = "响应不是合法 JSON（" + briefBody(body) + "）";
        return nullptr;
    }
    *rootOut = root;
    cJSON* code = cJSON_GetObjectItem(root, "code");
    if (code && cJSON_IsNumber(code) && code->valueint != 0) {
        err = "code=" + std::to_string(code->valueint) + " " + jStr(root, "msg");
        return nullptr;
    }
    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (data && cJSON_IsObject(data)) {
        cJSON* bizCode = cJSON_GetObjectItem(data, "biz_code");
        if (bizCode && cJSON_IsNumber(bizCode) && bizCode->valueint != 0) {
            err = "biz_code=" + std::to_string(bizCode->valueint)
                + " " + jStr(data, "biz_msg");
            return nullptr;
        }
        cJSON* bizData = cJSON_GetObjectItem(data, "biz_data");
        if (bizData && cJSON_IsObject(bizData)) { return bizData; }
    }
    err = "缺少 data.biz_data（" + briefBody(body) + "）";
    return nullptr;
}

// SSE 聚合状态
struct DsAgg {
    std::map<int, std::string> frag;      // 各片段正文
    std::map<int, std::string> fragType;  // THINK / RESPONSE / 缺省
    int maxIdx = -1;
    std::string status;                   // response/status
    std::string quasi;                    // response/quasi_status（BATCH 内）
    std::string errMsg;
    std::string curPath;                  // 粘性 p：跨事件持久化（对齐前端 DeltaParser）
    std::string curOp;                    // 粘性 o：缺省继承上一帧（空视作 SET）
    int nData = 0;                        // SSE data: 段数
    int nJson = 0;                        // 成功解析的 JSON 段数
    int nOps = 0;                         // 实际应用的 patch 数
    std::string respMsgId;                // event: ready → response_message_id
    bool sessionFull = false;             // finish_reason=input_exceeds_limit（会话已满）
};

// SSE 解析统计（供日志）
struct DsSseStat {
    int dataFrames = 0;
    int jsonFrames = 0;
    int ops = 0;
    size_t bytes = 0;
    std::string respMsgId;
    bool sessionFull = false;
};

int dsResolveIdx(DsAgg& a, const std::string& key) {
    int idx = 0;
    if (key == "-1" || key.empty()) {
        idx = a.maxIdx;
        if (idx < 0) { idx = 0; }
    } else {
        idx = std::atoi(key.c_str());
        if (idx < 0) { idx = 0; }
    }
    if (idx > a.maxIdx) { a.maxIdx = idx; }
    return idx;
}

void dsSeedFragments(DsAgg& a, cJSON* resp) {
    if (!resp || !cJSON_IsObject(resp)) { return; }
    const std::string st = jStr(resp, "status");
    if (!st.empty()) { a.status = st; }
    cJSON* frags = cJSON_GetObjectItem(resp, "fragments");
    if (!frags || !cJSON_IsArray(frags)) { return; }
    const int n = cJSON_GetArraySize(frags);
    for (int i = 0; i < n; ++i) {
        cJSON* f = cJSON_GetArrayItem(frags, i);
        if (!f || !cJSON_IsObject(f)) { continue; }
        a.frag[i] = jStr(f, "content");
        const std::string t = jStr(f, "type");
        if (!t.empty()) { a.fragType[i] = t; }
        if (i > a.maxIdx) { a.maxIdx = i; }
    }
}

// 追加到活跃片段（maxIdx）；尚无片段则建 idx 0。裸 {"v":"…"} 增量靠此落地。
void dsAppendActive(DsAgg& a, const std::string& text) {
    if (text.empty()) { return; }
    if (a.maxIdx < 0) {
        a.maxIdx = 0;
        a.frag[a.maxIdx] = "";
    }
    a.frag[a.maxIdx] += text;
}

// 应用一个操作符（op ∈ SET/APPEND，v 已 trim 的原始 JSON 文本）
void dsApplyOp(DsAgg& a, const std::string& path, const std::string& op,
               const std::string& vraw) {
    if (path.empty() || vraw.empty()) { return; }
    // BATCH：v 是数组，逐项以父路径为前缀递归
    if (op == "BATCH") {
        cJSON* arr = cJSON_Parse(vraw.c_str());
        if (!arr || !cJSON_IsArray(arr)) {
            if (arr) { cJSON_Delete(arr); }
            return;
        }
        const int n = cJSON_GetArraySize(arr);
        for (int i = 0; i < n; ++i) {
            cJSON* item = cJSON_GetArrayItem(arr, i);
            if (!item || !cJSON_IsObject(item)) { continue; }
            const std::string cp = jStr(item, "p");
            if (cp.empty()) { continue; }
            std::string full;
            if (cp[0] == '/') { full = path + cp; }
            else if (!path.empty()) { full = path + "/" + cp; }
            else { full = cp; }
            std::string sub = jStr(item, "o");
            if (sub.empty()) { sub = "SET"; }
            cJSON* vnode = cJSON_GetObjectItem(item, "v");
            if (!vnode) { continue; }
            char* vjson = cJSON_PrintUnformatted(vnode);
            if (vjson) {
                dsApplyOp(a, full, sub, vjson);
                free(vjson);
            }
        }
        cJSON_Delete(arr);
        return;
    }
    // 路径分段
    std::vector<std::string> toks;
    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '/') {
            if (!cur.empty()) { toks.push_back(cur); }
            cur.clear();
        } else {
            cur += path[i];
        }
    }
    if (!cur.empty()) { toks.push_back(cur); }
    if (toks.empty()) { return; }

    // 提取 v 的内容（字符串则去引号，其余原样）
    std::string vstr;
    {
        cJSON* vjson = cJSON_Parse(vraw.c_str());
        if (vjson) {
            if (cJSON_IsString(vjson) && vjson->valuestring) { vstr = vjson->valuestring; }
            else if (cJSON_IsNumber(vjson)) { vstr = vraw; }
            cJSON_Delete(vjson);
        }
    }

    // response/fragments...
    if (toks.size() >= 2 && toks[0] == "response" && toks[1] == "fragments") {
        if (toks.size() == 2) {
            // APPEND 追加新片段；SET 重置整个 fragments 数组
            cJSON* vjson = cJSON_Parse(vraw.c_str());
            if (vjson && cJSON_IsArray(vjson)) {
                if (op != "APPEND") {
                    a.frag.clear();
                    a.fragType.clear();
                    a.maxIdx = -1;
                }
                const int n = cJSON_GetArraySize(vjson);
                for (int i = 0; i < n; ++i) {
                    cJSON* f = cJSON_GetArrayItem(vjson, i);
                    if (!f || !cJSON_IsObject(f)) { continue; }
                    const int idx = (op == "APPEND") ? (++a.maxIdx) : i;
                    a.frag[idx] = jStr(f, "content");
                    const std::string t = jStr(f, "type");
                    if (!t.empty()) { a.fragType[idx] = t; }
                    if (i > a.maxIdx) { a.maxIdx = i; }
                }
            } else if (vjson && cJSON_IsObject(vjson) && op == "APPEND") {
                // 单个片段对象 {id,type,content,…}
                const int idx = ++a.maxIdx;
                a.frag[idx] = jStr(vjson, "content");
                const std::string t = jStr(vjson, "type");
                if (!t.empty()) { a.fragType[idx] = t; }
            }
            if (vjson) { cJSON_Delete(vjson); }
            return;
        }
        const int idx = dsResolveIdx(a, toks[2]);
        if (toks.size() == 3) {
            // SET 整个片段对象 {type,content}
            cJSON* vjson = cJSON_Parse(vraw.c_str());
            if (vjson && cJSON_IsObject(vjson)) {
                a.frag[idx] = jStr(vjson, "content");
                const std::string t = jStr(vjson, "type");
                if (!t.empty()) { a.fragType[idx] = t; }
            }
            if (vjson) { cJSON_Delete(vjson); }
            return;
        }
        const std::string field = toks[3];
        if (field == "content") {
            if (op == "APPEND") { a.frag[idx] += vstr; }
            else { a.frag[idx] = vstr; }
        } else if (field == "type") {
            a.fragType[idx] = vstr;
        }
        return;
    }
    // response/content 与 response/thinking_content（旧模型 / 兼容路径）
    if (toks.size() == 2 && toks[0] == "response" && toks[1] == "content") {
        if (op == "SET" && a.maxIdx >= 0) { a.frag[a.maxIdx] = vstr; }
        else { dsAppendActive(a, vstr); }
        return;
    }
    if (toks.size() == 2 && toks[0] == "response" && toks[1] == "thinking_content") {
        if (op == "SET" && a.maxIdx >= 0) { a.frag[a.maxIdx] = vstr; }
        else { dsAppendActive(a, vstr); }
        return;
    }
    // response/status、response/quasi_status
    if (toks.size() == 2 && toks[0] == "response" && toks[1] == "status") {
        a.status = vstr;
        return;
    }
    if (toks.size() == 2 && toks[0] == "response" && toks[1] == "quasi_status") {
        a.quasi = vstr;
        return;
    }
}

// 解析 deepseek SSE 全量响应体（EventPoller 已缓冲整包），聚合出正文
bool parseDeepseekSse(const std::string& body, std::string& out, std::string& err,
                      DsSseStat* stat = nullptr) {
    out.clear();
    err.clear();
    DsAgg a;
    std::string evt;
    size_t pos = 0;
    const size_t n = body.size();
    while (pos <= n) {
        size_t eol = body.find('\n', pos);
        std::string line = (eol == std::string::npos)
                ? body.substr(pos) : body.substr(pos, eol - pos);
        if (!line.empty() && line[line.size() - 1] == '\r') { line.erase(line.size() - 1); }
        pos = (eol == std::string::npos) ? n + 1 : eol + 1;

        if (line.empty()) {
            evt.clear();
            continue;
        }
        if (line.compare(0, 7, "event:") == 0) {
            evt = trimStr(line.substr(7));
            continue;
        }
        if (line.compare(0, 5, "data:") != 0) { continue; }
        const std::string data = trimStr(line.substr(5));
        if (data.empty()) { continue; }
        ++a.nData;

        cJSON* j = cJSON_Parse(data.c_str());
        if (!j) { continue; }
        ++a.nJson;
        if (cJSON_IsObject(j)) {
            // 错误事件：event=hint 且 type=error，或顶层 error 对象
            const std::string jtype = jStr(j, "type");
            cJSON* jerr = cJSON_GetObjectItem(j, "error");
            if ((evt == "hint" && jtype == "error") || (jerr && cJSON_IsObject(jerr))) {
                std::string m = jStr(j, "message");
                if (m.empty()) { m = jStr(j, "msg"); }
                if (m.empty() && jerr) { m = jStr(jerr, "message"); }
                if (m.empty()) { m = jStr(j, "content"); }
                if (m.empty()) { m = "服务端错误"; }
                a.errMsg = m;
                if (jStr(j, "finish_reason") == "input_exceeds_limit") { a.sessionFull = true; }
            }
            // event: ready → 下一条消息的 parent_message_id
            if (evt == "ready") {
                cJSON* rid = cJSON_GetObjectItem(j, "response_message_id");
                if (rid && cJSON_IsNumber(rid)) {
                    a.respMsgId = std::to_string((long long)rid->valuedouble);
                } else if (rid && cJSON_IsString(rid) && rid->valuestring) {
                    a.respMsgId = rid->valuestring;
                }
            }
            // 粘性 p/o（对齐前端 DeltaParser）：帧缺省时继承上一帧
            cJSON* pnode = cJSON_GetObjectItem(j, "p");
            if (pnode && cJSON_IsString(pnode) && pnode->valuestring) {
                a.curPath = pnode->valuestring;
            }
            cJSON* onode = cJSON_GetObjectItem(j, "o");
            if (onode && cJSON_IsString(onode) && onode->valuestring) {
                a.curOp = onode->valuestring;
            }
            cJSON* vnode = cJSON_GetObjectItem(j, "v");
            if (vnode) {
                if (a.curPath.empty()) {
                    // 首帧快照 {"v":{"response":{...}}}；否则退回裸增量
                    cJSON* resp = cJSON_GetObjectItem(vnode, "response");
                    if (resp) {
                        dsSeedFragments(a, resp);
                    } else if (cJSON_IsString(vnode) && vnode->valuestring) {
                        dsAppendActive(a, vnode->valuestring);
                    }
                } else {
                    // 裸 {"v":"深圳"} 靠粘性 curPath 落到 -1/content + APPEND
                    const std::string op = a.curOp.empty() ? "SET" : a.curOp;
                    char* vjson = cJSON_PrintUnformatted(vnode);
                    if (vjson) {
                        dsApplyOp(a, a.curPath, op, vjson);
                        ++a.nOps;
                        free(vjson);
                    }
                }
            }
        }
        cJSON_Delete(j);
    }

    if (stat) {
        stat->dataFrames = a.nData;
        stat->jsonFrames = a.nJson;
        stat->ops = a.nOps;
        stat->bytes = body.size();
        stat->respMsgId = a.respMsgId;
        stat->sessionFull = a.sessionFull;
    }
    if (!a.errMsg.empty()) { err = a.errMsg; return false; }
    if (a.status == "INCOMPLETE" || a.quasi == "INCOMPLETE") {
        err = "生成被中断（INCOMPLETE）";
        return false;
    }
    for (int i = 0; i <= a.maxIdx; ++i) {
        std::map<int, std::string>::const_iterator t = a.fragType.find(i);
        if (t != a.fragType.end() && t->second == "THINK") { continue; }
        std::map<int, std::string>::const_iterator c = a.frag.find(i);
        if (c != a.frag.end()) { out += c->second; }
    }
    out = trimStr(out);
    if (out.empty()) {
        err = "回复为空（status=" + a.status + "）";
        return false;
    }
    return true;
}

// deepseek 分步回包（step 见 sendDeepseek）
void dsOnDone(const HttpResponse& resp, Session* s, const AigptbotProvider& p) {
    if (s->step == 0) {
        // HIF 风控令牌
        if (resp.httpCode != 200) {
            webFail(s, p.name, "HIF 查询 HTTP " + std::to_string(resp.httpCode)
                    + (resp.curlErrStr.empty() ? "" : " " + resp.curlErrStr));
            return;
        }
        cJSON* root = nullptr;
        std::string err;
        cJSON* biz = dsParseEnvelope(resp.body, &root, err);
        if (!biz) {
            if (root) { cJSON_Delete(root); }
            webFail(s, p.name, "HIF 解析失败：" + err);
            return;
        }
        const std::string value = jStr(biz, "value");
        cJSON_Delete(root);
        if (value.empty()) {
            webFail(s, p.name, "HIF 令牌为空");
            return;
        }
        s->wv["hif"] = value;
        qWarning("aigptbot: deepseek HIF 令牌获取成功（%d B）", (int)value.size());
        ++s->step;
        sendDeepseek(s, p);
        return;
    }
    if (s->step == 1) {
        // PoW challenge
        if (resp.httpCode != 200) {
            webFail(s, p.name, "create_pow_challenge HTTP "
                    + std::to_string(resp.httpCode)
                    + (resp.curlErrStr.empty() ? "" : " " + resp.curlErrStr));
            return;
        }
        cJSON* root = nullptr;
        std::string err;
        cJSON* biz = dsParseEnvelope(resp.body, &root, err);
        if (!biz) {
            if (root) { cJSON_Delete(root); }
            webFail(s, p.name, "challenge 解析失败：" + err);
            return;
        }
        cJSON* chal = cJSON_GetObjectItem(biz, "challenge");
        std::string algorithm, challenge, salt, signature, targetPath;
        long long expireAt = 0;
        double difficulty = 0.0;
        if (chal && cJSON_IsObject(chal)) {
            algorithm = jStr(chal, "algorithm");
            challenge = jStr(chal, "challenge");
            salt = jStr(chal, "salt");
            signature = jStr(chal, "signature");
            targetPath = jStr(chal, "target_path");
            expireAt = jInt(chal, "expire_at");
            difficulty = jNum(chal, "difficulty");
        }
        cJSON_Delete(root);
        if (algorithm != "DeepSeekHashV1" || challenge.size() != 64
            || salt.empty() || difficulty <= 0.0) {
            webFail(s, p.name, "challenge 字段异常（algorithm=" + algorithm
                    + " challenge_len=" + std::to_string((int)challenge.size()) + "）");
            return;
        }
        const std::string prefix = salt + "_" + std::to_string(expireAt) + "_";
        qWarning("aigptbot: deepseek PoW 开始 difficulty=%.0f prefix_len=%d",
                 difficulty, (int)prefix.size());
        solve_result r = dspow_solve(challenge.c_str(), 64,
                                     (const uint8_t*)prefix.c_str(),
                                     (uint32_t)prefix.size(), difficulty);
        if (!r.found) {
            webFail(s, p.name, "PoW 求解失败（difficulty=" + std::to_string((int)difficulty) + "）");
            return;
        }
        std::string powJson = "{\"algorithm\":\"" + jsonEscape(algorithm)
            + "\",\"challenge\":\"" + jsonEscape(challenge)
            + "\",\"salt\":\"" + jsonEscape(salt)
            + "\",\"answer\":" + std::to_string((long long)r.nonce)
            + ",\"signature\":\"" + jsonEscape(signature)
            + "\",\"target_path\":\"" + jsonEscape(targetPath) + "\"}";
        s->wv["pow"] = base64Encode(powJson);
        qWarning("aigptbot: deepseek PoW 解出 answer=%llu attempts=%.0f",
                 (unsigned long long)r.nonce, r.attempts);
        // 会话复用：非强制新会话且命中缓存 → 跳过 create，直接 completion
        s->sessKey = webSessionKey(p);
        AigptSessionRecord crec;
        if (!s->req.newChat && aigptSessionGet(s->sessKey, crec) && !crec.sessionId.empty()) {
            s->wv["session"] = crec.sessionId;
            s->wv["parent"]  = crec.parentId;
            qWarning("aigptbot: deepseek 复用会话 id=%s parent=%s n=%lld",
                     crec.sessionId.c_str(),
                     crec.parentId.empty() ? "null" : crec.parentId.c_str(),
                     (long long)crec.msgCount);
            s->step = 3;
        } else {
            if (s->req.newChat) {
                aigptSessionInvalidate(s->sessKey);
                qWarning("aigptbot: deepseek 用户新对话 → 丢弃旧会话");
            }
            s->step = 2;
        }
        sendDeepseek(s, p);
        return;
    }
    if (s->step == 2) {
        // 创建会话
        if (resp.httpCode != 200) {
            webFail(s, p.name, "chat_session/create HTTP "
                    + std::to_string(resp.httpCode)
                    + (resp.curlErrStr.empty() ? "" : " " + resp.curlErrStr));
            return;
        }
        cJSON* root = nullptr;
        std::string err;
        cJSON* biz = dsParseEnvelope(resp.body, &root, err);
        if (!biz) {
            if (root) { cJSON_Delete(root); }
            webFail(s, p.name, "会话创建失败：" + err);
            return;
        }
        cJSON* sessObj = cJSON_GetObjectItem(biz, "chat_session");
        const std::string sessId = jStr(sessObj, "id");
        cJSON_Delete(root);
        if (sessId.empty()) {
            webFail(s, p.name, "chat_session.id 为空");
            return;
        }
        s->wv["session"] = sessId;
        s->wv["parent"]  = "";
        qWarning("aigptbot: deepseek 会话创建成功 id=%s", sessId.c_str());
        {
            AigptSessionRecord nrec;
            nrec.sessionId = sessId;
            nrec.createdAt = (long long)time(nullptr);
            aigptSessionPut(s->sessKey, nrec);
        }
        s->step = 3;
        sendDeepseek(s, p);
        return;
    }
    if (s->step == 3) {
        // completion（SSE）
        if (resp.httpCode != 200) {
            webFail(s, p.name, "completion HTTP " + std::to_string(resp.httpCode)
                    + (resp.curlErrStr.empty() ? "" : " " + resp.curlErrStr));
            return;
        }
        DsSseStat st;
        std::string out, err;
        const TimePoint pt0 = timeNow();
        if (!parseDeepseekSse(resp.body, out, err, &st)) {
            qWarning("aigptbot: deepseek SSE 解析失败 segs=%d json=%d ops=%d bytes=%d elapsed=%lldms err=%s sessionFull=%d",
                     st.dataFrames, st.jsonFrames, st.ops, (int)resp.body.size(),
                     (long long)resp.elapsedMs, err.c_str(), (int)st.sessionFull);
            // 会话已满（input_exceeds_limit）：丢弃旧会话，以新会话重发一次
            if (st.sessionFull && s->wv["ds_retried"].empty()) {
                aigptSessionInvalidate(s->sessKey);
                s->wv["ds_retried"] = "1";
                s->wv["session"] = "";
                s->wv["parent"]  = "";
                qWarning("aigptbot: deepseek 会话满 → 新建会话重发");
                s->step = 2;
                sendDeepseek(s, p);
                return;
            }
            if (!resp.curlErrStr.empty()) {
                err += "（" + resp.curlErrStr + "）";
            }
            webFail(s, p.name, err);
            return;
        }
        const long long parseMs = elapsedMs(pt0);
        qWarning("aigptbot: deepseek SSE segs=%d json=%d ops=%d bytes=%d outlen=%d parse=%lldms elapsed=%lldms",
                 st.dataFrames, st.jsonFrames, st.ops, (int)resp.body.size(),
                 (int)out.size(), (long long)parseMs, (long long)resp.elapsedMs);
        // TODO(临时·验证后删除): 打印 SSE 原文前 4000 字
        qWarning("aigptbot: deepseek SSE raw[0..4000]=[%s]",
                 resp.body.substr(0, 4000).c_str());
        // 成功：记录 parent_message_id / 计数，供下一条复用
        if (!s->sessKey.empty()) {
            AigptSessionRecord urec;
            aigptSessionGet(s->sessKey, urec);
            urec.sessionId = s->wv["session"];
            if (urec.createdAt == 0) { urec.createdAt = (long long)time(nullptr); }
            if (!st.respMsgId.empty()) { urec.parentId = st.respMsgId; }
            urec.msgCount += 1;
            aigptSessionPut(s->sessKey, urec);
            qWarning("aigptbot: deepseek 会话已更新 parent=%s n=%lld",
                     urec.parentId.c_str(), (long long)urec.msgCount);
        }
        webSucceed(s, p.name, out);
        return;
    }
    webFail(s, p.name, "内部错误：未知 step=" + std::to_string(s->step));
}

void sendDeepseek(Session* s, const AigptbotProvider& p) {
    switch (s->step) {
    case 0: {   // HIF 风控令牌（无鉴权）
        std::map<std::string, std::string> h = dsHeaders(false);
        sendWebHttp(s, "https://hif-leim.deepseek.com/query", "GET", "", h, 35, 30);
        break;
    }
    case 1: {   // PoW challenge
        std::map<std::string, std::string> h = dsHeaders(true);
        sendWebHttp(s, "https://chat.deepseek.com/api/v0/chat/create_pow_challenge",
                    "POST", "{\"target_path\":\"/api/v0/chat/completion\"}", h, 35, 30);
        break;
    }
    case 2: {   // 创建会话
        std::map<std::string, std::string> h = dsHeaders(true);
        sendWebHttp(s, "https://chat.deepseek.com/api/v0/chat_session/create",
                    "POST", "{}", h, 35, 30);
        break;
    }
    case 3: {   // completion（SSE，带 PoW + HIF 头）
        std::map<std::string, std::string> h = dsHeaders(true);
        h["X-Ds-Pow-Response"] = s->wv["pow"];
        h["X-Hif-Leim"] = s->wv["hif"];
        h["Accept"] = "text/event-stream";
        std::string prompt = s->req.text;
        if (s->req.brief) {
            prompt = std::string("请尽量用最简洁的方式回答，简短直接，避免冗长。\n\n") + prompt;
        }
        std::string body = "{\"chat_session_id\":\"" + jsonEscape(s->wv["session"])
            + "\",\"parent_message_id\":"
            + ((s->wv.count("parent") && !s->wv["parent"].empty()) ? s->wv["parent"] : std::string("null"))
            + ",\"model_type\":\"default\","
              "\"prompt\":\"" + jsonEscape(prompt)
            + "\",\"ref_file_ids\":[],\"thinking_enabled\":"
            + (s->req.thinking ? "true" : "false")
            + ",\"search_enabled\":"
            + (s->req.search ? "true" : "false")
            + ",\"action\":null,\"preempt\":false}";
        sendWebHttp(s, "https://chat.deepseek.com/api/v0/chat/completion",
                    "POST", body, h, 180, 60);
        break;
    }
    default: {
        webFail(s, p.name, "内部错误：未知 step=" + std::to_string(s->step));
        break;
    }
    }
}

// ── gemini-web（协议见 docs/aigptbot-web-sources.md §6.3）──

std::map<std::string, std::string> gemHeaders(bool form) {
    std::map<std::string, std::string> h;
    h["User-Agent"] = webUA(false);
    h["Cookie"] = webCredGet("gemini_web", kGeminiWebCred).value;
    h["Accept"] = "*/*";
    webBrowserHeaders(h);
    if (form) {
        h["Content-Type"] = "application/x-www-form-urlencoded;charset=utf-8";
        h["Referer"] = "https://gemini.google.com/";
        h["Origin"] = "https://gemini.google.com";
        h["X-Same-Domain"] = "1";
    }
    return h;
}

// 在 HTML/JS 文本中取 "KEY":"value"
std::string gemExtractStr(const std::string& text, const std::string& key) {
    const size_t k = text.find(key);
    if (k == std::string::npos) { return std::string(); }
    size_t q1 = text.find('"', k + key.size());
    if (q1 == std::string::npos) { return std::string(); }
    size_t q2 = text.find('"', q1 + 1);
    if (q2 == std::string::npos) { return std::string(); }
    return text.substr(q1 + 1, q2 - q1 - 1);
}

// 构造 inner 81 元素稀疏数组（null 即缺省）
std::string gemInnerJson(const std::string& prompt, const std::string& lang,
                         const std::string& uuidUp,
                         const std::string& convId, const std::string& respId) {
    std::string slot[81];
    for (int i = 0; i < 81; ++i) { slot[i] = ""; }
    slot[0] = "[\"" + jsonEscape(prompt) + "\",0,null,null,null,null,0]";
    slot[1] = "[\"" + jsonEscape(lang) + "\"]";
    slot[2] = "[\"" + jsonEscape(convId) + "\",\"" + jsonEscape(respId)
            + "\",null,null,null,null,null,null,null,\"\"]";
    slot[6] = "[1]";
    slot[7] = "1";
    slot[10] = "1";
    slot[11] = "0";
    slot[17] = "[[0]]";
    slot[18] = "0";
    slot[27] = "1";
    slot[30] = "[4]";
    slot[41] = "[1]";
    slot[53] = "0";
    slot[59] = "\"" + jsonEscape(uuidUp) + "\"";
    slot[61] = "[]";
    slot[68] = "1";
    slot[79] = "1";
    slot[80] = "1";
    std::string in = "[";
    for (int i = 0; i < 81; ++i) {
        if (i > 0) { in += ","; }
        in += slot[i].empty() ? "null" : slot[i].c_str();
    }
    in += "]";
    return in;
}

// 剔除占位链接 ARTIFACTS_RE = https?://googleusercontent\.com/(?:\w+/)+\d+\n*
std::string gemStripArtifacts(const std::string& in) {
    std::string out;
    size_t i = 0;
    while (i < in.size()) {
        // 找到 googleusercontent.com/ 起点（含 http(s)://）
        size_t dom = std::string::npos;
        size_t pos = i;
        while (pos < in.size()) {
            const size_t p1 = in.find("http://googleusercontent.com/", pos);
            const size_t p2 = in.find("https://googleusercontent.com/", pos);
            if (p1 == std::string::npos && p2 == std::string::npos) { break; }
            dom = (p2 == std::string::npos || (p1 != std::string::npos && p1 < p2)) ? p1 : p2;
            break;
        }
        if (dom == std::string::npos) {
            out += in.substr(i);
            return out;
        }
        size_t j = dom;
        if (in.compare(j, 7, "http://") == 0) { j += 7; }
        else { j += 8; }
        j += std::string("googleusercontent.com/").size();
        // (?:\w+/)+ \d+ \n*
        size_t seg = j;
        bool ok = false;
        while (true) {
            size_t k = seg;
            while (k < in.size() && (isalnum((unsigned char)in[k]) || in[k] == '_')) { ++k; }
            if (k > seg && k < in.size() && in[k] == '/') {
                seg = k + 1;
                ok = true;
                continue;
            }
            break;
        }
        if (!ok) {
            // 模式不匹配：原样输出到 dom 之后继续
            out += in.substr(i, dom + 1 - i);
            i = dom + 1;
            continue;
        }
        size_t d = seg;
        while (d < in.size() && isdigit((unsigned char)in[d])) { ++d; }
        if (d == seg) {
            out += in.substr(i, dom + 1 - i);
            i = dom + 1;
            continue;
        }
        while (d < in.size() && in[d] == '\n') { ++d; }
        i = d;   // 整段匹配剔除
    }
    return out;
}

// cJSON 数组安全取项
cJSON* jAt(cJSON* n, int idx) {
    if (!n || !cJSON_IsArray(n)) { return nullptr; }
    if (idx < 0 || idx >= cJSON_GetArraySize(n)) { return nullptr; }
    return cJSON_GetArrayItem(n, idx);
}

// 解析分帧响应（)]}' + <UTF-16 长度>\n<帧>），聚合正文
bool parseGeminiResponse(const std::string& body, std::string& out, std::string& err,
                         std::string* convOut = nullptr, std::string* respOut = nullptr) {
    out.clear();
    err.clear();
    size_t i = 0;
    if (body.compare(0, 4, ")]}'") == 0) { i = 4; }
    std::string lastText;
    int frames = 0;

    while (i < body.size()) {
        while (i < body.size() && isspace((unsigned char)body[i])) { ++i; }
        if (i >= body.size()) { break; }
        const size_t numStart = i;
        while (i < body.size() && body[i] >= '0' && body[i] <= '9') { ++i; }
        if (i == numStart || i >= body.size() || body[i] != '\n') { break; }
        const long long expect = atoll(body.substr(numStart, i - numStart).c_str());
        ++i;   // skip '\n'
        const size_t payloadStart = i;
        long long units = 0;
        while (i < body.size() && units < expect) {
            const unsigned char c = (unsigned char)body[i];
            int cpLen = 1;
            unsigned int cp = c;
            if (c >= 0xF0 && c < 0xF8 && i + 4 <= body.size()) {
                cpLen = 4;
                cp = ((c & 0x07u) << 18) | ((body[i + 1] & 0x3F) << 12)
                   | ((body[i + 2] & 0x3F) << 6) | (body[i + 3] & 0x3F);
            } else if (c >= 0xE0 && c < 0xF0 && i + 3 <= body.size()) {
                cpLen = 3;
                cp = ((c & 0x0Fu) << 12) | ((body[i + 1] & 0x3F) << 6) | (body[i + 2] & 0x3F);
            } else if (c >= 0xC0 && c < 0xE0 && i + 2 <= body.size()) {
                cpLen = 2;
                cp = ((c & 0x1Fu) << 6) | (body[i + 1] & 0x3F);
            } else {
                cp = c;
            }
            const long long add = (cp > 0xFFFF) ? 2 : 1;
            if (units + add > expect) { break; }
            units += add;
            i += cpLen;
        }
        const std::string frame = body.substr(payloadStart, i - payloadStart);
        if (frame.empty() || frame.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }
        ++frames;
        cJSON* fj = cJSON_Parse(frame.c_str());
        if (!fj) { continue; }
        // 帧 = 数组（多个 part），或单个 part 对象
        cJSON* parts[2];
        int nparts = 0;
        if (cJSON_IsArray(fj)) {
            for (int k = 0; k < cJSON_GetArraySize(fj) && nparts < 2; ++k) {
                parts[nparts++] = cJSON_GetArrayItem(fj, k);
            }
        } else {
            parts[nparts++] = fj;
        }
        for (int pi = 0; pi < nparts; ++pi) {
            cJSON* part = parts[pi];
            if (!part || !cJSON_IsArray(part)) { continue; }
            // 错误码 part[5][2][0][1][0]
            cJSON* e = jAt(jAt(jAt(jAt(jAt(part, 5), 2), 0), 1), 0);
            if (e && cJSON_IsNumber(e) && e->valueint != 0) {
                const long long code = (long long)e->valuedouble;
                err = "错误码 " + std::to_string(code);
                if (code == 7) { err += "（未认证，Cookie 失效）"; }
            }
            // 正文 part[2]（JSON 字符串）
            cJSON* innerStr = jAt(part, 2);
            if (!innerStr || !cJSON_IsString(innerStr) || !innerStr->valuestring) {
                continue;
            }
            cJSON* inner = cJSON_Parse(innerStr->valuestring);
            if (!inner) { continue; }
            // 会话元数据 inner[1] = [conversation_id, response_id, …]（TODO 索引实测）
            if (convOut || respOut) {
                cJSON* meta = jAt(inner, 1);
                cJSON* cid = jAt(meta, 0);
                cJSON* rid = jAt(meta, 1);
                if (convOut && cid && cJSON_IsString(cid) && cid->valuestring) { *convOut = cid->valuestring; }
                if (respOut && rid && cJSON_IsString(rid) && rid->valuestring) { *respOut = rid->valuestring; }
            }
            cJSON* cands = jAt(inner, 4);
            const int nc = (cands && cJSON_IsArray(cands)) ? cJSON_GetArraySize(cands) : 0;
            for (int ci = 0; ci < nc; ++ci) {
                cJSON* cand = cJSON_GetArrayItem(cands, ci);
                if (!cand || !cJSON_IsArray(cand)) { continue; }
                cJSON* t1 = jAt(cand, 1);
                cJSON* text = jAt(t1, 0);
                std::string s;
                if (text && cJSON_IsString(text) && text->valuestring) {
                    s = text->valuestring;
                }
                const bool card = (s.compare(0, 44,
                        "https://googleusercontent.com/card_content/") == 0)
                        || (s.compare(0, 43, "http://googleusercontent.com/card_content/") == 0);
                if (card) {
                    cJSON* fb = jAt(jAt(cand, 22), 0);
                    if (fb && cJSON_IsString(fb) && fb->valuestring && fb->valuestring[0]) {
                        s = fb->valuestring;
                    }
                }
                if (!s.empty()) { lastText = s; }
            }
            cJSON_Delete(inner);
        }
        cJSON_Delete(fj);
    }

    if (!err.empty()) { return false; }
    if (frames == 0 && !body.empty()) {
        // 无分帧：尝试整段 JSON 兜底
        std::string t = trimStr(body);
        if (!t.empty() && (t[0] == '[' || t[0] == '{')) {
            err = "响应无法分帧（" + briefBody(body) + "）";
            return false;
        }
        err = "响应为空或非 JSON";
        return false;
    }
    lastText = gemStripArtifacts(lastText);
    lastText = trimStr(lastText);
    if (lastText.empty()) {
        err = "回复为空（frames=" + std::to_string(frames) + "）";
        return false;
    }
    out = lastText;
    return true;
}

// gemini 分步回包（step 见 sendGemini）
void gemOnDone(const HttpResponse& resp, Session* s, const AigptbotProvider& p) {
    if (s->step == 0) {
        if (resp.httpCode != 200) {
            webFail(s, p.name, "GET /app HTTP " + std::to_string(resp.httpCode)
                    + (resp.curlErrStr.empty() ? "" : " " + resp.curlErrStr));
            return;
        }
        const std::string at = gemExtractStr(resp.body, "\"SNlM0e\"");
        if (at.empty()) {
            webFail(s, p.name, "未取到 SNlM0e（Cookie 无效或被重定向到同意页）");
            return;
        }
        s->wv["at"] = at;
        std::string lang = gemExtractStr(resp.body, "\"TuX5cc\"");
        if (lang.empty()) { lang = "en"; }
        s->wv["lang"] = lang;
        qWarning("aigptbot: gemini at 获取成功（%d B） lang=%s",
                 (int)at.size(), lang.c_str());
        // 会话复用：命中则带 conversation_id/response_id
        s->sessKey = webSessionKey(p);
        AigptSessionRecord crec;
        if (!s->req.newChat && aigptSessionGet(s->sessKey, crec) && !crec.sessionId.empty()) {
            s->wv["conv"] = crec.sessionId;
            s->wv["rid"]  = crec.parentId;
            qWarning("aigptbot: gemini 复用会话 conv=%s rid=%s",
                     crec.sessionId.c_str(), crec.parentId.c_str());
        } else {
            if (s->req.newChat) {
                aigptSessionInvalidate(s->sessKey);
                qWarning("aigptbot: gemini 用户新对话 → 丢弃旧会话");
            }
            s->wv.erase("conv");
            s->wv.erase("rid");
        }
        ++s->step;
        sendGemini(s, p);
        return;
    }
    if (s->step == 1) {
        if (resp.httpCode != 200) {
            webFail(s, p.name, "StreamGenerate HTTP " + std::to_string(resp.httpCode)
                    + (resp.curlErrStr.empty() ? "" : " " + resp.curlErrStr));
            return;
        }
        std::string out, err;
        std::string conv, rid;
        if (!parseGeminiResponse(resp.body, out, err, &conv, &rid)) {
            if (!resp.curlErrStr.empty()) { err += "（" + resp.curlErrStr + "）"; }
            webFail(s, p.name, err);
            return;
        }
        if (!conv.empty() && !s->sessKey.empty()) {
            AigptSessionRecord rec;
            aigptSessionGet(s->sessKey, rec);
            rec.sessionId = conv;
            rec.parentId  = rid;
            if (rec.createdAt == 0) { rec.createdAt = (long long)time(nullptr); }
            rec.msgCount += 1;
            aigptSessionPut(s->sessKey, rec);
            qWarning("aigptbot: gemini 会话已更新 conv=%s rid=%s n=%lld",
                     conv.c_str(), rid.c_str(), (long long)rec.msgCount);
        }
        webSucceed(s, p.name, out);
        return;
    }
    webFail(s, p.name, "内部错误：未知 step=" + std::to_string(s->step));
}

void sendGemini(Session* s, const AigptbotProvider& p) {
    switch (s->step) {
    case 0: {   // 取 SNlM0e
        std::map<std::string, std::string> h = gemHeaders(false);
        h["Accept"] = "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8";
        sendWebHttp(s, "https://gemini.google.com/app", "GET", "", h, 35, 30);
        break;
    }
    case 1: {   // StreamGenerate（表单 + 长度分帧响应）
        static int g_gemReqid = 0;
        if (g_gemReqid == 0) { g_gemReqid = rand() % 90000 + 10000; }
        const int reqid = g_gemReqid;
        g_gemReqid += 100000;

        std::string prompt = s->req.text;
        if (s->req.brief) {
            prompt = std::string("请尽量用最简洁的方式回答，简短直接，避免冗长。\n\n") + prompt;
        }
        const std::string uuidUp = toUpperAscii(uuidV4());
        const std::string lang = s->wv.count("lang") ? s->wv["lang"] : std::string("en");
        const std::string conv = s->wv.count("conv") ? s->wv["conv"] : std::string();
        const std::string rid  = s->wv.count("rid")  ? s->wv["rid"]  : std::string();
        const std::string inner = gemInnerJson(prompt, lang, uuidUp, conv, rid);
        // 外层 f.req = [null, "<inner JSON 字符串>"]
        const std::string outer = "[null,\"" + jsonEscape(inner) + "\"]";
        std::string form = "at=" + urlEncode(s->wv["at"])
            + "&f.req=" + urlEncode(outer);

        std::map<std::string, std::string> h = gemHeaders(true);
        h["x-goog-ext-525005358-jspb"] = "[\"" + uuidUp + "\",1]";

        char url[512];
        snprintf(url, sizeof(url),
                 "https://gemini.google.com/_/BardChatUi/data/"
                 "assistant.lamda.BardFrontendService/StreamGenerate"
                 "?hl=%s&_reqid=%d&rt=c",
                 urlEncode(lang).c_str(), reqid);
        sendWebHttp(s, url, "POST", form, h, 120, 60);
        break;
    }
    default: {
        webFail(s, p.name, "内部错误：未知 step=" + std::to_string(s->step));
        break;
    }
    }
}
// ── grok-web（协议见 docs/aigptbot-web-sources.md §6.2）──
// x-statsig-id 本地生成挑战（常量齐备时启用）；浏览器指纹头与 Cookie 源一致

// token[70] = header(49B) ‖ u32le(counter) ‖ sha256(msg)[0..16] ‖ trailer(1B)
// msg = "<METHOD>!<PATH>!<counter><SUFFIX>"；整体 XOR 同一随机字节后 base64 无 padding
// counter = unix_now − 1682924400（2023-05-01 UTC）
std::string grokStatsigChallenge(const std::string& method, const std::string& path) {
    const int now = (int)time(nullptr);
    const unsigned int counter = (unsigned int)(now - 1682924400);
    std::string header(49, '\0');
    if (strlen(kGrokChallengeHeaderHex) >= 98) {
        for (size_t i = 0; i < 49; ++i) {
            unsigned int b = 0;
            sscanf(kGrokChallengeHeaderHex + i * 2, "%2x", &b);
            header[i] = (char)(unsigned char)b;
        }
    }
    const std::string msg = method + "!" + path + "!"
                         + std::to_string(counter) + kGrokChallengeSuffix;
    unsigned char d[SHA256_DIGEST_LENGTH];
    {
        SHA2_CTX ctx;
        SHA256Init(&ctx);
        SHA256Update(&ctx, msg.data(), msg.size());
        SHA256Final(d, &ctx);
    }
    unsigned char token[70];
    for (int i = 0; i < 49; ++i) { token[i] = (unsigned char)header[i]; }
    token[49] = (unsigned char)(counter & 0xff);
    token[50] = (unsigned char)((counter >> 8) & 0xff);
    token[51] = (unsigned char)((counter >> 16) & 0xff);
    token[52] = (unsigned char)((counter >> 24) & 0xff);
    for (int i = 0; i < 16; ++i) { token[53 + i] = d[i]; }
    token[69] = (unsigned char)(kGrokChallengeTrailer[0]
                                ? kGrokChallengeTrailer[0] : '3');
    const unsigned char r = (unsigned char)(rand() & 0xff);
    for (int i = 0; i < 70; ++i) { token[i] ^= r; }
    return base64EncodeNoPad(std::string((const char*)token, 70));
}

std::map<std::string, std::string> grokHeaders(const std::string& urlPath) {
    std::map<std::string, std::string> h;
    h["User-Agent"] = webUA(false);
    h["Content-Type"] = "application/json";
    h["Cookie"] = webCredGet("groq_web", kGrokWebCred).value;
    h["Accept"] = "*/*";
    h["Origin"] = "https://grok.com";
    h["Referer"] = "https://grok.com";
    webBrowserHeaders(h);
    h["sec-fetch-dest"] = "empty";
    h["sec-fetch-mode"] = "cors";
    h["sec-fetch-site"] = "same-origin";
    h["x-xai-request-id"] = uuidV4();
    if (kGrokChallengeHeaderHex[0]) { h["x-statsig-id"] = grokStatsigChallenge("POST", urlPath); }
    return h;
}

bool jBool(cJSON* obj, const char* name) {
    if (!obj) { return false; }
    cJSON* n = cJSON_GetObjectItem(obj, name);
    if (!n) { return false; }
    if (cJSON_IsBool(n)) { return cJSON_IsTrue(n) != 0; }
    if (cJSON_IsNumber(n)) { return n->valueint != 0; }
    return false;
}

// 解析 grok NDJSON 全量响应体，聚合正文（thinking token 丢弃）
bool parseGrokNdjson(const std::string& body, std::string& out, std::string& err,
                     std::string* convOut = nullptr) {
    out.clear();
    err.clear();
    int lines = 0;
    size_t pos = 0;
    const size_t n = body.size();
    while (pos <= n) {
        size_t eol = body.find('\n', pos);
        std::string line = (eol == std::string::npos)
                ? body.substr(pos) : body.substr(pos, eol - pos);
        pos = (eol == std::string::npos) ? n + 1 : eol + 1;
        if (!line.empty() && line[line.size() - 1] == '\r') { line.erase(line.size() - 1); }
        line = trimStr(line);
        if (line.empty()) { continue; }
        cJSON* j = cJSON_Parse(line.c_str());
        if (!j) { ++lines; continue; }
        ++lines;
        if (cJSON_IsObject(j)) {
            cJSON* e = cJSON_GetObjectItem(j, "error");
            if (e && cJSON_IsObject(e)) {
                std::string m = jStr(e, "message");
                if (m.empty()) { m = "服务端错误"; }
                err = m;
            }
            cJSON* r = cJSON_GetObjectItem(j, "result");
            if (r && cJSON_IsObject(r) && err.empty()) {
                cJSON* conv = cJSON_GetObjectItem(r, "conversation");
                if (conv && cJSON_IsObject(conv)) {
                    const std::string cid = jStr(conv, "conversationId");
                    if (!cid.empty()) {
                        qWarning("aigptbot: grok conversationId=%s", cid.c_str());
                        if (convOut) { *convOut = cid; }
                    }
                }
                // payload = r.response（首帧）或 r 本身（扁平）
                cJSON* payload = cJSON_GetObjectItem(r, "response");
                if (!payload || !cJSON_IsObject(payload)) { payload = r; }
                const std::string tok = jStr(payload, "token");
                const bool thinking = jBool(payload, "isThinking");
                if (!tok.empty() && !thinking) { out += tok; }
            }
        }
        cJSON_Delete(j);
        if (!err.empty()) { break; }
    }
    if (!err.empty()) { return false; }
    out = trimStr(out);
    if (out.empty()) {
        err = "回复为空（lines=" + std::to_string(lines) + "）";
        return false;
    }
    return true;
}

void grokOnDone(const HttpResponse& resp, Session* s, const AigptbotProvider& p) {
    if (s->step != 0) {
        webFail(s, p.name, "内部错误：未知 step=" + std::to_string(s->step));
        return;
    }
    if (resp.httpCode == 401) {
        webFail(s, p.name, "未认证（Cookie 失效）");
        return;
    }
    if (resp.httpCode == 403) {
        webFail(s, p.name, kGrokChallengeHeaderHex[0]
            ? "被拦截（403）：挑战常量已配置仍被拒——常量可能过期（grok 发版更换）或需 session 绑定，重提取见 docs §6.2"
            : "被拦截（403）：x-statsig-id 未配置——2026-06 起空 statsig 串被 anti-bot 拒，填挑战常量见 docs §6.2");
        return;
    }
    if (resp.httpCode == 429) {
        webFail(s, p.name, "请求过于频繁（429）");
        return;
    }
    if (resp.httpCode != 200) {
        webFail(s, p.name, "conversations/new HTTP " + std::to_string(resp.httpCode)
                + (resp.curlErrStr.empty() ? "" : " " + resp.curlErrStr));
        return;
    }
    std::string out, err;
    std::string conv;
    if (!parseGrokNdjson(resp.body, out, err, &conv)) {
        if (!resp.curlErrStr.empty()) { err += "（" + resp.curlErrStr + "）"; }
        webFail(s, p.name, err);
        return;
    }
    if (!conv.empty() && !s->sessKey.empty()) {
        AigptSessionRecord rec;
        aigptSessionGet(s->sessKey, rec);
        rec.sessionId = conv;
        if (rec.createdAt == 0) { rec.createdAt = (long long)time(nullptr); }
        rec.msgCount += 1;
        aigptSessionPut(s->sessKey, rec);
        qWarning("aigptbot: grok 会话已更新 id=%s n=%lld",
                 conv.c_str(), (long long)rec.msgCount);
    }
    webSucceed(s, p.name, out);
}

void sendGrok(Session* s, const AigptbotProvider& p) {
    if (s->step != 0) {
        webFail(s, p.name, "内部错误：未知 step=" + std::to_string(s->step));
        return;
    }
    // 会话复用：命中则改走 /conversations/{id}/message（TODO 端点/字段实测）
    s->sessKey = webSessionKey(p);
    AigptSessionRecord crec;
    std::string path = "/rest/app-chat/conversations/new";
    if (!s->req.newChat && aigptSessionGet(s->sessKey, crec) && !crec.sessionId.empty()) {
        path = "/rest/app-chat/conversations/" + crec.sessionId + "/message";
        s->wv["conv"] = crec.sessionId;
        qWarning("aigptbot: grok 复用会话 id=%s", crec.sessionId.c_str());
    } else if (s->req.newChat) {
        aigptSessionInvalidate(s->sessKey);
        qWarning("aigptbot: grok 用户新对话 → 丢弃旧会话");
    }
    std::string body = "{\"message\":\"" + jsonEscape(s->req.text) + "\"";
    if (s->req.brief) { body += ",\"forceConcise\":true"; }
    body += "}";
    sendWebHttp(s, "https://grok.com" + path,
                "POST", body, grokHeaders(path), 180, 60);
}
// ── chatgpt-web（实验性，协议见 docs/aigptbot-web-sources.md §6.4）──
// prepare → 本地 PoW → finalize（Turnstile/so 本地不可解，预期失败并明确报错）

std::map<std::string, std::string> cgptHeaders(Session* s) {
    std::map<std::string, std::string> h;
    h["Authorization"] = std::string("Bearer ") + webCredGet("chatgpt_web", kChatgptWebCred).value;
    h["Content-Type"] = "application/json";
    h["Accept"] = "*/*";
    h["User-Agent"] = webUA(false);
    webBrowserHeaders(h);
    h["Origin"] = "https://chatgpt.com";
    h["Referer"] = "https://chatgpt.com/";
    if (s && s->wv.count("sentinel") && !s->wv["sentinel"].empty()) {
        h["x-openai-sentinel-chat-requirements-token"] = s->wv["sentinel"];
    }
    return h;
}

// PoW：sha256(seed ASCII + 十进制 counter)，前 3 字节（大端 uint24）< parseInt(difficulty,16)
bool cgptSolvePow(const std::string& seed, const std::string& difficultyHex,
                  long long& answerOut) {
    if (seed.empty() || difficultyHex.empty()) { return false; }
    const long long target = strtoll(difficultyHex.c_str(), nullptr, 16);
    if (target <= 0) { return false; }
    SHA2_CTX ctx;
    uint8_t d[SHA256_DIGEST_LENGTH];
    for (long long i = 0; i < 10000000LL; ++i) {
        const std::string msg = seed + std::to_string(i);
        SHA256Init(&ctx);
        SHA256Update(&ctx, msg.data(), msg.size());
        SHA256Final(d, &ctx);
        const unsigned int u24 = ((unsigned int)d[0] << 16)
                               | ((unsigned int)d[1] << 8)
                               | (unsigned int)d[2];
        if ((long long)u24 < target) {
            answerOut = i;
            return true;
        }
    }
    return false;
}

// 解析 f/conversation 的 OpenAI 风格 SSE（data: {"v":{...}}，取最后非空累计正文）
bool parseCgptSse(const std::string& body, std::string& out, std::string& err,
                  std::string* convOut = nullptr, std::string* msgOut = nullptr) {
    out.clear();
    err.clear();
    std::string last;
    size_t pos = 0;
    const size_t n = body.size();
    while (pos <= n) {
        size_t eol = body.find('\n', pos);
        std::string line = (eol == std::string::npos)
                ? body.substr(pos) : body.substr(pos, eol - pos);
        pos = (eol == std::string::npos) ? n + 1 : eol + 1;
        if (!line.empty() && line[line.size() - 1] == '\r') { line.erase(line.size() - 1); }
        if (line.compare(0, 5, "data:") != 0) { continue; }
        const std::string data = trimStr(line.substr(5));
        if (data.empty() || data == "[DONE]") { continue; }
        cJSON* j = cJSON_Parse(data.c_str());
        if (!j) { continue; }
        if (cJSON_IsObject(j)) {
            cJSON* e = cJSON_GetObjectItem(j, "error");
            if (e && cJSON_IsObject(e)) {
                std::string m = jStr(e, "message");
                if (m.empty()) { m = "服务端错误"; }
                err = m;
            } else {
                cJSON* v = cJSON_GetObjectItem(j, "v");
                if (v && cJSON_IsObject(v)) {
                    if (convOut) {
                        const std::string c = jStr(v, "conversation_id");
                        if (!c.empty()) { *convOut = c; }
                    }
                    cJSON* msg = cJSON_GetObjectItem(v, "message");
                    if (msg && cJSON_IsObject(msg)) {
                        if (msgOut) {
                            const std::string mid = jStr(msg, "id");
                            if (!mid.empty()) { *msgOut = mid; }
                        }
                        cJSON* content = cJSON_GetObjectItem(msg, "content");
                        if (content && cJSON_IsObject(content)) {
                            cJSON* parts = cJSON_GetObjectItem(content, "parts");
                            if (parts && cJSON_IsArray(parts)) {
                                std::string s;
                                for (int k = 0; k < cJSON_GetArraySize(parts); ++k) {
                                    cJSON* pt = cJSON_GetArrayItem(parts, k);
                                    if (pt && cJSON_IsString(pt) && pt->valuestring) {
                                        s += pt->valuestring;
                                    }
                                }
                                if (!s.empty()) { last = s; }
                            }
                        }
                    }
                }
            }
        }
        cJSON_Delete(j);
        if (!err.empty()) { break; }
    }
    if (!err.empty()) { return false; }
    out = trimStr(last);
    if (out.empty()) {
        err = "回复为空";
        return false;
    }
    return true;
}

void cgptOnDone(const HttpResponse& resp, Session* s, const AigptbotProvider& p) {
    if (s->step == 0) {
        // prepare
        s->sessKey = webSessionKey(p);
        if (s->req.newChat) {
            aigptSessionInvalidate(s->sessKey);
            qWarning("aigptbot: chatgpt 用户新对话 → 丢弃旧会话");
        }
        if (resp.httpCode == 401 || resp.httpCode == 403) {
            webFail(s, p.name, "prepare 被拒（HTTP " + std::to_string(resp.httpCode)
                    + "，accessToken 无效？）");
            return;
        }
        if (resp.httpCode != 200) {
            webFail(s, p.name, "prepare HTTP " + std::to_string(resp.httpCode)
                    + (resp.curlErrStr.empty() ? "" : " " + resp.curlErrStr));
            return;
        }
        cJSON* root = cJSON_Parse(trimStr(resp.body).c_str());
        if (!root) {
            webFail(s, p.name, "prepare 响应非 JSON（" + briefBody(resp.body) + "）");
            return;
        }
        const std::string ptoken = jStr(root, "prepare_token");
        cJSON* pow = cJSON_GetObjectItem(root, "proofofwork");
        const std::string seed = jStr(pow, "seed");
        const std::string diff = jStr(pow, "difficulty");
        cJSON* so = cJSON_GetObjectItem(root, "so");
        s->wv["cdx"] = jStr(so, "collector_dx");
        s->wv["sdx"] = jStr(so, "snapshot_dx");
        cJSON_Delete(root);
        if (ptoken.empty() || seed.empty() || diff.empty()) {
            webFail(s, p.name, "prepare 缺字段（ptoken="
                    + std::to_string((int)ptoken.size()) + " seed="
                    + std::to_string((int)seed.size()) + "）");
            return;
        }
        s->wv["ptoken"] = ptoken;
        s->wv["seed"] = seed;
        s->wv["difficulty"] = diff;
        long long answer = -1;
        if (!cgptSolvePow(seed, diff, answer)) {
            webFail(s, p.name, "PoW 求解失败（difficulty=" + diff + "）");
            return;
        }
        s->wv["pow_answer"] = std::to_string(answer);
        qWarning("aigptbot: chatgpt prepare ok, PoW answer=%lld difficulty=%s",
                 answer, diff.c_str());
        ++s->step;
        sendChatgpt(s, p);
        return;
    }
    if (s->step == 1) {
        // finalize：Turnstile/so 本地不可解，预期失败
        const std::string detail = briefBody(resp.body);
        if (resp.httpCode != 200) {
            webFail(s, p.name, "ChatGPT 被 Turnstile/so 拦截（实验性，finalize HTTP "
                    + std::to_string(resp.httpCode) + "）：" + detail);
            return;
        }
        cJSON* root = cJSON_Parse(trimStr(resp.body).c_str());
        if (!root) {
            webFail(s, p.name, "finalize 响应非 JSON（" + detail + "）");
            return;
        }
        cJSON* e = cJSON_GetObjectItem(root, "error");
        cJSON* d = cJSON_GetObjectItem(root, "detail");
        const bool failed = (e && !cJSON_IsNull(e)) || (d && !cJSON_IsNull(d));
        if (failed) {
            std::string m;
            if (d && cJSON_IsString(d)) { m = d->valuestring; }
            else if (e && cJSON_IsString(e)) { m = e->valuestring; }
            cJSON_Delete(root);
            webFail(s, p.name, "ChatGPT 被 Turnstile/so 拦截（实验性）：" + m);
            return;
        }
        // 意外成功：尝试取 sentinel token（字段名 TODO 实测）
        const char* keys[] = {"token", "sentinel_token", "chat_requirements_token",
                              "requirements_token"};
        for (int i = 0; i < 4; ++i) {
            const std::string t = jStr(root, keys[i]);
            if (!t.empty()) { s->wv["sentinel"] = t; break; }
        }
        char* fin = cJSON_PrintUnformatted(root);
        if (fin) {
            s->wv["fin"] = fin;
            free(fin);
        }
        cJSON_Delete(root);
        qWarning("aigptbot: chatgpt finalize 意外成功（sentinel=%d B），继续 f/conversation",
                 (int)s->wv["sentinel"].size());
        ++s->step;
        sendChatgpt(s, p);
        return;
    }
    if (s->step == 2) {
        // f/conversation（body/SSE 字段名 TODO 实测）
        if (resp.httpCode == 403) {
            webFail(s, p.name, "f/conversation 403（sentinel 未通过，实验性）");
            return;
        }
        if (resp.httpCode != 200) {
            webFail(s, p.name, "f/conversation HTTP " + std::to_string(resp.httpCode)
                    + (resp.curlErrStr.empty() ? "" : " " + resp.curlErrStr));
            return;
        }
        std::string out, err;
        std::string conv, mid;
        if (!parseCgptSse(resp.body, out, err, &conv, &mid)) {
            if (!resp.curlErrStr.empty()) { err += "（" + resp.curlErrStr + "）"; }
            webFail(s, p.name, err);
            return;
        }
        if (!s->sessKey.empty()) {
            AigptSessionRecord rec;
            aigptSessionGet(s->sessKey, rec);
            if (!conv.empty()) { rec.sessionId = conv; }
            if (!mid.empty()) { rec.parentId = mid; }
            if (rec.createdAt == 0) { rec.createdAt = (long long)time(nullptr); }
            rec.msgCount += 1;
            aigptSessionPut(s->sessKey, rec);
            qWarning("aigptbot: chatgpt 会话已更新 conv=%s parent=%s n=%lld",
                     rec.sessionId.c_str(), rec.parentId.c_str(), (long long)rec.msgCount);
        }
        webSucceed(s, p.name, out);
        return;
    }
    webFail(s, p.name, "内部错误：未知 step=" + std::to_string(s->step));
}

void sendChatgpt(Session* s, const AigptbotProvider& p) {
    switch (s->step) {
    case 0: {   // sentinel prepare
        std::map<std::string, std::string> h = cgptHeaders(s);
        sendWebHttp(s, "https://chatgpt.com/backend-api/sentinel/chat-requirements/prepare",
                    "POST", "{}", h, 35, 30);
        break;
    }
    case 1: {   // finalize（预期失败）
        std::map<std::string, std::string> h = cgptHeaders(s);
        std::string body = "{\"prepare_token\":\"" + jsonEscape(s->wv["ptoken"])
            + "\",\"turnstile\":\"\","
              "\"proofofwork\":{\"seed\":\"" + jsonEscape(s->wv["seed"])
            + "\",\"difficulty\":\"" + jsonEscape(s->wv["difficulty"])
            + "\",\"answer\":\"" + jsonEscape(s->wv["pow_answer"])
            + "\"},\"so\":{\"collector_dx\":\"" + jsonEscape(s->wv["cdx"])
            + "\",\"snapshot_dx\":\"" + jsonEscape(s->wv["sdx"]) + "\"}}";
        sendWebHttp(s, "https://chatgpt.com/backend-api/sentinel/chat-requirements/finalize",
                    "POST", body, h, 35, 30);
        break;
    }
    case 2: {   // f/conversation（TODO 实测）
        std::map<std::string, std::string> h = cgptHeaders(s);
        std::string prompt = s->req.text;
        if (s->req.brief) {
            prompt = std::string("请尽量用最简洁的方式回答，简短直接，避免冗长。\n\n") + prompt;
        }
        std::string convField, parentVal("null");
        AigptSessionRecord crec;
        if (!s->req.newChat && aigptSessionGet(s->sessKey, crec) && !crec.sessionId.empty()) {
            convField = "\"conversation_id\":\"" + jsonEscape(crec.sessionId) + "\",";
            if (!crec.parentId.empty()) { parentVal = "\"" + jsonEscape(crec.parentId) + "\""; }
        }
        std::string body = "{\"action\":\"next\",\"messages\":[{\"id\":\"" + uuidV4()
            + "\",\"author\":{\"role\":\"user\"},"
              "\"content\":{\"content_type\":\"text\",\"parts\":[\""
            + jsonEscape(prompt)
            + "\"]},\"content_type\":\"text\"}],\"model\":\"auto\","
            + convField
            + "\"parent_message_id\":" + parentVal + ",\"timezone\":\"Asia/Shanghai\","
              "\"variant\":null,\"suggested_replies\":false}";
        sendWebHttp(s, "https://chatgpt.com/backend-api/f/conversation",
                    "POST", body, h, 180, 60);
        break;
    }
    default: {
        webFail(s, p.name, "内部错误：未知 step=" + std::to_string(s->step));
        break;
    }
    }
}

// ── meta-web（实验性；协议见 docs/aigptbot-web-sources.md §6.6）──
// 取页(lsd/datr) → useAbraAcceptTOSForTempUserMutation 取 token →
// useAbraSendMessageMutation。Meta 已把聊天迁到 DGW WebSocket（消息完整性校验，手工
// 无法构造），本实现走旧 HTTP GraphQL，随时可能整体失效 → 失败给明确报错并降级。

std::string metaExtract(const std::string& text, const char* start, const char* end) {
    const size_t p = text.find(start);
    if (p == std::string::npos) { return std::string(); }
    const size_t b = p + std::string(start).size();
    const size_t e = text.find(end, b);
    if (e == std::string::npos) { return std::string(); }
    return text.substr(b, e - b);
}

std::string metaOfflineThreadingId() {
    const unsigned long long ms = (unsigned long long)time(nullptr) * 1000ULL;
    const unsigned long long rnd =
            ((unsigned long long)rand() << 16 ^ (unsigned long long)rand()) & 0x3FFFFFULL;
    return std::to_string((ms << 22) | rnd);
}

// 配置 cookie（meta 字段）与页面提取 cookie 合并
std::string metaCookie(Session* s) {
    std::string c = webCredGet("meta_web", kMetaWebCred).value;
    if (!s) { return c; }
    const char* keys[] = { "_js_datr", "datr", "abra_csrf" };
    for (int i = 0; i < 3; ++i) {
        const std::map<std::string, std::string>::iterator it = s->wv.find(keys[i]);
        if (it == s->wv.end() || it->second.empty()) { continue; }
        if (c.find(std::string(keys[i]) + "=") != std::string::npos) { continue; }
        if (!c.empty()) { c += "; "; }
        c += std::string(keys[i]) + "=" + it->second;
    }
    return c;
}

// 解析 meta NDJSON：取最后一条含 composed_text 的 bot_response_message 正文
bool parseMetaNdjson(const std::string& body, std::string& out, std::string& err) {
    out.clear();
    err.clear();
    int lines = 0;
    size_t pos = 0;
    const size_t n = body.size();
    while (pos <= n) {
        size_t eol = body.find('\n', pos);
        std::string line = (eol == std::string::npos)
                ? body.substr(pos) : body.substr(pos, eol - pos);
        pos = (eol == std::string::npos) ? n + 1 : eol + 1;
        if (!line.empty() && line[line.size() - 1] == '\r') { line.erase(line.size() - 1); }
        line = trimStr(line);
        if (line.empty()) { continue; }
        cJSON* j = cJSON_Parse(line.c_str());
        if (!j) { ++lines; continue; }
        ++lines;
        if (cJSON_IsObject(j)) {
            cJSON* e = cJSON_GetObjectItem(j, "errors");
            if (e && cJSON_IsArray(e) && cJSON_GetArraySize(e) > 0) {
                cJSON* first = cJSON_GetArrayItem(e, 0);
                std::string m = first ? jStr(first, "message") : std::string();
                if (m.empty()) { m = "GraphQL 错误"; }
                err = m;
            }
            cJSON* d = cJSON_GetObjectItem(j, "data");
            cJSON* node = d ? cJSON_GetObjectItem(d, "node") : 0;
            cJSON* bot = node ? cJSON_GetObjectItem(node, "bot_response_message") : 0;
            if (bot && cJSON_IsObject(bot)) {
                std::string txt;
                cJSON* ct = cJSON_GetObjectItem(bot, "composed_text");
                cJSON* content = ct ? cJSON_GetObjectItem(ct, "content") : 0;
                if (content && cJSON_IsArray(content)) {
                    for (int i = 0; i < cJSON_GetArraySize(content); ++i) {
                        cJSON* item = cJSON_GetArrayItem(content, i);
                        const std::string t = item ? jStr(item, "text") : std::string();
                        if (!t.empty()) { txt += t + "\n"; }
                    }
                }
                if (!txt.empty()) { out = txt; }
            }
        }
        cJSON_Delete(j);
        if (!err.empty()) { break; }
    }
    if (!err.empty()) { return false; }
    out = trimStr(out);
    if (out.empty()) {
        err = "回复为空（lines=" + std::to_string(lines)
             + "；协议可能已迁 DGW，见 docs §6.6）";
        return false;
    }
    return true;
}

void sendMetaWeb(Session* s, const AigptbotProvider& p) {
    if (s->step == 0) {
        std::map<std::string, std::string> h;
        h["User-Agent"] = webUA(false);
        h["Accept"] = "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8";
        webBrowserHeaders(h);
        const std::string ck = webCredGet("meta_web", kMetaWebCred).value;
        if (!ck.empty()) { h["Cookie"] = ck; }
        sendWebHttp(s, "https://www.meta.ai/", "GET", "", h, 35, 30);
        return;
    }
    if (s->step == 1) {
        const std::string variables =
            "{\"dob\":\"1999-01-01\",\"icebreaker_type\":\"TEXT\","
            "\"__relay_internal__pv__WebPixelRatiorelayprovider\":1}";
        std::string form = "lsd=" + urlEncode(s->wv["lsd"])
            + "&fb_api_caller_class=RelayModern"
              "&fb_api_req_friendly_name=useAbraAcceptTOSForTempUserMutation"
              "&doc_id=7604648749596940"
            + "&variables=" + urlEncode(variables);
        std::map<std::string, std::string> h;
        h["User-Agent"] = webUA(false);
        h["Accept"] = "*/*";
        h["Origin"] = "https://www.meta.ai";
        h["Referer"] = "https://www.meta.ai/";
        h["Cookie"] = metaCookie(s);
        h["Content-Type"] = "application/x-www-form-urlencoded;charset=utf-8";
        h["x-fb-friendly-name"] = "useAbraAcceptTOSForTempUserMutation";
        h["sec-fetch-mode"] = "cors";
        h["sec-fetch-site"] = "same-origin";
        sendWebHttp(s, "https://www.meta.ai/api/graphql/", "POST", form, h, 35, 30);
        return;
    }
    if (s->step == 2) {
        std::string prompt = s->req.text;
        if (s->req.brief) {
            prompt = std::string("请尽量用最简洁的方式回答，简短直接，避免冗长。\n\n") + prompt;
        }
        const std::string variables =
            "{\"message\":{\"sensitive_string_value\":\"" + jsonEscape(prompt) + "\"},"
            "\"externalConversationId\":\"" + s->wv["conv"] + "\","
            "\"offlineThreadingId\":\"" + metaOfflineThreadingId() + "\","
            "\"suggestedPromptIndex\":null,\"promptPrefix\":null,"
            "\"entrypoint\":\"ABRA__CHAT__TEXT\",\"icebreaker_type\":\"TEXT\","
            "\"__relay_internal__pv__AbraDebugDevOnlyrelayprovider\":false,"
            "\"__relay_internal__pv__WebPixelRatiorelayprovider\":1}";
        std::string form = "access_token=" + urlEncode(s->wv["token"])
            + "&fb_api_caller_class=RelayModern"
              "&fb_api_req_friendly_name=useAbraSendMessageMutation"
              "&doc_id=7783822248314888&server_timestamps=true"
            + "&variables=" + urlEncode(variables);
        std::map<std::string, std::string> h;
        h["User-Agent"] = webUA(false);
        h["Accept"] = "*/*";
        h["Origin"] = "https://www.meta.ai";
        h["Referer"] = "https://www.meta.ai/";
        h["Cookie"] = metaCookie(s);
        h["Content-Type"] = "application/x-www-form-urlencoded;charset=utf-8";
        h["x-fb-friendly-name"] = "useAbraSendMessageMutation";
        h["sec-fetch-mode"] = "cors";
        h["sec-fetch-site"] = "same-origin";
        sendWebHttp(s, "https://graph.meta.ai/graphql?locale=user", "POST", form, h, 120, 60);
        return;
    }
    webFail(s, p.name, "内部错误：未知 step=" + std::to_string(s->step));
}

void metaOnDone(const HttpResponse& resp, Session* s, const AigptbotProvider& p) {
    if (s->step == 0) {
        if (resp.httpCode != 200) {
            webFail(s, p.name, "GET www.meta.ai HTTP " + std::to_string(resp.httpCode)
                    + (resp.curlErrStr.empty() ? "" : " " + resp.curlErrStr));
            return;
        }
        const std::string lsd   = metaExtract(resp.body, "\"LSD\",[],{\"token\":\"", "\"");
        const std::string datr  = metaExtract(resp.body, "datr\":{\"value\":\"", "\"");
        const std::string jsDatr = metaExtract(resp.body, "_js_datr\":{\"value\":\"", "\"");
        const std::string csrf  = metaExtract(resp.body, "abra_csrf\":{\"value\":\"", "\"");
        if (lsd.empty()) {
            webFail(s, p.name, "页面解析失败（lsd 为空：地区受限或页面改版）");
            return;
        }
        s->wv["lsd"] = lsd;
        if (!datr.empty())   { s->wv["datr"] = datr; }
        if (!jsDatr.empty()) { s->wv["_js_datr"] = jsDatr; }
        if (!csrf.empty())   { s->wv["abra_csrf"] = csrf; }
        // 会话复用：命中则复用 externalConversationId
        s->sessKey = webSessionKey(p);
        AigptSessionRecord crec;
        if (!s->req.newChat && aigptSessionGet(s->sessKey, crec) && !crec.sessionId.empty()) {
            s->wv["conv"] = crec.sessionId;
            qWarning("aigptbot: meta-web 复用会话 conv=%s", crec.sessionId.c_str());
        } else {
            if (s->req.newChat) {
                aigptSessionInvalidate(s->sessKey);
                qWarning("aigptbot: meta-web 用户新对话 → 丢弃旧会话");
            }
            s->wv["conv"] = uuidV4();
        }
        ++s->step;
        sendMetaWeb(s, p);
        return;
    }
    if (s->step == 1) {
        if (resp.httpCode != 200) {
            webFail(s, p.name, "graphql/token HTTP " + std::to_string(resp.httpCode));
            return;
        }
        std::string token;
        std::string gerr;
        cJSON* root = cJSON_Parse(resp.body.c_str());
        if (root && cJSON_IsObject(root)) {
            cJSON* d = cJSON_GetObjectItem(root, "data");
            cJSON* mut = d ? cJSON_GetObjectItem(d, "xab_abra_accept_terms_of_service") : 0;
            cJSON* nu = mut ? cJSON_GetObjectItem(mut, "new_temp_user_auth") : 0;
            token = (nu && cJSON_IsObject(nu)) ? jStr(nu, "access_token") : std::string();
            if (token.empty()) {
                cJSON* e = cJSON_GetObjectItem(root, "errors");
                cJSON* first = (e && cJSON_IsArray(e) && cJSON_GetArraySize(e) > 0)
                        ? cJSON_GetArrayItem(e, 0) : 0;
                gerr = first ? jStr(first, "message") : std::string();
            }
            cJSON_Delete(root);
        }
        if (token.empty()) {
            webFail(s, p.name, "取 access_token 失败"
                    + (gerr.empty() ? std::string() : std::string("（") + gerr + "）")
                    + "：旧 GraphQL 可能已失效，见 docs §6.6");
            return;
        }
        s->wv["token"] = token;
        ++s->step;
        sendMetaWeb(s, p);
        return;
    }
    if (s->step == 2) {
        if (resp.httpCode != 200) {
            webFail(s, p.name, "sendMessage HTTP " + std::to_string(resp.httpCode));
            return;
        }
        std::string out, err;
        if (!parseMetaNdjson(resp.body, out, err)) {
            webFail(s, p.name, err.empty() ? std::string("回复为空") : err);
            return;
        }
        if (!s->sessKey.empty() && !s->wv["conv"].empty()) {
            AigptSessionRecord rec;
            aigptSessionGet(s->sessKey, rec);
            rec.sessionId = s->wv["conv"];
            if (rec.createdAt == 0) { rec.createdAt = (long long)time(nullptr); }
            rec.msgCount += 1;
            aigptSessionPut(s->sessKey, rec);
            qWarning("aigptbot: meta-web 会话已更新 conv=%s n=%lld",
                     rec.sessionId.c_str(), (long long)rec.msgCount);
        }
        webSucceed(s, p.name, out);
        return;
    }
    webFail(s, p.name, "内部错误：未知 step=" + std::to_string(s->step));
}

void sendWebHost(Session* s, const AigptbotProvider& p) {
    s->step = 0;
    s->wv.clear();
    switch (p.webKind) {
    case kAigptbotWebDeepseek: { sendDeepseek(s, p); break; }
    case kAigptbotWebGemini:   { sendGemini(s, p); break; }
    case kAigptbotWebGrok:     { sendGrok(s, p); break; }
    case kAigptbotWebChatgpt:  { sendChatgpt(s, p); break; }
    case kAigptbotWebMeta:     { sendMetaWeb(s, p); break; }
    case kAigptbotWebNone:     { webFail(s, p.name, "内部错误：webKind 为空"); break; }
    }
}

void onWebDone(const HttpResponse& resp, void* udata) {
    Session* s = static_cast<Session*>(udata);
    if (!s) { return; }
    if (!isCurrentSeq(s)) {
        qWarning("aigptbot: 迟到 web 回包丢弃 seq=%u", (unsigned int)s->seq);
        removeSession(s);
        return;
    }
    if (s->index < 0 || s->index >= (int)s->cands.size()) {
        removeSession(s);
        return;
    }
    const AigptbotProvider* h = s->cands[s->index];
    if (!h) {
        removeSession(s);
        return;
    }
    qWarning("aigptbot: web resp provider=%s webKind=%d step=%d http=%d curl=[%s] body=[%s]",
             h->name, (int)h->webKind, s->step, resp.httpCode,
             resp.curlErrStr.c_str(), briefBody(resp.body).c_str());
    switch (h->webKind) {
    case kAigptbotWebDeepseek: { dsOnDone(resp, s, *h); break; }
    case kAigptbotWebGemini:   { gemOnDone(resp, s, *h); break; }
    case kAigptbotWebGrok:     { grokOnDone(resp, s, *h); break; }
    case kAigptbotWebChatgpt:  { cgptOnDone(resp, s, *h); break; }
    case kAigptbotWebMeta:     { metaOnDone(resp, s, *h); break; }
    default: {
        webFail(s, h->name, "内部错误：该 webKind 尚无状态机（step="
                + std::to_string(s->step) + "）");
        break;
    }
    }
}

void onDone(const HttpResponse& resp, void* udata) {
    Session* s = static_cast<Session*>(udata);
    if (!s) { return; }
    if (!isCurrentSeq(s)) {
        qWarning("aigptbot: 迟到回包丢弃 seq=%u", (unsigned int)s->seq);
        removeSession(s);
        return;
    }
    if (s->index < 0 || s->index >= (int)s->cands.size()) {
        removeSession(s);
        return;
    }
    const AigptbotProvider* h = s->cands[s->index];
    std::string apiErr;
    qWarning("aigptbot: resp provider=%s http=%d curl=[%s] body=[%s]",
             h ? h->name : "?", resp.httpCode,
             resp.curlErrStr.c_str(), briefBody(resp.body).c_str());
    if (resp.httpCode >= 200 && resp.httpCode < 300) {
        const std::string content = parseOpenAiContent(resp.body, apiErr);
        if (!content.empty()) {
            qWarning("aigptbot: provider=%s 成功 textlen=%d B",
                     h ? h->name : "?", (int)content.size());
            postResult(s, true, content, h ? h->name : std::string(), std::string());
            return;
        }
    }
    // 失败
    std::string lastErr;
    if (h) { lastErr += h->name; } else { lastErr += "provider"; }
    lastErr += ": HTTP " + std::to_string(resp.httpCode);
    if (!resp.curlErrStr.empty()) { lastErr += " " + resp.curlErrStr; }
    if (resp.httpCode >= 200 && resp.httpCode < 300 && !resp.curlErrStr.empty()) {
        lastErr += "（收到 HTTP " + std::to_string(resp.httpCode) + " 但响应体不完整）";
    }
    if (!apiErr.empty()) { lastErr += "（" + apiErr + "）"; }
    s->lastError = lastErr;
    qWarning("aigptbot: provider=%s 失败: %s → 切换候选", h ? h->name : "?", lastErr.c_str());
    ++s->index;
    startNext(s);
}

std::vector<const AigptbotProvider*> buildCandidates(const AigptbotRequest& req,
                                                   std::string& reason) {
    std::vector<const AigptbotProvider*> out;
    std::string prov = req.provider;
    // 空串视为 any
    if (prov.empty()) { prov = "any"; }
    if (prov == "all") {
        for (int i = 0; i < kAigptProviderCount; ++i) {
            const AigptbotProvider& p = kAigptProviders[i];
            if (p.id == kAigptbotAiHorde) { continue; } // any/all 跳过
            if (p.webKind != kAigptbotWebNone) {
                // web：凭据为空 → 视为未配置，跳过
                const WebCredResult cr = webCredResult(p);
                if (!cr.ok) {
                    qWarning("aigptbot: skip provider=%s cred=%s", p.name, webCredTag(cr.status));
                    continue;
                }
            } else if (p.needsKey) {
                if (!nonWebKeyReady(p)) { continue; } // 跳过未配置（读侧车 <slug>_api）
            }
            out.push_back(&p);
        }
        if (out.empty()) {
            reason = "any/all：无可用候选（未配置 key 或仅含暂不支持项）";
        }
        return out;
    }
    if (prov == "any") {
        for (int i = 0; i < kAigptProviderCount; ++i) {
            const AigptbotProvider& p = kAigptProviders[i];
            if (p.id == kAigptbotAiHorde) { continue; } // any/all 跳过
            if (p.webKind != kAigptbotWebNone) {
                // web：凭据为空 → 视为未配置，跳过
                const WebCredResult cr = webCredResult(p);
                if (!cr.ok) {
                    qWarning("aigptbot: skip provider=%s cred=%s", p.name, webCredTag(cr.status));
                    continue;
                }
            } else if (p.needsKey) {
                if (!nonWebKeyReady(p)) { continue; } // 跳过未配置（读侧车 <slug>_api）
            }
            out.push_back(&p);
        }
        if (!out.empty()) {
            randomShuffle(out.begin(), out.end());
        } else {
            reason = "any/all：无可用候选（未配置 key 或仅含暂不支持项）";
        }
        return out;
    }
    // 指定
    for (int i = 0; i < kAigptProviderCount; ++i) {
        const AigptbotProvider& p = kAigptProviders[i];
        if (prov == p.name) {
            // 指定：不跳过（包括 AI Horde、needsKey）
            out.push_back(&p);
            return out;
        }
    }
    reason = "未知服务：" + prov;
    return out;
}

} // namespace

const std::vector<AigptbotProvider>& aigptbotProviders() {
    static std::vector<AigptbotProvider> list(
        kAigptProviders, kAigptProviders + kAigptProviderCount);
    return list;
}

std::string aigptbotWebCredStatusName(const char* providerName) {
    if (!providerName || !providerName[0]) { return std::string(); }
    for (int i = 0; i < kAigptProviderCount; ++i) {
        const AigptbotProvider& p = kAigptProviders[i];
        if (std::string(p.name) != providerName) { continue; }
        if (p.webKind != kAigptbotWebNone) {
            return std::string(webCredStatusName(webCredResult(p).status));
        }
        const char* kf = nonWebCredField(p);
        if (kf && kf[0]) {
            return std::string(webCredStatusName(webCredGet(kf, "").status));
        }
        return std::string();
    }
    return std::string();
}

void aigptbotChatStart(const AigptbotRequest& req, QObject* target) {
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
    for (size_t i = 0; i < s->cands.size(); ++i) {
        if (i) { order += " → "; }
        order += s->cands[i]->name;
    }
    qWarning("aigptbot: provider=%s model=%s 候选 %d 个: %s",
             req.provider.empty() ? "any" : req.provider.c_str(),
             req.model.empty() ? "(默认)" : req.model.c_str(),
             (int)s->cands.size(), order.c_str());
    startNext(s);
}
