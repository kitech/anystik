#ifndef EVENTPOLLER_H
#define EVENTPOLLER_H

#include "compatcore34.h"
#include <vector>
#include <deque>
#include <string>
#include <map>
#include <cstdint>
#include <curl/curl.h>
#include <qpixmap.h>
#include <qthread.h>
#include <qmutex.h>

// ── 事件类型常量 ──
const EventType34 EventListReadyType = toEventType34(QEvent::User + 100);
const EventType34 ApiResultReadyType = toEventType34(QEvent::User + 102);
const EventType34 MediaDownloadReadyType = toEventType34(QEvent::User + 103);
const EventType34 AvatarDownloadReadyType = toEventType34(QEvent::User + 104);
const EventType34 DiskLoadReadyType       = toEventType34(QEvent::User + 105);
const EventType34 RowidBackfillReadyType  = toEventType34(QEvent::User + 106);
const EventType34 MediaDownloadProgressType = toEventType34(QEvent::User + 107);
const EventType34 PastebinResultType      = toEventType34(QEvent::User + 108);
const EventType34 FanyibotDoneType        = toEventType34(QEvent::User + 109);
const EventType34 AigptbotDoneType        = toEventType34(QEvent::User + 110);
const EventType34 WebsearchDoneType       = toEventType34(QEvent::User + 111);
const EventType34 TtsbotDoneType          = toEventType34(QEvent::User + 112);
const EventType34 UrlPreviewReadyType     = toEventType34(QEvent::User + 113);

// ── API 请求类型 ──
enum ApiRequestType {
    ApiPollEvents,
    ApiGetSelf,
    ApiGetFriends,
    ApiLoadGroupMembers,
    ApiLoadMessageHistory,
    ApiSendFriendMessage,
    ApiSendMessage,
    ApiSendConferenceMessage,
    ApiSendGroupMessage,
    ApiAddFriend,
    ApiJoinConference,
    ApiTranslate,
    ApiTranslateForSend,
    ApiSetSelfInfo,
    ApiDeleteFriend,
    ApiCreateConference,
    ApiLeaveConference,
    ApiInviteToConference,
    ApiRejectConference,
    ApiIgnoreConference,
    ApiCreateGroup,
    ApiLeaveGroup,
    ApiInviteToGroup,
    ApiJoinGroup,
    ApiJoinGroupByChatId,
    ApiSetGroupSelfName,
    ApiSetGroupTopic,
    ApiSetConferenceTitle,
    ApiGetRandomName,
    ApiLoadAllData,
    ApiLoadFriendDetail,
    ApiLoadPartialData,
    ApiMediaDownload,
    ApiAvatarDownload,
    ApiRedactMessage,
    ApiUrlPreview,
};

// ── 数据类型（事件契约）──

struct Event {
    uint64_t id;
    std::string type;
    std::string data;
    std::string timestamp;
};

struct PeerInfo {
    int peerNumber;
    std::string userName;
    std::string nickname;
    int status = 0;
    std::string statusStr;
    std::string statusText;
    std::string iconUrl;
    int role = 0;
    std::string roleStr;
    std::string publicKey;
    bool isSelf = false;
    std::string peerIp;
    std::string userStatus;
    uint64_t lastSeen = 0;
};

struct HistoryMessage {
    int64_t rowid;
    std::string message;
    std::string sender_pubkey;
    uint32_t sender_number;
    std::string direction;
    std::string created_at;
    std::string roomId;
    std::string eventId;
    // Media (e.g. Matrix image)
    std::string msgtype;       // "image"/"video"/"audio"/"file"/""(text)
    std::string mediaUrl;      // "mxc://..."
    std::string mediaMime;     // "image/png"
    std::string thumbnailUrl;  // preview thumb mxc:// (video)
    int duration       = 0;    // milliseconds (video/audio)
    int mediaWidth  = 0;
    int mediaHeight = 0;
    int fileSize    = 0;    // bytes
    std::vector<std::string> relatesTos;    // 回复的 eventId 列表
    std::vector<std::string> mentions;    // 被 @ 的用户名/ID 列表
    bool redacted = false;                // gomuks redacted_by 标记
};

