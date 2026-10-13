#include "storage.h"
#include "channel_db.h"
#include "message_db.h"
#include "pending_db.h"
#include "cache_db.h"
#include "cache_fs.h"
#include "sticker_db.h"
#include <qstring.h>

std::string mediaCacheKey(const char* prefix, const QString& mxcUrl) {
#ifdef QT3_BUILD
    int slash = mxcUrl.findRev('/');
    if (slash < 0) return {};
    QString id = mxcUrl.mid(slash + 1);
    int q = id.find('?');
    if (q >= 0) id = id.left(q);
    return std::string(prefix) + "_" + std::string(id.utf8());
#else
    int slash = mxcUrl.lastIndexOf('/');
    if (slash < 0) return {};
    QString id = mxcUrl.mid(slash + 1);
    int q = id.indexOf('?');
    if (q >= 0) id = id.left(q);
    return std::string(prefix) + "_" + std::string(id.toUtf8().data());
#endif
}

// ── SqliteStatement ──

SqliteStatement::SqliteStatement(sqlite3* db, const char* sql) {
    if (sqlite3_prepare_v2(db, sql, -1, &m_stmt, nullptr) != SQLITE_OK) {
        qWarning("SqliteStatement prepare failed: %s\nSQL: %s",
                 sqlite3_errmsg(db), sql);
        m_stmt = nullptr;
    }
}

SqliteStatement::~SqliteStatement() { finalize(); }

SqliteStatement::SqliteStatement(SqliteStatement&& other) noexcept
    : m_stmt(other.m_stmt) {
    other.m_stmt = nullptr;
}

SqliteStatement& SqliteStatement::operator=(SqliteStatement&& other) noexcept {
    if (this != &other) {
        finalize();
        m_stmt = other.m_stmt;
        other.m_stmt = nullptr;
    }
    return *this;
}

bool SqliteStatement::bind(int idx, int val) {
    if (!m_stmt) { return false; }
    if (sqlite3_bind_int(m_stmt, idx, val) != SQLITE_OK) {
        qWarning("SqliteStatement::bind(%d) failed: %s", idx, sqliteError(m_stmt));
        return false;
    }
    return true;
}

bool SqliteStatement::bind(int idx, int64_t val) {
    if (!m_stmt) { return false; }
    if (sqlite3_bind_int64(m_stmt, idx, val) != SQLITE_OK) {
        qWarning("SqliteStatement::bind(%d) failed: %s", idx, sqliteError(m_stmt));
        return false;
    }
    return true;
}

bool SqliteStatement::bind(int idx, const char* val) {
    if (!m_stmt) { return false; }
    int rc = sqlite3_bind_text(m_stmt, idx, val, -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        qWarning("SqliteStatement::bind(%d) failed: %s", idx, sqliteError(m_stmt));
        return false;
    }
    return true;
}

bool SqliteStatement::bind(int idx, const void* data, int len) {
    if (!m_stmt) { return false; }
    int rc = sqlite3_bind_blob(m_stmt, idx, data, len, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        qWarning("SqliteStatement::bind(%d) failed: %s", idx, sqliteError(m_stmt));
        return false;
    }
    return true;
}

bool SqliteStatement::bindNull(int idx) {
    if (!m_stmt) { return false; }
    if (sqlite3_bind_null(m_stmt, idx) != SQLITE_OK) {
        qWarning("SqliteStatement::bindNull(%d) failed: %s", idx, sqliteError(m_stmt));
        return false;
    }
    return true;
}

bool SqliteStatement::step() {
    if (!m_stmt) { return false; }
    int rc = sqlite3_step(m_stmt);
    if (rc == SQLITE_ROW) { return true; }
    if (rc == SQLITE_DONE) { return true; }
    qWarning("SqliteStatement step error: %s",
             sqlite3_errmsg(sqlite3_db_handle(m_stmt)));
    return false;
}

bool SqliteStatement::stepRow() {
    if (!m_stmt) { return false; }
    int rc = sqlite3_step(m_stmt);
    if (rc == SQLITE_ROW) { return true; }
    if (rc == SQLITE_DONE) { return false; }
    qWarning("SqliteStatement stepRow error: %s",
             sqlite3_errmsg(sqlite3_db_handle(m_stmt)));
    return false;
}

