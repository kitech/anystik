#include "unknownparser.h"
#include "pureconsts.hpp"
#include "cJSON.h"
#include "compatcore34.h"
#include "tagutil.h"
#include <dlfcn.h>
#include <ctime>
#include <cstdlib>
#include <cassert> 

// ── JSON 路径导航 ──

static bool isDigits(const std::string& s) {
    return !s.empty() && s.find_first_not_of("0123456789") == std::string::npos;
}

static cJSON* jsonPath(cJSON* root, const char* path) {
    if (!root || !path) return NULL;
    cJSON* cur = root;
    std::string s = path;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t dot = s.find('.', pos);
        std::string key = (dot == std::string::npos) ? s.substr(pos) : s.substr(pos, dot - pos);
        if (cJSON_IsArray(cur) && isDigits(key)) {
            long long idx = std::stoll(key);
            cur = cJSON_GetArrayItem(cur, (int)idx);
        } else {
            cur = cJSON_GetObjectItem(cur, key.c_str());
        }
        if (!cur) return NULL;
        pos = (dot == std::string::npos) ? s.size() : dot + 1;
    }
    return cur;
}

static std::string jsonGetString(cJSON* root, const char* path) {
    cJSON* item = jsonPath(root, path);
    return (item && cJSON_IsString(item)) ? cJSON_GetStringValue(item) : "";
}

static int64_t jsonGetInt64(cJSON* root, const char* path) {
    cJSON* item = jsonPath(root, path);
    return (item && cJSON_IsNumber(item)) ? (int64_t)item->valuedouble : 0;
}

// jsonGetString 只认字符串；部分订阅流字段是 JSON number（如红果 rank/episode_cnt），
// 该辅助同时支持字符串与数字。
static std::string jsonGetStrNum(cJSON* root, const char* path) {
    cJSON* item = jsonPath(root, path);
    if (!item) return "";
    if (cJSON_IsString(item)) { return cJSON_GetStringValue(item); }
    if (cJSON_IsNumber(item)) { return std::to_string((long long)item->valuedouble); }
    return "";
}

static double jsonGetDouble(cJSON* root, const char* path) {
    cJSON* item = jsonPath(root, path);
    return (item && cJSON_IsNumber(item)) ? item->valuedouble : 0.0;
}

// ── 信封 Value.from 的 PeerId 解码 ──
// from = identity-multihash(0x00 0x24) + libp2p protobuf 公钥，逐字节：
//   0x00 0x24   identity 多哈希编码头（code=0x00，length=36）
//   0x08 0x01   protobuf 头字节：field1 varint（KeyType），0x01 = Ed25519
//   0x12 0x20   field2 varint（Data），长度 32
//   0x00…0xFF   ed25519 公钥 32 字节
// 上游以 string([]byte) 把原始二进制直塞 JSON 字符串：合法 UTF-8 段（>=0x80）原样保留为裸字节，
//   0x00–0x1F 及 " \ 等以 \uXXXX/短转义出现，取低字节 (v & 0xFF) 即原值。
// 非法 UTF-8 字节被 Go encoding/json 替换为 \ufffd（已丢失，不可还原）。
// cJSON 以 C 字符串承载 valuestring，strlen 遇前导 NUL 截断 → 绕开 cJSON，
// 从原始 JSON 文本逐字节重建（decodeEnvelopeFromBytes），再统一 base58btc 编码为 libp2p PeerId。

static const char kBase58Alphabet[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

static std::string base58Encode(const std::vector<uint8_t>& in) {
    size_t zeros = 0;
    while (zeros < in.size() && in[zeros] == 0) zeros++;
    std::vector<uint8_t> tmp(in.size() * 138 / 100 + 1, 0);
    size_t length = 0;
    for (size_t i = zeros; i < in.size(); i++) {
        unsigned int carry = in[i];
        for (size_t j = 0; j < length; j++) {
            carry += tmp[j] * 256;
            tmp[j] = (uint8_t)(carry % 58);
            carry /= 58;
        }
        while (carry > 0) {
            tmp[length++] = (uint8_t)(carry % 58);
            carry /= 58;
        }
    }
    std::string out(std::string::size_type(zeros), '1');
    while (length > 0) {
        length--;
        out += kBase58Alphabet[tmp[length]];
    }
    return out;
}

static bool decodeEnvelopeFromBytes(const std::string& jsonData, std::vector<uint8_t>& out) {
    size_t pos = jsonData.find("\"Value\"");
    if (pos == std::string::npos) return false;
    pos = jsonData.find("\"from\"", pos);
    if (pos == std::string::npos) return false;
    pos = jsonData.find(':', pos);
    if (pos == std::string::npos) return false;
    pos = jsonData.find('"', pos);
    if (pos == std::string::npos) return false;
    pos++;   // 越过值左引号
    const size_t n = jsonData.size();
    out.clear();
    while (pos < n) {
        unsigned char c = (unsigned char)jsonData[pos];
        if (c == '"') return !out.empty();
        if (c != '\\') { out.push_back(c); pos++; continue; }
        pos++;
        if (pos >= n) return false;
        char e = jsonData[pos++];
        switch (e) {
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'u': {
                if (pos + 4 > n) return false;
                unsigned int v = 0;
                for (int k = 0; k < 4; k++) {
                    char h = jsonData[pos + k];
                    v <<= 4;
                    if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
                    else return false;
                }
                pos += 4;
                // \uXXXX 低字节即原始字节：\uXXXX → (v & 0xFF)
                out.push_back((uint8_t)(v & 0xFF));
                break;
            }
            default: return false;
        }
    }
    return false;
}

// ── Matrix/gomuks sync_complete 解析 ──

static void parseGomuksEvents(cJSON* roomObj, const std::string& roomId, ParseResult& ret) {
    // 从 events 数组中提取 m.room.member displayname → nickname
    auto scanMembers = [](cJSON* arr, std::vector<PeerInfo>& peers) {
        if (!arr) return;
        int n = cJSON_GetArraySize(arr);
        for (int i = 0; i < n; i++) {
            cJSON* ev = cJSON_GetArrayItem(arr, i);
            if (jsonGetString(ev, "type") != "m.room.member") continue;
            std::string sender = jsonGetString(ev, "sender");
            if (sender.empty()) continue;
            std::string dn = jsonGetString(ev, "content.displayname");
            std::string av = jsonGetString(ev, "content.avatar_url");
            if (dn.empty() && av.empty()) continue;

            PeerInfo* pi = nullptr;
            for (auto& p : peers)
                if (p.publicKey == sender) { pi = &p; break; }
            if (!pi) {
                peers.push_back({});
                pi = &peers.back();
                pi->publicKey  = sender;
                pi->userName       = sender;
                pi->peerNumber = (int)peers.size() - 1;
            }
            if (!dn.empty()) pi->nickname = dn;
            if (!av.empty()) pi->iconUrl  = av;
        }
    };

    cJSON* events = jsonPath(roomObj, "events");
    if (!events) return;
    int n = cJSON_GetArraySize(events);

    // 从 state.events（room state）+ events（timeline）两个来源提取 nickname
    scanMembers(jsonPath(roomObj, "state.events"), ret.peers);
    scanMembers(events, ret.peers);

    // 消息（仅来自 events）
    for (int i = 0; i < n; i++) {
        cJSON* ev = cJSON_GetArrayItem(events, i);
        if (jsonGetString(ev, "type") != "m.room.message")
            continue;

        HistoryMessage hm;
        hm.redacted = !jsonGetString(ev, "redacted_by").empty();
        hm.message       = jsonGetString(ev, "content.body");
        {
            // ── 媒体消息检测 ──
            std::string msgtype = jsonGetString(ev, "content.msgtype");
            cJSON* info = jsonPath(ev, "content.info");
            if (msgtype.find("m.image") == 0) {
                hm.msgtype  = "image";
                hm.mediaUrl = jsonGetString(ev, "content.url");
                if (info) {
                    hm.mediaWidth  = (int)jsonGetInt64(ev, "content.info.w");
                    hm.mediaHeight = (int)jsonGetInt64(ev, "content.info.h");
                    hm.mediaMime   = jsonGetString(ev, "content.info.mimetype");
                    hm.fileSize    = (int)jsonGetInt64(ev, "content.info.size");
                }
            } else if (msgtype.find("m.video") == 0) {
                hm.msgtype      = "video";
                hm.mediaUrl     = jsonGetString(ev, "content.url");
                if (info) {
                    hm.mediaWidth   = (int)jsonGetInt64(ev, "content.info.w");
                    hm.mediaHeight  = (int)jsonGetInt64(ev, "content.info.h");
                    hm.mediaMime    = jsonGetString(ev, "content.info.mimetype");
                    hm.duration     = (int)jsonGetInt64(ev, "content.info.duration");
                    hm.thumbnailUrl = jsonGetString(ev, "content.info.thumbnail_url");
                    hm.fileSize     = (int)jsonGetInt64(ev, "content.info.size");
                }
            } else if (msgtype.find("m.audio") == 0) {
                hm.msgtype  = "audio";
                hm.mediaUrl = jsonGetString(ev, "content.url");
                if (info) {
                    hm.mediaMime = jsonGetString(ev, "content.info.mimetype");
                    hm.duration  = (int)jsonGetInt64(ev, "content.info.duration");
                    hm.fileSize  = (int)jsonGetInt64(ev, "content.info.size");
                }
            } else if (msgtype.find("m.file") == 0) {
                hm.msgtype  = "file";
                hm.mediaUrl = jsonGetString(ev, "content.url");
                if (info) {
                    hm.mediaMime = jsonGetString(ev, "content.info.mimetype");
                    hm.fileSize  = (int)jsonGetInt64(ev, "content.info.size");
                }
            }
        }
        {
            cJSON* content = cJSON_GetObjectItem(ev, "content");
            if (content) {
                std::vector<std::string> replyTexts;
                cJSON* rt = cJSON_GetObjectItem(content, "m.relates_to");
                if (rt) {
                    cJSON* ir = cJSON_GetObjectItem(rt, "m.in_reply_to");
                    if (ir) {
                        cJSON* eid = cJSON_GetObjectItem(ir, "event_id");
                        if (eid && cJSON_IsString(eid)) {
                            replyTexts.push_back(cJSON_GetStringValue(eid));
                            hm.relatesTos.push_back(cJSON_GetStringValue(eid));
                        }
                    }
                }
                cJSON* mt = cJSON_GetObjectItem(content, "m.mentions");
                if (mt) {
                    cJSON* uids = cJSON_GetObjectItem(mt, "user_ids");
                    if (uids && cJSON_IsArray(uids)) {
                        std::string ms;
                        int n = cJSON_GetArraySize(uids);
                        for (int j = 0; j < n; j++) {
                            cJSON* uid = cJSON_GetArrayItem(uids, j);
                            if (uid && cJSON_IsString(uid)) {
                                hm.mentions.push_back(cJSON_GetStringValue(uid));
                                if (!ms.empty()) ms += " ";
                                ms += cJSON_GetStringValue(uid);
                            }
                        }
                        if (!ms.empty())
                            replyTexts.push_back(ms);
                    }
                }
                // 若存在 @ 开头的回复文本，只保留这些
                bool hasAt = false;
                for (const auto& s : replyTexts) {
                    if (!s.empty() && s[0] == '@') { hasAt = true; break; }
                }
                for (const auto& s : replyTexts) {
                    if (hasAt && (s.empty() || s[0] != '@')) continue;
                    hm.message += " -- Re: " + s;
                }
            }
        }
        hm.sender_pubkey = jsonGetString(ev, "sender");
        hm.sender_number = i;
        hm.direction     = "received";
        {
            int64_t tsMs = jsonGetInt64(ev, "timestamp");
            char tbuf[32] = {0};
            if (tsMs > 0) {
                time_t sec = (time_t)(tsMs / 1000);
                struct tm tmv;
                localtime_r(&sec, &tmv);
                strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmv);
            }
            hm.created_at = tbuf;
        }
        hm.roomId        = roomId;
        hm.eventId       = jsonGetString(ev, "event_id");
        ret.messages.push_back(hm);

        bool found = false;
        for (const auto& p : ret.peers) {
            if (p.publicKey == hm.sender_pubkey) {
                found = true;
                break;
            }
        }
        if (!found) {
            PeerInfo pi;
            pi.publicKey  = hm.sender_pubkey;
            pi.userName       = hm.sender_pubkey;
            pi.peerNumber = (int)ret.peers.size();
            ret.peers.push_back(pi);
        }
    }
}

static bool tryParseGomuksSync(const std::string& rawStr, ParseResult& ret) {
    if (rawStr.empty()) return false;

    cJSON* root = cJSON_Parse(rawStr.c_str());
    if (!root) return false;

    if (jsonGetString(root, "command") != "sync_complete") {
        cJSON_Delete(root);
        return false;
    }

    cJSON* rooms = jsonPath(root, "data.rooms");
    if (!rooms || !cJSON_IsObject(rooms)) {
        cJSON_Delete(root);
        return false;
    }

    for (cJSON* r = rooms->child; r; r = r->next) {
        if (r->type != cJSON_Object) continue;

        std::string roomId = r->string ? r->string : "";
        ContactData cd;
        cd.id          = (int)(std::hash<std::string>{}(roomId) & 0x7fffffff);
        cd.name        = jsonGetString(r, "meta.name");
        if (cd.name.empty())
            cd.name    = jsonGetString(r, "meta.canonical_alias");
        if (cd.name.empty())
            cd.name    = roomId;
        cd.chatId      = roomId;
        cd.type        = kGomuksRoomType;
        cd.status      = "online";
        cd.isConnected = true;
        ret.contacts.push_back(cd);

        parseGomuksEvents(r, roomId, ret);
    }

    // ret.handled = !ret.contacts.empty() || !ret.peers.empty() || !ret.messages.empty();
    cJSON_Delete(root);
    return true;
}

// ── Matrix/mtxlite 单事件解析 ──
// 识别: Value.data 为单条 Matrix m.room.message 事件（非 sync_complete 包裹）。
//   识别条件: type=="m.room.message" 且 event_id/room_id/sender 非空、origin_server_ts 非 0。
//   与 gomuks sync_complete（command=="sync_complete"）天然互斥；媒体/提及/关系解析自成一体，
//   不复用 parseGomuksEvents（仅共用文件级 jsonGetString/jsonGetInt64 工具）。
//   时间字段走 Matrix 标准 origin_server_ts（毫秒），而非 gomuks 的 timestamp。
//   room_profile.name → contact 名（空则回退 room_id）；room_profile.topic → cd.statusText；
//   sender_profile.display_name/avatar_url → peer 昵称/头像。

