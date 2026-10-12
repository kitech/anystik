#include "cache_db.h"
#include "cache_fs.h"
#include <ctime>
#include <atomic>

namespace {

class CacheDbSync final : public CacheDbSyncInterface {
    std::shared_ptr<SqliteConnectionSafe> m_cacheConn;
    std::string m_baseDir;

public:
    CacheDbSync(std::shared_ptr<SqliteConnectionSafe> cacheConn,
                const char* baseDir)
        : m_cacheConn(std::move(cacheConn)), m_baseDir(baseDir) {}

private:
    bool putInternal(const char* key, const void* data, size_t size,
             const char* mime, int tag) {
        SlowGuard _w("cache::put", 60);
        auto _lock = m_cacheConn->get();
        auto stmt = _lock->prepare(
            "INSERT INTO cache "
            "(key,data,mime_type,tag,access_time,size) "
            "VALUES (?1,?2,?3,?4,?5,?6) "
            "ON CONFLICT(key) DO UPDATE SET access_time=excluded.access_time");
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, key)) { return false; }
        if (!stmt.bind(2, data, (int)size)) { return false; }
        if (!stmt.bind(3, mime)) { return false; }
        if (!stmt.bind(4, tag)) { return false; }
        if (!stmt.bind(5, (int64_t)std::time(nullptr))) { return false; }
        if (!stmt.bind(6, (int64_t)size)) { return false; }
        if (!stmt.step()) {
            qWarning("CacheDb::put failed for %s: %s",
                     key, sqliteError(*_lock));
            return false;
        }
        return true;
    }

