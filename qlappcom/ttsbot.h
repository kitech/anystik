#ifndef TTSBOT_H
#define TTSBOT_H

#include <string>
#include <vector>
#include <cstdint>

class QObject;

enum TtsbotProviderId {
    kTtsbotEdge = 0,
    kTtsbotGoogle,
    kTtsbotOpenai,
    kTtsbotElevenLabs,
    kTtsbotDashScope,
    kTtsbotEspeak,
    // 可后续扩展：iflytek/tencent/baidu/volcengine
};

struct TtsbotProvider {
    TtsbotProviderId id;
    const char* name;        // 标识名（edge/google/openai/...）
    bool needsKey;           // 是否需要 API Key
    bool needsProxy;         // 是否需要代理
    const char* baseUrl;     // 可为空
};

const std::vector<TtsbotProvider>& ttsbotProviders();

struct TtsbotRequest {
    std::string text;        // 原文（UTF-8）
    std::string provider;    // "any"/"all"/具体名称（空视为"any"）
    std::string voice;       // 音色
    std::string lang;        // 语言
    std::string rate;        // 语速（如 "+0%"）
    long long localId = 0;   // 宿主 ChatElement 定位；0=朗读入口
    int chatId = 0;
    std::string chatType;
};

// 启动 TTS 合成：降级链 + std::thread 阻塞式；完成后发 TtsbotDoneEvent
void ttsbotSynthStart(const TtsbotRequest& req, QObject* target);

#endif // TTSBOT_H