static bool tryParseMtxliteRoom(const std::string& rawStr, ParseResult& ret) {
    if (rawStr.empty()) return false;

    cJSON* root = cJSON_Parse(rawStr.c_str());
    if (!root) return false;

    std::string eventId = jsonGetString(root, "event_id");
    std::string roomId  = jsonGetString(root, "room_id");
    std::string sender  = jsonGetString(root, "sender");
    int64_t tsMs = jsonGetInt64(root, "origin_server_ts");
    if (jsonGetString(root, "type") != "m.room.message"
            || eventId.empty() || roomId.empty() || sender.empty() || tsMs <= 0) {
        cJSON_Delete(root);
        return false;
    }

    std::string roomName = jsonGetString(root, "room_profile.name");
    if (roomName.empty()) {
        roomName = roomId;                       // 回退保持现状
    }

    ContactData cd;
    cd.id          = (int)(std::hash<std::string>{}(roomId) & 0x7fffffff);
    cd.name        = roomName;                    // contact name ← room_profile.name
    cd.type        = kMtxliteRoomType;
    cd.chatId      = roomId;
    cd.status      = "online";
    cd.isConnected = true;
    cd.statusText  = jsonGetString(root, "room_profile.topic");   // ← room_profile.topic
    ret.contacts.push_back(cd);

    HistoryMessage hm;
    hm.message       = jsonGetString(root, "content.body");
    hm.sender_pubkey = sender;
    hm.sender_number = 0;
    hm.direction     = "received";
    {
        char tbuf[32] = {0};
        if (tsMs > 0) {
            time_t sec = (time_t)(tsMs / 1000);
            struct tm tmv;
            localtime_r(&sec, &tmv);
            strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmv);
        }
        hm.created_at = tbuf;
    }
    hm.roomId        = roomId;
    hm.eventId       = eventId;

    // m.room.message 媒体检测（org.matrix 规范：content.url + content.info）
    std::string msgtype = jsonGetString(root, "content.msgtype");
    cJSON* info = jsonPath(root, "content.info");
    if (msgtype.find("m.image") == 0) {
        hm.msgtype      = "image";
        hm.mediaUrl     = jsonGetString(root, "content.url");
        if (info) {
            hm.mediaMime   = jsonGetString(root, "content.info.mimetype");
            hm.mediaWidth  = (int)jsonGetInt64(root, "content.info.w");
            hm.mediaHeight = (int)jsonGetInt64(root, "content.info.h");
            hm.fileSize    = (int)jsonGetInt64(root, "content.info.size");
        }
    } else if (msgtype.find("m.video") == 0) {
        hm.msgtype      = "video";
        hm.mediaUrl     = jsonGetString(root, "content.url");
        if (info) {
            hm.mediaMime    = jsonGetString(root, "content.info.mimetype");
            hm.mediaWidth   = (int)jsonGetInt64(root, "content.info.w");
            hm.mediaHeight  = (int)jsonGetInt64(root, "content.info.h");
            hm.duration     = (int)jsonGetInt64(root, "content.info.duration");
            hm.thumbnailUrl = jsonGetString(root, "content.info.thumbnail_url");
            hm.fileSize     = (int)jsonGetInt64(root, "content.info.size");
        }
    } else if (msgtype.find("m.audio") == 0) {
        hm.msgtype  = "audio";
        hm.mediaUrl = jsonGetString(root, "content.url");
        if (info) {
            hm.mediaMime = jsonGetString(root, "content.info.mimetype");
            hm.duration  = (int)jsonGetInt64(root, "content.info.duration");
            hm.fileSize  = (int)jsonGetInt64(root, "content.info.size");
        }
    } else if (msgtype.find("m.file") == 0) {
        hm.msgtype  = "file";
        hm.mediaUrl = jsonGetString(root, "content.url");
        if (info) {
            hm.mediaMime = jsonGetString(root, "content.info.mimetype");
            hm.fileSize  = (int)jsonGetInt64(root, "content.info.size");
        }
        std::string filename = jsonGetString(root, "content.filename");
        if (!filename.empty()) {
            hm.message = filename;
        }
    }

    // Matrix 键名内含 '.'（"m.relates_to"/"m.mentions"/…），jsonPath 按 '.' 分段取不到，
    // 必须像 gomuks 分支一样用 cJSON_GetObjectItem 直接导航。
    cJSON* content = jsonPath(root, "content");

    // 提及 content."m.mentions".user_ids[]
    cJSON* mentions = content ? cJSON_GetObjectItem(content, "m.mentions") : NULL;
    if (mentions) {
        cJSON* uids = cJSON_GetObjectItem(mentions, "user_ids");
        if (uids && cJSON_IsArray(uids)) {
            int mn = cJSON_GetArraySize(uids);
            for (int j = 0; j < mn; j++) {
                cJSON* m = cJSON_GetArrayItem(uids, j);
                if (m && cJSON_IsString(m)) {
                    hm.mentions.push_back(cJSON_GetStringValue(m));
                }
            }
        }
    }

    // 关系 content."m.relates_to"：回复 "m.in_reply_to".event_id；线程 rel_type=="m.thread"
    cJSON* relatesTo = content ? cJSON_GetObjectItem(content, "m.relates_to") : NULL;
    if (relatesTo) {
        std::string relType = jsonGetString(relatesTo, "rel_type");
        cJSON* inReply = cJSON_GetObjectItem(relatesTo, "m.in_reply_to");
        if (inReply) {
            std::string replyEid = jsonGetString(inReply, "event_id");
            if (!replyEid.empty()) {
                hm.relatesTos.push_back(replyEid);
            }
        }
        if (relType == "m.thread") {
            std::string threadRoot = jsonGetString(relatesTo, "event_id");
            if (!threadRoot.empty()) {
                hm.relatesTos.push_back(threadRoot);
            }
        }
        // 编辑（rel_type=="m.replace"）：真实正文在 m.new_content.body，优先取用
        if (relType == "m.replace" && msgtype.find("m.file") != 0) {
            cJSON* newContent = cJSON_GetObjectItem(relatesTo, "m.new_content");
            std::string newBody = newContent ? jsonGetString(newContent, "body") : "";
            if (!newBody.empty()) {
                hm.message = newBody;
            }
        }
    }

    bool textMsg = msgtype.find("m.image") != 0 && msgtype.find("m.video") != 0
                && msgtype.find("m.audio") != 0 && msgtype.find("m.file") != 0;

    // 引用/提及标尾：" -- Re: @id ..."，供客户端渲染为可点链接。
    // 优先 m.mentions（@user:server）；否则从正文首行 "> <@user:server> 原文" 提取；
    // 两者皆无而存在 relates_to 时回退为不可点的 event_id（仅作"这是回复"的指示）。
    // 正文已自带标尾（桥接消息）时不重复追加。
    if (textMsg && hm.message.find(" -- Re: ") == std::string::npos) {
        std::string reSuffix;
        if (!hm.mentions.empty()) {
            for (size_t j = 0; j < hm.mentions.size(); ++j) {
                if (j) { reSuffix += " "; }
                reSuffix += hm.mentions[j];
            }
        } else if (!hm.relatesTos.empty()) {
            size_t nl = hm.message.find('\n');
            std::string first = (nl == std::string::npos) ? hm.message : hm.message.substr(0, nl);
            if (first.size() >= 2 && first[0] == '>' && first[1] == ' ') {
                size_t a = first.find("<@");
                if (a != std::string::npos) {
                    size_t b = first.find('>', a + 2);
                    if (b != std::string::npos && b > a + 2) {
                        reSuffix = first.substr(a + 1, b - a - 1);
                    }
                }
            }
            if (reSuffix.empty()) {
                reSuffix = hm.relatesTos[0];
            }
        }
        if (!reSuffix.empty()) {
            hm.message += " -- Re: " + reSuffix;
        }
    }

    ret.messages.push_back(hm);

    PeerInfo pi;
    pi.publicKey  = sender;
    pi.userName   = sender;
    pi.nickname   = jsonGetString(root, "sender_profile.display_name"); // 气泡显示名 ← "Feeds"
    pi.iconUrl    = jsonGetString(root, "sender_profile.avatar_url");   // 头像 mxc://
    pi.peerNumber = 0;
    ret.peers.push_back(pi);

    ret.senderName  = qFromUtf8(sender);
    ret.handled     = true;

    cJSON_Delete(root);
    return true;
}

// ── Tox 消息事件解析 ──

static bool tryParseToxMessage(const std::string& rawStr, ParseResult& ret) {
    cJSON* root = cJSON_Parse(rawStr.c_str());
    if (!root) return false;

    std::string timestamp = jsonGetString(root, "timestamp");
    std::string eventType = jsonGetString(root, "event_type");

    std::string innerDataStr = jsonGetString(root, "data");
    if (!innerDataStr.empty()) {
        cJSON* inner = cJSON_Parse(innerDataStr.c_str());
        if (inner) {
            ContactData cd;
            cd.isConnected = true;
            cd.status = "online";

            if (eventType == "friend_message") {
                int64_t friendId = jsonGetInt64(inner, "friend_id");
                cd.id     = (int)friendId;
                cd.name   = "friend_" + std::to_string(friendId);
                cd.type   = kUnktoxFriendType;
                cd.chatId = std::to_string(friendId);
            } else if (eventType == "conference_message") {
                int64_t confNum = jsonGetInt64(inner, "conference_number");
                cd.id     = (int)confNum;
                cd.name   = "conf_" + std::to_string(confNum);
                cd.type   = kUnktoxConferenceType;
                cd.chatId = std::to_string(confNum);
            } else if (eventType == "group_message") {
                int64_t groupNum = jsonGetInt64(inner, "group_number");
                cd.id     = (int)groupNum;
                cd.name   = "group_" + std::to_string(groupNum);
                cd.type   = kUnktoxGroupType;
                cd.chatId = std::to_string(groupNum);
            }

            if (!cd.chatId.empty()) {
                ret.contactName = qFromUtf8(cd.name);
                ret.contacts.push_back(cd);
            }

            HistoryMessage hm;
            hm.message       = jsonGetString(inner, "message");
            hm.sender_pubkey = jsonGetString(inner, "sender_pubkey");
            hm.sender_number = (uint32_t)jsonGetInt64(inner, "sender");
            hm.direction     = jsonGetString(inner, "direction");
            hm.created_at    = timestamp;
            hm.roomId        = cd.chatId;
            hm.eventId       = std::to_string(jsonGetInt64(root, "event_id"));

            // 创建 peer 信息，供后续显示使用 nickname
            if (!hm.sender_pubkey.empty()) {
                PeerInfo pi;
                pi.publicKey  = hm.sender_pubkey;
                pi.userName       = hm.sender_pubkey;
                pi.peerNumber = (int)hm.sender_number;
                std::string peerName = jsonGetString(inner, "peer_name");
                if (!peerName.empty())
                    pi.nickname = peerName;
                ret.peers.push_back(pi);
            }

            if (!hm.message.empty())
                ret.messages.push_back(hm);

            cJSON_Delete(inner);
        }
    }

    ret.handled = !ret.contacts.empty() || !ret.peers.empty() || !ret.messages.empty();
    cJSON_Delete(root);
    return ret.handled;
}

// ── IMAP 邮件解析 ──

// uchardet 动态库检测编码（无外部链接依赖）
static std::string detectEncoding(const QByteArray& data) {
    void* h = dlopen("libuchardet.so.0", RTLD_LAZY);
    if (!h) h = dlopen("libuchardet.so", RTLD_LAZY);
    if (!h) return "";

    auto uchardet_new          = (void*(*)())dlsym(h, "uchardet_new");
    auto uchardet_delete       = (void(*)(void*))dlsym(h, "uchardet_delete");
    auto uchardet_handle_data  = (int(*)(void*,const char*,size_t))dlsym(h, "uchardet_handle_data");
    auto uchardet_data_end     = (void(*)(void*))dlsym(h, "uchardet_data_end");
    auto uchardet_get_charset  = (const char*(*)(void*))dlsym(h, "uchardet_get_charset");
    if (!(uchardet_new && uchardet_delete && uchardet_handle_data &&
          uchardet_data_end && uchardet_get_charset)) {
        dlclose(h);
        return "";
    }

    void* ud = uchardet_new();
    if (!ud) { dlclose(h); return ""; }

    std::string result;
    if (uchardet_handle_data(ud, data.data(), data.size()) == 0) {
        uchardet_data_end(ud);
        const char* cs = uchardet_get_charset(ud);
        if (cs && cs[0]) result = cs;
    }
    uchardet_delete(ud);
    dlclose(h);
    return result;
}

