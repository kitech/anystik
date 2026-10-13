#include "pending_db.h"

namespace {

class PendingDbSync final : public PendingDbSyncInterface {
    std::shared_ptr<SqliteConnectionSafe> m_conn;
public:
    explicit PendingDbSync(std::shared_ptr<SqliteConnectionSafe> conn)
        : m_conn(std::move(conn)) {}

    int64_t insert_pending(const PendingRow& row) override {
        auto _ = m_conn->get();
        auto stmt = _->prepare(
            "INSERT INTO pending_messages "
            "(chanid,peer_number,data,etype,message_text,"
            " media_url,file_name,file_size,retry_count,"
            " max_retries,last_error,status) "
            "VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12)");
        if (!stmt.isPrepared()) { return 0; }
        if (!stmt.bind(1, row.chanid.c_str())) { return 0; }
        if (!stmt.bind(2, row.peer_number)) { return 0; }
        if (!stmt.bind(3, row.data.c_str())) { return 0; }
        if (!stmt.bind(4, row.etype)) { return 0; }
        if (!stmt.bind(5, row.message_text.c_str())) { return 0; }
        if (!stmt.bind(6, row.media_url.c_str())) { return 0; }
        if (!stmt.bind(7, row.file_name.c_str())) { return 0; }
        if (!stmt.bind(8, row.file_size)) { return 0; }
        if (!stmt.bind(9, row.retry_count)) { return 0; }
        if (!stmt.bind(10, row.max_retries)) { return 0; }
        if (!stmt.bind(11, row.last_error.c_str())) { return 0; }
        if (!stmt.bind(12, row.status)) { return 0; }
        if (!stmt.step()) {
            qWarning("PendingDb::insert_pending failed");
            return 0;
        }
        return sqlite3_last_insert_rowid(_->raw());
    }

    bool update_pending(int64_t id, const PendingUpdate& upd) override {
        auto _ = m_conn->get();
        std::string sql = "UPDATE pending_messages SET ";
        int n = 0;
        auto addField = [&](const char* name) {
            if (n++ > 0) { sql += ","; }
            char idx[8]; snprintf(idx, sizeof(idx), "?%d", n + 1);
            sql += name; sql += "="; sql += idx;
        };
        if (upd.hasRetryCount) { addField("retry_count"); }
        if (upd.hasLastError)  { addField("last_error"); }
        if (upd.hasStatus)     { addField("status"); }
        if (n == 0) { return true; }
        sql += " WHERE id=?1";
        auto stmt = _->prepare(sql.c_str());
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, id)) { return false; }
        int idx = 2;
        if (upd.hasRetryCount) { stmt.bind(idx++, upd.retry_count); }
        if (upd.hasLastError)  { stmt.bind(idx++, upd.last_error.c_str()); }
        if (upd.hasStatus)     { stmt.bind(idx++, upd.status); }
        return stmt.step();
    }

    bool delete_pending(int64_t id) override {
        auto _ = m_conn->get();
        auto stmt = _->prepare("DELETE FROM pending_messages WHERE id=?1");
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, id)) { return false; }
        return stmt.step();
    }

    std::unique_ptr<PendingRow> get_pending(int64_t id) override {
        auto _ = m_conn->get();
        auto stmt = _->prepare(
            "SELECT id,chanid,peer_number,data,etype,message_text,"
            " media_url,file_name,file_size,retry_count,"
            " max_retries,last_error,status "
            "FROM pending_messages WHERE id=?1");
        if (!stmt.isPrepared()) { return nullptr; }
        if (!stmt.bind(1, id)) { return nullptr; }
        if (!stmt.stepRow()) { return nullptr; }
        auto row = std::unique_ptr<PendingRow>(new PendingRow());
        int i = 0;
        row->id           = stmt.columnInt64(i++);
        row->chanid       = stmt.columnText(i++);
        row->peer_number  = stmt.columnInt(i++);
        row->data         = stmt.columnText(i++);
        row->etype        = stmt.columnInt(i++);
        row->message_text = stmt.columnText(i++);
        row->media_url    = stmt.columnText(i++);
        row->file_name    = stmt.columnText(i++);
        row->file_size    = stmt.columnInt(i++);
        row->retry_count  = stmt.columnInt(i++);
        row->max_retries  = stmt.columnInt(i++);
        row->last_error   = stmt.columnText(i++);
        row->status       = stmt.columnInt(i++);
        return row;
    }

    std::vector<PendingRow> load_pending(int status) override {
        auto _ = m_conn->get();
        std::vector<PendingRow> rows;
        auto stmt = _->prepare(
            "SELECT id,chanid,peer_number,data,etype,message_text,"
            " media_url,file_name,file_size,retry_count,"
            " max_retries,last_error,status "
            "FROM pending_messages WHERE status=?1 "
            "ORDER BY id ASC");
        if (!stmt.isPrepared()) { return rows; }
        if (!stmt.bind(1, status)) { return rows; }
        while (stmt.stepRow()) {
            PendingRow row;
            int i = 0;
            row.id           = stmt.columnInt64(i++);
            row.chanid       = stmt.columnText(i++);
            row.peer_number  = stmt.columnInt(i++);
            row.data         = stmt.columnText(i++);
            row.etype        = stmt.columnInt(i++);
            row.message_text = stmt.columnText(i++);
            row.media_url    = stmt.columnText(i++);
            row.file_name    = stmt.columnText(i++);
            row.file_size    = stmt.columnInt(i++);
            row.retry_count  = stmt.columnInt(i++);
            row.max_retries  = stmt.columnInt(i++);
            row.last_error   = stmt.columnText(i++);
            row.status       = stmt.columnInt(i++);
            rows.push_back(std::move(row));
        }
        return rows;
    }

    bool begin_write_transaction() override { auto _ = m_conn->get(); return _->beginTransaction(); }
    bool commit_transaction() override { auto _ = m_conn->get(); return _->commitTransaction(); }

    int64_t countPending() override {
        SlowGuard _w("pending::count", 300);
        auto _ = m_conn->get();
        auto stmt = _->prepare("SELECT COUNT(*) FROM pending_messages");
        if (!stmt.isPrepared() || !stmt.stepRow()) { return -1; }
        return stmt.columnInt64(0);
    }
};