struct TranslateApiResult {
    bool success = false;
    std::string translatedText;
    std::string errorMessage;
};

struct ContactData {
    int id;
    std::string name;
    std::string type;
    std::string status;
    std::string chatId;
    bool isConnected;
    std::string iconUrl;
    std::string statusText;
    int memberCount = 0;
    ContactData() : id(-1), isConnected(false) {}
};

std::string webUrlEncode(const std::string& str);

typedef std::vector<Event> EventList;

// ── CustomEvent 基类 ──

class EventListEvent : public CustomEventBase {
public:
    EventListEvent(const EventList& evts) : CustomEventBase(EventListReadyType), events(evts) {}
    EventList events;
};

class ApiResultEvent : public CustomEventBase {
public:
    ApiResultEvent(ApiRequestType t) : CustomEventBase(ApiResultReadyType), type(t), elapsedMs(0) {}
    ApiRequestType type;
    int64_t elapsedMs = 0;   // HTTP 请求耗时，单位毫秒
};

class MediaDownloadEvent : public CustomEventBase {
public:
    MediaDownloadEvent() : CustomEventBase(MediaDownloadReadyType) {}
    int chatId = 0;
    std::string chatType;
    int msgIndex = 0;
    std::string mxcUrl;
    std::string rawData;   // 原始字节，WebP 等 Qt 不支持时 fallback
    bool success = false;
    std::string errorInfo;
    // 后处理字段（postprocDone 时填充）：实体化原文件 + 短动画探测/转码结果
    std::string localFile;    // 实体化后的原文件全路径
    std::string gifFile;      // 转码后 GIF 路径（仅短片）
    std::string thumbFile;    // 本地抽帧预览图路径（仅长视频）
    int durationSec = 0;
    bool isShortGif  = false;
    bool pendingPlay = false;
    bool postprocDone = false;   // true=后处理完成，走播放/动图落地；false=下载完成
};

// 媒体下载进度（100ms 节流；total<=0 表示未知长度，UI 走不确定进度）
class MediaDownloadProgressEvent : public CustomEventBase {
public:
    MediaDownloadProgressEvent() : CustomEventBase(MediaDownloadProgressType) {}
    int chatId = 0;
    std::string chatType;
    int msgIndex = 0;
    std::string mxcUrl;   // 身份解析用（环形裁剪/prepend 后索引会错位）
    long long received = 0;
    long long total = 0;
    long long speedBps = 0;   // 本次节流区间实测速率（B/s）
};

class AvatarDownloadEvent : public CustomEventBase {
public:
    AvatarDownloadEvent() : CustomEventBase(AvatarDownloadReadyType) {}
    std::string mxcUrl;
    std::string body;
    bool success = false;
    std::string errorInfo;
    QPixmap pixmap; // qlver use, qskver not
};

class DiskLoadEvent : public CustomEventBase {
public:
    DiskLoadEvent() : CustomEventBase(DiskLoadReadyType) {}
    int msgIndex = 0;
    std::string mediaUrl;
    std::vector<uint8_t> rawData;
    bool success = false;
    std::string errorInfo;
};

class RowidBackfillEvent : public CustomEventBase {
public:
    RowidBackfillEvent() : CustomEventBase(RowidBackfillReadyType) {}
    int chatId = 0;
    std::string chatType;
    int64_t rowid = 0;
    std::string eventId;
};

// 链接预览抓取结果（客户端直连外部站点，不经后端）
class UrlPreviewEvent : public CustomEventBase {
public:
    UrlPreviewEvent() : CustomEventBase(UrlPreviewReadyType) {}
    std::string urlKey;
    bool success = false;
    std::string title;
    std::string description;
    std::string siteName;
    std::string errorInfo;
};