static bool tryParseImapMessage(const std::string& rawStr, ParseResult& ret) {
    cJSON* root = cJSON_Parse(rawStr.c_str());
    if (!root) return false;

    std::string subject    = jsonGetString(root, "subject");
    std::string from       = jsonGetString(root, "from");
    std::string toRecip    = jsonGetString(root, "toRecipients.0");
    std::string bodyB64    = jsonGetString(root, "bodyPreview");
    std::string receivedAt = jsonGetString(root, "receivedDateTime");

    if (subject.empty() && from.empty() && toRecip.empty()) {
        cJSON_Delete(root);
        return false;
    }

    std::string cleanB64;
    for (unsigned char c : bodyB64) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '+' || c == '/' ||
            c == '=' || c == '-' || c == '_')
            cleanB64 += c;
    }

    std::string fullText = subject;
    fullText += "\n[raw](" + std::to_string(bodyB64.size()) + "): "
             + bodyB64.substr(0, 512) + "\n";
    QByteArray decoded = base64Decode(cleanB64);
    if (!decoded.isEmpty()) {
        fullText += "\n";
        // uchardet 检测结果（如果有）
        std::string enc = detectEncoding(decoded);
        if (!enc.empty()) {
            QString t = qToUnicode(decoded, enc.c_str());
            if (!t.isEmpty()) {
                std::string s(qToUtf8(t).data());
                fullText += std::string("[uchardet] ") + enc + "(" + std::to_string(s.size()) + "): " + s + "\n";
            }
        }
        // 所有候选编码依次解码
        static const char* kCodecs[] = {
            "UTF-8", "GBK", "Shift-JIS", "Big5", "EUC-KR", "ISO-8859-1"
        };
        for (const char* name : kCodecs) {
            QString t = qToUnicode(decoded, name);
            if (t.isEmpty()) continue;
            std::string s(qToUtf8(t).data());
            fullText += std::string(name) + "(" + std::to_string(s.size()) + "): " + s + "\n";
        }
        if (fullText.size() > 1 && fullText.back() == '\n')
            fullText.pop_back();
    } else if (!cleanB64.empty()) {
        // base64 数据存在但解码后为空 → 解码失败，附上原始 base64 文本
        fullText += "\n(dcode failed, raw: " + cleanB64 + ")";
    }
    
    // todo 根据account区分room,但现在没有
    std::string chatId = jsonGetString(root, "account_id"); // std::to_string(kOutlookGraphId);
    std::string chatName = jsonGetString(root, "account_name"); // "ImapGraph收件箱";
	if (chatName.empty()) { chatName = chatId; }
	assert(!chatId.empty());

    ContactData cd;
    cd.id          = (int)(std::hash<std::string>{}(chatId + kImapMailType) & 0x7fffffff);
    cd.name        = chatName; // toRecip;
    cd.type        = kImapMailType;
    cd.chatId      = chatId;
    cd.status      = "online";
    cd.isConnected = true;
    ret.contacts.push_back(cd);

    HistoryMessage hm;
    hm.message       = fullText;
    hm.sender_pubkey = from;
    hm.sender_number = 0;
    hm.direction     = "received";
    hm.created_at    = receivedAt;
    hm.roomId        = cd.chatId;
    hm.eventId       = jsonGetString(root, "id");
    ret.messages.push_back(hm);

    // 加入 sender peer 供 ChatView 查找显示名称
    PeerInfo pi;
    pi.publicKey  = from;
    pi.userName       = from;
    pi.peerNumber = 0;
    ret.peers.push_back(pi);

    ret.senderName  = qFromUtf8(from);
    if (ret.contactName.isEmpty())
        ret.contactName = qFromUtf8(cd.name);

    ret.handled = true;
    cJSON_Delete(root);
    return true;
}

// ── filesync 事件解析 ──

static bool tryParseFilesyncEvent(const std::string& rawStr, ParseResult& ret) {
    cJSON* root = cJSON_Parse(rawStr.c_str());
    if (!root) return false;

    std::string type   = jsonGetString(root, "type");
    std::string event  = jsonGetString(root, "event");
    std::string path   = jsonGetString(root, "path");
    int64_t     size   = jsonGetInt64(root, "size");
    std::string mime   = jsonGetString(root, "mime");
    std::string sha256 = jsonGetString(root, "sha256");
    std::string chunk0 = jsonGetString(root, "chunk0");
    cJSON_Delete(root);

    if (type != "filesync") return false;

    std::string topic = qToUtf8(ret.contactName).data();
    if (topic.empty()) return false;

    ContactData cd;
    cd.id          = (int)(std::hash<std::string>{}(topic + kFilesyncType) & 0x7fffffff);
    cd.name        = "filesync";
    cd.type        = kFilesyncType;
    cd.chatId      = topic;
    cd.status      = "online";
    cd.isConnected = true;
    ret.contacts.push_back(cd);

    HistoryMessage hm;
    hm.message    = event + ": " + path + " chunk0=" + std::to_string(chunk0.length());
    hm.direction  = "received";
    hm.roomId     = topic;
    hm.fileSize   = size;
    hm.mediaMime  = mime;
    ret.messages.push_back(hm);

    ret.handled = true;
    return true;
}

// ── clipboard 事件解析 ──

static bool tryParseClipboardEvent(const std::string& rawStr, ParseResult& ret) {
    cJSON* root = cJSON_Parse(rawStr.c_str());
    if (!root) return false;

    std::string type = jsonGetString(root, "type");
    std::string fmt  = jsonGetString(root, "format");
    std::string data = jsonGetString(root, "data");
    cJSON_Delete(root);

    if (type != "clipboard") return false;

    std::string topic = qToUtf8(ret.contactName).data();
    if (topic.empty()) return false;

    ContactData cd;
    cd.id          = (int)(std::hash<std::string>{}(topic + kClipboardType) & 0x7fffffff);
    cd.name        = "clipboard";
    cd.type        = kClipboardType;
    cd.chatId      = topic;
    cd.status      = "online";
    cd.isConnected = true;
    ret.contacts.push_back(cd);

    HistoryMessage hm;
    hm.message    = data;
    hm.direction  = "received";
    hm.roomId     = topic;
    ret.messages.push_back(hm);

    ret.handled = true;
    return true;
}

// ── Misskey Note 事件解析 ──
// 检测: user 为 object + createdAt 存在

static std::string misskeyMimeToMsgtype(const std::string& mime) {
    if (mime.find("image/") == 0) return "image";
    if (mime.find("video/") == 0) return "video";
    if (mime.find("audio/") == 0) return "audio";
    return "file";
}

static bool tryParseMisskeyNote(const std::string& rawStr, ParseResult& ret) {
    cJSON* root = cJSON_Parse(rawStr.c_str());
    if (!root) return false;

    cJSON* userIdItem = cJSON_GetObjectItem(root, "userId");
    cJSON* userItem   = cJSON_GetObjectItem(root, "user");
    cJSON* createdItem = cJSON_GetObjectItem(root, "createdAt");
    cJSON* visibItem = cJSON_GetObjectItem(root, "visibility");
    if (!userIdItem || !cJSON_IsString(userIdItem) ||
        !userItem || !cJSON_IsObject(userItem) ||
        !createdItem || !cJSON_IsString(createdItem) ||
        !visibItem || !cJSON_IsString(visibItem)) {
        cJSON_Delete(root);
        return false;
    }

    std::string userId    = cJSON_GetStringValue(userIdItem);
    std::string createdAt = cJSON_GetStringValue(createdItem);
    std::string userName  = jsonGetString(userItem, "name");
    std::string userAlias = jsonGetString(userItem, "username");
    std::string userHost  = jsonGetString(userItem, "host");
    std::string avatarUrl = jsonGetString(userItem, "avatarUrl");
    std::string noteId    = jsonGetString(root, "id");
    std::string text      = jsonGetString(root, "text");
    std::string cw        = jsonGetString(root, "cw");

    std::string displayName = userName.empty() ? userAlias : userName;
    if (displayName.empty()) displayName = userId;
    std::string peerId = userHost.empty() ? userId : userId + "@" + userHost;

    std::string chatId = jsonGetString(root, "account_id"); // std::to_string(kMisskeyTimelineId); // userId;
    std::string chatName = jsonGetString(root, "account_name"); // "Misskey时间线";
	if (chatName.empty()) { chatName = chatId; }
	assert(!chatId.empty());
    static const std::string kAvatarSuffix = "&avatar=1";
    if (!avatarUrl.empty()
        && avatarUrl.size() >= kAvatarSuffix.size()
        && avatarUrl.compare(avatarUrl.size() - kAvatarSuffix.size(),
                             kAvatarSuffix.size(), kAvatarSuffix) == 0) {
        // with suffix it will be webp, or png/jpg
        // https://p.misskey.gg/avatar.webp?url=https%3A%2F%2Fxxxxxbe9aee0.png&avatar=1
        // 但是需要保留p.misskey.gg的转发,可能后端的url可能直接无法访问
        avatarUrl.erase(avatarUrl.size() - kAvatarSuffix.size(),
                        kAvatarSuffix.size());
    }

    ContactData cd;
    cd.id          = (int)(std::hash<std::string>{}(chatId + kMisskeyType) & 0x7fffffff);
    cd.name        = chatName;
    cd.type        = kMisskeyType;
    cd.chatId      = chatId;
    cd.status      = "online";
    cd.isConnected = true;
    if (!avatarUrl.empty()) cd.iconUrl = avatarUrl;
    ret.contacts.push_back(cd);

    HistoryMessage hm;
    hm.sender_pubkey = peerId;
    hm.sender_number = 0;
    hm.direction     = "received";
    hm.created_at    = createdAt;
    hm.roomId        = chatId;
    hm.eventId       = noteId;

    std::string fullText;
    if (!cw.empty()) {
        fullText += "CW: " + cw + "\n---\n";
    }
    if (!text.empty()) {
        fullText += text;
    }
    hm.message = fullText;

    cJSON* files = cJSON_GetObjectItem(root, "files");
    if (files && cJSON_IsArray(files) && cJSON_GetArraySize(files) > 0) {
        cJSON* f0 = cJSON_GetArrayItem(files, 0);
        hm.msgtype  = misskeyMimeToMsgtype(jsonGetString(f0, "type"));
        hm.mediaUrl = jsonGetString(f0, "url");
        hm.thumbnailUrl = jsonGetString(f0, "thumbnailUrl");
        hm.mediaMime = jsonGetString(f0, "type");
        cJSON* props = cJSON_GetObjectItem(f0, "properties");
        if (props) {
            hm.mediaWidth  = (int)jsonGetInt64(props, "width");
            hm.mediaHeight = (int)jsonGetInt64(props, "height");
        }
        hm.fileSize = (int)jsonGetInt64(f0, "size");
    }

    cJSON* replyIdItem = cJSON_GetObjectItem(root, "replyId");
    if (replyIdItem && cJSON_IsString(replyIdItem)) {
        hm.relatesTos.push_back(cJSON_GetStringValue(replyIdItem));
        cJSON* replyObj = cJSON_GetObjectItem(root, "reply");
        if (replyObj && cJSON_IsObject(replyObj)) {
            std::string replyText = jsonGetString(replyObj, "text");
            if (!replyText.empty()) {
                hm.message += " -- Re: " + replyText;
            }
        }
    }

    cJSON* renoteIdItem = cJSON_GetObjectItem(root, "renoteId");
    if (renoteIdItem && cJSON_IsString(renoteIdItem)) {
        hm.relatesTos.push_back(cJSON_GetStringValue(renoteIdItem));
        cJSON* renoteObj = cJSON_GetObjectItem(root, "renote");
        if (renoteObj && cJSON_IsObject(renoteObj)) {
            std::string renoteText = jsonGetString(renoteObj, "text");
            if (!renoteText.empty()) {
                if (hm.message.empty()) {
                    hm.message = "RT: " + renoteText;
                } else {
                    hm.message += "\nRT: " + renoteText;
                }
            }
        }
    }

    cJSON* mentionsArr = cJSON_GetObjectItem(root, "mentions");
    if (mentionsArr && cJSON_IsArray(mentionsArr)) {
        int mn = cJSON_GetArraySize(mentionsArr);
        for (int j = 0; j < mn; j++) {
            cJSON* m = cJSON_GetArrayItem(mentionsArr, j);
            if (m && cJSON_IsString(m))
                hm.mentions.push_back(cJSON_GetStringValue(m));
        }
    }

    if (hm.message.empty() && hm.mediaUrl.empty()) {
        cJSON_Delete(root);
        return false;
    }

    ret.messages.push_back(hm);

    PeerInfo pi;
    pi.publicKey  = peerId;
    pi.userName       = displayName;
    pi.peerNumber = 0;
    if (!avatarUrl.empty()) pi.iconUrl = avatarUrl;
    ret.peers.push_back(pi);

    ret.senderName  = qFromUtf8(displayName);
    ret.contactName = qFromUtf8(chatName);
    ret.handled = true;

    cJSON_Delete(root);
    return true;
}

// ── 头条热闻订阅流解析 ──
// 识别: Value.data 为 toutiao 新闻 JSON（proto_type==news + title 非空 + source_url/group_id 可构 URL）
//   proto_type 与知乎 hotlist 共用字段但取值互斥（toutiao=news, zhihu=hotlist）；
//   图片取 middle_image（p3.toutiaoimg.com 存活），image_url 为 p*.pstatp.com 死域则忽略；
//   URL 由 source_url 补域名（自动检测 http(s):// 与 // 前缀）；
//   无稳定用户头像字段（media_avatar_url 为签名过期 URL），pi.iconUrl 保持默认空

static bool tryParseToutiaoNews(const std::string& rawStr, ParseResult& ret) {
    cJSON* root = cJSON_Parse(rawStr.c_str());
    if (!root) return false;

    std::string protoType = jsonGetString(root, "proto_type");
    std::string title     = jsonGetString(root, "title");
    std::string groupId   = jsonGetString(root, "group_id");
    std::string itemId    = jsonGetString(root, "item_id");
    std::string url       = jsonGetString(root, "source_url");
    // 正向匹配：news + title + (group_id / item_id / source_url 任一)。这三个字段为头条独有，
    // 酷安 feed（proto_type==news）仅有相对路径 url/id，无三者 → 天然互斥，无需负向守卫。
    if (protoType != "news" || title.empty() ||
        (groupId.empty() && itemId.empty() && url.empty())) {
        cJSON_Delete(root);
        return false;
    }
    // URL 构造：已带 http(s):// 原样用；// 开头补 https:；否则前缀主域名
    if (url.empty()) {
        url = "https://www.toutiao.com/group/" + groupId + "/";
    } else if (url.compare(0, 4, "http") != 0) {
        if (url.compare(0, 2, "//") == 0) {
            url = "https:" + url;
        } else {
            url = "https://www.toutiao.com" + url;
        }
    }

    // 只接纳 middle_image（p3.toutiaoimg.com 等存活主机）；
    // image_url 多为 p*.pstatp.com 旧版死域（DNS 已下线/NXDOMAIN），命中即忽略、仅保留文本。
    std::string image = jsonGetString(root, "middle_image");
    if (image.find("pstatp.com/") != std::string::npos) {
        image.clear();
    }

    std::string tag    = jsonGetString(root, "chinese_tag");
    std::string durStr = jsonGetString(root, "video_duration_str");
    std::string source = jsonGetString(root, "source");
    int64_t behotTime = jsonGetInt64(root, "behot_time");

    ContactData cd;
    cd.id          = kToutiaoHotnewsId;
    cd.name        = "头条热闻";
    cd.type        = kToutiaoHotnewsType;
    cd.chatId      = kToutiaoHotnewsType;
    cd.status      = "online";
    cd.isConnected = true;
    ret.contacts.push_back(cd);

    std::string meta;
    std::string timeStr;
    if (behotTime > 0) {
        char tbuf[32] = {0};
        time_t sec = (time_t)behotTime;
        struct tm tmv;
        localtime_r(&sec, &tmv);
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmv);
        timeStr = tbuf;
    }
    if (!tag.empty()) {
        meta += "[" + tag + "]";
    }
    if (!durStr.empty()) {
        if (!meta.empty()) meta += " · ";
        meta += "时长 " + durStr;
    }
    if (!timeStr.empty()) {
        if (!meta.empty()) meta += " · ";
        meta += timeStr;
    }

    HistoryMessage hm;
    hm.created_at    = timeStr;
    hm.message       = title + "\n" + url + (meta.empty() ? "" : "\n" + meta);
    hm.sender_pubkey = "fedone";
    hm.sender_number = 0;
    hm.direction     = "received";
    hm.roomId        = kToutiaoHotnewsType;
    std::string eventId = groupId.empty() ? jsonGetString(root, "item_id") : groupId;
    hm.eventId       = eventId;
    if (!image.empty()) {
        hm.msgtype      = "image";
        hm.mediaMime    = "image/jpeg";
        hm.mediaUrl     = image;
        hm.mediaWidth   = UnkSize;
        hm.mediaHeight  = UnkSize;
        hm.fileSize     = UnkSize;
    } else {
        hm.msgtype  = "";
        hm.mediaUrl = "";
    }
    ret.messages.push_back(hm);

    std::string sourceName = source.empty() ? "fedone" : source;

    PeerInfo pi;
    pi.publicKey  = "fedone";
    pi.userName   = sourceName;
    pi.nickname   = sourceName;
    pi.peerNumber = 0;
    ret.peers.push_back(pi);

    ret.senderName  = qFromUtf8(sourceName);
    ret.handled     = true;

    cJSON_Delete(root);
    return true;
}

