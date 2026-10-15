#ifndef PURECONSTS_HPP
#define PURECONSTS_HPP

#include <stdint.h>
#include <qstring.h>

// ── 联系人类型常量 ──
// C++11 constexpr（内部链接，每 TU 一份；消费方为内容比较/赋值，无指针同一性依赖）
constexpr const char* const kImapMailType       = "imap_mail";
constexpr const char* const kGomuksRoomType     = "gomuks_room";
// db中有联系人类型的字符串副本, 这个值改了无用,只在初次创建时有效,会有db遗留无效数据
constexpr const char* const kMtxliteRoomType    = "matrix"; // fedbrg need raw name, "mtxlite_room";
constexpr const char* const kUnktoxFriendType   = "unktox_friend";
constexpr const char* const kUnktoxConferenceType = "unktox_conference";
constexpr const char* const kUnktoxGroupType    = "unktox_group";
constexpr const char* const kSyseventType       = "sysevent";
constexpr const char* const kTopicType          = "topic";
constexpr const char* const kFilesyncType       = "filesync";
constexpr const char* const kClipboardType      = "clipboard";
constexpr const char* const kUnknownType        = "unknown";
constexpr const char* const kBookmarkType       = "bookmark";
constexpr const char* const kBookmarkName       = "Bookmark·收藏夹";
constexpr const char* const kAichatType         = "aichat";
constexpr const char* const kAichatName         = "AI·GPT Chat";
constexpr const char* const kWebsearchType      = "websearch";
constexpr const char* const kWebsearchName      = "Search·网页搜索";
constexpr const char* const kPastebinType       = "pastebin";
constexpr const char* const kPastebinName       = "Paste Bin·Txt";
constexpr const char* const kTtsbotType         = "tts";
constexpr const char* const kTtsbotName         = "Txt2Snd·文转音";
constexpr const char* const kTranslateType      = "translate";
constexpr const char* const kTranslateName      = "Translate·翻译";
constexpr const char* const kMobPushType        = "mobpush";
constexpr const char* const kMobPushName        = "MobPush·发布";
constexpr const char* const kSnapType            = "snap";
constexpr const char* const kSnapName            = "Snap·贴图";
constexpr const char* const kMisskeyType        = "misskey_note";
constexpr const char* const kToutiaoHotnewsType = "toutiao_hotnews";   // 头条热闻订阅流（type == chatId）
constexpr int kToutiaoHotnewsId = -107;                                // 头条热闻保留 id（-107，固定不 hash）
constexpr const char* const kZhihuNotifyType = "zhihu_notify";         // 知乎通知订阅流（type == chatId）
constexpr int kZhihuNotifyId = -108;                                   // 知乎通知保留 id（-108，固定不 hash）
constexpr const char* const kZhihuHotnewsType = "zhihu_hotnews";       // 知乎热闻订阅流（type == chatId）
constexpr int kZhihuHotnewsId = -109;                                  // 知乎热闻保留 id（-109，固定不 hash）
constexpr const char* const kBiliNotifyType = "bili_notify";           // 哔喱关注动态订阅流（type == chatId）
constexpr int kBiliNotifyId = -110;                                    // 哔喱通知保留 id（-110，固定不 hash）
constexpr const char* const kWeiboHotnewsType = "weibo_hotnews";       // 微博热闻订阅流（type == chatId == roomId）
constexpr int kWeiboHotnewsId = -111;                                  // 微博热闻保留 id（-111，固定不 hash）
constexpr const char* const kXiaohongshuNotifyType = "xiaohongshu_notify"; // 小红书通知订阅流（type == chatId == roomId）
constexpr int kXiaohongshuNotifyId = -112;                             // 小红书通知保留 id（-112，固定不 hash）
constexpr const char* const kCoolapkTimelineType = "coolapk_timeline";  // 酷安时线订阅流（type == chatId == roomId）
constexpr int kCoolapkTimelineId = -113;                                // 酷安时线保留 id（-113，固定不 hash）
constexpr const char* const kXiaohongshuRecommendType = "xiaohongshu_recommend"; // 小红书推荐流订阅流（type == chatId == roomId）
constexpr int kXiaohongshuRecommendId = -114;                                    // 小红书推荐流保留 id（-114，固定不 hash）
constexpr const char* const kXiaohongshuHotnewsType = "xiaohongshu_hotnews"; // 小红书热闻订阅流（type == chatId == roomId）
constexpr int kXiaohongshuHotnewsId = -115;                                // 小红书热闻保留 id（-115，固定不 hash）
constexpr const char* const kHongguoHotlistType = "hongguo_hotlist";       // 红果热榜订阅流（type == chatId == roomId）
constexpr int kHongguoHotlistId = -116;                                    // 红果热榜保留 id（-116，固定不 hash）
constexpr const char* const kXiaohongshuNoteType = "xiaohongshu_note";     // 小红书笔记卡片订阅流（type == chatId == roomId）
constexpr int kXiaohongshuNoteId = -117;                                   // 小红书笔记保留 id（-117，固定不 hash）