// Pastebin 虚拟联系人的上传结果（客户端直连各粘贴/临时文件服务，不经后端）
class PasteResultEvent : public CustomEventBase {
public:
    PasteResultEvent() : CustomEventBase(PastebinResultType) {}
    int chatId = 0;
    std::string chatType;
    long long localId = 0;      // 宿主侧 ChatElement 定位（跨事件循环稳定）
    bool success = false;
    std::string url;            // 成功：可直接上屏的链接
    std::string providerUsed;   // 成功：实际命中的服务
    std::string errorMsg;       // 失败：含已试服务与最后错误
};

// 翻译虚拟联系人的翻译结果（客户端直连各翻译服务，不经后端）
class FanyibotDoneEvent : public CustomEventBase {
public:
    FanyibotDoneEvent() : CustomEventBase(FanyibotDoneType) {}
    int chatId = 0;
    std::string chatType;
    long long localId = 0;      // 宿主侧 ChatElement 定位（跨事件循环稳定）
    bool success = false;
    std::string translatedText; // 成功：译文
    std::string engineUsed;     // 成功：实际命中的引擎
    std::string errorMsg;       // 失败：含已试引擎与最后错误
};

// AI GPT 虚拟联系人的对话结果（客户端直连各 AI 服务，不经后端）
class AigptbotDoneEvent : public CustomEventBase {
public:
    AigptbotDoneEvent() : CustomEventBase(AigptbotDoneType) {}
    int chatId = 0;
    std::string chatType;
    long long localId = 0;      // 宿主侧 ChatElement 定位（跨事件循环稳定）
    bool success = false;
    std::string reply;           // 成功：AI 回复文本
    std::string providerUsed;    // 成功：实际命中的 provider 名称
    std::string errorMsg;        // 失败：含已试 provider 与最后错误
};

// 网页搜索单条结果（websearch 虚拟联系人）
struct WebSearchItem {
    std::string title;
    std::string url;
    std::string snippet;
};

// 网页搜索虚拟联系人的搜索结果（客户端直连各匿名搜索后端，不经后端）
class WebsearchDoneEvent : public CustomEventBase {
public:
    WebsearchDoneEvent() : CustomEventBase(WebsearchDoneType) {}
    int chatId = 0;
    std::string chatType;
    long long localId = 0;      // 宿主侧 ChatElement 定位（跨事件循环稳定）
    bool success = false;
    std::vector<WebSearchItem> items;  // 成功：至多 3 条结果（sogou 经 follow 解析）
    std::string backendUsed;          // 成功：实际命中的后端名
    std::string errorMsg;             // 失败：含已试后端与最后错误
};

// 文转音虚拟联系人的合成结果（客户端直连各 TTS 服务 / 本地引擎，不经后端）
class TtsbotDoneEvent : public CustomEventBase {
public:
    TtsbotDoneEvent() : CustomEventBase(TtsbotDoneType) {}
    int chatId = 0;
    std::string chatType;
    long long localId = 0;      // 0=朗读入口(无原消息待更新)；否则宿主 ChatElement 定位
    bool success = false;
    std::string providerUsed;   // 成功：实际命中的 provider 名称
    std::string errorMsg;       // 失败：含已试 provider 与最后错误
    std::string voice;          // 成功：音色/语言（展示用）
    std::string text;           // 原文（气泡 caption）
    std::string audioUrl;       // URL 型 provider：远程音频 URL
    std::string mime;           // 音频 MIME，如 audio/mpeg
    std::vector<uint8_t> audioData;  // 字节型/本地引擎：音频字节
    int durationSec = 0;
};

class SelfInfoResultEvent : public ApiResultEvent {
public:
    SelfInfoResultEvent() : ApiResultEvent(ApiGetSelf) {}
    bool success = false;
    std::string name, statusMsg, connStatus, address;
};