int SqliteStatement::changes() {
    return m_stmt ? sqlite3_changes(sqlite3_db_handle(m_stmt)) : 0;
}

int SqliteStatement::columnInt(int col) {
    return m_stmt ? sqlite3_column_int(m_stmt, col) : 0;
}

int64_t SqliteStatement::columnInt64(int col) {
    return m_stmt ? sqlite3_column_int64(m_stmt, col) : 0;
}

const char* SqliteStatement::columnText(int col) {
    if (!m_stmt) { return ""; }
    const char* s = (const char*)sqlite3_column_text(m_stmt, col);
    return s ? s : "";
}

int SqliteStatement::columnBytes(int col) {
    return m_stmt ? sqlite3_column_bytes(m_stmt, col) : 0;
}

const void* SqliteStatement::columnBlob(int col) {
    return m_stmt ? sqlite3_column_blob(m_stmt, col) : nullptr;
}

void SqliteStatement::reset() {
    if (m_stmt) { sqlite3_reset(m_stmt); }
}

void SqliteStatement::finalize() {
    if (m_stmt) {
        sqlite3_finalize(m_stmt);
        m_stmt = nullptr;
    }
}

// ── SqliteDb ──

bool SqliteDb::exec(const char* sql) {
    if (!m_db) { return false; }
    char* err = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        qWarning("SqliteDb exec failed: %s\nSQL: %s", err ? err : "unknown", sql);
        sqlite3_free(err);
        return false;
    }
    return true;
}

bool SqliteDb::tryExec(const char* sql) {
    if (!m_db) { return false; }
    char* err = nullptr;
    int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        qWarning("SqliteDb tryExec failed: %s\nSQL: %s", err ? err : "unknown", sql);
        sqlite3_free(err);
        return false;
    }
    return true;
}

SqliteStatement SqliteDb::prepare(const char* sql) {
    return SqliteStatement(m_db, sql);
}

bool SqliteDb::beginTransaction() {
    return exec("BEGIN IMMEDIATE");
}

bool SqliteDb::commitTransaction() {
    return exec("COMMIT");
}

bool SqliteDb::rollbackTransaction() {
    return exec("ROLLBACK");
}


#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>

typedef std::chrono::steady_clock::time_point TimePoint;

static long long elapsedMs(TimePoint start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
}