// ── 头条热榜热搜话题卡片解析 ──
// 识别: Value.data 为 toutiao 热榜话题 JSON（proto_type==hotlist + 顶层 Title/Url 非空 + ClusterId 非空）
//   proto_type==hotlist 与知乎热榜卡片取值相同，靠 schema 形状互斥：
//   知乎卡片要求 card_id/id(=rank_秒.微秒)/target.*，本变体无这些字段（Title/Url/Image 在顶层）。
//   配图取 Image.url（p3-sign.toutiaoimg.com 存活域），尺寸取 Image.width/height（真实尺寸）；
//   fileSize 沿用哨兵 1（与 news/hotlist 一致，图片显示受 mainwindow sizeOk 校验约束）。

static bool tryParseToutiaoHotlist(const std::string& rawStr, ParseResult& ret) {
    cJSON* root = cJSON_Parse(rawStr.c_str());
    if (!root) return false;

    std::string protoType  = jsonGetString(root, "proto_type");
    std::string title      = jsonGetString(root, "Title");
    std::string url        = jsonGetString(root, "Url");
    std::string clusterStr = jsonGetString(root, "ClusterIdStr");
    if (protoType != "hotlist" || title.empty() || url.empty()) {
        cJSON_Delete(root);
        return false;
    }
    if (clusterStr.empty()) {
        int64_t clusterId = jsonGetInt64(root, "ClusterId");
        if (clusterId <= 0) {
            cJSON_Delete(root);
            return false;
        }
        clusterStr = std::to_string(clusterId);
    }

    std::string image = jsonGetString(root, "Image.url");
    int64_t imgW = jsonGetInt64(root, "Image.width");
    int64_t imgH = jsonGetInt64(root, "Image.height");

    std::string labelDesc = jsonGetString(root, "LabelDesc");
    int64_t cycleCount = jsonGetInt64(root, "cycle_count");

    ContactData cd;
    cd.id          = kToutiaoHotnewsId;
    cd.name        = "头条热闻";
    cd.type        = kToutiaoHotnewsType;
    cd.chatId      = kToutiaoHotnewsType;
    cd.status      = "online";
    cd.isConnected = true;
    ret.contacts.push_back(cd);

    std::string meta;
    if (!labelDesc.empty()) {
        meta += "[" + labelDesc + "]";
    }
    if (cycleCount > 0) {
        if (!meta.empty()) meta += " · ";
        meta += std::to_string(cycleCount) + " 轮";
    }

    HistoryMessage hm;
    hm.message       = title + "\n" + url + (meta.empty() ? "" : "\n" + meta);
    hm.sender_pubkey = "fedone";
    hm.sender_number = 0;
    hm.direction     = "received";
    hm.roomId        = kToutiaoHotnewsType;
    hm.eventId       = clusterStr;
    if (!image.empty()) {
        hm.msgtype      = "image";
        hm.mediaMime    = "image/jpeg";
        hm.mediaUrl     = image;
        hm.mediaWidth   = (imgW > 0) ? (int)imgW : UnkSize;
        hm.mediaHeight  = (imgH > 0) ? (int)imgH : UnkSize;
        hm.fileSize     = UnkSize;
    } else {
        hm.msgtype  = "";
        hm.mediaUrl = "";
    }
    ret.messages.push_back(hm);

    PeerInfo pi;
    pi.publicKey  = "fedone";
    pi.userName   = "fedone";
    pi.peerNumber = 0;
    ret.peers.push_back(pi);

    ret.senderName  = qFromUtf8("fedone");
    ret.handled     = true;

    cJSON_Delete(root);
    return true;
}

// ── 知乎 API URL → 浏览器 URL ──
// api.zhihu.com 的 REST 接口 URL → 浏览器可访问 URL。
// 规则表覆盖：问题/回答/用户(members|people)/话题/收藏夹/想法(pin)/
// 专栏文章(articles|p)/视频(zvideos|videos)/live/专栏(columns|zhuanlan)，
// 兼容 www.zhihu.com/api/v4/... 形式与 "//zhihu" 协议相对地址。
// 非 zhihu 或无法识别：原样返回；query/fragment 一律丢弃。

struct ZhUrlRule { const char* apiPrefix; const char* webBase; };
static const ZhUrlRule kZhRules[] = {
    { "/questions/",  "https://www.zhihu.com/question/"  },
    { "/question/",   "https://www.zhihu.com/question/"  },
    { "/answers/",    "https://www.zhihu.com/answer/"    },
    { "/members/",    "https://www.zhihu.com/people/"    },
    { "/people/",     "https://www.zhihu.com/people/"    },
    { "/topics/",     "https://www.zhihu.com/topic/"     },
    { "/collections/","https://www.zhihu.com/collection/"},
    { "/pins/",       "https://www.zhihu.com/pin/"       },
    { "/articles/",   "https://zhuanlan.zhihu.com/p/"    },
    { "/p/",          "https://zhuanlan.zhihu.com/p/"    },
    { "/zvideos/",    "https://www.zhihu.com/zvideo/"    },
    { "/videos/",     "https://www.zhihu.com/zvideo/"    },
    { "/lives/",      "https://www.zhihu.com/lives/"     },
    { "/columns/",    "https://zhuanlan.zhihu.com/"      },
    { "/zhuanlan/",   "https://zhuanlan.zhihu.com/"      },
};

// 取路径首段 id/slug（截断其余子路径，避免把 /answers 之类的子资源凑进来）
static std::string zhUrlFirstSegment(const std::string& rest) {
    std::size_t e = rest.find('/');
    std::string seg = (e == std::string::npos) ? rest : rest.substr(0, e);
    return seg;
}

// 规则表转换：apiPath 已剥掉 query，命中返回浏览器 URL，未命中返回空串
static std::string zhUrlApplyRules(const std::string& path) {
    for (size_t i = 0; i < sizeof(kZhRules) / sizeof(kZhRules[0]); ++i) {
        std::string prefix = kZhRules[i].apiPrefix;
        if (path.rfind(prefix, 0) != 0) { continue; }
        std::string seg = zhUrlFirstSegment(path.substr(prefix.size()));
        if (seg.empty()) { continue; }
        return std::string(kZhRules[i].webBase) + seg;
    }
    return std::string();
}

static std::string zhihuBrowserUrl(const std::string& url) {
    std::string s = url;
    if (s.rfind("//", 0) == 0) { s = "https:" + s; }
    std::size_t sp = s.find("://");
    std::string rest = (sp == std::string::npos) ? s : s.substr(sp + 3);
    std::size_t slash = rest.find('/');
    if (slash == std::string::npos) { return s; }
    std::string host = rest.substr(0, slash);
    std::string path = rest.substr(slash);
    std::size_t q = path.find_first_of("?#");
    if (q != std::string::npos) { path = path.substr(0, q); }

    if (host != "api.zhihu.com" && host != "www.zhihu.com") { return s; }
    if (host == "www.zhihu.com") {
        const std::string kApiV4 = "/api/v4";
        if (path.rfind(kApiV4, 0) != 0) { return s; }
        path = path.substr(kApiV4.size());
    }
    std::string hit = zhUrlApplyRules(path);
    if (!hit.empty()) { return hit; }
    return "https://www.zhihu.com" + path;   // 兜底：换到浏览器宿主，至少不再返回 API JSON
}

// ── 知乎通知订阅流解析 ──
// 识别: Value.data 为 zhihu_collection 收藏动态 JSON（kind==zhihu_collection + title/url 非空）
//   纯文本类型（无 image），消息 = 标题 + 链接 + 全文摘录 + 元信息

static bool tryParseZhihuNotify(const std::string& rawStr, ParseResult& ret) {
    cJSON* root = cJSON_Parse(rawStr.c_str());
    if (!root) return false;

    std::string kind  = jsonGetString(root, "kind");
    std::string title = jsonGetString(root, "title");
    std::string url   = jsonGetString(root, "url");
    if (kind != "zhihu_collection" || title.empty() || url.empty()) {
        cJSON_Delete(root);
        return false;
    }

    std::string author      = jsonGetString(root, "author");
    std::string excerpt     = jsonGetString(root, "excerpt");
    std::string contentType = jsonGetString(root, "content_type");
    int64_t publishedAt     = jsonGetInt64(root, "published_at");

    ContactData cd;
    cd.id          = kZhihuNotifyId;
    cd.name        = "知乎通知";
    cd.type        = kZhihuNotifyType;
    cd.chatId      = kZhihuNotifyType;
    cd.status      = "online";
    cd.isConnected = true;
    ret.contacts.push_back(cd);

    std::string message = title + "\n" + zhihuBrowserUrl(url);
    if (!excerpt.empty()) {
        message += "\n" + excerpt;
    }
    std::string meta = "作者 " + author + " · " + contentType;
    if (publishedAt > 0) {
        char tbuf[32] = {0};
        time_t sec = (time_t)publishedAt;
        struct tm tmv;
        localtime_r(&sec, &tmv);
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmv);
        meta += " · " + std::string(tbuf);
    }
    message += "\n" + meta;

    HistoryMessage hm;
    hm.message       = message;
    hm.sender_pubkey = author;
    hm.sender_number = 0;
    hm.direction     = "received";
    hm.created_at    = "";
    if (publishedAt > 0) {
        char tbuf[32] = {0};
        time_t sec = (time_t)publishedAt;
        struct tm tmv;
        localtime_r(&sec, &tmv);
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmv);
        hm.created_at = tbuf;
    }
    hm.roomId   = kZhihuNotifyType;
    hm.eventId  = std::to_string(jsonGetInt64(root, "content_id"));
    hm.msgtype  = "";
    hm.mediaUrl = "";
ret.messages.push_back(hm);

    PeerInfo pi;
    pi.publicKey  = "fedone";
    pi.userName   = "fedone";
    pi.peerNumber = 0;
    ret.peers.push_back(pi);

    ret.senderName  = qFromUtf8("fedone");
    ret.handled     = true;

    cJSON_Delete(root);
    return true;
}

// ── 知乎热闻订阅流解析 ──
// 识别: Value.data 为 zhihu 热榜 JSON（proto_type==hotlist + card_id/id 非空 + target 对象且 title/url 非空）
//   id 格式 "{rank}_{unix秒}.{微秒}"（前缀=热榜名次，中段=推送时间，微秒忽略）；
//   识别走 proto_type，与 toutiao 的 kind 天然互斥；有条件附带热榜缩略图

static bool tryParseZhihuHotnews(const std::string& rawStr, ParseResult& ret) {
    cJSON* root = cJSON_Parse(rawStr.c_str());
    if (!root) return false;

    std::string protoType = jsonGetString(root, "proto_type");
    std::string cardId    = jsonGetString(root, "card_id");
    std::string feedId    = jsonGetString(root, "id");
    std::string title     = jsonGetString(root, "target.title");
    std::string url       = jsonGetString(root, "target.url");
    if (protoType != "hotlist" || cardId.empty() || feedId.empty() ||
        title.empty() || url.empty()) {
        cJSON_Delete(root);
        return false;
    }

    // 知乎热榜 author 对象（新 schema 位于 target.author）：
    //   username ← author.name（空回退 fedone）；usernick ← author.headline（不回退）；
    //   iconurl ← author.avatar_url。url_token 为个人主页 slug，不作昵称使用。
    std::string authorName     = jsonGetString(root, "target.author.name");
    std::string authorHeadline = jsonGetString(root, "target.author.headline");
    std::string authorAvatar   = jsonGetString(root, "target.author.avatar_url");
    std::string peerName       = authorName.empty() ? "fedone" : authorName;

    std::string meta;
    std::string feedTime;
    int64_t rank = 0;
    size_t usPos = feedId.find('_');
    if (usPos != std::string::npos && usPos > 0) {
        std::string rankStr = feedId.substr(0, usPos);
        if (isDigits(rankStr)) {
            rank = std::atoll(rankStr.c_str());
        }
        size_t dotPos = feedId.find('.', usPos + 1);
        size_t endPos = (dotPos == std::string::npos) ? feedId.size() : dotPos;
        std::string tstr = feedId.substr(usPos + 1, endPos - (usPos + 1));
        if (!tstr.empty()) {
            int64_t sec = std::atoll(tstr.c_str());
            if (sec > 0) {
                char tbuf[32] = {0};
                time_t tt = (time_t)sec;
                struct tm tmv;
                localtime_r(&tt, &tmv);
                strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmv);
                feedTime = tbuf;
            }
        }
    }
    if (rank > 0) {
        meta += "第 " + std::to_string(rank) + " 名";
    }
    int64_t cycleCount = jsonGetInt64(root, "cycle_count");
    if (cycleCount > 0) {
        if (!meta.empty()) meta += " · ";
        meta += std::to_string(cycleCount);
    }
    std::string detailText = jsonGetString(root, "detail_text");
    if (!detailText.empty()) {
        if (!meta.empty()) meta += " · ";
        meta += detailText;
    }
    if (!feedTime.empty()) {
        if (!meta.empty()) meta += " · ";
        meta += feedTime;
    }

    ContactData cd;
    cd.id          = kZhihuHotnewsId;
    cd.name        = "知乎热闻";
    cd.type        = kZhihuHotnewsType;
    cd.chatId      = kZhihuHotnewsType;
    cd.status      = "online";
    cd.isConnected = true;
    ret.contacts.push_back(cd);

    HistoryMessage hm;
    hm.created_at    = feedTime;
    hm.message       = title + "\n" + zhihuBrowserUrl(url) + (meta.empty() ? "" : "\n" + meta);
    hm.sender_pubkey = "fedone";
    hm.sender_number = 0;
    hm.direction     = "received";
    hm.roomId        = kZhihuHotnewsType;
    hm.eventId       = std::to_string(jsonGetInt64(root, "target.id"));
    std::string thumb = jsonGetString(root, "children.0.thumbnail");
    if (!thumb.empty()) {
        hm.msgtype     = "image";
        hm.mediaUrl    = thumb;
        hm.fileSize    = UnkSize;
        hm.mediaWidth  = UnkSize;
        hm.mediaHeight = UnkSize;
    } else {
        hm.msgtype  = "";
        hm.mediaUrl = "";
    }
    ret.messages.push_back(hm);

    PeerInfo pi;
    pi.publicKey  = "fedone";
    pi.userName   = peerName;
    pi.nickname   = authorHeadline;
    pi.iconUrl    = authorAvatar;
    pi.peerNumber = 0;
    ret.peers.push_back(pi);

    ret.senderName  = qFromUtf8(peerName);
    ret.handled     = true;

    cJSON_Delete(root);
    return true;
}

