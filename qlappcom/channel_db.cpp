#include "channel_db.h"
#include "tagutil.h"

namespace {

class ChannelDbSync final : public ChannelDbSyncInterface {
    std::shared_ptr<SqliteConnectionSafe> m_conn;
public:
    explicit ChannelDbSync(std::shared_ptr<SqliteConnectionSafe> conn)
        : m_conn(std::move(conn)) {}

    bool add_channel(const ChannelRow& row) override {
        auto _ = m_conn->get();
        auto stmt = _->prepare(
            "INSERT OR REPLACE INTO channels "
            "(chanid,proto_type,last_message_rowid,last_read_rowid,"
            " unread_count,pinned_order,draft_text,muted) "
            "VALUES (?1,?2,?3,?4,?5,?6,?7,?8)");
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, row.chanid.c_str())) { return false; }
        if (!stmt.bind(2, row.proto_type.c_str())) { return false; }
        if (!stmt.bind(3, row.last_message_rowid)) { return false; }
        if (!stmt.bind(4, row.last_read_rowid)) { return false; }
        if (!stmt.bind(5, row.unread_count)) { return false; }
        if (!stmt.bind(6, row.pinned_order)) { return false; }
        if (!stmt.bind(7, row.draft_text.c_str())) { return false; }
        if (!stmt.bind(8, row.muted)) { return false; }
        if (!stmt.step()) {
            qWarning("ChannelDb::add_channel failed for %s", row.chanid.c_str());
            return false;
        }
        return true;
    }

    std::unique_ptr<ChannelRow> get_channel(const char* chanid) override {
        auto _ = m_conn->get();
        auto stmt = _->prepare(
            "SELECT chanid,proto_type,last_message_rowid,last_read_rowid,"
            "       unread_count,pinned_order,draft_text,muted,"
            "       name,status,is_connected,last_message_text,last_message_time,last_active,auto_translate "
            "FROM channels WHERE chanid=?1");
        if (!stmt.isPrepared()) { return nullptr; }
        if (!stmt.bind(1, chanid)) { return nullptr; }
        if (!stmt.stepRow()) { return nullptr; }
        auto row = std::unique_ptr<ChannelRow>(new ChannelRow());
        row->chanid = stmt.columnText(0);
        row->proto_type = stmt.columnText(1);
        row->last_message_rowid = stmt.columnInt64(2);
        row->last_read_rowid = stmt.columnInt64(3);
        row->unread_count = stmt.columnInt(4);
        row->pinned_order = stmt.columnInt(5);
        row->draft_text = stmt.columnText(6);
        row->muted = stmt.columnInt(7);
        row->name = stmt.columnText(8);
        row->status = stmt.columnText(9);
        row->is_connected = stmt.columnInt(10);
        row->last_message_text = stmt.columnText(11);
        row->last_message_time = stmt.columnText(12);
        row->last_active = stmt.columnInt64(13);
        row->auto_translate = stmt.columnInt(14);
        return row;
    }

    bool update_channel(const char* chanid, const ChannelUpdate& upd) override {
        auto _ = m_conn->get();
        std::string sql = "UPDATE channels SET ";
        int n = 0;
        auto addField = [&](const char* name) {
            if (n++ > 0) { sql += ","; }
            char idx[8]; snprintf(idx, sizeof(idx), "?%d", n + 1);
            sql += name; sql += "="; sql += idx;
        };
        if (upd.hasProtoType)        addField("proto_type");
        if (upd.hasLastMessageRowid) addField("last_message_rowid");
        if (upd.hasLastReadRowid)    addField("last_read_rowid");
        if (upd.hasUnreadCount)      addField("unread_count");
        if (upd.hasPinnedOrder)      addField("pinned_order");
        if (upd.hasDraftText)        addField("draft_text");
        if (upd.hasMuted)            addField("muted");
        if (upd.hasLastMessageText)  addField("last_message_text");
        if (upd.hasLastMessageTime)  addField("last_message_time");
        if (upd.hasLastActive)       addField("last_active");
        if (n == 0) { return true; }
        sql += " WHERE chanid=?1";
        auto stmt = _->prepare(sql.c_str());
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, chanid)) { return false; }
        int idx = 2;
        if (upd.hasProtoType)        { stmt.bind(idx++, upd.proto_type.c_str()); }
        if (upd.hasLastMessageRowid) { stmt.bind(idx++, upd.last_message_rowid); }
        if (upd.hasLastReadRowid)    { stmt.bind(idx++, upd.last_read_rowid); }
        if (upd.hasUnreadCount)      { stmt.bind(idx++, upd.unread_count); }
        if (upd.hasPinnedOrder)      { stmt.bind(idx++, upd.pinned_order); }
        if (upd.hasDraftText)        { stmt.bind(idx++, upd.draft_text.c_str()); }
        if (upd.hasMuted)            { stmt.bind(idx++, upd.muted); }
        if (upd.hasLastMessageText)  { stmt.bind(idx++, upd.last_message_text.c_str()); }
        if (upd.hasLastMessageTime)  { stmt.bind(idx++, upd.last_message_time.c_str()); }
        if (upd.hasLastActive)       { stmt.bind(idx++, upd.last_active); }
        return stmt.step();
    }

    int delete_channel(const char* chanid) override {
        auto _ = m_conn->get();
        int prevFk = 0;
        {
            auto q = _->prepare("PRAGMA foreign_keys");
            if (q.isPrepared() && q.stepRow()) { prevFk = q.columnInt(0); }
        }
        _->exec("PRAGMA foreign_keys=OFF");
        int n = -1;
        auto stmt = _->prepare("DELETE FROM channels WHERE chanid=?1");
        if (stmt.isPrepared() && stmt.bind(1, chanid) && stmt.step()) {
            n = stmt.changes();
        }
        if (prevFk) { _->exec("PRAGMA foreign_keys=ON"); }
        qWarning("[SQL] local del channel '%s' -> %d row(s)", chanid, n);
        return n;
    }

    std::vector<ChannelRow> load_pinned() override {
        auto _ = m_conn->get();
        std::vector<ChannelRow> rows;
        auto stmt = _->prepare(
            "SELECT chanid,proto_type,last_message_rowid,last_read_rowid,"
            "       unread_count,pinned_order,draft_text,muted,"
            "       name,status,is_connected,last_message_text,last_message_time,last_active,auto_translate "
            "FROM channels WHERE pinned_order>0 ORDER BY pinned_order ASC");
        if (!stmt.isPrepared()) { return rows; }
        while (stmt.stepRow()) {
            ChannelRow row;
            row.chanid = stmt.columnText(0);
            row.proto_type = stmt.columnText(1);
            row.last_message_rowid = stmt.columnInt64(2);
            row.last_read_rowid = stmt.columnInt64(3);
            row.unread_count = stmt.columnInt(4);
            row.pinned_order = stmt.columnInt(5);
            row.draft_text = stmt.columnText(6);
            row.muted = stmt.columnInt(7);
            row.name = stmt.columnText(8);
            row.status = stmt.columnText(9);
            row.is_connected = stmt.columnInt(10);
            row.last_message_text = stmt.columnText(11);
            row.last_message_time = stmt.columnText(12);
            row.last_active = stmt.columnInt64(13);
            row.auto_translate = stmt.columnInt(14);
            rows.push_back(std::move(row));
        }
        return rows;
    }

    std::vector<ChannelRow> load_all_channels() override {
        auto _ = m_conn->get();
        std::vector<ChannelRow> rows;
        auto stmt = _->prepare(
            "SELECT chanid,proto_type,last_message_rowid,last_read_rowid,"
            "       unread_count,pinned_order,draft_text,muted,"
            "       name,status,is_connected,last_message_text,"
            "       last_message_time,last_active,auto_translate,"
            "       icon_url,pubkey "
            "FROM channels ORDER BY last_active DESC");
        if (!stmt.isPrepared()) { return rows; }
        while (stmt.stepRow()) {
            ChannelRow row;
            int i = 0;
            row.chanid            = stmt.columnText(i++);
            row.proto_type        = stmt.columnText(i++);
            row.last_message_rowid= stmt.columnInt64(i++);
            row.last_read_rowid   = stmt.columnInt64(i++);
            row.unread_count      = stmt.columnInt(i++);
            row.pinned_order      = stmt.columnInt(i++);
            row.draft_text        = stmt.columnText(i++);
            row.muted             = stmt.columnInt(i++);
            row.name              = stmt.columnText(i++);
            row.status            = stmt.columnText(i++);
            row.is_connected      = stmt.columnInt(i++);
            row.last_message_text = stmt.columnText(i++);
            row.last_message_time = stmt.columnText(i++);
            row.last_active       = stmt.columnInt64(i++);
            row.auto_translate    = stmt.columnInt(i++);
            row.icon_url          = stmt.columnText(i++);
            row.pubkey            = stmt.columnText(i++);
            rows.push_back(std::move(row));
        }
        return rows;
    }

    bool add_peer(const PeerRow& row) override {
        auto _ = m_conn->get();
        auto stmt = _->prepare(
            "INSERT OR REPLACE INTO peers "
            "(chanid,peer_number,public_key,name,nickname,"
            " avatar_url,status_text,status_str,user_status,"
            " peer_ip,role,role_str,is_self,last_seen,status) "
            "VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15)");
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, row.chanid.c_str())) { return false; }
        if (!stmt.bind(2, row.peer_number)) { return false; }
        if (!stmt.bind(3, row.public_key.c_str())) { return false; }
        if (!stmt.bind(4, row.name.c_str())) { return false; }
        if (!stmt.bind(5, row.nickname.c_str())) { return false; }
        if (!stmt.bind(6, row.avatar_url.c_str())) { return false; }
        if (!stmt.bind(7, row.status_text.c_str())) { return false; }
        if (!stmt.bind(8, row.status_str.c_str())) { return false; }
        if (!stmt.bind(9, row.user_status.c_str())) { return false; }
        if (!stmt.bind(10, row.peer_ip.c_str())) { return false; }
        if (!stmt.bind(11, row.role)) { return false; }
        if (!stmt.bind(12, row.role_str.c_str())) { return false; }
        if (!stmt.bind(13, (int)row.is_self)) { return false; }
        if (!stmt.bind(14, row.last_seen)) { return false; }
        if (!stmt.bind(15, row.status)) { return false; }
        return stmt.step();
    }

    bool update_peer(const PeerRow& row) override {
        auto _ = m_conn->get();
        auto stmt = _->prepare(
            "INSERT INTO peers "
            "(chanid,peer_number,public_key,name,nickname,"
            " avatar_url,status_text,status_str,user_status,"
            " peer_ip,role,role_str,is_self,last_seen,status) "
            "VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15) "
            "ON CONFLICT(chanid,peer_number) DO UPDATE SET "
            "public_key=CASE WHEN excluded.public_key!='' THEN excluded.public_key ELSE peers.public_key END,"
            "name=CASE WHEN excluded.name!='' THEN excluded.name ELSE peers.name END,"
            "nickname=CASE WHEN excluded.nickname!='' THEN excluded.nickname ELSE peers.nickname END,"
            "avatar_url=CASE WHEN excluded.avatar_url!='' THEN excluded.avatar_url ELSE peers.avatar_url END,"
            "status_text=CASE WHEN excluded.status_text!='' THEN excluded.status_text ELSE peers.status_text END,"
            "status_str=CASE WHEN excluded.status_str!='' THEN excluded.status_str ELSE peers.status_str END,"
            "user_status=CASE WHEN excluded.user_status!='' THEN excluded.user_status ELSE peers.user_status END,"
            "peer_ip=CASE WHEN excluded.peer_ip!='' THEN excluded.peer_ip ELSE peers.peer_ip END,"
            "role=excluded.role,"
            "role_str=CASE WHEN excluded.role_str!='' THEN excluded.role_str ELSE peers.role_str END,"
            "is_self=excluded.is_self,"
            "last_seen=excluded.last_seen,"
            "status=excluded.status");
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, row.chanid.c_str())) { return false; }
        if (!stmt.bind(2, row.peer_number)) { return false; }
        if (!stmt.bind(3, row.public_key.c_str())) { return false; }
        if (!stmt.bind(4, row.name.c_str())) { return false; }
        if (!stmt.bind(5, row.nickname.c_str())) { return false; }
        if (!stmt.bind(6, row.avatar_url.c_str())) { return false; }
        if (!stmt.bind(7, row.status_text.c_str())) { return false; }
        if (!stmt.bind(8, row.status_str.c_str())) { return false; }
        if (!stmt.bind(9, row.user_status.c_str())) { return false; }
        if (!stmt.bind(10, row.peer_ip.c_str())) { return false; }
        if (!stmt.bind(11, row.role)) { return false; }
        if (!stmt.bind(12, row.role_str.c_str())) { return false; }
        if (!stmt.bind(13, (int)row.is_self)) { return false; }
        if (!stmt.bind(14, row.last_seen)) { return false; }
        if (!stmt.bind(15, row.status)) { return false; }
        return stmt.step();
    }

    std::unique_ptr<PeerRow> get_chan_peer(const char* chanid, int peer_number) override {
        auto _ = m_conn->get();
        auto stmt = _->prepare(
            "SELECT chanid,peer_number,public_key,name,nickname,"
            " avatar_url,status_text,status_str,user_status,"
            " peer_ip,role,role_str,is_self,last_seen,status "
            "FROM peers WHERE chanid=?1 AND peer_number=?2");
        if (!stmt.isPrepared()) { return nullptr; }
        if (!stmt.bind(1, chanid)) { return nullptr; }
        if (!stmt.bind(2, peer_number)) { return nullptr; }
        if (!stmt.stepRow()) { return nullptr; }
        auto row = std::unique_ptr<PeerRow>(new PeerRow());
        int i = 0;
        row->chanid       = stmt.columnText(i++);
        row->peer_number  = stmt.columnInt(i++);
        row->public_key   = stmt.columnText(i++);
        row->name         = stmt.columnText(i++);
        row->nickname     = stmt.columnText(i++);
        row->avatar_url   = stmt.columnText(i++);
        row->status_text  = stmt.columnText(i++);
        row->status_str   = stmt.columnText(i++);
        row->user_status  = stmt.columnText(i++);
        row->peer_ip      = stmt.columnText(i++);
        row->role         = stmt.columnInt(i++);
        row->role_str     = stmt.columnText(i++);
        row->is_self      = stmt.columnInt(i++) != 0;
        row->last_seen    = stmt.columnInt64(i++);
        row->status       = stmt.columnInt(i++);
        return row;
    }

    std::vector<PeerRow> load_chan_peers(const char* chanid) override {
        auto _ = m_conn->get();
        std::vector<PeerRow> rows;
        auto stmt = _->prepare(
            "SELECT chanid,peer_number,public_key,name,nickname,"
            " avatar_url,status_text,status_str,user_status,"
            " peer_ip,role,role_str,is_self,last_seen,status "
            "FROM peers WHERE chanid=?1 "
            "ORDER BY peer_number ASC");
        if (!stmt.isPrepared()) { return rows; }
        if (!stmt.bind(1, chanid)) { return rows; }
        while (stmt.stepRow()) {
            PeerRow row;
            int i = 0;
            row.chanid       = stmt.columnText(i++);
            row.peer_number  = stmt.columnInt(i++);
            row.public_key   = stmt.columnText(i++);
            row.name         = stmt.columnText(i++);
            row.nickname     = stmt.columnText(i++);
            row.avatar_url   = stmt.columnText(i++);
            row.status_text  = stmt.columnText(i++);
            row.status_str   = stmt.columnText(i++);
            row.user_status  = stmt.columnText(i++);
            row.peer_ip      = stmt.columnText(i++);
            row.role         = stmt.columnInt(i++);
            row.role_str     = stmt.columnText(i++);
            row.is_self      = stmt.columnInt(i++) != 0;
            row.last_seen    = stmt.columnInt64(i++);
            row.status       = stmt.columnInt(i++);
            rows.push_back(std::move(row));
        }
        return rows;
    }

    bool remove_chan_peers(const char* chanid) override {
        auto _ = m_conn->get();
        auto stmt = _->prepare("DELETE FROM peers WHERE chanid=?1");
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, chanid)) { return false; }
        return stmt.step();
    }

    bool add_contact_channel(const ChannelRow& row) override {
        auto _ = m_conn->get();
        auto stmt = _->prepare(
            "INSERT OR REPLACE INTO channels "
            "(chanid,proto_type,name,status,is_connected,"
            " last_message_text,last_message_time,last_active,"
            " unread_count,pinned_order,muted,auto_translate) "
            "VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12)");
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, row.chanid.c_str()))          { return false; }
        if (!stmt.bind(2, row.proto_type.c_str()))       { return false; }
        if (!stmt.bind(3, row.name.c_str()))             { return false; }
        if (!stmt.bind(4, row.status.c_str()))           { return false; }
        if (!stmt.bind(5, row.is_connected))             { return false; }
        if (!stmt.bind(6, row.last_message_text.c_str())) { return false; }
        if (!stmt.bind(7, row.last_message_time.c_str())) { return false; }
        if (!stmt.bind(8, row.last_active))              { return false; }
        if (!stmt.bind(9, row.unread_count))             { return false; }
        if (!stmt.bind(10, row.pinned_order))            { return false; }
        if (!stmt.bind(11, row.muted))                   { return false; }
        if (!stmt.bind(12, row.auto_translate))          { return false; }
        return stmt.step();
    }

    bool update_contact_channel(const ChannelRow& row) override {
        auto _ = m_conn->get();
        auto stmt = _->prepare(
            "INSERT INTO channels "
            "(chanid,proto_type,name,status,is_connected,icon_url,pubkey,"
            " last_message_text,last_message_time,last_active,"
            " unread_count,pinned_order,muted,auto_translate) "
            "VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14) "
            "ON CONFLICT(chanid) DO UPDATE SET "
            "name=CASE WHEN excluded.name!='' THEN excluded.name ELSE channels.name END,"
            "status=CASE WHEN excluded.status!='' THEN excluded.status ELSE channels.status END,"
            "is_connected=CASE WHEN excluded.is_connected!=-1 THEN excluded.is_connected ELSE channels.is_connected END,"
            "icon_url=CASE WHEN excluded.icon_url!='' THEN excluded.icon_url ELSE channels.icon_url END,"
            "pubkey=CASE WHEN excluded.pubkey!='' THEN excluded.pubkey ELSE channels.pubkey END,"
            // 会话摘要与活跃时间：自己发的消息同样要刷新列表预览与排序
            "last_message_text=CASE WHEN excluded.last_message_text!='' THEN excluded.last_message_text ELSE channels.last_message_text END,"
            "last_message_time=CASE WHEN excluded.last_message_time!='' THEN excluded.last_message_time ELSE channels.last_message_time END,"
            "last_active=CASE WHEN excluded.last_active>0 THEN excluded.last_active ELSE channels.last_active END");
        if (!stmt.isPrepared()) {
            qWarning("ChannelDb::update_contact_channel prepare failed for %s", row.chanid.c_str());
            return false;
        }
        if (!stmt.bind(1, row.chanid.c_str()))          { return false; }
        if (!stmt.bind(2, row.proto_type.c_str()))       { return false; }
        if (!stmt.bind(3, row.name.c_str()))             { return false; }
        if (!stmt.bind(4, row.status.c_str()))           { return false; }
        if (!stmt.bind(5, row.is_connected))             { return false; }
        if (!stmt.bind(6, row.icon_url.c_str()))         { return false; }
        if (!stmt.bind(7, row.pubkey.c_str()))           { return false; }
        if (!stmt.bind(8, row.last_message_text.c_str())) { return false; }
        if (!stmt.bind(9, row.last_message_time.c_str())) { return false; }
        if (!stmt.bind(10, row.last_active))             { return false; }
        if (!stmt.bind(11, row.unread_count))            { return false; }
        if (!stmt.bind(12, row.pinned_order))            { return false; }
        if (!stmt.bind(13, row.muted))                   { return false; }
        if (!stmt.bind(14, row.auto_translate))          { return false; }
        return stmt.step();
    }

    std::vector<std::string> get_channel_tags(const char* chanid) override {
        auto _ = m_conn->get();
        std::vector<std::string> out;
        auto stmt = _->prepare(
            "SELECT tag FROM channel_tags "
            "WHERE chanid=?1 ORDER BY seq ASC");
        if (!stmt.isPrepared()) { return out; }
        if (!stmt.bind(1, chanid)) { return out; }
        while (stmt.stepRow()) { out.push_back(stmt.columnText(0)); }
        return out;
    }

    bool set_channel_tags(const char* chanid, const char* tags) override {
        auto _ = m_conn->get();
        std::vector<std::string> list = TagUtil::splitTags(tags ? tags : "");
        TagUtil::dedupTags(list);
        if (!_->beginTransaction()) { return false; }
        {
            auto del = _->prepare("DELETE FROM channel_tags WHERE chanid=?1");
            if (!del.bind(1, chanid) || !del.step()) {
                _->rollbackTransaction(); return false;
            }
        }
        for (size_t i = 0; i < list.size(); i++) {
            auto ins = _->prepare(
                "INSERT INTO channel_tags (chanid,tag,seq)"
                " VALUES (?1,?2,?3)");
            if (!ins.bind(1, chanid) || !ins.bind(2, list[i].c_str())
                || !ins.bind(3, (int64_t)i) || !ins.step()) {
                _->rollbackTransaction(); return false;
            }
        }
        return _->commitTransaction();
    }

    bool increment_unread(const char* chanid, int delta,
                          int64_t msgRowid = 0) override {
        auto _ = m_conn->get();
        auto stmt = _->prepare(
            "UPDATE channels SET unread_count = unread_count + ?1 "
            "WHERE chanid = ?2 AND (?3 = 0 OR ?3 > last_read_rowid)");
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, delta)) { return false; }
        if (!stmt.bind(2, chanid)) { return false; }
        if (!stmt.bind(3, msgRowid)) { return false; }
        return stmt.step();
    }

    bool mark_read(const char* chanid, int64_t lastReadRowid) override {
        auto _ = m_conn->get();
        // 已读水位只进不退：旧实现直接覆盖，读到更小的 rowid 会把水位拉回去
        auto stmt = _->prepare(
            "UPDATE channels SET last_read_rowid = MAX(COALESCE(last_read_rowid, 0), ?1), "
            "unread_count = 0 WHERE chanid = ?2");
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, lastReadRowid)) { return false; }
        if (!stmt.bind(2, chanid)) { return false; }
        return stmt.step();
    }

    bool begin_write_transaction() override { auto _ = m_conn->get(); return _->beginTransaction(); }
    bool commit_transaction() override { auto _ = m_conn->get(); return _->commitTransaction(); }

    int64_t countChannels() override {
        SlowGuard _w("chan::count_channels", 300);
        auto _ = m_conn->get();
        auto stmt = _->prepare("SELECT COUNT(*) FROM channels");
        if (!stmt.isPrepared() || !stmt.stepRow()) { return -1; }
        return stmt.columnInt64(0);
    }

    int64_t countPeers() override {
        SlowGuard _w("chan::count_peers", 300);
        auto _ = m_conn->get();
        auto stmt = _->prepare("SELECT COUNT(*) FROM peers");
        if (!stmt.isPrepared() || !stmt.stepRow()) { return -1; }
        return stmt.columnInt64(0);
    }

    int64_t totalUnread() override {
        SlowGuard _w("chan::total_unread", 300);
        auto _ = m_conn->get();
        auto stmt = _->prepare("SELECT COALESCE(SUM(unread_count),0) FROM channels");
        if (!stmt.isPrepared() || !stmt.stepRow()) { return -1; }
        return stmt.columnInt64(0);
    }
};

