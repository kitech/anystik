#include "messageattribdefs.h"
#include "compatcore34.h"            // qFromUtf8
#include "../qltox/pureconsts.hpp"   // 类型常量 kImapMailType / kGomuksRoomType / ...
#include "aigptbot.h"
#include "websearchbot.h"
#include "ttsbot.h"

// ── 占位 schema：按联系人类型返回属性集(键名/选项后续按需调整) ──
static MessageAttrDef comboDef(const QString& key, const QString& label,
                               const QStringList& options, const QString& def,
                               const QString& tooltip = QString()) {
    MessageAttrDef d;
    d.key = key; d.label = label; d.kind = MessageAttrDef::kCombo;
    d.options = options; d.defValue = def;
    d.tooltip = tooltip;
    return d;
}
static MessageAttrDef checkDef(const QString& key, const QString& label,
                               const QString& tooltip = QString()) {
    MessageAttrDef d;
    d.key = key; d.label = label; d.kind = MessageAttrDef::kCheck;
    d.defValue = QString::fromLatin1("0");
    d.tooltip = tooltip;
    return d;
}
static MessageAttrDef lineDef(const QString& key, const QString& label,
                              const QString& hint,
                              const QString& tooltip = QString()) {
    MessageAttrDef d;
    d.key = key; d.label = label; d.kind = MessageAttrDef::kLine; d.hint = hint;
    d.tooltip = tooltip;
    return d;
}
static MessageAttrDef tagsDef(const QString& key, const QString& label,
                              const QString& tooltip = QString()) {
    MessageAttrDef d;
    d.key = key; d.label = label; d.kind = MessageAttrDef::kTags;
    d.hint = qFromUtf8("标签1,标签2");
    d.tooltip = tooltip;
    return d;
}
static MessageAttrDef spinDef(const QString& key, const QString& label,
                              int mn, int mx, int step, int def,
                              const QString& tooltip = QString()) {
    MessageAttrDef d;
    d.key = key; d.label = label; d.kind = MessageAttrDef::kSpin;
    d.spinMin = mn; d.spinMax = mx; d.spinStep = step;
    d.defValue = QString::number(def);
    d.tooltip = tooltip;
    return d;
}

