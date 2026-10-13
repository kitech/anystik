#ifndef WEBSEARCHBOT_H
#define WEBSEARCHBOT_H

#include <string>
#include <vector>

class QObject;

// 网页搜索后端（表内顺序 = backend="any" 随机 shuffle 前的初始顺序）
enum WebsearchBackendId {
    kWebsearchGoogle = 0,
    kWebsearchBing,
    kWebsearchDuckduckgo,
    kWebsearchYandex,
    kWebsearchSogou,
};

// 单个后端的静态契约（零认证：直连匿名公共搜索页，无 API key / webcreds）
struct WebsearchBackend {
    WebsearchBackendId id;
    const char* name;           // 属性值（backend 下拉、匹配、日志、消息尾标）
    bool needsProxy;            // true=境外站点经用户配置的代理访问
};

const std::vector<WebsearchBackend>& websearchBackends();

// 语言码 → 各后端参数（sogou 无语言参数，固定中文）
struct WebsearchLangParam {
    const char* code;           // 属性值（lang 下拉 wire 值）：zh/ja/ko/en/ru/de/fr/ar
    const char* display;        // 下拉原生显示名（UTF-8）
    const char* hl;             // google hl
    const char* gl;             // google gl
    const char* mkt;            // bing mkt
    const char* kl;             // duckduckgo kl
    const char* yalang;         // yandex lang（空=省略；zh/ja/ko/fr/ar 不支持）
    const char* yalr;           // yandex lr（与 yalang 同空）
};

// 按语言码查参数表；未知码返回 nullptr
const WebsearchLangParam* websearchLangParam(const std::string& code);

// 语言码 → 原生显示名（未知码回落 "中文"）
const char* websearchLangDisplay(const std::string& code);

struct WebsearchRequest {
    std::string text;      // 查询（UTF-8 原样）
    std::string backend;   // "any" | <name>（空串视为 "any"）
    std::string lang;      // 语言码（空串默认 "zh"）
    long long localId = 0; // 回传定位：宿主侧 ChatElement
    int chatId = 0;
    std::string chatType;
};

// 启动一次网页搜索：按候选序逐个降级（any=随机 shuffle），首个成功即止；
// 全败发 WebsearchDoneEvent(success=false)。
// target 接收 WebsearchDoneEvent（须为 QObject，通常是 MainWindow）。
void websearchStart(const WebsearchRequest& req, QObject* target);

#endif // WEBSEARCHBOT_H