class FriendsResultEvent : public ApiResultEvent {
public:
    FriendsResultEvent() : ApiResultEvent(ApiGetFriends) {}
    std::vector<int> friendIds;
};

class MessageSentResultEvent : public ApiResultEvent {
public:
    MessageSentResultEvent(ApiRequestType t) : ApiResultEvent(t), success(false) {}
    bool success;
    int sendmsgseq = 0;
    std::string message;
    int chatId;
    std::string chatType;
    std::string errorMessage;
    std::string messageId;
};

class RedactResultEvent : public ApiResultEvent {
public:
    RedactResultEvent() : ApiResultEvent(ApiRedactMessage), success(false) {}
    bool success;
    int chatId = 0;
    std::string chatType;
    std::string messageId;
    std::string errorMessage;
};

class MembersLoadedEvent : public ApiResultEvent {
public:
    MembersLoadedEvent() : ApiResultEvent(ApiLoadGroupMembers) {}
    int contactId = 0;
    std::string contactType;
    std::vector<PeerInfo> members;
};

class MessageHistoryLoadedEvent : public ApiResultEvent {
public:
    MessageHistoryLoadedEvent() : ApiResultEvent(ApiLoadMessageHistory) {}
    int contactId = 0;
    std::string contactType;
    bool success = false;
    std::string errorMessage;
    std::vector<HistoryMessage> messages;
};

class TranslateResultEvent : public ApiResultEvent {
public:
    TranslateResultEvent() : ApiResultEvent(ApiTranslate) {}
    int chatId = -1;
    std::string chatType;
    int msgIndex = 0;
    bool success = false;
    std::string translatedText;
    std::string errorMessage;
};

class TranslateForSendResultEvent : public ApiResultEvent {
public:
    TranslateForSendResultEvent() : ApiResultEvent(ApiTranslateForSend) {}
    bool success = false;
    std::string translatedText;
    std::string errorMessage;
};

class AllDataLoadedEvent : public ApiResultEvent {
public:
    AllDataLoadedEvent() : ApiResultEvent(ApiLoadAllData) {}
    bool success = false;
    std::string selfName, selfStatusMsg, selfConnStatus, selfAddress;
    std::vector<ContactData> contacts;
};

class PartialDataEvent : public ApiResultEvent {
public:
    static const int kSelf = 1;
    static const int kContacts = 2;
    PartialDataEvent() : ApiResultEvent(ApiLoadPartialData) {}
    int loadedMask = 0;
    std::string selfName, selfStatusMsg, selfConnStatus, selfAddress;
    std::vector<ContactData> contacts;
};

class FriendDetailEvent : public ApiResultEvent {
public:
    FriendDetailEvent() : ApiResultEvent(ApiLoadFriendDetail) {}
    int friendId = 0;
    bool success = false;
    std::string name;
    std::string publicKey;
    std::string statusStr;
    std::string statusText;
    std::string iconUrl;
    std::string userStatus;
    std::string peerIp;
    uint64_t lastSeen = 0;
};

// ── curl_multi HTTP 引擎 ──

struct HttpResponse {
    int httpCode = 0;
    std::string curlErrStr;
    std::string body;
    std::map<std::string, std::string> headers;
    int64_t elapsedMs = 0;   // 请求耗时，单位毫秒
    std::string finalUrl;    // 跟随重定向后的最终 URL（空=无/未跟随）；落地页/壳链接解析用
};