MessageAttrDefList messageAttribBarDefsForType(const QString& type) {
    MessageAttrDefList defs;
    QStringList pri;  pri << "low" << "normal" << "high";
    QStringList pre;  pre << "normal" << "away" << "busy";
    QStringList sm;   sm  << "auto" << "ptt" << "muted";
    QStringList lv;   lv  << "info" << "warning" << "error";
    if (type == "friend") {
        defs << comboDef("presence", qFromUtf8("状态"), pre, "normal",
                         qFromUtf8("当前在线状态：normal=在线 / away=离开 / busy=忙碌"));
        defs << lineDef("note", qFromUtf8("备注"), qFromUtf8("给该好友的备注"),
                        qFromUtf8("该好友的本地备注名，仅用于查找与展示，不下发"));
        defs << tagsDef("tags", qFromUtf8("标签"));
    } else if (type == "group") {
        defs << comboDef("priority", qFromUtf8("优先级"), pri, "normal",
                         qFromUtf8("消息优先级：low/normal/high，影响提醒强度"));
        defs << checkDef("sticky", qFromUtf8("置顶"),
                         qFromUtf8("是否将该群会话固定在列表顶部"));
        defs << lineDef("subject", qFromUtf8("主题"), QString(),
                        qFromUtf8("群会话的主题/标题，用于列表与查找"));
        defs << tagsDef("tags", qFromUtf8("标签"));
    } else if (type == "conference") {
        defs << comboDef("speak_mode", qFromUtf8("发言"), sm, "auto",
                         qFromUtf8("发言方式：auto=自动 / ptt=按住说话 / muted=静音"));
        defs << checkDef("record", qFromUtf8("录音"),
                         qFromUtf8("是否对该语音会议会话录音"));
        defs << tagsDef("tags", qFromUtf8("标签"));
    } else if (type == kUnktoxFriendType) {
        defs << comboDef("priority", qFromUtf8("优先级"), pri, "normal",
                         qFromUtf8("消息优先级：low/normal/high，影响提醒强度"));
        defs << lineDef("note", qFromUtf8("备注"), qFromUtf8("未知协议好友备注"),
                        qFromUtf8("该好友的本地备注名，仅用于查找与展示，不下发"));
        defs << tagsDef("tags", qFromUtf8("标签"));
    } else if (type == kUnktoxConferenceType) {
        defs << comboDef("speak_mode", qFromUtf8("发言"), sm, "auto",
                         qFromUtf8("发言方式：auto=自动 / ptt=按住说话 / muted=静音"));
        defs << checkDef("record", qFromUtf8("录音"),
                         qFromUtf8("是否对该语音会议会话录音"));
        defs << tagsDef("tags", qFromUtf8("标签"));
    } else if (type == kUnktoxGroupType) {
        defs << comboDef("priority", qFromUtf8("优先级"), pri, "normal",
                         qFromUtf8("消息优先级：low/normal/high，影响提醒强度"));
        defs << checkDef("sticky", qFromUtf8("置顶"),
                         qFromUtf8("是否将该群会话固定在列表顶部"));
        defs << tagsDef("tags", qFromUtf8("标签"));
    } else if (type == kGomuksRoomType) {
        defs << checkDef("notify", qFromUtf8("提醒"),
                         qFromUtf8("新消息到达时是否弹出提醒通知"));
        defs << comboDef("priority", qFromUtf8("优先级"), pri, "normal",
                         qFromUtf8("消息优先级：low/normal/high，影响提醒强度"));
        defs << tagsDef("tags", qFromUtf8("标签"));
    } else if (type == kMtxliteRoomType) {
        defs << checkDef("notify", qFromUtf8("提醒"),
                         qFromUtf8("新消息到达时是否弹出提醒通知"));
        defs << comboDef("priority", qFromUtf8("优先级"), pri, "normal",
                         qFromUtf8("消息优先级：low/normal/high，影响提醒强度"));
        defs << tagsDef("tags", qFromUtf8("标签"));
    } else if (type == kImapMailType) {
        defs << checkDef("mark_as_read", qFromUtf8("自动已读"),
                         qFromUtf8("新邮件到达时是否自动标记为已读"));
        defs << comboDef("priority", qFromUtf8("优先级"), pri, "normal",
                         qFromUtf8("消息优先级：low/normal/high，影响提醒强度"));
        defs << tagsDef("tags", qFromUtf8("标签"));
    } else if (type == kMisskeyType) {
        QStringList msvis; msvis << "public" << "home" << "followers" << "specified";
        defs << comboDef("visibility", qFromUtf8("可见性"), msvis, "public",
                         qFromUtf8("笔记可见范围：public=公开（默认，公共时间线并联邦）；home=仅首页；followers=仅粉丝；specified=仅指定用户"));
        defs << checkDef("localOnly", qFromUtf8("仅本站"),
                         qFromUtf8("仅本站可见，不通过 ActivityPub 联邦到远端实例。与可见性正交可任意组合，如 public+localOnly 本地公开不出站"));
        defs << lineDef("cw", qFromUtf8("折叠警告"), qFromUtf8("如: 内含剧透"),
                        qFromUtf8("CW 内容警告（1-100 字符）。设置后正文折叠，需点击展开；不能为空串"));
        defs << lineDef("note", qFromUtf8("备注"), QString(),
                        qFromUtf8("本联系人的本地备注，仅用于查找与展示，不下发"));
        defs << tagsDef("tags", qFromUtf8("标签"),
                        qFromUtf8("逗号分隔的标签；Misskey 侧解析为笔记哈希标签"));
    } else if (type == kFilesyncType) {
        defs << lineDef("sync_dir", qFromUtf8("同步目录"), qFromUtf8("如 /data/sync"),
                        qFromUtf8("与该会话绑定的本地同步目录路径"));
        defs << checkDef("overwrite", qFromUtf8("覆盖"),
                         qFromUtf8("目标文件已存在时是否直接覆盖"));
    } else if (type == kClipboardType) {
        defs << checkDef("keep_history", qFromUtf8("保留历史"),
                         qFromUtf8("剪贴板同步是否保留历史记录"));
        defs << tagsDef("tags", qFromUtf8("标签"));
    } else if (type == kSyseventType) {
        defs << comboDef("level", qFromUtf8("级别"), lv, "info",
                         qFromUtf8("系统事件的记录级别：info=信息 / warning=警告 / error=错误"));
        defs << checkDef("sound", qFromUtf8("声音"),
                         qFromUtf8("系统事件到达时是否播放提示音"));
    } else if (type == kTopicType) {
        defs << lineDef("subreddit", qFromUtf8("分区"), QString(),
                        qFromUtf8("REDDIT 话题订阅的分区名（subreddit）"));
        defs << checkDef("pinned", qFromUtf8("置顶"),
                         qFromUtf8("该话题帖子是否固定在消息列表顶部"));
    } else if (type == kToutiaoHotnewsType
               || type == kZhihuHotnewsType
               || type == kZhihuNotifyType
               || type == kBiliNotifyType
               || type == kWeiboHotnewsType
               || type == kXiaohongshuNotifyType
               || type == kXiaohongshuRecommendType
               || type == kXiaohongshuHotnewsType
               || type == kCoolapkTimelineType) {
        defs << spinDef("auto_refresh", qFromUtf8("刷新间隔"), 30, 3600, 30, 300,
                        qFromUtf8("自动刷新时间线间隔（秒）"));
        defs << checkDef("only_video", qFromUtf8("仅视频"),
                         qFromUtf8("仅推送视频类内容，忽略图文"));
    } else if (type == kUnknownType) {
        defs << comboDef("priority", qFromUtf8("优先级"), pri, "normal",
                         qFromUtf8("消息优先级：low/normal/high，影响提醒强度"));
        defs << lineDef("note", qFromUtf8("备注"), qFromUtf8("给该会话的备注"),
                        qFromUtf8("该会话的本地备注，仅用于查找与展示，不下发"));
        defs << tagsDef("tags", qFromUtf8("标签"));
    } else if (type == kTranslateType) {
        QStringList eng;  eng << "any" << "all" << "msedge" << "google"
                              << "youdao" << "yandex" << "deepl" << "deepl-web";
        defs << comboDef("engine", qFromUtf8("翻译引擎"), eng, "any",
                         qFromUtf8("翻译引擎：any=随机取序逐个尝试，首个成功即止（默认） / "
                                   "all=按固定优先级依次尝试，成功即止 / 指定项=只用该引擎。"
                                   "客户端直连翻译服务，不经后端。"
                                   "deepl-web=DeepL 免鉴权网页端点（oneshot）；"
                                   "deepl 需官方 auth key，暂不可用，选中即报错"));
        QStringList lang;
        lang << qFromUtf8("中文") << qFromUtf8("繁體中文") << qFromUtf8("日本語")
             << qFromUtf8("한국어") << qFromUtf8("English") << qFromUtf8("Français")
             << qFromUtf8("Deutsch") << qFromUtf8("Русский") << qFromUtf8("العربية")
             << qFromUtf8("地球语");
        defs << comboDef("tolang", qFromUtf8("目标语言"), lang, "中文",
                         qFromUtf8("翻译目标语言（各语言原生名称）：中文=简体（默认）/ 繁體中文 / 日本語 / 한국어 / "
                                   "English / Français / Deutsch / Русский / العربية / 地球语=世界语"));
    } else if (type == kMobPushType) {
        QStringList pch;  pch << "all" << "ntfy" << "mozilla";
        defs << comboDef("ctype", qFromUtf8("类型"), pch, "all",
                         qFromUtf8("推送通道：all=自动选优 / ntfy / mozilla（占位属性，暂不生效）"));
        defs << lineDef("server_url", qFromUtf8("服务端 URL"), qFromUtf8("https://ntfy.sh"),
                        qFromUtf8("自建推送服务端地址（ntfy 自托管 / Mozilla autopush），占位属性暂不生效"));
    } else if (type == kSnapType) {
        QStringList pf;  pf << "all" << "any" << "instagram" << "snapchat" << "giphy";
        defs << comboDef("platform", qFromUtf8("平台"), pf, "all",
                         qFromUtf8("贴图/短视频平台：all=自动选优 / instagram / snapchat / giphy（占位属性，暂不生效）"));
    } else if (type == kAichatType) {
        const auto& plist = aigptbotProviders();
        QStringList svc;
        QStringList svcDisp;
        svc << "any" << "all";
        svcDisp << "any" << "all";
        for (size_t i = 0; i < plist.size(); ++i) {
            svc << qFromUtf8(plist[i].name);
            QString nm = qFromUtf8(plist[i].name);
            if (plist[i].webKind != kAigptbotWebNone
                    || plist[i].id == kAigptbotMetaApi) {
                // web / meta(API key)：凭据状态后缀标签（仅显示层，wire 值仍为纯 provider 名）
                const std::string st = aigptbotWebCredStatusName(plist[i].name);
                if (!st.empty()) {
                    nm += qFromUtf8(" [") + qFromUtf8(st.c_str()) + qFromUtf8("]");
                }
            }
            svcDisp << nm;
        }
        MessageAttrDef def = comboDef("provider", qFromUtf8("接入服务商"), svc, "any",
                                      qFromUtf8("客户端直连：any=随机降级 / all=固定顺序降级 / 指定单一服务商"));
        def.displayOptions = svcDisp;
        defs << def;
        defs << lineDef("model", qFromUtf8("模型"), QString(),
                        qFromUtf8("留空使用服务商默认模型"));
        {
            MessageAttrDef briefDef = checkDef("brief", qFromUtf8("简洁回复"),
                                               qFromUtf8("勾选后请求简短回复（前置简洁指令并限制 max_tokens=512），回复尾部标注 BY 实际服务"));
            briefDef.defValue = QString::fromLatin1("1");
            defs << briefDef;
        }
        {
            MessageAttrDef thinkDef = checkDef("thinking", qFromUtf8("深度思考"),
                                               qFromUtf8("联网深度思考开关（thinking_enabled）；仅 deepseek-web 等支持的服务商生效，默认开"));
            thinkDef.defValue = QString::fromLatin1("1");
            defs << thinkDef;
        }
        {
            MessageAttrDef searchDef = checkDef("search", qFromUtf8("联网搜索"),
                                                qFromUtf8("联网检索开关（search_enabled）；仅 deepseek-web 等支持联网的服务商生效，默认开"));
            searchDef.defValue = QString::fromLatin1("1");
            defs << searchDef;
        }
        defs << checkDef("new_chat", qFromUtf8("新对话"),
                         qFromUtf8("勾选后本条消息丢弃旧会话、开启全新会话；发送后自动复位"));
    } else if (type == kWebsearchType) {
        QStringList ws;  ws << "any" << "google" << "bing" << "duckduckgo" << "yandex" << "sogou";
        defs << comboDef("backend", qFromUtf8("搜索后端"), ws, "any",
                         qFromUtf8("网页搜索后端：any=随机取序逐个尝试，首个成功即止（默认） / google / bing / "
                                   "duckduckgo / yandex / sogou=只用该后端。零认证，客户端直连匿名搜索页，"
                                   "境外后端按全局代理设置访问，sogou 国内直连"));
        QStringList lw;
        QStringList ld;
        const char* codes[] = { "zh", "ja", "ko", "en", "ru", "de", "fr", "ar" };
        for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
            lw << qFromUtf8(codes[i]);
            ld << qFromUtf8(websearchLangDisplay(codes[i]));
        }
        MessageAttrDef langDef = comboDef("lang", qFromUtf8("搜索语言"), lw, "zh",
                                          qFromUtf8("搜索结果语言（zh=中文，默认）：zh / 日本語 / 한국어 / English / "
                                                    "Русский / Deutsch / Français / العربية；sogou 固定中文不随语言改变"));
        langDef.displayOptions = ld;
        defs << langDef;
    } else if (type == kTtsbotType) {
        const std::vector<TtsbotProvider>& plist = ttsbotProviders();
        QStringList svc;    svc << "any" << "all";
        QStringList svcDisp; svcDisp << "any" << "all";
        for (size_t i = 0; i < plist.size(); ++i) {
            svc << qFromUtf8(plist[i].name);
            svcDisp << qFromUtf8(plist[i].name);
        }
        MessageAttrDef def = comboDef("provider", qFromUtf8("合成引擎"), svc, "edge",
                                      qFromUtf8("TTS 引擎：any=随机取序逐个尝试，首个成功即止 / all=按固定优先级依次尝试 / "
                                                "指定项=只用该引擎。edge=微软 Edge 免鉴权（默认）/ google=Google 翻译 TTS 免鉴权 / "
                                                "openai、elevenlabs、dashscope 需 API Key / espeak=本地引擎（离线）"));
        def.displayOptions = svcDisp;
        defs << def;
        defs << lineDef("voice", qFromUtf8("音色"), qFromUtf8("zh-CN-XiaoxiaoNeural"),
                        qFromUtf8("音色名（部分引擎生效）：edge 如 zh-CN-XiaoxiaoNeural / en-US-EmmaMultilingualNeural；espeak 如 cmn"));
        {
            QStringList lw;  lw << "zh-CN" << "en-US" << "ja-JP" << "ko-KR";
            defs << comboDef("lang", qFromUtf8("语言"), lw, "zh-CN",
                             qFromUtf8("合成语言/区域（google tl 参数与 SSML xml:lang）"));
        }
        {
            QStringList rt;  rt << "+0%" << "-20%" << "+20%" << "+50%";
            defs << comboDef("rate", qFromUtf8("语速"), rt, "+0%",
                             qFromUtf8("语速（edge prosody rate，如 +0% / -20% / +50%）"));
        }
    } else if (type == kPastebinType) {
        QStringList psvc;  psvc << "any" << "all"
                                << "pastebin.com" << "dpaste.com" << "0x0.st"
                                << "transfer.sh" << "catbox" << "litterbox"
                                << "mhimg.cn" << "img.scdn.io"
                                << "tmpfile.link" << "tempfile.org" << "storage.to";
        defs << comboDef("provider", qFromUtf8("接入服务商"), psvc, "any",
                         qFromUtf8("粘贴/临时文件服务：any=随机取序逐个尝试，首个成功即止（默认） / "
                                   "all=按固定优先级依次尝试，成功即止 / 指定项=只用该服务。"
                                   "pastebin.com 需官方 api_dev_key，暂不可用，选中即报错"));
        QStringList pexp;  pexp << "auto" << "1h" << "1d" << "1w" << "1m" << "1y" << "never";
        defs << comboDef("expire", qFromUtf8("有效期"), pexp, "auto",
                         qFromUtf8("有效期预设：auto=不按有效期过滤，由服务能力决定（默认）；"
                                   "1h / 1d / 1w / 1m / 1y / never=按服务能力过滤候选，"
                                   "无服务支持该有效期时报错"));
    } else if (type == kBookmarkType) {
        QStringList bsvc;  bsvc << "all" << "delicious.com" << "pinboard.in"
                                << "raindrop.io" << "instapaper.com" << "floccus.org";
        defs << comboDef("provider", qFromUtf8("接入服务商"), bsvc, "all",
                         qFromUtf8("后台书签服务站点：all=聚合自动选优 / delicious.com / pinboard.in / raindrop.io / instapaper.com / floccus.org=开源自托管同步（占位属性，暂不生效）"));
    }
    // 其余未注册类型 → 空(隐藏)
    return defs;
}
