#pragma once
#include "storage.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class CacheDbSyncInterface {
public:
    virtual ~CacheDbSyncInterface() = default;

    // cache 表（inline BLOB ≤1MB）
    virtual std::vector<uint8_t> get(const char* key,
                                     std::string* out_mime = nullptr) = 0;
    virtual bool exists(const char* key) = 0;
    virtual bool remove(const char* key) = 0;
    virtual bool clear_by_tag(int tag) = 0;
    virtual int64_t total_cache_size() = 0;

    // file_refs 表（>1MB 文件路径索引）
    virtual bool put_ref(const char* key, const char* file_path,
                         const char* mime, int tag, int64_t size) = 0;
    virtual std::string get_ref_path(const char* key) = 0;
    virtual bool remove_ref(const char* key) = 0;

    // 统一存取：≤1MB → cache 表内联 BLOB；>1MB → 文件系统 + file_refs
    virtual bool storeMedia(const char* key, const void* data, size_t size,
                            const char* mime, int tag) = 0;
    virtual std::vector<uint8_t> loadMedia(const char* key,
                                           std::string* out_mime = nullptr) = 0;

    // 维护
    virtual bool evict(int64_t target_size) = 0;
    virtual bool vacuum() = 0;

    virtual bool begin_write_transaction() = 0;
    virtual bool commit_transaction() = 0;

    virtual int64_t countCache() = 0;
    virtual int64_t countFileRefs() = 0;
};

class CacheDbSyncSafeInterface {
public:
    virtual ~CacheDbSyncSafeInterface() = default;
    virtual CacheDbSyncInterface& get() = 0;
};

class WriteQueue;

class CacheDbAsyncInterface {
public:
    virtual ~CacheDbAsyncInterface() = default;
    virtual void get(std::string key,
                     std::function<void(std::vector<uint8_t>, std::string)> done) = 0;
    virtual void exists(std::string key,
                        std::function<void(bool)> done) = 0;
    virtual void remove(std::string key,
                        std::function<void(bool)> done) = 0;
    virtual void clear_by_tag(int tag,
                              std::function<void(bool)> done) = 0;
    virtual void put_ref(std::string key, std::string file_path,
                         std::string mime, int tag, int64_t size,
                         std::function<void(bool)> done) = 0;
    virtual void get_ref_path(std::string key,
                              std::function<void(std::string)> done) = 0;
    virtual void storeMedia(std::string key, std::vector<uint8_t> data,
                            std::string mime, int tag,
                            std::function<void(bool)> done) = 0;
    virtual void loadMedia(std::string key,
                           std::function<void(std::vector<uint8_t>, std::string)> done) = 0;
    virtual void evict(int64_t target_size,
                       std::function<void(bool)> done) = 0;
    virtual void close(std::function<void()> done) = 0;
};

std::shared_ptr<CacheDbSyncSafeInterface> create_cache_db(
    std::shared_ptr<SqliteConnectionSafe> cache_conn,
    const char* baseDir);

std::shared_ptr<CacheDbAsyncInterface> create_cache_db_async(
    std::shared_ptr<CacheDbSyncSafeInterface> sync,
    std::shared_ptr<WriteQueue> queue);

bool init_cache_db(SqliteDb& db);
bool drop_cache_db(SqliteDb& db);