struct HttpRequest {
    // 停滞看门狗默认值：所有请求默认启用 150s（boottime，含休眠）兜底；
    // 更敏感的场景用构造参数显式覆盖（事件 poll 60 / 媒体下载 25）
    static const int kDefaultStallSec = 150;
    std::string url;
    std::string method;
    std::string data;
    int timeoutSec;
    int stallSec;      // 无进展停滞超时（秒）；0=禁用；默认 kDefaultStallSec；看门狗 boottime
    long lowSpeedLimit = 0;   // curl 原生低速中止（B/s）；0=禁用
    long lowSpeedTime  = 0;   // 低于 lowSpeedLimit 持续 lowSpeedTime 秒 → CURLE_OPERATION_TIMEDOUT
    int connectTimeoutSec = 0;   // 建连阶段超时（秒）；0=用系统默认；切网后重试快速失败
    int delayMs = 0;          // 延迟调度毫秒；>0 时 delayMs 后才真正发出（非阻塞，pump 照常运转）
    bool followRedirects = false;   // 是否跟随 3xx（默认关，保持既有请求行为不变）
    // 代理 URL（http://host:port 或 socks5://host:port）；空=直连。
    // 由调用方按 provider 的 needsProxy 标记 + 全局设置填充。
    std::string proxy;
    std::map<std::string, std::string> extraHeaders;
    // 下载进度回调（poller 线程触发；100ms 节流在 xferinfoCb 内处理）
    void (*progress)(long long received, long long total,
                     long long speedBps, void* udata) = nullptr;

    HttpRequest() : method("GET"), timeoutSec(35), stallSec(kDefaultStallSec) {}
    HttpRequest(std::string url, std::string method = "GET",
                std::string data = "", int timeoutSec = 35,
                std::map<std::string, std::string> extraHeaders = {},
                int stallSec = kDefaultStallSec)
        : url(std::move(url)), method(std::move(method)),
          data(std::move(data)), timeoutSec(timeoutSec),
          extraHeaders(std::move(extraHeaders)), stallSec(stallSec) {}
};

struct HttpCtx {
    std::string urlStr;
    std::string postData;
    std::string body;
    std::map<std::string, std::string> headers;
    curl_slist* requestHeaders;
    void (*done)(const HttpResponse& resp, void* udata);
    void* udata;
    void (*progress)(long long received, long long total,
                     long long speedBps, void* udata);
    TimePoint lastEmitTp;   // 进度节流：上一次发射时刻（单调时钟）
    TimePoint startTp;      // 请求发出时刻（看门狗/诊断用）
    long long lastEmitBytes;   // 上次节流发射时的已收字节（算速度用）
    long long lastSpeedBps;    // 上次节流实测速率（B/s）
    TimePoint lastActiveTp;    // 最近一次收发活动时刻（boottime 时钟，看门狗用；含休眠）
    int stallSec;              // 无进展超时（秒）；0=禁用
};

class EventPoller : public QThread {
public:
    static void start();
    static void stop();
    static void addRequest(const HttpRequest& req,
                           void (*done)(const HttpResponse& resp, void* udata),
                           void* udata = nullptr);
    static bool isReady();   // 泵线程与 multi 均已就绪

private:
    EventPoller();
    void run();
    // 构造 curl easy + HttpCtx，失败返回 nullptr（调用方负责不推进 udata 生命周期）
    static CURL* buildHandle(const HttpRequest& req,
                             void (*done)(const HttpResponse& resp, void* udata),
                             void* udata);
    // multi 级错误恢复（泵线程）：libcurl 规定 curl_multi_perform 报错后该 multi
    // 状态不可继续，需移除全部句柄后重建；否则 perform 静默空转、在途句柄永不完成，
    // 事件长轮询链会永久死亡（仅重启可恢复）。
    void recoverMulti(const char* where, int mcode);
    static EventPoller* s_instance;
    bool running;
    CURLM* multi;
    QMutex pendingMutex;            // 保护 pendingHandles + delayedReqs（GUI↔泵线程交接）
    std::deque<CURL*> pendingHandles;
    std::vector<CURL*> activeHandles;   // 泵线程私有：在跑句柄（看门狗用）

    // 非阻塞延迟重发（boottime 时刻到点后由泵线程放入 pendingHandles）
    struct DelayedReq {
        HttpRequest req;
        void (*done)(const HttpResponse& resp, void* udata);
        void* udata;
        TimePoint readyAt;          // boottime
    };
    std::deque<DelayedReq> delayedReqs;
};

#endif
