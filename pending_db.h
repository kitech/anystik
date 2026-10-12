#pragma once
#include "storage.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct PendingRow {
    int64_t id = 0;
    std::string chanid;
    int peer_number = -1;
    std::string data;
    int etype = 0;
    std::string message_text;
    std::string media_url;
    std::string file_name;
    int file_size = 0;
    int retry_count = 0;
    int max_retries = 3;
    std::string last_error;
    int status = 0;  // 0=pending, 1=in_flight, 2=failed
};

struct PendingUpdate {
    bool hasRetryCount = false;  int retry_count;
    bool hasLastError = false;   std::string last_error;
    bool hasStatus = false;      int status;
};

class PendingDbSyncInterface {
public:
    virtual ~PendingDbSyncInterface() = default;

    virtual int64_t insert_pending(const PendingRow& row) = 0;
    virtual bool update_pending(int64_t id, const PendingUpdate& upd) = 0;
    virtual bool delete_pending(int64_t id) = 0;
    virtual std::unique_ptr<PendingRow> get_pending(int64_t id) = 0;
    virtual std::vector<PendingRow> load_pending(int status = 0) = 0;

    virtual bool begin_write_transaction() = 0;
    virtual bool commit_transaction() = 0;

    virtual int64_t countPending() = 0;
};

class PendingDbSyncSafeInterface {
public:
    virtual ~PendingDbSyncSafeInterface() = default;
    virtual PendingDbSyncInterface& get() = 0;
};

class WriteQueue;

class PendingDbAsyncInterface {
public:
    virtual ~PendingDbAsyncInterface() = default;
    virtual void insert_pending(PendingRow row,
                                std::function<void(int64_t)> done) = 0;
    virtual void update_pending(int64_t id, PendingUpdate upd,
                                std::function<void(bool)> done) = 0;
    virtual void delete_pending(int64_t id,
                                std::function<void(bool)> done) = 0;
    virtual void get_pending(int64_t id,
                             std::function<void(std::unique_ptr<PendingRow>)> done) = 0;
    virtual void load_pending(int status,
                              std::function<void(std::vector<PendingRow>)> done) = 0;
    virtual void close(std::function<void()> done) = 0;
};

std::shared_ptr<PendingDbSyncSafeInterface> create_pending_db(
    std::shared_ptr<SqliteConnectionSafe> conn);

std::shared_ptr<PendingDbAsyncInterface> create_pending_db_async(
    std::shared_ptr<PendingDbSyncSafeInterface> sync,
    std::shared_ptr<WriteQueue> queue);

bool init_pending_db(SqliteDb& db);
bool drop_pending_db(SqliteDb& db);
