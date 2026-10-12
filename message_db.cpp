#include "message_db.h"
#include <algorithm>
#include "tagutil.h"

namespace {

class MessageDbSync final : public MessageDbSyncInterface {
    std::shared_ptr<SqliteConnectionSafe> m_conn;
public:
    explicit MessageDbSync(std::shared_ptr<SqliteConnectionSafe> conn)
        : m_conn(std::move(conn)) {}

    int64_t insert_message(const MessageRow& row) override {
        SlowGuard _w("msg::insert", 200);
        auto _ = m_conn->get();
        auto stmt = _->prepare(
            "INSERT INTO messages "
            "(event_id,chanid,data,etype,"
            " sender_name,sender_nick,peer_number,sender_pubkey,"
            " signature,avatar_url,time_text,ip_address,"
            " category,caption,media_url,media_mime,"
            " media_width,media_height,file_name,file_size,"
            " duration_sec,local_path,gif_path,thumbnail_key,"
            " cache_tag,send_state,relates_to_rowid,edited,"
            " forwarded_from,mention,"
            " relates_to_ids,mentions_text,redacted) "
            "VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,"
            "?11,?12,?13,?14,?15,?16,?17,?18,?19,?20,"
            "?21,?22,?23,?24,?25,?26,?27,?28,?29,?30,"
            "?31,?32,?33)");
        if (!stmt.isPrepared()) { return 0; }
        int i = 1;
        if (!stmt.bind(i++, row.event_id.c_str())) { return 0; }
        if (!stmt.bind(i++, row.chanid.c_str())) { return 0; }
        if (!stmt.bind(i++, row.data.c_str())) { return 0; }
        if (!stmt.bind(i++, row.etype)) { return 0; }
        if (!stmt.bind(i++, row.sender_name.c_str())) { return 0; }
        if (!stmt.bind(i++, row.sender_nick.c_str())) { return 0; }
        if (!stmt.bind(i++, row.peer_number)) { return 0; }
        if (!stmt.bind(i++, row.sender_pubkey.c_str())) { return 0; }
        if (!stmt.bind(i++, row.signature.c_str())) { return 0; }
        if (!stmt.bind(i++, row.avatar_url.c_str())) { return 0; }
        if (!stmt.bind(i++, row.time_text.c_str())) { return 0; }
        if (!stmt.bind(i++, row.ip_address.c_str())) { return 0; }
        if (!stmt.bind(i++, row.category.c_str())) { return 0; }
        if (!stmt.bind(i++, row.caption.c_str())) { return 0; }
        if (!stmt.bind(i++, row.media_url.c_str())) { return 0; }
        if (!stmt.bind(i++, row.media_mime.c_str())) { return 0; }
        if (!stmt.bind(i++, row.media_width)) { return 0; }
        if (!stmt.bind(i++, row.media_height)) { return 0; }
        if (!stmt.bind(i++, row.file_name.c_str())) { return 0; }
        if (!stmt.bind(i++, row.file_size)) { return 0; }
        if (!stmt.bind(i++, row.duration_sec)) { return 0; }
        if (!stmt.bind(i++, row.local_path.c_str())) { return 0; }
        if (!stmt.bind(i++, row.gif_path.c_str())) { return 0; }
        if (!stmt.bind(i++, row.thumbnail_key.c_str())) { return 0; }
        if (!stmt.bind(i++, row.cache_tag)) { return 0; }
        if (!stmt.bind(i++, row.send_state)) { return 0; }
        if (!stmt.bind(i++, row.relates_to_rowid)) { return 0; }
        if (!stmt.bind(i++, row.edited)) { return 0; }
        if (!stmt.bind(i++, row.forwarded_from.c_str())) { return 0; }
        if (!stmt.bind(i++, row.mention)) { return 0; }
        if (!stmt.bind(i++, row.relates_to_ids.c_str())) { return 0; }
        if (!stmt.bind(i++, row.mentions_text.c_str())) { return 0; }
        if (!stmt.bind(i++, row.redacted)) { return 0; }
        if (!stmt.step()) {
            qWarning("MessageDb::insert_message failed for chanid=%s: %s",
                     row.chanid.c_str(), sqliteError(*_));
            return 0;
        }
        return sqlite3_last_insert_rowid(_->raw());
    }

