#ifndef AIGPTBOT_H
#define AIGPTBOT_H

#include <string>
#include <vector>

class QObject;

// AI 对话 provider（表内顺序 = provider="all" 的固定尝试序）
// 端点参考 ../anystik/stikcommon/imageaiutil.cpp
enum AigptbotProviderId {
    kAigptbotPollinations = 0,
    kAigptbotPollinationsText,
    kAigptbotZhipu,
    kAigptbotSiliconFlow,
    kAigptbotNvidia,
    kAigptbotOpenRouter,
    kAigptbotBlockRun,
    kAigptbotLlm7,
    kAigptbotCloudflare,
    kAigptbotDashScope,
    kAigptbotOvh,
    kAigptbotVolcengine,
    kAigptbotModelScope,
    kAigptbotModelScopeIntl,
    kAigptbotGroq,
    kAigptbotHuggingFace,
    kAigptbotGemini,
    kAigptbotOllama,
    kAigptbotZai,
    kAigptbotGroqViaCf,
    kAigptbotGeminiViaCf,
    kAigptbotAiHorde,
    kAigptbotMetaApi,             // meta 官方 Model API（OpenAI 兼容，key=meta_api）
    // web 版直连（凭据读加密侧车 webcreds，见 docs/aigptbot-web-sources.md §3.0）
    kAigptbotDeepseekWeb,
    kAigptbotGeminiWeb,
    kAigptbotGrokWeb,
    kAigptbotChatgptWeb,       // 实验性（sentinel Turnstile 可能被拦）
    kAigptbotMetaWeb,          // meta-web 实验性（旧 HTTP GraphQL，随时可能失效）
};

// web provider 内部协议分支（=0 走 OpenAI 兼容 sendHost）
enum AigptbotWebKind {
    kAigptbotWebNone = 0,
    kAigptbotWebDeepseek,
    kAigptbotWebGemini,
    kAigptbotWebGrok,
    kAigptbotWebChatgpt,
    kAigptbotWebMeta,
};

// 单个 provider 的静态契约
struct AigptbotProvider {
    AigptbotProviderId id;
    const char* name;           // 属性值（用于 provider 下拉、精确匹配、日志）—— 原样拷贝自 imageaiutil
    bool needsKey;              // true 表示“未配置 key”时视为不满足候选条件（any/all 跳过）
    bool allowEmptyKey;         // true 表示不发送 Authorization 头（BlockRun/OVH/Ollama）
    const char* baseUrl;        // 完整 chat/completions URL（CF Gateway 含模板化 %1/%2）
    const char* modelDefault;   // 文本默认模型名（可为空字符串）
    AigptbotWebKind webKind;    // 末位：非 None 时走 sendWebHost 分派（聚合初始化省略=0，存量表行不动）
    bool needsProxy;            // 末位：true=该 provider 需经用户配置的代理访问（境外站点）
};

const std::vector<AigptbotProvider>& aigptbotProviders();

// 返回 provider 名对应的 web 凭据状态名（非 web provider / 未知名称返回空串）
std::string aigptbotWebCredStatusName(const char* providerName);

struct AigptbotRequest {
    std::string text;      // 待发送文本（UTF-8 原样）
    std::string provider;  // "any" | "all" | <name>（空串视为 "any"）
    std::string model;     // UI 传入，空串表示“使用服务商默认模型”
    bool brief = false;    // 简洁回复：附加简洁指令并限制 max_tokens
    bool thinking = false; // 深度思考（thinking_enabled；仅 deepseek-web 等支持）
    bool search = false;   // 联网搜索（search_enabled；仅 deepseek-web 等支持）
    bool newChat = false;  // 手动「新对话」：丢弃缓存会话，强制新建一次
    long long localId = 0; // 回传定位：宿主侧 ChatElement
    int chatId = 0;
    std::string chatType;
};

// 启动一次 AI 对话：按候选序逐个降级，首个成功即止；
// 全败发 AigptbotDoneEvent(success=false)。
// target 接收 AigptbotDoneEvent（须为 QObject，通常是 MainWindow）。
void aigptbotChatStart(const AigptbotRequest& req, QObject* target);

#endif // AIGPTBOT_H