public:
    std::vector<uint8_t> get(const char* key, std::string* out_mime) override {
        SlowGuard _w("cache::get", 30, key);
        auto _lock = m_cacheConn->get();
        TimedReadGuard _t(_lock->raw(), 50);
        std::vector<uint8_t> result;
        auto stmt = _lock->prepare(
            "SELECT data,mime_type FROM cache WHERE key=?1");
        if (!stmt.isPrepared()) { return result; }
        if (!stmt.bind(1, key)) { return result; }
        if (!stmt.stepRow()) {
            if (_t.timedOut()) { qWarning("cache::get timed out for '%s'", key); }
            return result;
        }

        int bytes = stmt.columnBytes(0);
        const void* blob = stmt.columnBlob(0);
        if (blob && bytes > 0) {
            const uint8_t* p = (const uint8_t*)blob;
            result.assign(p, p + bytes);
        }
        if (out_mime) { *out_mime = stmt.columnText(1); }

        auto upd = _lock->prepare(
            "UPDATE cache SET access_time=?1 WHERE key=?2");
        if (upd.isPrepared()) {
            upd.bind(1, (int64_t)std::time(nullptr));
            upd.bind(2, key);
            upd.step();
        }
        return result;
    }

    bool exists(const char* key) override {
        auto _lock = m_cacheConn->get();
        auto stmt = _lock->prepare("SELECT 1 FROM cache WHERE key=?1");
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, key)) { return false; }
        return stmt.stepRow();
    }

    bool remove(const char* key) override {
        auto _lock = m_cacheConn->get();
        auto stmt = _lock->prepare("DELETE FROM cache WHERE key=?1");
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, key)) { return false; }
        return stmt.step();
    }

    bool clear_by_tag(int tag) override {
        auto _lock = m_cacheConn->get();
        if (tag == 0) {
            return _lock->exec("DELETE FROM cache") &&
                   _lock->exec("DELETE FROM file_refs");
        }
        _lock->exec(std::string("DELETE FROM cache WHERE tag=" +
                      std::to_string(tag)).c_str());
        _lock->exec(std::string("DELETE FROM file_refs WHERE tag=" +
                      std::to_string(tag)).c_str());
        return true;
    }

    int64_t total_cache_size() override {
        auto _lock = m_cacheConn->get();
        auto stmt = _lock->prepare("SELECT COALESCE(SUM(size),0) FROM cache");
        if (!stmt.isPrepared()) { return 0; }
        if (!stmt.stepRow()) { return 0; }
        int64_t s1 = stmt.columnInt64(0);
        auto stmt2 = _lock->prepare("SELECT COALESCE(SUM(size),0) FROM file_refs");
        if (!stmt2.isPrepared()) { return s1; }
        if (!stmt2.stepRow()) { return s1; }
        return s1 + stmt2.columnInt64(0);
    }

    bool put_ref(const char* key, const char* file_path,
                 const char* mime, int tag, int64_t size) override {
        SlowGuard _w("cache::put_ref", 60);
        auto _lock = m_cacheConn->get();
        auto stmt = _lock->prepare(
            "INSERT INTO file_refs "
            "(key,file_path,mime_type,tag,access_time,size) "
            "VALUES (?1,?2,?3,?4,?5,?6) "
            "ON CONFLICT(key) DO UPDATE SET access_time=excluded.access_time");
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, key)) { return false; }
        if (!stmt.bind(2, file_path)) { return false; }
        if (!stmt.bind(3, mime)) { return false; }
        if (!stmt.bind(4, tag)) { return false; }
        if (!stmt.bind(5, (int64_t)std::time(nullptr))) { return false; }
        if (!stmt.bind(6, size)) { return false; }
        return stmt.step();
    }

    std::string get_ref_path(const char* key) override {
        SlowGuard _w("cache::get_ref", 30, key);
        auto _lock = m_cacheConn->get();
        TimedReadGuard _t(_lock->raw(), 50);
        auto stmt = _lock->prepare(
            "SELECT file_path FROM file_refs WHERE key=?1");
        if (!stmt.isPrepared()) { return {}; }
        if (!stmt.bind(1, key)) { return {}; }
        if (!stmt.stepRow()) {
            if (_t.timedOut()) { qWarning("cache::get_ref timed out for '%s'", key); }
            return {};
        }
        auto upd = _lock->prepare(
            "UPDATE file_refs SET access_time=?1 WHERE key=?2");
        if (upd.isPrepared()) {
            upd.bind(1, (int64_t)std::time(nullptr));
            upd.bind(2, key);
            upd.step();
        }
        return stmt.columnText(0);
    }

    bool remove_ref(const char* key) override {
        auto _lock = m_cacheConn->get();
        auto stmt = _lock->prepare("DELETE FROM file_refs WHERE key=?1");
        if (!stmt.isPrepared()) { return false; }
        if (!stmt.bind(1, key)) { return false; }
        return stmt.step();
    }

    bool storeMedia(const char* key, const void* data, size_t size,
                    const char* mime, int tag) override {
        const int64_t kMaxInline = 1 * 1024 * 1024;
        if (size <= (size_t)kMaxInline) {
            return putInternal(key, data, size, mime, tag);
        }
        std::string relPath = makeCacheFsPath(key);
        std::string fullPath = m_baseDir + "/" + relPath;
        if (!writeCacheFile(fullPath, data, size)) { return false; }
        return put_ref(key, relPath.c_str(), mime, tag, (int64_t)size);
    }

    std::vector<uint8_t> loadMedia(const char* key, std::string* out_mime) override {
        auto _lock = m_cacheConn->tryRead();
        static std::atomic<int> s_total{0};
        static std::atomic<int> s_skip{0};
        s_total++;
        if (!_lock) {
            int n = ++s_skip;
            int t = s_total.load();
            if (n <= 10 || n % 500 == 0) {
                qWarning("loadMedia: tryRead blocked %d/%d (%.1f%%)",
                         n, t, (double)n / t * 100.0);
            }
            return {};
        }

        auto stmt = _lock->prepare(
            "SELECT file_path,mime_type FROM file_refs WHERE key=?1");
        if (stmt.isPrepared() && stmt.bind(1, key) && stmt.stepRow()) {
            std::string relPath = stmt.columnText(0);
            if (out_mime) { *out_mime = stmt.columnText(1); }
            std::string fullPath = m_baseDir + "/" + relPath;
            auto data = readCacheFile(fullPath);
            if (!data.empty()) {
                auto upd = _lock->prepare(
                    "UPDATE file_refs SET access_time=?1 WHERE key=?2");
                if (upd.isPrepared()) {
                    upd.bind(1, (int64_t)std::time(nullptr));
                    upd.bind(2, key);
                    upd.step();
                }
                return data;
            }
        }

        SlowGuard _w("cache::get", 30, key);
        TimedReadGuard _t(_lock->raw(), 50);
        std::vector<uint8_t> result;
        auto stmt2 = _lock->prepare(
            "SELECT data,mime_type FROM cache WHERE key=?1");
        if (!stmt2.isPrepared()) { return result; }
        if (!stmt2.bind(1, key)) { return result; }
        if (!stmt2.stepRow()) {
            if (_t.timedOut()) { qWarning("cache::get timed out for '%s'", key); }
            return result;
        }

        int bytes = stmt2.columnBytes(0);
        const void* blob = stmt2.columnBlob(0);
        if (blob && bytes > 0) {
            const uint8_t* p = (const uint8_t*)blob;
            result.assign(p, p + bytes);
        }
        if (out_mime) { *out_mime = stmt2.columnText(1); }

        auto upd2 = _lock->prepare(
            "UPDATE cache SET access_time=?1 WHERE key=?2");
        if (upd2.isPrepared()) {
            upd2.bind(1, (int64_t)std::time(nullptr));
            upd2.bind(2, key);
            upd2.step();
        }
        return result;
    }

    bool evict(int64_t target_size) override {
        SlowGuard _w("cache::evict", 500);
        auto _lock = m_cacheConn->get();
        int64_t now = (int64_t)std::time(nullptr);
        int64_t total = total_cache_size();

        if (total > target_size) {
            std::string sql =
                "DELETE FROM cache WHERE access_time < " +
                std::to_string(now - 30 * 86400) + " AND size > 2097152";
            _lock->exec(sql.c_str());
            auto stmt = _lock->prepare(
                "DELETE FROM cache WHERE rowid IN ("
                "  SELECT rowid FROM cache"
                "  ORDER BY access_time ASC LIMIT 100)");
            if (stmt.isPrepared()) { stmt.step(); }

            auto refStmt = _lock->prepare(
                "SELECT key,file_path FROM file_refs"
                "  WHERE access_time < ?1"
                "  ORDER BY access_time ASC LIMIT 100");
            if (refStmt.isPrepared()) {
                refStmt.bind(1, now - 7 * 86400);
                while (refStmt.stepRow()) {
                    const char* relPath = refStmt.columnText(1);
                    if (relPath && *relPath) {
                        removeCacheFile(m_baseDir + "/" + relPath);
                    }
                    remove_ref(refStmt.columnText(0));
                }
            }
        }
        return true;
    }

    bool vacuum() override {
        auto _lock = m_cacheConn->get();
        return _lock->exec("VACUUM");
    }

    bool begin_write_transaction() override {
        auto _lock = m_cacheConn->get();
        return _lock->beginTransaction();
    }
    bool commit_transaction() override {
        auto _lock = m_cacheConn->get();
        return _lock->commitTransaction();
    }

    int64_t countCache() override {
        SlowGuard _w("cache::count_cache", 300);
        auto _lock = m_cacheConn->get();
        auto stmt = _lock->prepare("SELECT COUNT(*) FROM cache");
        if (!stmt.isPrepared() || !stmt.stepRow()) { return -1; }
        return stmt.columnInt64(0);
    }

    int64_t countFileRefs() override {
        SlowGuard _w("cache::count_filerefs", 300);
        auto _lock = m_cacheConn->get();
        auto stmt = _lock->prepare("SELECT COUNT(*) FROM file_refs");
        if (!stmt.isPrepared() || !stmt.stepRow()) { return -1; }
        return stmt.columnInt64(0);
    }
};