// ── 哔喱关注动态订阅流解析 ──
// 识别: Value.data 为 B 站原生动态卡片（proto_type==follow_feed + DYNAMIC_TYPE_*）；视频/图文带封面图

static std::string biliTypeLabel(const std::string& type) {
    if (type == "DYNAMIC_TYPE_AV")      return "视频";
    if (type == "DYNAMIC_TYPE_DRAW")    return "图文";
    if (type == "DYNAMIC_TYPE_FORWARD") return "转发";
    if (type == "DYNAMIC_TYPE_ORIGINAL") return "原创";
    if (type == "DYNAMIC_TYPE_REPOST")  return "转发";
    if (type == "DYNAMIC_TYPE_WORD")    return "文字";
    return type;
}

static std::string biliNormalizeUrl(const std::string& url) {
    if (url.rfind("//", 0) == 0) return "https:" + url;
    if (url.rfind("/", 0) == 0)  return "https://www.bilibili.com" + url;
    return url;
}

// 识别: proto_type==follow_feed + DYNAMIC_TYPE_* + module_author.name + id_str
// 替换旧 kind==follow_feed+url/author 格式；联系人/参与者沿用同一槽位
static bool tryParseBiliNotify(const std::string& rawStr, ParseResult& ret) {
    cJSON* root = cJSON_Parse(rawStr.c_str());
    if (!root) return false;

    std::string proto  = jsonGetString(root, "proto_type");
    std::string type   = jsonGetString(root, "type");
    std::string idStr  = jsonGetString(root, "id_str");
    std::string author = jsonGetString(root, "modules.module_author.name");
    if (proto != "follow_feed" || type.rfind("DYNAMIC_TYPE_", 0) != 0
        || idStr.empty() || author.empty()) {
        cJSON_Delete(root);
        return false;
    }

    std::string face    = jsonGetString(root, "modules.module_author.face");
    std::string action  = jsonGetString(root, "modules.module_author.pub_action");
    std::string pubTime = jsonGetString(root, "modules.module_author.pub_time");
    int64_t pubTs     = jsonGetInt64(root, "modules.module_author.pub_ts");
    int64_t authorMid = jsonGetInt64(root, "modules.module_author.mid");
    std::string userid = authorMid > 0 ? std::to_string(authorMid) : author;

    std::string descText = jsonGetString(root, "modules.module_dynamic.desc.text");
    std::string arcTitle = jsonGetString(root, "modules.module_dynamic.major.archive.title");
    std::string arcDesc  = jsonGetString(root, "modules.module_dynamic.major.archive.desc");
    std::string bvid     = jsonGetString(root, "modules.module_dynamic.major.archive.bvid");
    std::string jumpUrl  = jsonGetString(root, "modules.module_dynamic.major.archive.jump_url");
    std::string durText  = jsonGetString(root, "modules.module_dynamic.major.archive.duration_text");
    std::string cover    = jsonGetString(root, "modules.module_dynamic.major.archive.cover");
    if (cover.empty())
        cover = jsonGetString(root, "modules.module_dynamic.major.draw.items.0.src");
    int64_t play    = jsonGetInt64(root, "modules.module_dynamic.major.archive.stat.play");
    int64_t like    = jsonGetInt64(root, "modules.module_stat.like.count");
    int64_t comment = jsonGetInt64(root, "modules.module_stat.comment.count");
    std::string origAuthor = jsonGetString(root, "orig.modules.module_author.name");

    std::string text = !arcTitle.empty() ? arcTitle : descText;
    if (text.empty()) text = !action.empty() ? action : author;

    std::string url = biliNormalizeUrl(jumpUrl);
    if (url.empty() && !bvid.empty())
        url = "https://www.bilibili.com/video/" + bvid;
    if (url.empty())
        url = "https://t.bilibili.com/" + idStr;

    ContactData cd;
    cd.id          = kBiliNotifyId;
    cd.name        = "哔喱通知";
    cd.type        = kBiliNotifyType;
    cd.chatId      = kBiliNotifyType;
    cd.status      = "online";
    cd.isConnected = true;
    ret.contacts.push_back(cd);

    // 首行：转发前缀 + 文本 + url
    std::string message;
    if (type == "DYNAMIC_TYPE_FORWARD" || type == "DYNAMIC_TYPE_REPOST") {
        if (!origAuthor.empty()) message += "转发 @" + origAuthor + "\n";
    }
    message += text + "\n" + url;

    std::string label = biliTypeLabel(type);
    std::string meta;
    if (!label.empty()) {
        meta += "[" + label + "] ";
    }
    meta += "作者 " + author;
    if (!durText.empty()) {
        meta += " · " + durText;
    }

    HistoryMessage hm;
    hm.created_at = "";
    if (pubTs > 0) {
        char tbuf[32] = {0};
        time_t sec = (time_t)pubTs;
        struct tm tmv;
        localtime_r(&sec, &tmv);
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmv);
        meta += " · " + std::string(tbuf);
        hm.created_at = tbuf;
    } else if (!pubTime.empty()) {
        meta += " · " + pubTime;
    }
    message += "\n" + meta;

    if (play > 0 || like > 0 || comment > 0) {
        std::string stats;
        if (play > 0) stats += "播放" + std::to_string(play);
        if (like > 0) {
            if (!stats.empty()) stats += " · ";
            stats += "赞" + std::to_string(like);
        }
        if (comment > 0) {
            if (!stats.empty()) stats += " · ";
            stats += "评论" + std::to_string(comment);
        }
        message += "\n" + stats;
    }
    if (!arcDesc.empty() && arcDesc != arcTitle) {
        message += "\n" + arcDesc;
    }

    hm.message       = message;
    hm.sender_pubkey = userid;
    hm.sender_number = 0;
    hm.direction     = "received";
    hm.roomId        = kBiliNotifyType;
    hm.eventId       = idStr;
    hm.msgtype       = cover.empty() ? "" : "image";
    hm.mediaUrl      = cover;
    hm.mediaWidth    = 0;
    hm.mediaHeight   = 0;
    hm.fileSize      = 0;
    ret.messages.push_back(hm);

    PeerInfo pi;
    pi.publicKey  = userid;
    pi.userName       = userid;
    pi.nickname   = author;
    pi.peerNumber = 0;
    // face 为 B 站真实头像（*.hdslb.com），可直接下载，无需 unavatar
    if (!face.empty()) {
        pi.iconUrl = face;
    }
    ret.peers.push_back(pi);

    ret.senderName  = qFromUtf8(userid);
    ret.handled     = true;

    cJSON_Delete(root);
    return true;
}

// ── 微博热闻订阅流解析 ──
// 识别: Value.data 为微博热榜 JSON（proto_type==weibo_hot / weibo_hotlist + 顶层 word 非空）
//   数据无 URL/作者字段，消息 = 词条 + 自拼接 URL + 备注(与 word 不同时) + meta(第 N 名 / [icon_desc])；
//   icon 为 24x24 小角标图（如"辟谣"）作为消息图片，尺寸取 icon_width/icon_height

