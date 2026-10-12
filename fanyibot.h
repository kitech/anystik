#ifndef FANYIBOT_H
#define FANYIBOT_H

#include <string>
#include <vector>

class QObject;

// 翻译引擎（表内顺序 = engine="all" 的固定尝试序）
enum FanyibotEngineId {
    kFanyibotMsedge = 0,
    kFanyibotGoogle,
    kFanyibotYoudao,
    kFanyibotYandex,
    kFanyibotDeepl,
    kFanyibotDeeplWeb,
};

// 单个引擎的静态契约
struct FanyibotEngine {
    FanyibotEngineId id;
    const char* name;      // msedge / google / youdao / yandex / deepl / deepl-web
    bool unsupported;      // true=保留属性选项但选中即报错（deepl 需官方 auth key；deepl-web 免鉴权可用）
    bool needsProxy;       // true=该引擎需经用户配置的代理访问（境外站点）
};

const std::vector<FanyibotEngine>& fanyibotEngines();

// 目标语言显示名 → 各家语言码（各家不同：zh-Hans / zh-CN / zh-CHS / zh）。
// 该引擎不支持此语言时返回空串，调用方据此跳过该候选。
std::string fanyibotLangCode(FanyibotEngineId id, const std::string& toLang);

struct FanyibotRequest {
    std::string text;       // UTF-8 原样待译
    std::string toLang;     // 目标语言显示名（中文 / English / …）
    std::string engine;     // any / all / 具体（空串视为 any）
    long long localId = 0;  // 回传定位：宿主侧 ChatElement
    int chatId = 0;
    std::string chatType;
};

// 启动一次翻译：按候选序逐个降级，首个成功即止；
// 全败（含 unsupported、目标语言不支持）发 FanyibotDoneEvent(success=false)。
// target 接收 FanyibotDoneEvent（须为 QObject，通常是 MainWindow）。
void fanyibotTranslateStart(const FanyibotRequest& req, QObject* target);

#endif // FANYIBOT_H