    bool update_message(int64_t rowid, const MessageUpdate& upd) override {
        SlowGuard _w("msg::update", 200);
        auto _ = m_conn->get();
        std::string sql = "UPDATE messages SET ";
        int n = 0;
        auto addField = [&](const char* name) {
            if (n++ > 0) { sql += ","; }
            char idx[8]; snprintf(idx, sizeof(idx), "?%d", n + 1);
            sql += name; sql += "="; sql += idx;
        };
        if (upd.hasEtype)        addField("etype");
        if (upd.hasSenderName)   addField("sender_name");
        if (upd.hasSenderNick)   addField("sender_nick");
        if (upd.hasPeerNumber)   addField("peer_number");
        if (upd.hasCategory)     addField("category");
        if (upd.hasCaption)      addField("caption");
        if (upd.hasMediaUrl)     addField("media_url");
        if (upd.hasMediaMime)    addField("media_mime");
        if (upd.hasMediaWidth)   addField("media_width");
        if (upd.hasMediaHeight)  addField("media_height");
        if (upd.hasFileName)     addField("file_name");
        if (upd.hasFileSize)     addField("file_size");
        if (upd.hasDurationSec)  addField("duration_sec");
        if (upd.hasLocalPath)    addField("local_path");
        if (upd.hasThumbnailKey) addField("thumbnail_key");
        if (upd.hasSendState)    addField("send_state");
        if (upd.hasEdited)       addField("edited");
        if (upd.hasRedacted)     addField("redacted");
        if (upd.hasForwardedFrom) addField("forwarded_from");
        if (upd.hasMention)      addField("mention");
        if (upd.hasRelatesToIds)   addField("relates_to_ids");
        if (upd.hasMentionsText) addField("mentions_text");
        if (n == 0) { return true; }
        sql += " WHERE rowid=?1";
        auto stmt = _->prepare(sql.c_str());
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, rowid)) { return false; }
        int idx = 2;
        if (upd.hasEtype)        { stmt.bind(idx++, upd.etype); }
        if (upd.hasSenderName)   { stmt.bind(idx++, upd.sender_name.c_str()); }
        if (upd.hasSenderNick)   { stmt.bind(idx++, upd.sender_nick.c_str()); }
        if (upd.hasPeerNumber)   { stmt.bind(idx++, upd.peer_number); }
        if (upd.hasCategory)     { stmt.bind(idx++, upd.category.c_str()); }
        if (upd.hasCaption)      { stmt.bind(idx++, upd.caption.c_str()); }
        if (upd.hasMediaUrl)     { stmt.bind(idx++, upd.media_url.c_str()); }
        if (upd.hasMediaMime)    { stmt.bind(idx++, upd.media_mime.c_str()); }
        if (upd.hasMediaWidth)   { stmt.bind(idx++, upd.media_width); }
        if (upd.hasMediaHeight)  { stmt.bind(idx++, upd.media_height); }
        if (upd.hasFileName)     { stmt.bind(idx++, upd.file_name.c_str()); }
        if (upd.hasFileSize)     { stmt.bind(idx++, upd.file_size); }
        if (upd.hasDurationSec)  { stmt.bind(idx++, upd.duration_sec); }
        if (upd.hasLocalPath)    { stmt.bind(idx++, upd.local_path.c_str()); }
        if (upd.hasThumbnailKey) { stmt.bind(idx++, upd.thumbnail_key.c_str()); }
        if (upd.hasSendState)    { stmt.bind(idx++, upd.send_state); }
        if (upd.hasEdited)       { stmt.bind(idx++, upd.edited); }
        if (upd.hasRedacted)     { stmt.bind(idx++, upd.redacted); }
        if (upd.hasForwardedFrom) { stmt.bind(idx++, upd.forwarded_from.c_str()); }
        if (upd.hasMention)      { stmt.bind(idx++, upd.mention); }
        if (upd.hasRelatesToIds)   { stmt.bind(idx++, upd.relates_to_ids.c_str()); }
        if (upd.hasMentionsText) { stmt.bind(idx++, upd.mentions_text.c_str()); }
        return stmt.step();
    }

    bool delete_message(int64_t rowid) override {
        SlowGuard _w("msg::delete", 200);
        auto _ = m_conn->get();
        auto stmt = _->prepare("DELETE FROM messages WHERE rowid=?1");
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, rowid)) { return false; }
        return stmt.step();
    }

    std::unique_ptr<MessageRow> get_message(int64_t rowid) override {
        SlowGuard _w("msg::get", 200);
        auto _ = m_conn->get();
        auto stmt = _->prepare(
            "SELECT rowid,event_id,chanid,data,etype,"
            " sender_name,sender_nick,peer_number,sender_pubkey,"
            " signature,avatar_url,time_text,ip_address,"
            " category,caption,media_url,media_mime,"
            " media_width,media_height,file_name,file_size,"
            " duration_sec,local_path,gif_path,thumbnail_key,"
            " cache_tag,send_state,relates_to_rowid,edited,"
            " forwarded_from,mention,"
            " relates_to_ids,mentions_text,redacted "
            "FROM messages WHERE rowid=?1");
        if (!stmt.isPrepared()) { return nullptr; }
        if (!stmt.bind(1, rowid)) { return nullptr; }
        if (!stmt.stepRow()) { return nullptr; }
        auto row = std::unique_ptr<MessageRow>(new MessageRow());
        int i = 0;
        row->rowid          = stmt.columnInt64(i++);
        row->event_id       = stmt.columnText(i++);
        row->chanid         = stmt.columnText(i++);
        row->data           = stmt.columnText(i++);
        row->etype          = stmt.columnInt(i++);
        row->sender_name    = stmt.columnText(i++);
        row->sender_nick    = stmt.columnText(i++);
        row->peer_number    = stmt.columnInt(i++);
        row->sender_pubkey  = stmt.columnText(i++);
        row->signature      = stmt.columnText(i++);
        row->avatar_url     = stmt.columnText(i++);
        row->time_text      = stmt.columnText(i++);
        row->ip_address     = stmt.columnText(i++);
        row->category       = stmt.columnText(i++);
        row->caption        = stmt.columnText(i++);
        row->media_url      = stmt.columnText(i++);
        row->media_mime     = stmt.columnText(i++);
        row->media_width    = stmt.columnInt(i++);
        row->media_height   = stmt.columnInt(i++);
        row->file_name      = stmt.columnText(i++);
        row->file_size      = stmt.columnInt(i++);
        row->duration_sec   = stmt.columnInt(i++);
        row->local_path     = stmt.columnText(i++);
        row->gif_path       = stmt.columnText(i++);
        row->thumbnail_key  = stmt.columnText(i++);
        row->cache_tag      = stmt.columnInt(i++);
        row->send_state     = stmt.columnInt(i++);
row->relates_to_rowid = stmt.columnInt64(i++);
        row->edited         = stmt.columnInt(i++);
        row->forwarded_from = stmt.columnText(i++);
        row->mention        = stmt.columnInt(i++);
        row->relates_to_ids   = stmt.columnText(i++);
        row->mentions_text  = stmt.columnText(i++);
        row->redacted       = stmt.columnInt(i++);
        return row;
    }

    std::vector<MessageRow> load_messages(const char* chanid, int limit) override {
        SlowGuard _w("msg::load", 200);
        auto _ = m_conn->get();
        std::vector<MessageRow> rows;
        auto stmt = _->prepare(
            "SELECT rowid,event_id,chanid,data,etype,"
            " sender_name,sender_nick,peer_number,sender_pubkey,"
            " signature,avatar_url,time_text,ip_address,"
            " category,caption,media_url,media_mime,"
            " media_width,media_height,file_name,file_size,"
            " duration_sec,local_path,gif_path,thumbnail_key,"
            " cache_tag,send_state,relates_to_rowid,edited,"
            " forwarded_from,mention,"
            " relates_to_ids,mentions_text,redacted "
            "FROM messages WHERE chanid=?1 "
            "ORDER BY rowid DESC LIMIT ?2");
        if (!stmt.isPrepared()) { return rows; }
        if (!stmt.bind(1, chanid)) { return rows; }
        if (!stmt.bind(2, limit)) { return rows; }
        while (stmt.stepRow()) {
            rows.push_back(readRow(stmt));
        }
        std::reverse(rows.begin(), rows.end());
        return rows;
    }

    std::vector<MessageRow> load_messages_before(
        const char* chanid, int64_t before_rowid, int limit) override {
        SlowGuard _w("msg::load_before", 200);
        auto _ = m_conn->get();
        std::vector<MessageRow> rows;
        auto stmt = _->prepare(
            "SELECT rowid,event_id,chanid,data,etype,"
            " sender_name,sender_nick,peer_number,sender_pubkey,"
            " signature,avatar_url,time_text,ip_address,"
            " category,caption,media_url,media_mime,"
            " media_width,media_height,file_name,file_size,"
            " duration_sec,local_path,gif_path,thumbnail_key,"
            " cache_tag,send_state,relates_to_rowid,edited,"
            " forwarded_from,mention,"
            " relates_to_ids,mentions_text,redacted "
            "FROM messages WHERE chanid=?1 AND rowid<?2 "
            "ORDER BY rowid DESC LIMIT ?3");
        if (!stmt.isPrepared()) { return rows; }
        if (!stmt.bind(1, chanid)) { return rows; }
        if (!stmt.bind(2, before_rowid)) { return rows; }
        if (!stmt.bind(3, limit)) { return rows; }
        while (stmt.stepRow()) {
            rows.push_back(readRow(stmt));
        }
        std::reverse(rows.begin(), rows.end());
        return rows;
    }

    std::vector<int64_t> search_messages(const char* query, int limit) override {
        auto _ = m_conn->get();
        std::vector<int64_t> ids;
        auto stmt = _->prepare(
            "SELECT rowid FROM messages_fts "
            "WHERE messages_fts MATCH ?1 "
            "ORDER BY rank LIMIT ?2");
        if (!stmt.isPrepared()) { return ids; }
        if (!stmt.bind(1, query)) { return ids; }
        if (!stmt.bind(2, limit)) { return ids; }
        while (stmt.stepRow()) {
            ids.push_back(stmt.columnInt64(0));
        }
        return ids;
    }

    bool add_reaction(int64_t msg_rowid, const char* emoji,
                      const char* sender) override {
        auto _ = m_conn->get();
        auto stmt = _->prepare(
            "INSERT OR IGNORE INTO reactions "
            "(message_rowid,emoji,sender_name) VALUES (?1,?2,?3)");
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, msg_rowid)) { return false; }
        if (!stmt.bind(2, emoji)) { return false; }
        if (!stmt.bind(3, sender)) { return false; }
        return stmt.step();
    }

    bool remove_reaction(int64_t msg_rowid, const char* emoji,
                         const char* sender) override {
        auto _ = m_conn->get();
        auto stmt = _->prepare(
            "DELETE FROM reactions "
            "WHERE message_rowid=?1 AND emoji=?2 AND sender_name=?3");
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, msg_rowid)) { return false; }
        if (!stmt.bind(2, emoji)) { return false; }
        if (!stmt.bind(3, sender)) { return false; }
        return stmt.step();
    }

    std::vector<ReactionRow> get_reactions(int64_t msg_rowid) override {
        auto _ = m_conn->get();
        std::vector<ReactionRow> rows;
        auto stmt = _->prepare(
            "SELECT message_rowid,emoji,sender_name FROM reactions "
            "WHERE message_rowid=?1");
        if (!stmt.isPrepared()) { return rows; }
        if (!stmt.bind(1, msg_rowid)) { return rows; }
        while (stmt.stepRow()) {
            ReactionRow r;
            r.message_rowid = stmt.columnInt64(0);
            r.emoji = stmt.columnText(1);
            r.sender_name = stmt.columnText(2);
            rows.push_back(std::move(r));
        }
        return rows;
    }

    bool set_translation(const TranslationRow& row) override {
        auto _ = m_conn->get();
        auto stmt = _->prepare(
            "INSERT OR REPLACE INTO translations "
            "(message_rowid,target_lang,translated_text,"
            " translated_entities,source_lang,provider) "
            "VALUES (?1,?2,?3,?4,?5,?6)");
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, row.message_rowid)) { return false; }
        if (!stmt.bind(2, row.target_lang.c_str())) { return false; }
        if (!stmt.bind(3, row.translated_text.c_str())) { return false; }
        if (!stmt.bind(4, row.translated_entities.c_str())) { return false; }
        if (!stmt.bind(5, row.source_lang.c_str())) { return false; }
        if (!stmt.bind(6, row.provider.c_str())) { return false; }
        return stmt.step();
    }

    std::unique_ptr<TranslationRow> get_translation(
        int64_t msg_rowid, const char* lang) override {
        auto _ = m_conn->get();
        auto stmt = _->prepare(
            "SELECT message_rowid,target_lang,translated_text,"
            " translated_entities,source_lang,provider "
            "FROM translations WHERE message_rowid=?1 AND target_lang=?2");
        if (!stmt.isPrepared()) { return nullptr; }
        if (!stmt.bind(1, msg_rowid)) { return nullptr; }
        if (!stmt.bind(2, lang)) { return nullptr; }
        if (!stmt.stepRow()) { return nullptr; }
        auto row = std::unique_ptr<TranslationRow>(new TranslationRow());
        row->message_rowid = stmt.columnInt64(0);
        row->target_lang = stmt.columnText(1);
        row->translated_text = stmt.columnText(2);
        row->translated_entities = stmt.columnText(3);
        row->source_lang = stmt.columnText(4);
        row->provider = stmt.columnText(5);
        return row;
    }

    bool clear_translations_by_lang(const char* lang) override {
        auto _ = m_conn->get();
        auto stmt = _->prepare(
            "DELETE FROM translations WHERE target_lang=?1");
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, lang)) { return false; }
        return stmt.step();
    }

    bool add_bookmark(int64_t msg_rowid, const char* chanid,
                      const char* note) override {
        auto _ = m_conn->get();
        auto stmt = _->prepare(
            "INSERT OR IGNORE INTO bookmarks "
            "(message_rowid,chanid,note) VALUES (?1,?2,?3)");
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, msg_rowid)) { return false; }
        if (!stmt.bind(2, chanid)) { return false; }
        if (!stmt.bind(3, note)) { return false; }
        return stmt.step();
    }

    bool remove_bookmark(int64_t msg_rowid) override {
        auto _ = m_conn->get();
        auto stmt = _->prepare(
            "DELETE FROM bookmarks WHERE message_rowid=?1");
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, msg_rowid)) { return false; }
        return stmt.step();
    }

    std::unique_ptr<BookmarkRow> get_bookmark(int64_t msg_rowid) override {
        auto _ = m_conn->get();
        auto stmt = _->prepare(
            "SELECT id,message_rowid,chanid,note,tag "
            "FROM bookmarks WHERE message_rowid=?1");
        if (!stmt.isPrepared()) { return nullptr; }
        if (!stmt.bind(1, msg_rowid)) { return nullptr; }
        if (!stmt.stepRow()) { return nullptr; }
        auto row = std::unique_ptr<BookmarkRow>(new BookmarkRow());
        row->id = stmt.columnInt64(0);
        row->message_rowid = stmt.columnInt64(1);
        row->chanid = stmt.columnText(2);
        row->note = stmt.columnText(3);
        row->tag = stmt.columnText(4);
        return row;
    }

    std::vector<int64_t> list_bookmark_rowids(const char* chanid) override {
        auto _ = m_conn->get();
        auto stmt = _->prepare(
            "SELECT message_rowid FROM bookmarks WHERE chanid=?1");
        if (!stmt.isPrepared()) { return {}; }
        if (!stmt.bind(1, chanid)) { return {}; }
        std::vector<int64_t> out;
        while (stmt.stepRow()) { out.push_back(stmt.columnInt64(0)); }
        return out;
    }

    bool clear_channel(const char* chanid) override {
        auto _ = m_conn->get();
        auto s1 = _->prepare(
            "DELETE FROM reactions WHERE message_rowid IN "
            "(SELECT rowid FROM messages WHERE chanid=?1)");
        if (s1.isPrepared()) { s1.bind(1, chanid); s1.step(); }

        auto s2 = _->prepare("DELETE FROM bookmarks WHERE chanid=?1");
        if (s2.isPrepared()) { s2.bind(1, chanid); s2.step(); }

        auto s3 = _->prepare("DELETE FROM messages WHERE chanid=?1");
        if (s3.isPrepared()) { s3.bind(1, chanid); s3.step(); }

        return true;
    }

    bool set_message_tags(int64_t rowid, const char* tags) override {
        auto _ = m_conn->get();
        std::vector<std::string> list = TagUtil::splitTags(tags ? tags : "");
        TagUtil::dedupTags(list);
        if (!_->beginTransaction()) { return false; }
        {
            auto del = _->prepare("DELETE FROM message_tags WHERE message_rowid=?1");
            if (!del.bind(1, rowid) || !del.step()) {
                _->rollbackTransaction(); return false;
            }
        }
        for (size_t i = 0; i < list.size(); i++) {
            auto ins = _->prepare(
                "INSERT INTO message_tags (message_rowid,tag,seq)"
                " VALUES (?1,?2,?3)");
            if (!ins.bind(1, rowid) || !ins.bind(2, list[i].c_str())
                || !ins.bind(3, (int64_t)i) || !ins.step()) {
                _->rollbackTransaction(); return false;
            }
        }
        return _->commitTransaction();
    }

    std::vector<std::string> get_message_tags(int64_t rowid) override {
        auto _ = m_conn->get();
        std::vector<std::string> out;
        auto stmt = _->prepare(
            "SELECT tag FROM message_tags "
            "WHERE message_rowid=?1 ORDER BY seq ASC");
        if (!stmt.isPrepared()) { return out; }
        if (!stmt.bind(1, rowid)) { return out; }
        while (stmt.stepRow()) { out.push_back(stmt.columnText(0)); }
        return out;
    }

    bool begin_write_transaction() override { auto _ = m_conn->get(); return _->beginTransaction(); }
    bool commit_transaction() override { auto _ = m_conn->get(); return _->commitTransaction(); }

    int64_t countMessages() override {
        SlowGuard _w("msg::count_messages", 500);
        auto _ = m_conn->get();
        auto stmt = _->prepare("SELECT COUNT(*) FROM messages");
        if (!stmt.isPrepared() || !stmt.stepRow()) { return -1; }
        return stmt.columnInt64(0);
    }

    int64_t countTranslations() override {
        SlowGuard _w("msg::count_translations", 300);
        auto _ = m_conn->get();
        auto stmt = _->prepare("SELECT COUNT(*) FROM translations");
        if (!stmt.isPrepared() || !stmt.stepRow()) { return -1; }
        return stmt.columnInt64(0);
    }

    int64_t countReactions() override {
        SlowGuard _w("msg::count_reactions", 300);
        auto _ = m_conn->get();
        auto stmt = _->prepare("SELECT COUNT(*) FROM reactions");
        if (!stmt.isPrepared() || !stmt.stepRow()) { return -1; }
        return stmt.columnInt64(0);
    }

    int64_t countBookmarks() override {
        SlowGuard _w("msg::count_bookmarks", 300);
        auto _ = m_conn->get();
        auto stmt = _->prepare("SELECT COUNT(*) FROM bookmarks");
        if (!stmt.isPrepared() || !stmt.stepRow()) { return -1; }
        return stmt.columnInt64(0);
    }