static std::string qUrlEncode(const std::string& s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9') || c == '-' || c == '_'
            || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

static bool tryParseWeiboHotnews(const std::string& rawStr, ParseResult& ret) {
    cJSON* root = cJSON_Parse(rawStr.c_str());
    if (!root) return false;

    std::string protoType = jsonGetString(root, "proto_type");
    std::string word      = jsonGetString(root, "word");
    if ((protoType != "weibo_hot" && protoType != "weibo_hotlist")
        || word.empty()) {
        cJSON_Delete(root);
        return false;
    }

    std::string note     = jsonGetString(root, "note");
    std::string iconDesc = jsonGetString(root, "icon_desc");
    std::string icon     = jsonGetString(root, "icon");
    int64_t iconW = jsonGetInt64(root, "icon_width");
    int64_t iconH = jsonGetInt64(root, "icon_height");
    int64_t rank  = jsonGetInt64(root, "rank");
    int64_t num   = jsonGetInt64(root, "num");

    ContactData cd;
    cd.id          = kWeiboHotnewsId;
    cd.name        = "微博热闻";
    cd.type        = kWeiboHotnewsType;
    cd.chatId      = kWeiboHotnewsType;
    cd.status      = "online";
    cd.isConnected = true;
    ret.contacts.push_back(cd);

    std::string url = "https://s.weibo.com/weibo?q=%23"
                    + qUrlEncode(word) + "%23&Refer=top";

    std::string message = word + "\n" + url;
    if (!note.empty() && note != word) {
        message += "\n" + note;
    }
    std::string meta;
    if (rank > 0) {
        meta += "第 " + std::to_string(rank) + " 名";
    }
    if (!iconDesc.empty()) {
        if (!meta.empty()) meta += " · ";
        meta += "[" + iconDesc + "]";
    }
    if (!meta.empty()) {
        message += "\n" + meta;
    }

    HistoryMessage hm;
    hm.message       = message;
    hm.sender_pubkey = "fedone";
    hm.sender_number = 0;
    hm.direction     = "received";
    hm.roomId        = kWeiboHotnewsType;
    hm.eventId       = std::to_string(num);
    if (!icon.empty()) {
        hm.msgtype       = "image";
        hm.mediaMime     = "image/png";
        hm.mediaUrl      = icon;
        hm.mediaWidth    = (iconW > 0) ? (int)iconW : UnkSize;
        hm.mediaHeight   = (iconH > 0) ? (int)iconH : UnkSize;
        hm.fileSize      = UnkSize;
    } else {
        hm.msgtype  = "";
        hm.mediaUrl = "";
    }
    ret.messages.push_back(hm);

    PeerInfo pi;
    pi.publicKey  = "fedone";
    pi.userName   = "fedone";
    pi.peerNumber = 0;
    ret.peers.push_back(pi);

    ret.senderName  = qFromUtf8("fedone");
    ret.handled     = true;

    cJSON_Delete(root);
    return true;
}

// ── 小红书推荐流订阅流解析 ──
// 识别: Value.data 为小红书 pubsub 推荐流笔记 JSON（model_type==note + note_card.display_title 非空）
//   附封面图（cover.info_list 的 FD_WM_WEBP）；与通知流（proto_type==notification）天然互斥

static bool tryParseXiaohongshuRecommend(const std::string& rawStr, ParseResult& ret) {
    cJSON* root = cJSON_Parse(rawStr.c_str());
    if (!root) return false;

    std::string modelType = jsonGetString(root, "model_type");
    std::string title     = jsonGetString(root, "note_card.display_title");
    if (modelType != "note" || title.empty()) {
        cJSON_Delete(root);
        return false;
    }

    std::string noteId   = jsonGetString(root, "id");
    std::string xsecToken = jsonGetString(root, "xsec_token");
    // note_card.user：user_id 即 pubkey，nick_name/nickname 即昵称，avatar 即 peer icon
    std::string userId   = jsonGetString(root, "note_card.user.user_id");
    std::string author   = jsonGetString(root, "note_card.user.nick_name");
    if (author.empty()) {
        author = jsonGetString(root, "note_card.user.nickname");
    }
    std::string avatar   = jsonGetString(root, "note_card.user.avatar");
    std::string likes   = jsonGetString(root, "note_card.interact_info.liked_count");
    std::string noteType = jsonGetString(root, "note_card.type");
    int64_t duration    = jsonGetInt64(root, "note_card.video.capa.duration");
    int64_t coverW      = jsonGetInt64(root, "note_card.cover.width");
    int64_t coverH      = jsonGetInt64(root, "note_card.cover.height");

    // 封面图：优先 cover.info_list 中 image_scene==FD_WM_WEBP 的 url，回退顺序 cover.url → 首个 info_list url
    std::string coverUrl;
    cJSON* infoList = jsonPath(root, "note_card.cover.info_list");
    if (infoList && cJSON_IsArray(infoList)) {
        int n = cJSON_GetArraySize(infoList);
        for (int i = 0; i < n; i++) {
            cJSON* it = cJSON_GetArrayItem(infoList, i);
            std::string scene = jsonGetString(it, "image_scene");
            std::string u     = jsonGetString(it, "url");
            if (u.empty()) continue;
            if (scene == "FD_WM_WEBP") {
                coverUrl = u;
                break;
            }
            if (coverUrl.empty()) coverUrl = u;
        }
    }
    if (coverUrl.empty()) {
        coverUrl = jsonGetString(root, "note_card.cover.url");
    }

    ContactData cd;
    cd.id          = kXiaohongshuRecommendId;
    cd.name        = "小红书推荐流";
    cd.type        = kXiaohongshuRecommendType;
    cd.chatId      = kXiaohongshuRecommendType;
    cd.status      = "online";
    cd.isConnected = true;
    ret.contacts.push_back(cd);

    std::string url = "https://www.xiaohongshu.com/explore/" + noteId;
    if (!xsecToken.empty()) {
        url += "?xsec_token=" + xsecToken + "&xsec_source=pc_feed";
    }

    std::string message = title + "\n" + url;
    std::string meta = "作者 " + author;
    if (!likes.empty()) {
        meta += " · 点赞 " + likes;
    }
    if (noteType == "video" && duration > 0) {
        meta += " · 视频 " + std::to_string(duration) + "秒";
    }
    message += "\n" + meta;

    std::string peerId = userId.empty() ? "fedone" : userId;
    std::string peerNick = author.empty() ? "fedone" : author;

    HistoryMessage hm;
    hm.message       = message;
    hm.sender_pubkey = peerId;
    hm.sender_number = 0;
    hm.direction     = "received";
    hm.roomId        = kXiaohongshuRecommendType;
    hm.eventId       = noteId;
    if (!coverUrl.empty()) {
        hm.msgtype       = "image";
        hm.mediaMime     = "image/webp";
        hm.mediaUrl      = coverUrl;
        hm.mediaWidth    = (coverW > 0) ? (int)coverW : UnkSize;
        hm.mediaHeight   = (coverH > 0) ? (int)coverH : UnkSize;
        hm.fileSize      = UnkSize;
    } else {
        hm.msgtype  = "";
        hm.mediaUrl = "";
    }
    ret.messages.push_back(hm);

    PeerInfo pi;
    pi.publicKey  = peerId;
    pi.userName   = peerId;
    pi.nickname   = peerNick;
    pi.iconUrl    = avatar;
    pi.peerNumber = 0;
    ret.peers.push_back(pi);

    ret.senderName  = qFromUtf8(peerNick);
    ret.handled     = true;

    cJSON_Delete(root);
    return true;
}

// ── 小红书通知订阅流解析（评论/互动通知）──
// 识别: Value.data 为小红书 pubsub 通知 JSON（proto_type==notification + comment_info.id 非空 + item_info.id 非空）
//   发送者=评论者（comment_info.user_info）；与推荐流（model_type==note）天然互斥

static bool tryParseXiaohongshuNotify(const std::string& rawStr, ParseResult& ret) {
    cJSON* root = cJSON_Parse(rawStr.c_str());
    if (!root) return false;

    std::string protoType = jsonGetString(root, "proto_type");
    std::string title     = jsonGetString(root, "title");
    std::string commentId = jsonGetString(root, "comment_info.id");
    std::string itemId    = jsonGetString(root, "item_info.id");
    if (protoType != "notification" || commentId.empty()
        || itemId.empty() || title.empty()) {
        cJSON_Delete(root);
        return false;
    }

    std::string commentContent = jsonGetString(root, "comment_info.content");
    std::string targetContent  = jsonGetString(root, "comment_info.target_comment.content");
    std::string itemContent    = jsonGetString(root, "item_info.content");
    std::string itemAuthor     = jsonGetString(root, "item_info.user_info.nickname");
    std::string xsecToken      = jsonGetString(root, "item_info.xsec_token");

    std::string commenterId     = jsonGetString(root, "comment_info.user_info.userid");
    std::string commenterNick   = jsonGetString(root, "comment_info.user_info.nickname");
    std::string commenterAvatar = jsonGetString(root, "comment_info.user_info.image");

    std::string imageUrl = jsonGetString(root, "item_info.image_info.url");
    int64_t imageW = jsonGetInt64(root, "item_info.image_info.width");
    int64_t imageH = jsonGetInt64(root, "item_info.image_info.height");

    ContactData cd;
    cd.id          = kXiaohongshuNotifyId;
    cd.name        = "小红书通知";
    cd.type        = kXiaohongshuNotifyType;
    cd.chatId      = kXiaohongshuNotifyType;
    cd.status      = "online";
    cd.isConnected = true;
    ret.contacts.push_back(cd);

    std::string url = "https://www.xiaohongshu.com/explore/" + itemId;
    if (!xsecToken.empty()) {
        url += "?xsec_token=" + xsecToken + "&xsec_source=pc_feed";
    }

    std::string message = title + "\n" + commentContent;
    if (!targetContent.empty()) {
        message += "\n> " + targetContent;
    }
    std::string meta;
    if (!itemContent.empty()) { meta += "笔记 " + itemContent; }
    if (!itemAuthor.empty())  { meta += " · 作者 " + itemAuthor; }
    if (!meta.empty())        { message += "\n" + meta; }
    message += "\n" + url;

    std::string peerId   = commenterId.empty() ? "fedone" : commenterId;
    std::string peerNick = commenterNick.empty() ? "fedone" : commenterNick;

    HistoryMessage hm;
    hm.message       = message;
    hm.sender_pubkey = peerId;
    hm.sender_number = 0;
    hm.direction     = "received";
    hm.roomId        = kXiaohongshuNotifyType;
    hm.eventId       = commentId;
    {
        int64_t t = jsonGetInt64(root, "time");
        if (t > 0) {
            char tbuf[32] = {0};
            time_t sec = (time_t)t;
            struct tm tmv;
            localtime_r(&sec, &tmv);
            strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmv);
            hm.created_at = tbuf;
        }
    }
    if (!imageUrl.empty()) {
        hm.msgtype       = "image";
        hm.mediaMime     = "image/jpeg";
        hm.mediaUrl      = imageUrl;
        hm.mediaWidth    = (imageW > 0) ? (int)imageW : UnkSize;
        hm.mediaHeight   = (imageH > 0) ? (int)imageH : UnkSize;
        hm.fileSize      = UnkSize;
    } else {
        hm.msgtype  = "";
        hm.mediaUrl = "";
    }
    ret.messages.push_back(hm);

    PeerInfo pi;
    pi.publicKey  = peerId;
    pi.userName   = peerId;
    pi.nickname   = peerNick;
    pi.iconUrl    = commenterAvatar;
    pi.peerNumber = 0;
    ret.peers.push_back(pi);

    ret.senderName  = qFromUtf8(peerNick);
    ret.handled     = true;

    cJSON_Delete(root);
    return true;
}

// ── 小红书热闻订阅流解析 ──
// 识别: Value.data 为小红书热榜 JSON（proto_type==hotlist + 顶层小写 title/url + hot_value）
//   正向互斥：hot_value 为小红书热榜独有；toutiao_hotlist 要求大写 Title/Url + ClusterId*，
//   zhihu_hotnews 要求 card_id，均不命中 → 不与 tryParseToutiaoHotlist/tryParseZhihuHotnews 冲突。
//   封面为 picasso-static 图床 png，无尺寸信息 → UnkSize。

// 站点板图标（48px 级 PNG 直链）——无用户封面时作板来源头像
// 必须 PNG/JPG：Qt3 无 ICO 解码器（plugins/imageformats 仅 jpeg/mng），用 .ico 会加载失败
static const char* kBoardIcon(const std::string& board) {
    static const struct { const char* board; const char* url; } kMap[] = {
        {"xiaohongshu",  "https://picasso-static.xiaohongshu.com/fe-platform/f43dc4a8baf03678996c62d8db6ebc01a82256ff.png"},
        {"douban-group", "https://asset.doubanio.com/cuphead/movie-static/pics/apple-touch-icon.png"},
        {"douban-movie", "https://asset.doubanio.com/cuphead/movie-static/pics/apple-touch-icon.png"},
        {"hupu",         "https://w1.hoopchina.com.cn/images/m/hupu_logo_new.png"},
        {"csdn",         "https://img-home.csdnimg.cn/images/20201124032511.png"},
        {"weread",       "https://rescdn.qqmail.com/node/wr/wrpage/style/images/independent/favicon/favicon_48h.png"},
        {"ithome",       "https://www.ithome.com/img/t.png"},
        {"douyin",       "https://lf1-cdn-tos.bytegoofy.com/goofy/ies/douyin_web/public/favicon.png"},
        {"tieba",        "https://tb3.bdstatic.com/tb/wise/hybrid-usergrow-base/static/img/logo.aa4c16c1.png"},
        {"jianshu",      "https://cdn2.jianshu.io/assets/apple-touch-icons/57-a6f1f1ee62ace44f6dc2f6a08575abd3c3b163288881c78dd8d75247682a4b27.png"},
    };
    for (const auto& kv : kMap) {
        if (board == kv.board) return kv.url;
    }
    return nullptr;
}

static bool tryParseXiaohongshuHotnews(const std::string& rawStr, ParseResult& ret) {
    cJSON* root = cJSON_Parse(rawStr.c_str());
    if (!root) return false;

    std::string protoType = jsonGetString(root, "proto_type");
    std::string title     = jsonGetString(root, "title");
    std::string url       = jsonGetString(root, "url");
    std::string hotValue  = jsonGetString(root, "hot_value");
    std::string board     = jsonGetString(root, "board");
    if (protoType != "hotlist" || title.empty() || url.empty() || hotValue.empty()) {
        cJSON_Delete(root);
        return false;
    }

    // 板热榜（如 reddit 话题下的 csdn 板）userid/username/nickname 取 board 值；无 board 回退 fedone
    std::string peerId   = board.empty() ? "fedone"   : board;
    std::string peerName = board.empty() ? "小红书热闻" : board;

    int64_t index = jsonGetInt64(root, "index");

    ContactData cd;
    cd.id          = kXiaohongshuHotnewsId;
    cd.name        = "小红书热闻";
    cd.type        = kXiaohongshuHotnewsType;
    cd.chatId      = kXiaohongshuHotnewsType;
    cd.status      = "online";
    cd.isConnected = true;
    ret.contacts.push_back(cd);

    std::string meta = "热度 " + hotValue;
    if (index > 0) {
        meta += " · 第 " + std::to_string(index) + " 名";
    }

    HistoryMessage hm;
    hm.message       = title + "\n" + url + "\n" + meta;
    hm.sender_pubkey = peerId;
    hm.sender_number = 0;
    hm.direction     = "received";
    hm.roomId        = kXiaohongshuHotnewsType;
    hm.eventId       = url;
    std::string cover = jsonGetString(root, "cover");
    if (!cover.empty()) {
        hm.msgtype     = "image";
        hm.mediaUrl    = cover;
        hm.mediaMime   = "image/png";
        hm.fileSize    = UnkSize;
        hm.mediaWidth  = UnkSize;
        hm.mediaHeight = UnkSize;
    } else {
        hm.msgtype  = "";
        hm.mediaUrl = "";
    }
    ret.messages.push_back(hm);

    PeerInfo pi;
    pi.publicKey  = peerId;
    pi.userName   = peerId;
    pi.nickname   = peerName;
    // 发送者为“板”或热榜源，用户头像一律用站点图标（PNG 直链），
    // 绝不使用文章封面（cover 每篇不同且非用户头像，消息内联图已含 cover）
    {
        const char* icon = kBoardIcon(board);
        if (icon) pi.iconUrl = icon;
    }
    pi.peerNumber = 0;
    ret.peers.push_back(pi);

    ret.senderName  = qFromUtf8(peerName);
    ret.handled     = true;

    cJSON_Delete(root);
    return true;
}

// ── 小红书笔记卡片订阅流解析 ──
// 识别: Value.data 为小红书笔记 JSON（proto_type==xhs_collect，或 note_id+display_title+user.nickname 齐备）
//   正向互斥：推荐流要求 model_type==note、通知流要求 proto_type==notification、热榜要求 hotlist；
//   xhs_collect 为收藏/笔记卡片独有 → 不与 tryParseXiaohongshu* 兄弟函数冲突。
//   封面为 xhscdn 图床 webp，width/height 随附。

static bool tryParseXiaohongshuNote(const std::string& rawStr, ParseResult& ret) {
    cJSON* root = cJSON_Parse(rawStr.c_str());
    if (!root) return false;

    std::string protoType = jsonGetString(root, "proto_type");
    std::string noteId    = jsonGetString(root, "note_id");
    std::string title     = jsonGetString(root, "display_title");
    std::string author    = jsonGetString(root, "user.nickname");
    if (protoType != "xhs_collect"
            && (noteId.empty() || title.empty() || author.empty())) {
        cJSON_Delete(root);
        return false;
    }

    std::string authorId  = jsonGetString(root, "user.user_id");
    std::string avatar    = jsonGetString(root, "user.avatar");
    std::string likes     = jsonGetString(root, "interact_info.liked_count");
    std::string xsecToken = jsonGetString(root, "xsec_token");
    std::string coverUrl  = jsonGetString(root, "cover.url");
    int64_t coverW = jsonGetInt64(root, "cover.width");
    int64_t coverH = jsonGetInt64(root, "cover.height");

    ContactData cd;
    cd.id          = kXiaohongshuNoteId;
    cd.name        = "小红书笔记";
    cd.type        = kXiaohongshuNoteType;
    cd.chatId      = kXiaohongshuNoteType;
    cd.status      = "online";
    cd.isConnected = true;
    ret.contacts.push_back(cd);

    std::string url = "https://www.xiaohongshu.com/explore/" + noteId;
    if (!xsecToken.empty()) {
        url += "?xsec_token=" + xsecToken;
    }

    std::string message = title + "\n" + url;
    std::string meta;
    if (!author.empty()) { meta += "作者 " + author; }
    if (!likes.empty())   { meta += std::string(meta.empty() ? "" : " · ") + "点赞 " + likes; }
    if (!meta.empty())    { message += "\n" + meta; }

    std::string peerId   = authorId.empty() ? "fedone" : authorId;
    std::string peerNick = author.empty() ? "fedone" : author;

    HistoryMessage hm;
    hm.message       = message;
    hm.sender_pubkey = peerId;
    hm.sender_number = 0;
    hm.direction     = "received";
    hm.roomId        = kXiaohongshuNoteType;
    hm.eventId       = noteId;
    if (!coverUrl.empty()) {
        hm.msgtype     = "image";
        hm.mediaMime   = "image/webp";
        hm.mediaUrl    = coverUrl;
        hm.mediaWidth  = (coverW > 0) ? (int)coverW : UnkSize;
        hm.mediaHeight = (coverH > 0) ? (int)coverH : UnkSize;
        hm.fileSize    = UnkSize;
    } else {
        hm.msgtype  = "";
        hm.mediaUrl = "";
    }
    ret.messages.push_back(hm);

    PeerInfo pi;
    pi.publicKey  = peerId;
    pi.userName   = peerId;
    pi.nickname   = peerNick;
    pi.iconUrl    = avatar;
    pi.peerNumber = 0;
    ret.peers.push_back(pi);

    ret.senderName  = qFromUtf8(peerNick);
    ret.handled     = true;

    cJSON_Delete(root);
    return true;
}