class CacheDbSyncSafe final : public CacheDbSyncSafeInterface {
    std::shared_ptr<SqliteConnectionSafe> m_cacheConn;
    CacheDbSync m_impl;
public:
    CacheDbSyncSafe(std::shared_ptr<SqliteConnectionSafe> cacheConn,
                    const char* baseDir)
        : m_cacheConn(cacheConn), m_impl(cacheConn, baseDir) {}
    CacheDbSyncInterface& get() override { return m_impl; }
};

class CacheDbAsync final : public CacheDbAsyncInterface {
    std::shared_ptr<CacheDbSyncSafeInterface> m_sync;
    std::shared_ptr<WriteQueue> m_queue;
public:
    CacheDbAsync(std::shared_ptr<CacheDbSyncSafeInterface> sync,
                 std::shared_ptr<WriteQueue> queue)
        : m_sync(std::move(sync)), m_queue(std::move(queue)) {}

    void post(std::function<void()> task) { m_queue->post(std::move(task)); }

    void get(std::string key,
             std::function<void(std::vector<uint8_t>, std::string)> done) override {
        auto sync = m_sync;
        post([sync, key, done]() {
            std::string mime;
            auto data = sync->get().get(key.c_str(), &mime);
            if (done) { done(std::move(data), std::move(mime)); }
        });
    }