private:
    static MessageRow readRow(SqliteStatement& stmt) {
        MessageRow row;
        int i = 0;
        row.rowid          = stmt.columnInt64(i++);
        row.event_id       = stmt.columnText(i++);
        row.chanid         = stmt.columnText(i++);
        row.data           = stmt.columnText(i++);
        row.etype          = stmt.columnInt(i++);
        row.sender_name    = stmt.columnText(i++);
        row.sender_nick    = stmt.columnText(i++);
        row.peer_number    = stmt.columnInt(i++);
        row.sender_pubkey  = stmt.columnText(i++);
        row.signature      = stmt.columnText(i++);
        row.avatar_url     = stmt.columnText(i++);
        row.time_text      = stmt.columnText(i++);
        row.ip_address     = stmt.columnText(i++);
        row.category       = stmt.columnText(i++);
        row.caption        = stmt.columnText(i++);
        row.media_url      = stmt.columnText(i++);
        row.media_mime     = stmt.columnText(i++);
        row.media_width    = stmt.columnInt(i++);
        row.media_height   = stmt.columnInt(i++);
        row.file_name      = stmt.columnText(i++);
        row.file_size      = stmt.columnInt(i++);
        row.duration_sec   = stmt.columnInt(i++);
        row.local_path     = stmt.columnText(i++);
        row.gif_path       = stmt.columnText(i++);
        row.thumbnail_key  = stmt.columnText(i++);
        row.cache_tag      = stmt.columnInt(i++);
        row.send_state     = stmt.columnInt(i++);
        row.relates_to_rowid = stmt.columnInt64(i++);
        row.edited         = stmt.columnInt(i++);
        row.forwarded_from = stmt.columnText(i++);
        row.mention        = stmt.columnInt(i++);
        row.relates_to_ids   = stmt.columnText(i++);
        row.mentions_text  = stmt.columnText(i++);
        row.redacted       = stmt.columnInt(i++);
        return row;
    }
};