// ── 红果热榜订阅流解析 ──
// 识别: Value.bits 为红果短剧短剧热榜 JSON（series_id 非空 + url 指向 hongguoduanju.com 短剧详情 + title 非空）
//   正向互斥：series_id + hongguoduanju.com 为红果独有；小红书/微博/知乎/头条热闻要求 proto_type/proto_id，
//   且都要求 hot_value/hotlist/ClusterId，均不满足 → 不与 tryParseXiaohongshuHotnews 等冲突。
//   封面为 novel-pic byteimg 图床（~tplv-shrink:640:0.image 模板），无标注尺寸 → UnkSize。

static bool tryParseHongguoHotlist(const std::string& rawStr, ParseResult& ret) {
    cJSON* root = cJSON_Parse(rawStr.c_str());
    if (!root) return false;

    std::string seriesId      = jsonGetString(root, "series_id");
    std::string title         = jsonGetString(root, "title");
    std::string url           = jsonGetString(root, "url");
    if (seriesId.empty() || title.empty() || url.empty()
        || url.find("hongguoduanju.com") == std::string::npos) {
        cJSON_Delete(root);
        return false;
    }
    std::string rank = jsonGetStrNum(root, "rank");
    std::string cover = jsonGetString(root, "cover");
    std::string episodeCnt = jsonGetStrNum(root, "episode_cnt");
    std::string avatar = jsonGetString(root, "userAvatar");
    if (avatar.empty()) {
        avatar = jsonGetString(root, "userInfo.userAvatar");
    }
    if (avatar.empty()) {
		// 看短剧
        avatar = "https://img.utdstc.com/icon/9dc/a99/9dca996e59e2f2bef0fa99c195774a2266e994c4cc98faae03eef6b7aef9e94e:200";
        // 热门短剧,webp
        avatar = "https://tse1.mm.bing.net/th/id/OIP.Ca0bhQJAdDFLzqE6wejuJAHaHa?r=0&rs=1&pid=ImgDetMain&o=7&rm=3";
        // 热门短剧,jpg
        avatar = "https://is1-ssl.mzstatic.com/image/thumb/Purple211/v4/f2/8e/91/f28e91e3-a2e7-14de-1950-517cb6fed7b5/AppIcon-0-0-1x_U007emarketing-0-8-0-85-220.png/512x512bb.jpg";
    }
    
    ContactData cd;
    cd.id          = kHongguoHotlistId;
    cd.name        = "红果热榜";
    cd.type        = kHongguoHotlistType;
    cd.chatId      = kHongguoHotlistType;
    cd.status      = "online";
    cd.isConnected = true;
    ret.contacts.push_back(cd);

    std::string meta = "热度 " + rank;
    if (!episodeCnt.empty()) { meta += " · 全 " + episodeCnt + " 集"; }

    // tags 数组 → TagUtil::format 得 "#tag1, #tag2, ..."，追加为消息末行
    std::vector<std::string> rawTags;
    cJSON* tagsArr = jsonPath(root, "tags");
    if (tagsArr && cJSON_IsArray(tagsArr)) {
        int tagN = cJSON_GetArraySize(tagsArr);
        for (int i = 0; i < tagN; i++) {
            cJSON* tagItem = cJSON_GetArrayItem(tagsArr, i);
            if (!tagItem || !cJSON_IsString(tagItem)) { continue; }
            rawTags.push_back(cJSON_GetStringValue(tagItem));
        }
    }
    std::string tagsLine = TagUtil::format(rawTags);

    HistoryMessage hm;
    hm.message       = title + "\n" + url + "\n" + meta;
    if (!tagsLine.empty()) { hm.message += "\nTags: " + tagsLine; }
    hm.sender_pubkey = "fedone";
    hm.sender_number = 0;
    hm.direction     = "received";
    hm.roomId        = kHongguoHotlistType;
    hm.eventId       = url;
    if (!cover.empty()) {
        hm.msgtype    = "image";
        hm.mediaUrl   = cover;
        hm.mediaMime  = "image/png";
        hm.fileSize   = UnkSize;
        hm.mediaWidth = UnkSize, hm.mediaHeight = UnkSize;
    } else {
        hm.msgtype  = "";
        hm.mediaUrl = "";
    }
    ret.messages.push_back(hm);

    PeerInfo pi;
    pi.publicKey  = "fedone";
    pi.userName   = "fedone";
    pi.nickname   = "红果热榜";
    pi.iconUrl    = avatar;
    pi.peerNumber = 0;
    ret.peers.push_back(pi);

    ret.senderName = qFromUtf8("红果热榜");
    ret.handled    = true;

    cJSON_Delete(root);
    return true;
}

// ── sysinfo 系统信息公告板解析 ──
// 识别: Value.data 为系统信息 JSON（proto_type=="sysinfo" / kind=="sysinfo"），字段集合独有，天然互斥。
// peer id 与载荷无关：id 来自信封 ret.envelopeFrom（Value.from），hash 后得稳定 int id；
// name = host.hostname。正文格式化为 Markdown 表格（chatview 以纯文本按宽度换行）。
static bool tryParseSysinfo(const std::string& rawStr, ParseResult& ret) {
    cJSON* root = cJSON_Parse(rawStr.c_str());
    if (!root) return false;

    std::string protoType = jsonGetString(root, "proto_type");
    std::string kind      = jsonGetString(root, "kind");
    if (protoType != "sysinfo" && kind != "sysinfo") {
        cJSON_Delete(root);
        return false;
    }

    const std::string& from = ret.envelopeFrom;
    if (from.empty()) {
        cJSON_Delete(root);
        return false;
    }

    std::string hostname = jsonGetString(root, "host.hostname");
    if (hostname.empty()) hostname = kSysinfoBoardName;

    ContactData cd;
    cd.id          = kSysinfoBoardId;     // 固定板 id：所有 sysinfo 消息汇聚同一联系人（不做 hash）
    cd.name        = kSysinfoBoardName;   // 显示名 = "系统信息公告板"
    cd.type        = kSysinfoBoardType;
    cd.chatId      = kSysinfoBoardType;   // type == chatId == roomId（路由匹配用）
    cd.status      = "online";
    cd.isConnected = true;
    ret.contacts.push_back(cd);
    ret.contactName = qFromUtf8(cd.name);

    // ── Markdown 表格 ──
    std::string buf;
    buf.reserve(2048);
    const double GiB = 1073741824.0;
    char tmp[96];

    buf += "| " + std::string(kSysinfoBoardName) + " | " + hostname + " |\n";
    buf += "|---|---|\n";
    buf += "| 类别 | 字段 | 值 |\n";
    buf += "|---|---|---|\n";

    std::string arch   = jsonGetString(root, "host.arch");
    std::string os     = jsonGetString(root, "host.os");
    std::string plats  = jsonGetString(root, "host.platform");
    std::string kernel = jsonGetString(root, "host.kernel");
    buf += "| host | 主机 | " + hostname;
    if (!arch.empty()) buf += " · " + arch;
    if (!plats.empty()) buf += " · " + plats + "/" + os;
    buf += " |\n";
    buf += "| host | 内核/进程 | " + kernel
         + " · procs " + std::to_string(jsonGetInt64(root, "host.procs"))
         + " · up " + std::to_string(jsonGetInt64(root, "host.uptime_sec")) + "s |\n";

    std::string cpuModel = jsonGetString(root, "cpu.model");
    int64_t phy = jsonGetInt64(root, "cpu.counts_physical");
    int64_t lgc = jsonGetInt64(root, "cpu.counts_logical");
    buf += "| cpu | CPU | " + cpuModel;
    if (phy > 0) buf += " · " + std::to_string(phy) + "物理/" + std::to_string(lgc) + "逻辑";
    buf += " |\n";
    snprintf(tmp, sizeof(tmp), "| cpu | 使用率 | %.1f%% |", jsonGetDouble(root, "cpu.percent"));
    buf += tmp; buf += "\n";

    snprintf(tmp, sizeof(tmp), "| mem | 内存 | %.2f/%.2f GiB (%.1f%%) |",
             jsonGetDouble(root, "mem.used") / GiB,
             jsonGetDouble(root, "mem.total") / GiB,
             jsonGetDouble(root, "mem.used_percent"));
    buf += tmp; buf += "\n";
    if (jsonGetDouble(root, "mem.swap_total") > 0) {
        snprintf(tmp, sizeof(tmp), "| mem | swap | %.2f/%.2f GiB (%.1f%%) |",
                 jsonGetDouble(root, "mem.swap_used") / GiB,
                 jsonGetDouble(root, "mem.swap_total") / GiB,
                 jsonGetDouble(root, "mem.swap_used_percent"));
        buf += tmp; buf += "\n";
    }

    // battery：逐块一行（最多 4 块）；present<=0 且 state 空 → 无电池，跳过
    cJSON* batArr = cJSON_GetObjectItem(root, "battery");
    if (cJSON_IsArray(batArr)) {
        int nb = cJSON_GetArraySize(batArr);
        for (int i = 0; i < nb && i < 4; i++) {
            cJSON* b = cJSON_GetArrayItem(batArr, i);
            if (!b) continue;
            int64_t present = jsonGetInt64(b, "present");
            std::string st  = jsonGetString(b, "state");
            if (present <= 0 && st.empty()) continue;
            std::string bl = "| bat | 电池";
            if (nb > 1) bl += std::to_string((long long)jsonGetInt64(b, "index"));
            bl += " | ";
            bl += st.empty() ? std::string("—") : st;        // 原样显示（不中文化）
            double pct = jsonGetDouble(b, "percent");
            double hp  = jsonGetDouble(b, "health_percent");
            double tp  = jsonGetDouble(b, "temperature");
            double vt  = jsonGetDouble(b, "voltage");
            int64_t cyc = jsonGetInt64(b, "cycles");
            int64_t ful = jsonGetInt64(b, "full");
            int64_t dsn = jsonGetInt64(b, "design");
            if (pct >= 0) { snprintf(tmp, sizeof(tmp), " · %.1f%%", pct); bl += tmp; }
            if (hp  >= 0) { snprintf(tmp, sizeof(tmp), " · health %.0f%%", hp); bl += tmp; }
            if (cyc >= 0) bl += " · cycles " + std::to_string((long long)cyc);
            if (tp  > 0)  { snprintf(tmp, sizeof(tmp), " · %.1f°C", tp); bl += tmp; }
            if (vt  > 0)  { snprintf(tmp, sizeof(tmp), " · %.2fV", vt); bl += tmp; }
            if (ful > 0 && dsn > 0) {
                snprintf(tmp, sizeof(tmp), " · %lld/%lldmAh",
                         (long long)ful, (long long)dsn); bl += tmp;
            }
            if (jsonGetInt64(b, "ac_power") > 0) bl += " · AC";
            bl += " |\n";
            buf += bl;
        }
    }

    snprintf(tmp, sizeof(tmp), "| load | 负载 | 1m %.2f / 5m %.2f / 15m %.2f |",
             jsonGetDouble(root, "load.load1"), jsonGetDouble(root, "load.load5"),
             jsonGetDouble(root, "load.load15"));
    buf += tmp; buf += "\n";

    // disk：逐块一行（最多 8 块）
    cJSON* diskArr = cJSON_GetObjectItem(root, "disk");
    if (cJSON_IsArray(diskArr)) {
        int n = cJSON_GetArraySize(diskArr);
        for (int i = 0; i < n && i < 8; i++) {
            cJSON* d = cJSON_GetArrayItem(diskArr, i);
            if (!d) continue;
            snprintf(tmp, sizeof(tmp), "| disk | %s | %s · %.2f/%.2f GiB (%.1f%%) |",
                     jsonGetString(d, "mountpoint").c_str(),
                     jsonGetString(d, "device").c_str(),
                     jsonGetDouble(d, "used") / GiB, jsonGetDouble(d, "total") / GiB,
                     jsonGetDouble(d, "used_percent"));
            buf += tmp; buf += "\n";
        }
    }

    // net：仅 up+broadcast+multicast 物理口（跳过 loopback / pointtopoint）
    cJSON* netArr = cJSON_GetObjectItem(root, "net");
    if (cJSON_IsArray(netArr)) {
        int n = cJSON_GetArraySize(netArr);
        for (int i = 0; i < n && i < 8; i++) {
            cJSON* it = cJSON_GetArrayItem(netArr, i);
            if (!it) continue;
            std::string nname = jsonGetString(it, "name");
            std::string flags = jsonGetString(it, "flags");
            if (nname.empty() || nname == "lo" ||
                flags.find("broadcast") == std::string::npos) continue;
            snprintf(tmp, sizeof(tmp), "| net | %s | ↓%.1fKB/s · ↑%.1fKB/s |",
                     nname.c_str(),
                     jsonGetDouble(it, "recv_speed") / 1024.0,
                     jsonGetDouble(it, "send_speed") / 1024.0);
            buf += tmp; buf += "\n";
        }
    }

    // temp：合并一行（最多 8 个）
    cJSON* tempArr = cJSON_GetObjectItem(root, "temp");
    if (cJSON_IsArray(tempArr)) {
        int n = cJSON_GetArraySize(tempArr);
        std::string tl;
        for (int i = 0; i < n && i < 8; i++) {
            cJSON* t = cJSON_GetArrayItem(tempArr, i);
            if (!t) continue;
            char tb[24];
            snprintf(tb, sizeof(tb), "%.0f°C", jsonGetDouble(t, "celsius"));
            if (!tl.empty()) tl += " · ";
            tl += jsonGetString(t, "sensor") + " " + tb;
        }
        if (!tl.empty()) buf += "| temp | 温度 | " + tl + " |\n";
    }

    // errors：合并一行（最多 8 条）
    cJSON* errArr = cJSON_GetObjectItem(root, "errors");
    if (cJSON_IsArray(errArr)) {
        int n = cJSON_GetArraySize(errArr);
        std::string el;
        for (int i = 0; i < n && i < 8; i++) {
            cJSON* e = cJSON_GetArrayItem(errArr, i);
            if (!e || !cJSON_IsString(e)) continue;
            if (!el.empty()) el += " · ";
            el += cJSON_GetStringValue(e);
        }
        if (!el.empty()) buf += "| err | 异常 | " + el + " |\n";
    }

    // 时间（published_at 转本地）
    int64_t pub = jsonGetInt64(root, "published_at");
    std::string timeStr;
    if (pub > 0) {
        time_t sec = (time_t)pub;
        struct tm tmv;
        localtime_r(&sec, &tmv);
        char tbuf[32] = {0};
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmv);
        timeStr = tbuf;
    }
    buf += "| time | 时间 | " + timeStr + " |\n";

    HistoryMessage hm;
    hm.message       = buf;
    hm.sender_pubkey = from;
    hm.sender_number = 0;
    hm.direction     = "received";
    hm.created_at    = timeStr;
    hm.roomId        = kSysinfoBoardType;     // 与 cd.chatId 相等 → 通用路由命中单板
    {
        char eid[48];
        snprintf(eid, sizeof(eid), "sysinfo_%lld_%x", (long long)pub,
                 (unsigned)(std::hash<std::string>{}(from) & 0xffffffffu));
        hm.eventId = eid;   // published_at+from 双重去重：同源快照不重插，跨源同秒不误吞
    }
    ret.messages.push_back(hm);

    // uname -a 式显示名：sysname nodename release machine（os hostname kernel arch），缺省自动跳过
    std::string unameLike;
    if (!os.empty()) unameLike = os + " ";
    unameLike += hostname;
    if (!kernel.empty()) unameLike += " " + kernel;
    if (!arch.empty())   unameLike += " " + arch;

    PeerInfo pi;
    pi.publicKey  = from;
    pi.userName   = from;       // sendername == peerid == peerpubkey == value.from（独立于 payload）
    pi.nickname   = unameLike;  // 气泡显示名（例：linux node 5.15.0-91-generic x86_64）
    pi.peerNumber = 0;
    ret.peers.push_back(pi);

    ret.senderName = qFromUtf8(from);   // peerid == sendername == value.from
    ret.handled    = true;

    cJSON_Delete(root);
    return true;
}