constexpr const char* const kSysinfoBoardType = "sysinfo_board";   // 系统信息公告板（字符名，订阅流 type == chatId）
constexpr const char* const kSysinfoBoardName = "系统信息公告板";    // 显示名（表格标题 / hostname 缺失回退）
constexpr int kSysinfoBoardId = -121;                               // 系统信息公告板保留 id（-121，固定不 hash）

constexpr int kMisskeyTimelineId = -118;
constexpr int kOutlookGraphId = -119;
constexpr int kImapBareId = -120;

constexpr int UnkSize = 0;                                             // 未知大小/未知尺寸哨兵（0 = 未知）

// ── 联系人类型 → 图标（唯一来源：联系人列表绘制 + 输入框类型指示器共用）──
// 每行 {type, emoji 码点, emoji 的 UTF-8 字面量}；码点与字面量指向同一个字，
// 改动时必须同步，否则绘制（按码点）与按钮（按字面量）两处会显示不一致。
struct TypeEmojiDef {
    const char* type;
    uint32_t cp;
    const char* emoji;
};

constexpr TypeEmojiDef kTypeEmojiDefs[] = {
    { "friend",                   0x1F464, "👤" },
    { kUnktoxFriendType,          0x1F464, "👤" },
    { "group",                    0x1F465, "👥" },
    { kUnktoxGroupType,           0x1F465, "👥" },
    { "conference",               0x1F399, "🎙" },
    { kUnktoxConferenceType,      0x1F399, "🎙" },
    { kSyseventType,              0x2699,  "⚙" },
    { kUnknownType,               0x2753,  "❓" },
    { kTopicType,                 0x1F4CC, "📌" },
    { kFilesyncType,              0x1F4C1, "📁" },
    { kClipboardType,             0x1F4CB, "📋" },
    { kGomuksRoomType,            0x1F9EE, "🧮" },
    { kMtxliteRoomType,           0x264F,  "♏" },
    { kImapMailType,              0x2709,  "✉" },
    { kBookmarkType,              0x1F516, "🔖" },
    { kAichatType,                0x1F916, "🤖" },
    { kWebsearchType,             0x1F50D, "🔍" },
    { kPastebinType,              0x1F4E6, "📦" },
    { kTtsbotType,                0x1F50A, "🔊" },
    { kTranslateType,             0x1F524, "🔤" },
    { kMobPushType,               0x1F4E2, "📢" },
    { kSnapType,                  0x1F39E, "🎞" },
    { kToutiaoHotnewsType,        0x1F4F0, "📰" },
    { kZhihuNotifyType,           0x1F4D8, "📘" },
    { kZhihuHotnewsType,          0x1F4D8, "📘" },
    { kBiliNotifyType,            0x1F4FA, "📺" },
    { kWeiboHotnewsType,          0x1F525, "🔥" },
    { kXiaohongshuRecommendType,  0x1F4D5, "📕" },
    { kXiaohongshuNotifyType,     0x1F514, "🔔" },
    { kXiaohongshuHotnewsType,    0x1F4C8, "📈" },
    { kCoolapkTimelineType,       0x1F4F1, "📱" },
    { kMisskeyType,               0x1F431, "🐱" },
    { kSysinfoBoardType,          0x1F4CA, "📊" },
    { kXiaohongshuNoteType,       0x1F5BC, "🖼" },
    { kHongguoHotlistType,        0x1F3AC, "🎬" },
};

inline uint32_t typeToEmojiCp(const QString& type) {
    const int n = (int)(sizeof(kTypeEmojiDefs) / sizeof(kTypeEmojiDefs[0]));
    for (int i = 0; i < n; i++) {
        if (type == kTypeEmojiDefs[i].type) { return kTypeEmojiDefs[i].cp; }
    }
    return 0x1F464;   // 未知类型回落 👤
}

inline const char* typeToEmojiUtf8(const QString& type) {
    const int n = (int)(sizeof(kTypeEmojiDefs) / sizeof(kTypeEmojiDefs[0]));
    for (int i = 0; i < n; i++) {
        if (type == kTypeEmojiDefs[i].type) { return kTypeEmojiDefs[i].emoji; }
    }
    return "👤";   // 未知类型回落 👤
}

#endif // PURECONSTS_HPP
