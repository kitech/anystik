#ifndef AIGPT_SESSIONS_H
#define AIGPT_SESSIONS_H

#include <string>

// aigptbot web provider 会话缓存（跨消息复用 chat_session / conversation id）。
//
// 容器：~/.config/qltox/aigpt_sessions.json（JSON，0600），与 webcreds.enc 同目录
//   {"<key>":{"sid":"…","pid":"…","n":<long long>,"t":<unix 秒>}, …}
//   key = "<webKindName>|<凭据 sha256 前 16 hex>"（凭据变化 = 不同账号，不复用）
// env：QTOX_AIGPT_SESSION_FILE 覆盖文件路径（测试/多实例）。
// 禁异常；缓存非关键路径，IO 错误静默。

struct AigptSessionRecord {
    std::string sessionId;   // deepseek chat_session_id / gemini conversation_id /
                             // grok conversationId / chatgpt conversation_id /
                             // meta externalConversationId
    std::string parentId;    // deepseek/gemini/chatgpt 的 parent 消息 id（空 = null）
    long long msgCount = 0;
    long long createdAt = 0; // unix 秒
};

// 键：webKindName + 凭据指纹（cred 空则仅 webKindName）
std::string aigptSessionKey(const char* webKindName, const std::string& cred);
// 命中返回 true 并填 out
bool aigptSessionGet(const std::string& key, AigptSessionRecord& out);
// 写入/覆盖
void aigptSessionPut(const std::string& key, const AigptSessionRecord& rec);
// 删除该键（新会话 / 会话满）
void aigptSessionInvalidate(const std::string& key);
// 测试：覆盖路径并清缓存
void aigptSessionSetFileForTest(const std::string& path);

#endif // AIGPT_SESSIONS_H