class MessageDbSyncSafe final : public MessageDbSyncSafeInterface {
    std::shared_ptr<SqliteConnectionSafe> m_conn;
    MessageDbSync m_impl;
public:
    explicit MessageDbSyncSafe(std::shared_ptr<SqliteConnectionSafe> conn)
        : m_conn(conn), m_impl(conn) {}
    MessageDbSyncInterface& get() override { return m_impl; }
};

class MessageDbAsync final : public MessageDbAsyncInterface {
    std::shared_ptr<MessageDbSyncSafeInterface> m_sync;
    std::shared_ptr<WriteQueue> m_queue;
public:
    MessageDbAsync(std::shared_ptr<MessageDbSyncSafeInterface> sync,
                   std::shared_ptr<WriteQueue> queue)
        : m_sync(std::move(sync)), m_queue(std::move(queue)) {}

    void post(std::function<void()> task) { m_queue->post(std::move(task)); }

    void insert_message(MessageRow row, std::function<void(int64_t)> done) override {
        auto sync = m_sync;
        post([sync, row, done]() {
            int64_t id = sync->get().insert_message(row);
            if (done) { done(id); }
        });
    }

    void update_message(int64_t rowid, MessageUpdate upd,
                        std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, rowid, upd, done]() {
            bool ok = sync->get().update_message(rowid, upd);
            if (done) { done(ok); }
        });
    }

    void delete_message(int64_t rowid, std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, rowid, done]() {
            bool ok = sync->get().delete_message(rowid);
            if (done) { done(ok); }
        });
    }

    void get_message(int64_t rowid,
                     std::function<void(std::unique_ptr<MessageRow>)> done) override {
        auto sync = m_sync;
        post([sync, rowid, done]() {
            auto row = sync->get().get_message(rowid);
            if (done) { done(std::move(row)); }
        });
    }

    void load_messages(std::string chanid, int limit,
                       std::function<void(std::vector<MessageRow>)> done) override {
        auto sync = m_sync;
        post([sync, chanid, limit, done]() {
            auto rows = sync->get().load_messages(chanid.c_str(), limit);
            if (done) { done(std::move(rows)); }
        });
    }

    void load_messages_before(std::string chanid, int64_t before_rowid,
                              int limit,
                              std::function<void(std::vector<MessageRow>)> done) override {
        auto sync = m_sync;
        post([sync, chanid, before_rowid, limit,
              done]() {
            auto rows = sync->get().load_messages_before(chanid.c_str(), before_rowid, limit);
            if (done) { done(std::move(rows)); }
        });
    }

    void search_messages(std::string query, int limit,
                         std::function<void(std::vector<int64_t>)> done) override {
        auto sync = m_sync;
        post([sync, query, limit, done]() {
            auto ids = sync->get().search_messages(query.c_str(), limit);
            if (done) { done(std::move(ids)); }
        });
    }

    void add_reaction(int64_t msg_rowid, std::string emoji,
                      std::string sender,
                      std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, msg_rowid, emoji,
              sender, done]() {
            bool ok = sync->get().add_reaction(msg_rowid, emoji.c_str(), sender.c_str());
            if (done) { done(ok); }
        });
    }

    void remove_reaction(int64_t msg_rowid, std::string emoji,
                         std::string sender,
                         std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, msg_rowid, emoji,
              sender, done]() {
            bool ok = sync->get().remove_reaction(msg_rowid, emoji.c_str(), sender.c_str());
            if (done) { done(ok); }
        });
    }

    void get_reactions(int64_t msg_rowid,
                       std::function<void(std::vector<ReactionRow>)> done) override {
        auto sync = m_sync;
        post([sync, msg_rowid, done]() {
            auto rows = sync->get().get_reactions(msg_rowid);
            if (done) { done(std::move(rows)); }
        });
    }

    void set_message_tags(int64_t rowid, std::string tags,
                          std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, rowid, tags, done]() {
            bool ok = sync->get().set_message_tags(rowid, tags.c_str());
            if (done) { done(ok); }
        });
    }

    void get_message_tags(int64_t rowid,
                          std::function<void(std::vector<std::string>)> done) override {
        auto sync = m_sync;
        post([sync, rowid, done]() {
            auto rows = sync->get().get_message_tags(rowid);
            if (done) { done(std::move(rows)); }
        });
    }

    void set_translation(TranslationRow row,
                         std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, row, done]() {
            bool ok = sync->get().set_translation(row);
            if (done) { done(ok); }
        });
    }

    void get_translation(int64_t msg_rowid, std::string lang,
                         std::function<void(std::unique_ptr<TranslationRow>)> done) override {
        auto sync = m_sync;
        post([sync, msg_rowid, lang, done]() {
            auto row = sync->get().get_translation(msg_rowid, lang.c_str());
            if (done) { done(std::move(row)); }
        });
    }

    void clear_translations_by_lang(std::string lang,
                                    std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, lang, done]() {
            bool ok = sync->get().clear_translations_by_lang(lang.c_str());
            if (done) { done(ok); }
        });
    }

    void add_bookmark(int64_t msg_rowid, std::string chanid,
                      std::string note,
                      std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, msg_rowid, chanid,
              note, done]() {
            bool ok = sync->get().add_bookmark(msg_rowid, chanid.c_str(), note.c_str());
            if (done) { done(ok); }
        });
    }

    void remove_bookmark(int64_t msg_rowid,
                         std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, msg_rowid, done]() {
            bool ok = sync->get().remove_bookmark(msg_rowid);
            if (done) { done(ok); }
        });
    }

    void get_bookmark(int64_t msg_rowid,
                      std::function<void(std::unique_ptr<BookmarkRow>)> done) override {
        auto sync = m_sync;
        post([sync, msg_rowid, done]() {
            auto row = sync->get().get_bookmark(msg_rowid);
            if (done) { done(std::move(row)); }
        });
    }

    void clear_channel(std::string chanid,
                       std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, chanid, done]() {
            bool ok = sync->get().clear_channel(chanid.c_str());
            if (done) { done(ok); }
        });
    }

    void close(std::function<void()> done) override {
        auto sync = m_sync;
        post([sync, done]() {
            if (done) { done(); }
        });
    }
};

}  // namespace