// ── 酷安时线订阅流解析 ──
// 识别: Value.data 为酷安 feed JSON（proto_type==news + feedType/uid/username/title 正向特征）
//   正向互斥：feedType/uid/username 为酷安独有，头条新闻无 → 不与 tryParseToutiaoNews 冲突；
//   封面尺寸内嵌于 URL 尾 @WxH（如 _664@1384x625.jpg），由 parseCoolapkSize 提取。

static bool parseCoolapkSize(const std::string& url, int& w, int& h) {
    w = h = 0;
    size_t slash = url.find_last_of('/');
    if (slash == std::string::npos) slash = 0; else slash += 1;
    size_t at = url.find('@', slash);
    if (at == std::string::npos) return false;
    size_t dot = url.find('.', at);
    if (dot == std::string::npos) dot = url.size();
    std::string seg = url.substr(at + 1, dot - (at + 1));
    size_t x = seg.find('x');
    if (x == std::string::npos) return false;
    std::string ws = seg.substr(0, x);
    std::string hs = seg.substr(x + 1);
    if (!isDigits(ws) || !isDigits(hs)) return false;
    w = std::atoi(ws.c_str());
    h = std::atoi(hs.c_str());
    return w > 0 && h > 0;
}

static bool tryParseCoolapkTimeline(const std::string& rawStr, ParseResult& ret) {
    cJSON* root = cJSON_Parse(rawStr.c_str());
    if (!root) return false;

    std::string protoType = jsonGetString(root, "proto_type");
    std::string feedType  = jsonGetString(root, "feedType");
    std::string uidStr    = jsonGetString(root, "uid");
    std::string username  = jsonGetString(root, "username");
    std::string title     = jsonGetString(root, "message_title");
    if (title.empty()) {
        title = jsonGetString(root, "title");
    }
    std::string feedUrl = jsonGetString(root, "url");
    std::string feedId  = jsonGetString(root, "id");
    if (protoType != "news" || feedType.empty() || uidStr.empty() ||
        username.empty() || title.empty() || feedUrl.empty() || feedId.empty()) {
        cJSON_Delete(root);
        return false;
    }

    std::string displayName = jsonGetString(root, "userInfo.displayUsername");
    if (displayName.empty()) {
        displayName = username;
    }
    std::string avatar = jsonGetString(root, "userAvatar");
    if (avatar.empty()) {
        avatar = jsonGetString(root, "userInfo.userAvatar");
    }

	// must use real UA to access
    std::string pic = jsonGetString(root, "pic");
    if (pic.empty()) {
        pic = jsonGetString(root, "message_cover");
    }

    std::string feedTypeName = jsonGetString(root, "feedTypeName");
    std::string deviceTitle  = jsonGetString(root, "device_title");
    if (deviceTitle.empty()) {
        deviceTitle = jsonGetString(root, "ttitle");
    }
    if (deviceTitle.empty()) {
        deviceTitle = jsonGetString(root, "targetRow.title");
    }
    std::string likes       = jsonGetString(root, "likenum");
    std::string datelineStr = jsonGetString(root, "dateline_text");
    std::string body        = jsonGetString(root, "message");

    ContactData cd;
    cd.id          = kCoolapkTimelineId;
    cd.name        = "酷安时间线";
    cd.type        = kCoolapkTimelineType;
    cd.chatId      = kCoolapkTimelineType;
    cd.status      = "online";
    cd.isConnected = true;
    ret.contacts.push_back(cd);

    std::string link = "https://www.coolapk.com" + feedUrl;

    std::string message = title + "\n" + link;
    std::string meta = "作者 " + displayName;
    if (!feedTypeName.empty()) {
        meta += " · " + feedTypeName;
    }
    if (!deviceTitle.empty()) {
        meta += " · " + deviceTitle;
    }
    if (!likes.empty() && likes != "0") {
        meta += " · 点赞 " + likes;
    }
    if (!datelineStr.empty()) {
        meta += " · " + datelineStr;
    }
    message += "\n" + meta;

    // 正文摘录（超 300 字符截断）
    if (!body.empty()) {
        static const size_t kBodyMax = 300;
        if (body.size() > kBodyMax) {
            body = body.substr(0, kBodyMax) + "…";
        }
        message += "\n" + body;
    }

    std::string peerId = uidStr.empty() ? "fedone" : uidStr;
    std::string peerNick = displayName.empty() ? "fedone" : displayName;

    HistoryMessage hm;
    hm.message       = message;
    hm.sender_pubkey = peerId;
    hm.sender_number = 0;
    hm.direction     = "received";
    hm.roomId        = kCoolapkTimelineType;
    hm.eventId       = feedId;
    if (!pic.empty()) {
        int pw = 0, ph = 0;
        hm.msgtype       = "image";
        hm.mediaMime     = "image/jpeg";
        hm.mediaUrl      = pic;
        if (parseCoolapkSize(pic, pw, ph)) {
            hm.mediaWidth  = pw;
            hm.mediaHeight = ph;
        } else {
            hm.mediaWidth  = UnkSize;
            hm.mediaHeight = UnkSize;
        }
        hm.fileSize      = UnkSize;
    } else {
        hm.msgtype  = "";
        hm.mediaUrl = "";
    }
    ret.messages.push_back(hm);

    PeerInfo pi;
    pi.publicKey  = peerId;
    pi.userName   = peerId;
    pi.nickname   = peerNick;
    pi.iconUrl    = avatar;
    pi.peerNumber = 0;
    ret.peers.push_back(pi);

    ret.senderName  = qFromUtf8(peerNick);
    ret.handled     = true;

    cJSON_Delete(root);
    return true;
}

// ── 旧逻辑：纯文本降级 ──

static void extractSender(cJSON* valueItem, ParseResult& ret) {
    cJSON* rf = cJSON_GetObjectItem(valueItem, "ReceivedFrom");
    if (!rf || !cJSON_IsString(rf)) {
        ret.senderName = ret.contactName;
        return;
    }
    QString rfStr = qFromUtf8(cJSON_GetStringValue(rf));
    if (rfStr.isEmpty()) {
        ret.senderName = ret.contactName;
        return;
    }
    int peerPos =
#ifdef QT3_BUILD
        rfStr.find("peer.ID ");
#else
        rfStr.indexOf("peer.ID ");
#endif
    if (peerPos >= 0) {
        int start = peerPos + 8;
        int end =
#ifdef QT3_BUILD
            rfStr.find('>', start);
#else
            rfStr.indexOf('>', start);
#endif
        if (end < 0) end = rfStr.length();
        ret.senderName = rfStr.mid(start, end - start);
    } else {
        ret.senderName = rfStr;
    }
}

static void fallbackAsPlainText(cJSON* valueItem, ParseResult& ret) {
    cJSON* dataItem = cJSON_GetObjectItem(valueItem, "data");
    if (dataItem && cJSON_IsString(dataItem)) {
        ret.messageText = qFromUtf8(cJSON_GetStringValue(dataItem));
        qWarning("UnknownParser: extracted Value.data, len=%ld", (int64_t)ret.messageText.length());
    } else {
        char* raw = cJSON_PrintUnformatted(valueItem);
        ret.messageText = qFromUtf8(raw);
        qWarning("UnknownParser: no Value.data, fell back to Value JSON, len=%ld", (int64_t)ret.messageText.length());
        free(raw);
    }
    extractSender(valueItem, ret);
    ret.handled = true;
}

// ── 主流程 ──

ParseResult UnknownParser::parse(const std::string& eventType, const std::string& jsonData) {
    qWarning("UnknownParser::parse: type=[%s] data=[%.8192s]", eventType.c_str(), jsonData.c_str());

    if (eventType != "pubsub" && eventType != "unknown") {
        return {false, QString(), QString(), QString()};
    }

    cJSON* root = cJSON_Parse(jsonData.c_str());
    if (!root) {
        qWarning("UnknownParser: cJSON_Parse failed");
        return {false, QString(), QString(), QString()};
    }

    std::string realType = eventType;
    if (eventType == "unknown") {
        cJSON* typeItem = cJSON_GetObjectItem(root, "Type");
        if (typeItem && cJSON_IsString(typeItem))
            realType = cJSON_GetStringValue(typeItem);
    }

    if (realType != "pubsub") {
        cJSON_Delete(root);
        return {false, QString(), QString(), QString()};
    }

    ParseResult ret = {false, QString(), QString(), QString()};

    // Topic → contactName
    cJSON* topicItem = cJSON_GetObjectItem(root, "Topic");
    if (!topicItem || !cJSON_IsString(topicItem))
        topicItem = cJSON_GetObjectItem(root, "topic");
    if (topicItem && cJSON_IsString(topicItem)) {
        ret.contactName = qFromUtf8(cJSON_GetStringValue(topicItem));
        qWarning("UnknownParser: found topic=[%s]", qToUtf8(ret.contactName).data());
    } else {
        qWarning("UnknownParser: no topic field found");
    }

    // Value → 路由 Matrix sync / Tox 事件 / 旧逻辑
    cJSON* valueItem = cJSON_GetObjectItem(root, "Value");
    if (valueItem) {
        // peer 身份取信封 Value.from（libp2p ed25519 PeerId 字节序列）。
        // 转发过程中 from 恒定（ReceivedFrom 为上一跳，随链路变化，不可用于身份）。
        // 上游以 string([]byte) 把原始二进制直塞 JSON（未 base64）：前导含 NUL、非 UTF-8 字节
        // 已被替换为 \ufffd。cJSON 按 C 字符串承载会截断为空 → 绕开 cJSON 从原始 JSON 文本
        // 逐字节重建（decodeEnvelopeFromBytes），再 base58btc 编码为 12D3KooW… 稳定标识。
        std::vector<uint8_t> fromBytes;
        if (decodeEnvelopeFromBytes(jsonData, fromBytes)) {
            ret.envelopeFrom = base58Encode(fromBytes);
        } else {
            ret.envelopeFrom.clear();
        }
        cJSON* dataItem = cJSON_GetObjectItem(valueItem, "data");
        if (dataItem && cJSON_IsString(dataItem)) {
            const char* dataStr = cJSON_GetStringValue(dataItem);
            // 调试：输出符合 gomuks (sync_complete) 的原始数据（分析已确认，保留注释备用）
            // {
            //     cJSON* probe = cJSON_Parse(dataStr);
            //     if (probe) {
            //         cJSON* cmd = cJSON_GetObjectItem(probe, "command");
            //         if (cmd && cJSON_IsString(cmd)
            //             && std::string(cJSON_GetStringValue(cmd)) == "sync_complete") {
            //             qWarning("UnknownParser: gomuks raw data len=%d\n%s",
            //                      (int)strlen(dataStr), dataStr);
            //         }
            //         cJSON_Delete(probe);
            //     }
            // }
            if (tryParseGomuksSync(dataStr, ret))
                goto done;
            if (tryParseMtxliteRoom(dataStr, ret))
                goto done;
            if (tryParseToxMessage(dataStr, ret))
                goto done;
            if (tryParseImapMessage(dataStr, ret))
                goto done;
            if (tryParseFilesyncEvent(dataStr, ret))
                goto done;
            if (tryParseClipboardEvent(dataStr, ret))
                goto done;
            if (tryParseMisskeyNote(dataStr, ret))
                goto done;
            if (tryParseCoolapkTimeline(dataStr, ret))
                goto done;
            if (tryParseToutiaoNews(dataStr, ret))
                goto done;
            if (tryParseToutiaoHotlist(dataStr, ret))
                goto done;
            if (tryParseZhihuNotify(dataStr, ret))
                goto done;
            if (tryParseZhihuHotnews(dataStr, ret))
                goto done;
            if (tryParseXiaohongshuHotnews(dataStr, ret))
                goto done;
            if (tryParseBiliNotify(dataStr, ret))
                goto done;
            if (tryParseWeiboHotnews(dataStr, ret))
                goto done;
            if (tryParseXiaohongshuRecommend(dataStr, ret))
                goto done;
            if (tryParseXiaohongshuNotify(dataStr, ret))
                goto done;
            if (tryParseXiaohongshuNote(dataStr, ret))
                goto done;
            if (tryParseHongguoHotlist(dataStr, ret))
                goto done;
            if (tryParseSysinfo(dataStr, ret))
                goto done;
        }
        fallbackAsPlainText(valueItem, ret);
    } else {
        qWarning("UnknownParser: no Value field found");
    }

done:
    qWarning("UnknownParser: parse done (handled=%d contacts=%zu peers=%zu messages=%zu)",
             ret.handled, ret.contacts.size(), ret.peers.size(), ret.messages.size());

    cJSON_Delete(root);
    return ret;
}