class ChannelDbSyncSafe final : public ChannelDbSyncSafeInterface {
    std::shared_ptr<SqliteConnectionSafe> m_conn;
    ChannelDbSync m_impl;
public:
    explicit ChannelDbSyncSafe(std::shared_ptr<SqliteConnectionSafe> conn)
        : m_conn(conn), m_impl(conn) {}
    ChannelDbSyncInterface& get() override { return m_impl; }
};

class ChannelDbAsync final : public ChannelDbAsyncInterface {
    std::shared_ptr<ChannelDbSyncSafeInterface> m_sync;
    std::shared_ptr<WriteQueue> m_queue;
public:
    ChannelDbAsync(std::shared_ptr<ChannelDbSyncSafeInterface> sync,
                   std::shared_ptr<WriteQueue> queue)
        : m_sync(std::move(sync)), m_queue(std::move(queue)) {}

    void post(std::function<void()> task) { m_queue->post(std::move(task)); }

    void add_channel(ChannelRow row, std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, row, done]() {
            bool ok = sync->get().add_channel(row);
            if (done) { done(ok); }
        });
    }

    void get_channel(std::string chanid,
                     std::function<void(std::unique_ptr<ChannelRow>)> done) override {
        auto sync = m_sync;
        post([sync, chanid, done]() {
            auto row = sync->get().get_channel(chanid.c_str());
            if (done) { done(std::move(row)); }
        });
    }

    void update_channel(std::string chanid, ChannelUpdate upd,
                        std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, chanid, upd,
              done]() {
            bool ok = sync->get().update_channel(chanid.c_str(), upd);
            if (done) { done(ok); }
        });
    }

    void delete_channel(std::string chanid, std::function<void(int)> done) override {
        auto sync = m_sync;
        post([sync, chanid, done]() {
            int n = sync->get().delete_channel(chanid.c_str());
            if (done) { done(n); }
        });
    }

    void load_pinned(std::function<void(std::vector<ChannelRow>)> done) override {
        auto sync = m_sync;
        post([sync, done]() {
            auto rows = sync->get().load_pinned();
            if (done) { done(std::move(rows)); }
        });
    }

    void load_all_channels(
            std::function<void(std::vector<ChannelRow>)> done) override {
        auto sync = m_sync;
        post([sync, done]() {
            auto rows = sync->get().load_all_channels();
            if (done) { done(std::move(rows)); }
        });
    }

    void add_peer(PeerRow row, std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, row, done]() {
            bool ok = sync->get().add_peer(row);
            if (done) { done(ok); }
        });
    }

    void update_peer(PeerRow row, std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, row, done]() {
            bool ok = sync->get().update_peer(row);
            if (done) { done(ok); }
        });
    }

    void get_chan_peer(std::string chanid, int peer_number,
                       std::function<void(std::unique_ptr<PeerRow>)> done) override {
        auto sync = m_sync;
        post([sync, chanid, peer_number, done]() {
            auto row = sync->get().get_chan_peer(chanid.c_str(), peer_number);
            if (done) { done(std::move(row)); }
        });
    }

    void load_chan_peers(std::string chanid,
                         std::function<void(std::vector<PeerRow>)> done) override {
        auto sync = m_sync;
        post([sync, chanid, done]() {
            auto rows = sync->get().load_chan_peers(chanid.c_str());
            if (done) { done(std::move(rows)); }
        });
    }

    void remove_chan_peers(std::string chanid,
                           std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, chanid, done]() {
            bool ok = sync->get().remove_chan_peers(chanid.c_str());
            if (done) { done(ok); }
        });
    }

    void add_contact_channel(ChannelRow row, std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, row, done]() {
            bool ok = sync->get().add_contact_channel(row);
            if (done) { done(ok); }
        });
    }

    void update_contact_channel(ChannelRow row, std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, row, done]() {
            bool ok = sync->get().update_contact_channel(row);
            if (done) { done(ok); }
        });
    }

    void set_channel_tags(std::string chanid, std::string tags,
                          std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, chanid, tags, done]() {
            bool ok = sync->get().set_channel_tags(chanid.c_str(), tags.c_str());
            if (done) { done(ok); }
        });
    }

    void get_channel_tags(std::string chanid,
                          std::function<void(std::vector<std::string>)> done) override {
        auto sync = m_sync;
        post([sync, chanid, done]() {
            auto rows = sync->get().get_channel_tags(chanid.c_str());
            if (done) { done(std::move(rows)); }
        });
    }

    void increment_unread(std::string chanid, int delta, int64_t msgRowid,
                          std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, chanid, delta, msgRowid, done]() {
            bool ok = sync->get().increment_unread(chanid.c_str(), delta, msgRowid);
            if (done) { done(ok); }
        });
    }

    void mark_read(std::string chanid, int64_t lastReadRowid,
                   std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, chanid, lastReadRowid, done]() {
            bool ok = sync->get().mark_read(chanid.c_str(), lastReadRowid);
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

std::shared_ptr<ChannelDbSyncSafeInterface> create_channel_db(
    std::shared_ptr<SqliteConnectionSafe> conn) {
    return std::make_shared<ChannelDbSyncSafe>(std::move(conn));
}

std::shared_ptr<ChannelDbAsyncInterface> create_channel_db_async(
    std::shared_ptr<ChannelDbSyncSafeInterface> sync,
    std::shared_ptr<WriteQueue> queue) {
    return std::make_shared<ChannelDbAsync>(std::move(sync), std::move(queue));
}

bool init_channel_db(SqliteDb& db) {
    // db.exec("DROP TABLE IF EXISTS channels"); // 清表重建 — 注释此行保留数据
    bool ok = db.exec(
        "CREATE TABLE IF NOT EXISTS channels ("
        "  chanid             TEXT PRIMARY KEY,"
        "  proto_type         TEXT DEFAULT 'dox',"
        "  last_message_rowid INTEGER NOT NULL DEFAULT 0,"
        "  last_read_rowid    INTEGER NOT NULL DEFAULT 0,"
        "  unread_count       INTEGER DEFAULT 0,"
        "  pinned_order       INTEGER DEFAULT 0,"
        "  draft_text         TEXT DEFAULT '',"
        "  muted              INTEGER DEFAULT 0,"
        "  name               TEXT DEFAULT '',"
        "  status             TEXT DEFAULT '',"
        "  is_connected       INTEGER DEFAULT 0,"
        "  last_message_text  TEXT DEFAULT '',"
        "  last_message_time  TEXT DEFAULT '',"
        "  last_active        INTEGER DEFAULT 0,"
        "  auto_translate     INTEGER DEFAULT 0,"
        "  icon_url           TEXT DEFAULT '',"
        "  pubkey             TEXT DEFAULT '',"
        "  created_at         TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ")");
    ok = ok && db.exec(
        "CREATE INDEX IF NOT EXISTS idx_channels_last_active ON channels(last_active DESC)");
    // peers v2: drop old single-key table, recreate with composite PK
    // db.exec("DROP TABLE IF EXISTS peers");
    ok = ok && db.exec(
        "CREATE TABLE IF NOT EXISTS peers ("
        "  chanid        TEXT NOT NULL,"
        "  peer_number   INTEGER NOT NULL,"
        "  public_key    TEXT DEFAULT '',"
        "  name          TEXT DEFAULT '',"
        "  nickname      TEXT DEFAULT '',"
        "  remark_name   TEXT DEFAULT '',"
        "  avatar_url    TEXT DEFAULT '',"
        "  status_text   TEXT DEFAULT '',"
        "  status_str    TEXT DEFAULT '',"
        "  user_status   TEXT DEFAULT '',"
        "  peer_ip       TEXT DEFAULT '',"
        "  role          INTEGER DEFAULT 0,"
        "  role_str      TEXT DEFAULT '',"
        "  is_self       INTEGER DEFAULT 0,"
        "  last_seen     INTEGER DEFAULT 0,"
        "  status        INTEGER DEFAULT 0,"
        "  updated_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  PRIMARY KEY (chanid, peer_number)"
        ")");
    // peers 旧库升级：无 remark_name 列时补加
    {
        bool hasRemark = false;
        auto stmt = db.prepare("PRAGMA table_info(peers)");
        while (stmt.isPrepared() && stmt.stepRow()) {
            if (QString(stmt.columnText(1)) == "remark_name") { hasRemark = true; }
        }
        if (!hasRemark) {
            ok = ok && db.exec("ALTER TABLE peers ADD COLUMN remark_name TEXT DEFAULT ''");
        }
    }
    // 联系人 tags 规范化子表（镜像 message_tags，独立于 channels 原表）
    ok = ok && db.exec(
        "CREATE TABLE IF NOT EXISTS channel_tags ("
        "  chanid  TEXT NOT NULL REFERENCES channels(chanid) ON DELETE CASCADE,"
        "  tag     TEXT NOT NULL,"
        "  seq     INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY (chanid, tag)"
        ")");
    ok = ok && db.exec("CREATE INDEX IF NOT EXISTS idx_channel_tags_chanid"
                       "  ON channel_tags(chanid, seq)");
    return ok;
}

bool drop_channel_db(SqliteDb& db) {
    return db.exec("DROP TABLE IF EXISTS channels") &&
           db.exec("DROP TABLE IF EXISTS channel_tags") &&
           db.exec("DROP TABLE IF EXISTS peers");
}