std::shared_ptr<MessageDbSyncSafeInterface> create_message_db(
    std::shared_ptr<SqliteConnectionSafe> conn) {
    return std::make_shared<MessageDbSyncSafe>(std::move(conn));
}

std::shared_ptr<MessageDbAsyncInterface> create_message_db_async(
    std::shared_ptr<MessageDbSyncSafeInterface> sync,
    std::shared_ptr<WriteQueue> queue) {
    return std::make_shared<MessageDbAsync>(std::move(sync), std::move(queue));
}

bool init_message_db(SqliteDb& db) {
    // messages + reactions + translations + bookmarks + FTS
    if (!db.exec(
        "CREATE TABLE IF NOT EXISTS messages ("
        "  rowid       INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  event_id    TEXT UNIQUE,"
        "  chanid      TEXT NOT NULL REFERENCES channels(chanid),"
        "  data        TEXT NOT NULL,"
        "  etype       INTEGER DEFAULT 0,"
        "  sender_name TEXT DEFAULT '',"
        "  sender_nick TEXT DEFAULT '',"
        "  peer_number INTEGER DEFAULT -1,"
        "  sender_pubkey TEXT DEFAULT '',"
        "  signature   TEXT DEFAULT '',"
        "  avatar_url  TEXT DEFAULT '',"
        "  time_text   TEXT DEFAULT '',"
        "  ip_address  TEXT DEFAULT '',"
        "  category    TEXT DEFAULT '',"
        "  caption     TEXT DEFAULT '',"
        "  media_url   TEXT,"
        "  media_mime  TEXT,"
        "  media_width INTEGER DEFAULT 0,"
        "  media_height INTEGER DEFAULT 0,"
        "  file_name   TEXT,"
        "  file_size   INTEGER DEFAULT 0,"
        "  duration_sec INTEGER DEFAULT 0,"
        "  local_path  TEXT,"
        "  gif_path    TEXT DEFAULT '',"
        "  thumbnail_key TEXT,"
        "  cache_tag   INTEGER DEFAULT 0,"
        "  send_state  INTEGER DEFAULT 0,"
        "  relates_to_rowid INTEGER DEFAULT 0,"
        "  relates_to_ids   TEXT DEFAULT '',"
        "  mentions_text    TEXT DEFAULT '',"
        "  redacted         INTEGER DEFAULT 0,"
        "  edited      INTEGER DEFAULT 0,"
        "  deleted_at  TIMESTAMP,"
        "  forwarded_from TEXT DEFAULT '',"
        "  mention     INTEGER DEFAULT 0,"
        "  created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ")")) { return false; }

    // 身份不变量守卫：游标/未读/翻译缓存/FTS 均依赖 messages.rowid 单调永不复用（见 AGENTS.md）
    {
        auto guardStmt = db.prepare(
            "SELECT sql FROM sqlite_master WHERE type='table' AND name='messages'");
        if (guardStmt.isPrepared() && guardStmt.stepRow()) {
            std::string def = guardStmt.columnText(0);
            if (def.find("INTEGER PRIMARY KEY AUTOINCREMENT") == std::string::npos) {
                qWarning("[DB] GUARD: messages 缺少 INTEGER PRIMARY KEY AUTOINCREMENT，"
                         "rowid 游标/未读/翻译机制将失效！请恢复 AUTOINCREMENT 后重启。");
            }
        }
    }

    // 存量库迁移：新装库 CREATE 已含新列（自然跳过）；老库幂等补齐
    {
        bool hasRelRowid = false;
        bool hasRelIds = false;
        bool hasMentions = false;
        bool hasRedacted = false;
        auto stmt = db.prepare("PRAGMA table_info(messages)");
        while (stmt.isPrepared() && stmt.stepRow()) {
            const std::string name = stmt.columnText(1);
            if (name == "relates_to_rowid") hasRelRowid = true;
            if (name == "relates_to_ids")   hasRelIds   = true;
            if (name == "mentions_text")    hasMentions = true;
            if (name == "redacted")         hasRedacted = true;
        }
        if (!hasRelRowid &&
            !db.exec("ALTER TABLE messages ADD COLUMN relates_to_rowid INTEGER DEFAULT 0")) {
            return false;
        }
        if (!hasRelIds &&
            !db.exec("ALTER TABLE messages ADD COLUMN relates_to_ids TEXT DEFAULT ''")) {
            return false;
        }
        if (!hasMentions &&
            !db.exec("ALTER TABLE messages ADD COLUMN mentions_text TEXT DEFAULT ''")) {
            return false;
        }
        if (!hasRedacted &&
            !db.exec("ALTER TABLE messages ADD COLUMN redacted INTEGER DEFAULT 0")) {
            return false;
        }
    }

    db.exec("CREATE INDEX IF NOT EXISTS idx_messages_chanid"
            "  ON messages(chanid, rowid DESC)");
    db.exec("CREATE INDEX IF NOT EXISTS idx_messages_media_url"
            "  ON messages(media_url) WHERE media_url IS NOT NULL");
    db.exec("CREATE INDEX IF NOT EXISTS idx_messages_send_state"
            "  ON messages(chanid, send_state)");

    if (!db.exec(
        "CREATE TABLE IF NOT EXISTS reactions ("
        "  message_rowid  INTEGER NOT NULL REFERENCES messages(rowid),"
        "  emoji          TEXT NOT NULL,"
        "  sender_name    TEXT DEFAULT '',"
        "  created_at     TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  PRIMARY KEY (message_rowid, emoji, sender_name)"
        ")")) { return false; }
    db.exec("CREATE INDEX IF NOT EXISTS idx_reactions_message"
            "  ON reactions(message_rowid)");

    if (!db.exec(
        "CREATE TABLE IF NOT EXISTS translations ("
        "  message_rowid    INTEGER NOT NULL REFERENCES messages(rowid) ON DELETE CASCADE,"
        "  target_lang      TEXT NOT NULL,"
        "  translated_text  TEXT NOT NULL,"
        "  translated_entities TEXT,"
        "  source_lang      TEXT,"
        "  provider         TEXT DEFAULT 'builtin',"
        "  created_at       TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  PRIMARY KEY (message_rowid, target_lang)"
        ")")) { return false; }

    if (!db.exec(
        "CREATE TABLE IF NOT EXISTS bookmarks ("
        "  id             INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  message_rowid  INTEGER NOT NULL,"
        "  chanid         TEXT NOT NULL REFERENCES channels(chanid),"
        "  note           TEXT DEFAULT '',"
        "  tag            TEXT DEFAULT '',"
        "  created_at     TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  UNIQUE(message_rowid)"
        ")")) { return false; }

    // 消息 tags 规范化子表（对标 Telegram/Tigase junction 设计）；
    // seq 保插入顺序；ON DELETE CASCADE 与 translations 同位（FK pragma 已开启）
    if (!db.exec(
        "CREATE TABLE IF NOT EXISTS message_tags ("
        "  message_rowid INTEGER NOT NULL REFERENCES messages(rowid) ON DELETE CASCADE,"
        "  tag           TEXT NOT NULL,"
        "  seq           INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY (message_rowid, tag)"
        ")")) { return false; }
    db.exec("CREATE INDEX IF NOT EXISTS idx_message_tags_message"
            "  ON message_tags(message_rowid, seq)");

    // FTS — try trigram first, fallback to unicode61
    if (!db.tryExec(
        "CREATE VIRTUAL TABLE IF NOT EXISTS messages_fts USING fts5("
        "  content,"
        "  tokenize='trigram case_sensitive 0',"
        "  content='messages',"
        "  content_rowid='rowid')")) {
        qWarning("Trigram not available (需 SQLite >= 3.34.0), 消息搜索回退到 unicode61");
        if (!db.tryExec(
            "CREATE VIRTUAL TABLE IF NOT EXISTS messages_fts USING fts5("
            "  content,"
            "  tokenize='unicode61',"
            "  content='messages',"
            "  content_rowid='rowid')")) {
            qWarning("FTS5 not available, message search disabled");
        } else {
            qWarning("Message search: unicode61 (CJK 子串匹配降级, trigram 不可用)");
        }
    } else {
        qDebug("Message search: trigram");
    }

    db.exec("CREATE TRIGGER IF NOT EXISTS messages_fts_insert"
            " AFTER INSERT ON messages BEGIN"
            "  INSERT INTO messages_fts(rowid, content)"
            "  VALUES (NEW.rowid, NEW.data);"
            " END");
    db.exec("CREATE TRIGGER IF NOT EXISTS messages_fts_delete"
            " AFTER DELETE ON messages BEGIN"
            "  INSERT INTO messages_fts(messages_fts, rowid, content)"
            "  VALUES('delete', OLD.rowid, OLD.data);"
            " END");
    db.exec("CREATE TRIGGER IF NOT EXISTS messages_fts_update"
            " AFTER UPDATE ON messages BEGIN"
            "  INSERT INTO messages_fts(messages_fts, rowid, content)"
            "  VALUES('delete', OLD.rowid, OLD.data);"
            "  INSERT INTO messages_fts(rowid, content)"
            "  VALUES (NEW.rowid, NEW.data);"
            " END");

    return true;
}

bool drop_message_db(SqliteDb& db) {
    db.exec("DROP TABLE IF EXISTS messages_fts");
    db.exec("DROP TABLE IF EXISTS bookmarks");
    db.exec("DROP TABLE IF EXISTS translations");
    db.exec("DROP TABLE IF EXISTS reactions");
    db.exec("DROP TABLE IF EXISTS message_tags");
    db.exec("DROP TABLE IF EXISTS messages");
    return true;
}