static void mkdirRecursive(const char *path) {
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (tmp[len - 1] == '/') { tmp[len - 1] = 0; }
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static const char *basenamePtr(const char *path) {
    const char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

Storage &Storage::instance() {
    static Storage s;
    return s;
}

Storage::Storage() = default;

Storage::~Storage() { close(); }

static std::string pathFor(const char *dir, const char *name) {
    std::string s(dir);
    s += '/';
    s += name;
    return s;
}

static bool gPageCacheConfigured = false;

bool Storage::init(const char *dataDir) {
    auto totalStart = std::chrono::steady_clock::now();

    if (!gPageCacheConfigured) {
        int hdrSize;
        sqlite3_config(SQLITE_CONFIG_PCACHE_HDRSZ, &hdrSize);
        sqlite3_config(SQLITE_CONFIG_SMALL_MALLOC, 1);
        gPageCacheConfigured = true;
    }

    m_dataDir = dataDir;
    mkdirRecursive(dataDir);
    initCacheFsDirs(dataDir);

    std::string msgPath = pathFor(dataDir, "message.db");
    std::string cachePath = pathFor(dataDir, "cache.db");
    if (!openDb(msgPath.c_str()))      { return false; }
    if (!openDb(cachePath.c_str()))    { return false; }

    m_msgConn = std::make_shared<SqliteConnectionSafe>(m_msgDb.raw());
    m_cacheConn = std::make_shared<SqliteConnectionSafe>(m_cacheDb.raw());

    if (!initSyncDomains()) { return false; }

    m_queue = std::make_shared<WriteQueue>();

    if (!initAsyncDomains()) { return false; }

    checkFeatures();

    // SQLite 堆软限额 — 超额时自动 release_memory
    sqlite3_soft_heap_limit64(16LL * 1024 * 1024);

    qDebug("Storage init complete in %lldms", elapsedMs(totalStart));
    return true;
}

void Storage::close() {
    if (m_queue) { m_queue->stop(); m_queue.reset(); }

    m_channelDbAsync.reset();
    m_messageDbAsync.reset();
    m_pendingDbAsync.reset();
    m_cacheDbAsync.reset();
    m_channelDb.reset();
    m_messageDb.reset();
    m_pendingDb.reset();
    m_cacheDbObj.reset();

    m_msgConn.reset();
    m_cacheConn.reset();

    if (m_msgDb.raw())     { sqlite3_close(m_msgDb.raw()); }
    if (m_cacheDb.raw())   { sqlite3_close(m_cacheDb.raw()); }
    m_msgDb = SqliteDb();
    m_cacheDb = SqliteDb();
}

bool Storage::openDb(const char *path) {
    auto start = std::chrono::steady_clock::now();

    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(path, &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        qWarning("DB open failed [%s]: %s", path, sqlite3_errmsg(db));
        if (db) { sqlite3_close(db); }
        return false;
    }

    // 检测是哪个库，赋值到对应的 SqliteDb
    const char* name = basenamePtr(path);
    SqliteDb* target = nullptr;
    if (strcmp(name, "message.db") == 0)         { target = &m_msgDb; }
    else if (strcmp(name, "cache.db") == 0)      { target = &m_cacheDb; }

    if (target) {
        *target = SqliteDb(db);
    }

    // pragma
    auto execPragma = [&](const char* sql) {
        char* err = nullptr;
        if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
            qFatal("Pragma failed: %s\nSQL: %s", err ? err : "unknown", sql);
        }
    };
    execPragma("PRAGMA journal_mode=WAL");
    execPragma("PRAGMA synchronous=NORMAL");
    execPragma("PRAGMA page_size=4096");
    execPragma("PRAGMA cache_size=-500");
    execPragma("PRAGMA temp_store=MEMORY");
    execPragma("PRAGMA mmap_size=0");
    execPragma("PRAGMA wal_autocheckpoint=200");
    execPragma("PRAGMA journal_size_limit=4194304"); // default 4194304*4
    execPragma("PRAGMA busy_timeout=5000");
    execPragma("PRAGMA foreign_keys=ON");

    qDebug("DB opened [%s] in %lldms", name, elapsedMs(start));
    return true;
}

bool Storage::initSyncDomains() {
    auto start = std::chrono::steady_clock::now();

    if (!init_channel_db(m_msgDb))   { return false; }
    if (!init_message_db(m_msgDb))   { return false; }
    if (!init_pending_db(m_msgDb))   { return false; }
    if (!init_sticker_db(m_msgDb))   { return false; }
    if (!init_cache_db(m_cacheDb))   { return false; }

    // schema_version
    m_msgDb.exec("CREATE TABLE IF NOT EXISTS schema_version ("
                 "  version     INTEGER PRIMARY KEY,"
                 "  applied_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
                 ")");
    m_msgDb.exec("INSERT OR IGNORE INTO schema_version(version) VALUES(4)");

    // 创建域实例
    m_channelDb = create_channel_db(m_msgConn);
    m_messageDb = create_message_db(m_msgConn);
    m_pendingDb = create_pending_db(m_msgConn);
    m_stickerDb = create_sticker_db(m_msgConn);
    m_cacheDbObj = create_cache_db(m_cacheConn, m_dataDir.c_str());

    qDebug("Domain layer init complete in %lldms (5 domains, 12 tables)",
           elapsedMs(start));
    return true;
}

bool Storage::initAsyncDomains() {
    m_channelDbAsync = create_channel_db_async(m_channelDb, m_queue);
    m_messageDbAsync = create_message_db_async(m_messageDb, m_queue);
    m_pendingDbAsync = create_pending_db_async(m_pendingDb, m_queue);
    m_cacheDbAsync   = create_cache_db_async(m_cacheDbObj, m_queue);
    return true;
}

void Storage::checkFeatures() {
    m_sqliteVersion = sqlite3_libversion();
    const int vnum  = sqlite3_libversion_number();
    const int vMaj  = vnum / 1000000;
    const int vMin  = (vnum / 1000) % 1000;
    const int vPat  = vnum % 1000;
    const bool trigramCapable = (vMaj > 3 || (vMaj == 3 && vMin >= 34));
    qDebug("SQLite version: %s%s", m_sqliteVersion.c_str(),
           trigramCapable ? "" : " (trigram 需要 SQLite >= 3.34.0)");

    m_hasFts5 = m_msgDb.tryExec(
        "CREATE VIRTUAL TABLE IF NOT EXISTS _t_fts5 USING fts5(c)");
    m_msgDb.exec("DROP TABLE IF EXISTS _t_fts5");
    qDebug("FTS5: %s", m_hasFts5 ? "OK" : "NOT AVAILABLE");

    m_hasTrigram = m_msgDb.tryExec(
        "CREATE VIRTUAL TABLE IF NOT EXISTS _t_tri USING fts5(c, tokenize='trigram')");
    m_msgDb.exec("DROP TABLE IF EXISTS _t_tri");
    if (m_hasTrigram) {
        qDebug("Trigram: OK (CJK 子串搜索启用)");
    } else {
        qDebug("Trigram: NOT AVAILABLE (需 SQLite >= 3.34.0, 当前 %d.%d.%d; CJK 搜索降级为 unicode61)",
               vMaj, vMin, vPat);
    }
}

// ── 域类 accessors ──

ChannelDbSyncInterface* Storage::channelDb() {
    return m_channelDb ? &m_channelDb->get() : nullptr;
}

MessageDbSyncInterface* Storage::messageDb() {
    return m_messageDb ? &m_messageDb->get() : nullptr;
}

PendingDbSyncInterface* Storage::pendingDb() {
    return m_pendingDb ? &m_pendingDb->get() : nullptr;
}

CacheDbSyncInterface* Storage::cacheDb() {
    return m_cacheDbObj ? &m_cacheDbObj->get() : nullptr;
}

StickerDbSyncInterface* Storage::stickerDb() {
    return m_stickerDb ? &m_stickerDb->get() : nullptr;
}

ChannelDbAsyncInterface* Storage::channelDbAsync() {
    return m_channelDbAsync.get();
}

MessageDbAsyncInterface* Storage::messageDbAsync() {
    return m_messageDbAsync.get();
}

PendingDbAsyncInterface* Storage::pendingDbAsync() {
    return m_pendingDbAsync.get();
}

CacheDbAsyncInterface* Storage::cacheDbAsync() {
    return m_cacheDbAsync.get();
}

// ── WriteQueue ──

WriteQueue::WriteQueue() {
    start();
}

WriteQueue::~WriteQueue() {
    stop();
    wait();
}

void WriteQueue::post(std::function<void()> task) {
    QMutexLocker lock(&m_mutex);
    m_queue.push(std::move(task));
    m_cond.wakeOne();
}

void WriteQueue::flush() {
    std::queue<std::function<void()>> batch;
    {
        QMutexLocker lock(&m_mutex);
        batch.swap(m_queue);
    }
    while (!batch.empty()) {
        batch.front()();
        batch.pop();
    }
}

void WriteQueue::stop() {
    QMutexLocker lock(&m_mutex);
    m_stopped = true;
    m_cond.wakeOne();
}

void WriteQueue::run() {
    while (true) {
        std::function<void()> task;
        {
            QMutexLocker lock(&m_mutex);
            while (m_queue.empty() && !m_stopped) {
                m_cond.wait(&m_mutex);
            }
            if (m_stopped && m_queue.empty()) {
                qDebug("WriteQueue thread exited");
                return;
            }
            task = std::move(m_queue.front());
            m_queue.pop();
        }
        task();
        sqlite3_release_memory(-1);
    }
}

// ── TimedReadGuard ──

int TimedReadGuard::onProgress(void* ptr) {
    auto* self = static_cast<TimedReadGuard*>(ptr);
    auto elapsed = std::chrono::steady_clock::now() - self->m_start;
    if (elapsed > std::chrono::milliseconds(self->m_timeoutMs)) {
        self->m_timedOut = true;
        return 1;
    }
    return 0;
}

TimedReadGuard::TimedReadGuard(sqlite3* db, int timeoutMs)
    : m_db(db), m_start(std::chrono::steady_clock::now())
    , m_timeoutMs(timeoutMs)
{
	// TODO 版本相关
    // sqlite3_progress_handler(m_db, 10, onProgress, this);
}

TimedReadGuard::~TimedReadGuard() {
	// TODO 版本相关
    // sqlite3_progress_handler(m_db, 0, nullptr, nullptr);
}