    void exists(std::string key, std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, key, done]() {
            if (done) { done(sync->get().exists(key.c_str())); }
        });
    }

    void remove(std::string key, std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, key, done]() {
            if (done) { done(sync->get().remove(key.c_str())); }
        });
    }

    void clear_by_tag(int tag, std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, tag, done]() {
            if (done) { done(sync->get().clear_by_tag(tag)); }
        });
    }

    void put_ref(std::string key, std::string file_path,
                 std::string mime, int tag, int64_t size,
                 std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, key, file_path, mime, tag, size, done]() {
            bool ok = sync->get().put_ref(key.c_str(), file_path.c_str(),
                                          mime.c_str(), tag, size);
            if (done) { done(ok); }
        });
    }

    void get_ref_path(std::string key,
                      std::function<void(std::string)> done) override {
        auto sync = m_sync;
        post([sync, key, done]() {
            auto path = sync->get().get_ref_path(key.c_str());
            if (done) { done(std::move(path)); }
        });
    }

    void storeMedia(std::string key, std::vector<uint8_t> data,
                    std::string mime, int tag,
                    std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, key, data, mime, tag, done]() {
            bool ok = sync->get().storeMedia(key.c_str(), data.data(),
                                             data.size(), mime.c_str(), tag);
            if (done) { done(ok); }
        });
    }

    void loadMedia(std::string key,
                   std::function<void(std::vector<uint8_t>, std::string)> done) override {
        auto sync = m_sync;
        post([sync, key, done]() {
            std::string mime;
            auto data = sync->get().loadMedia(key.c_str(), &mime);
            if (done) { done(std::move(data), std::move(mime)); }
        });
    }

    void evict(int64_t target_size, std::function<void(bool)> done) override {
        auto sync = m_sync;
        post([sync, target_size, done]() {
            if (done) { done(sync->get().evict(target_size)); }
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

std::shared_ptr<CacheDbSyncSafeInterface> create_cache_db(
    std::shared_ptr<SqliteConnectionSafe> cache_conn,
    const char* baseDir) {
    return std::make_shared<CacheDbSyncSafe>(std::move(cache_conn), baseDir);
}

std::shared_ptr<CacheDbAsyncInterface> create_cache_db_async(
    std::shared_ptr<CacheDbSyncSafeInterface> sync,
    std::shared_ptr<WriteQueue> queue) {
    return std::make_shared<CacheDbAsync>(std::move(sync), std::move(queue));
}

bool init_cache_db(SqliteDb& db) {
    if (!db.exec(
        "CREATE TABLE IF NOT EXISTS cache ("
        "  key         TEXT PRIMARY KEY,"
        "  data        BLOB NOT NULL,"
        "  mime_type   TEXT DEFAULT '',"
        "  tag         INTEGER DEFAULT 0,"
        "  access_time INTEGER NOT NULL,"
        "  size        INTEGER NOT NULL,"
        "  created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ")")) { return false; }

    if (!db.exec(
        "CREATE TABLE IF NOT EXISTS file_refs ("
        "  key         TEXT PRIMARY KEY,"
        "  file_path   TEXT NOT NULL,"
        "  mime_type   TEXT DEFAULT '',"
        "  tag         INTEGER DEFAULT 0,"
        "  access_time INTEGER NOT NULL,"
        "  size        INTEGER NOT NULL,"
        "  created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ")")) { return false; }

    db.exec("CREATE INDEX IF NOT EXISTS idx_cache_tag ON cache(tag)");
    db.exec("CREATE INDEX IF NOT EXISTS idx_cache_access ON cache(access_time)");
    db.exec("CREATE INDEX IF NOT EXISTS idx_filerefs_tag ON file_refs(tag)");
    db.exec("CREATE INDEX IF NOT EXISTS idx_filerefs_access ON file_refs(access_time)");
    return true;
}

bool drop_cache_db(SqliteDb& db) {
    return db.exec("DROP TABLE IF EXISTS cache") &&
           db.exec("DROP TABLE IF EXISTS file_refs");
}