class PendingDbSyncSafe final : public PendingDbSyncSafeInterface {
    std::shared_ptr<SqliteConnectionSafe> m_conn;
    PendingDbSync m_impl;
public:
    explicit PendingDbSyncSafe(std::shared_ptr<SqliteConnectionSafe> conn)
        : m_conn(conn), m_impl(conn) {}
    PendingDbSyncInterface& get() override { return m_impl; }
};

class PendingDbAsync final : public PendingDbAsyncInterface {
    std::shared_ptr<PendingDbSyncSafeInterface> m_sync;
    std::shared_ptr<WriteQueue> m_queue;
public:
    PendingDbAsync(std::shared_ptr<PendingDbSyncSafeInterface> sync,
                   std::shared_ptr<WriteQueue> queue)
        : m_sync(std::move(sync)), m_queue(std::move(queue)) {}

    void post(std::function<void()> task) { m_queue->post(std::move(task)); }

    void insert_pending(PendingRow row, std::function<void(int64_t)> done) override {
        auto sync = m_sync;
        post([sync, row, done]() {
            int64_t id = sync->get().insert_pending(row);
            if (done) { done(id); }
        });
    }

    void update_pending(int64_t id, PendingUpdate upd,
                        std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, id, upd, done]() {
            bool ok = sync->get().update_pending(id, upd);
            if (done) { done(ok); }
        });
    }

    void delete_pending(int64_t id, std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, id, done]() {
            bool ok = sync->get().delete_pending(id);
            if (done) { done(ok); }
        });
    }

    void get_pending(int64_t id,
                     std::function<void(std::unique_ptr<PendingRow>)> done) override {
        auto sync = m_sync;
        post([sync, id, done]() {
            auto row = sync->get().get_pending(id);
            if (done) { done(std::move(row)); }
        });
    }

    void load_pending(int status,
                      std::function<void(std::vector<PendingRow>)> done) override {
        auto sync = m_sync;
        post([sync, status, done]() {
            auto rows = sync->get().load_pending(status);
            if (done) { done(std::move(rows)); }
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

std::shared_ptr<PendingDbSyncSafeInterface> create_pending_db(
    std::shared_ptr<SqliteConnectionSafe> conn) {
    return std::make_shared<PendingDbSyncSafe>(std::move(conn));
}

std::shared_ptr<PendingDbAsyncInterface> create_pending_db_async(
    std::shared_ptr<PendingDbSyncSafeInterface> sync,
    std::shared_ptr<WriteQueue> queue) {
    return std::make_shared<PendingDbAsync>(std::move(sync), std::move(queue));
}

bool init_pending_db(SqliteDb& db) {
    if (!db.exec(
        "CREATE TABLE IF NOT EXISTS pending_messages ("
        "  id             INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  chanid         TEXT NOT NULL,"
        "  peer_number    INTEGER DEFAULT -1,"
        "  data           TEXT NOT NULL,"
        "  etype          INTEGER DEFAULT 0,"
        "  message_text   TEXT DEFAULT '',"
        "  media_url      TEXT DEFAULT '',"
        "  file_name      TEXT DEFAULT '',"
        "  file_size      INTEGER DEFAULT 0,"
        "  retry_count    INTEGER DEFAULT 0,"
        "  max_retries    INTEGER DEFAULT 3,"
        "  last_error     TEXT DEFAULT '',"
        "  status         INTEGER DEFAULT 0,"
        "  created_at     TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ")")) { return false; }
    db.exec("CREATE INDEX IF NOT EXISTS idx_pending_status"
            "  ON pending_messages(status)");
    return true;
}

bool drop_pending_db(SqliteDb& db) {
    return db.exec("DROP TABLE IF EXISTS pending_messages");
}
