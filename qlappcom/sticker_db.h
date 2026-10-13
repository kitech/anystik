#pragma once
#include "storage.h"
#include <string>
#include <vector>
#include <memory>

struct StickerPackRow {
    std::string id;
    std::string title;
    std::string author;
    std::string cover_path;
    int installed = 1;
    int position = 0;
    int64_t created_at = 0;
};

struct StickerRow {
    std::string id;
    std::string pack_id;
    std::string file_path;
    std::string emoji;
    int width = 0;
    int height = 0;
    int size = 0;
    int64_t last_used = 0;
    int position = 0;
    std::string description;  // 图片描述（≤140 字，StickerStore::MaxDescriptionLength）
    int deleted = 0;
    int is_public = 0;       // 是否公开（默认不公开）
};

// list_packs 的 installed 取值：-1 = 全部（不按 installed 过滤）
constexpr int kPacksAll = -1;

class StickerDbSyncInterface {
public:
    virtual ~StickerDbSyncInterface() = default;

    // pack operations
    virtual bool add_pack(const StickerPackRow& pack) = 0;
    virtual bool delete_pack(const char* pack_id) = 0;
    virtual bool update_pack_position(const char* pack_id, int position) = 0;
    virtual bool update_pack_installed(const char* pack_id, int installed) = 0;
    virtual std::unique_ptr<StickerPackRow> get_pack(const char* pack_id) = 0;
    // orderby 白名单见 StickerDbSync 实现；orderby 未知时回落默认序。
    // limit=0 不限；offset 仅当 limit>0 时生效。
    virtual std::vector<StickerPackRow> list_packs(
        int installed = 1,
        const char* orderby = "created_at DESC",
        int limit = 0, int offset = 0) = 0;

    // sticker operations
    virtual bool add_sticker(const StickerRow& sticker) = 0;
    virtual bool delete_sticker(const char* sticker_id) = 0;
    virtual bool delete_stickers_by_pack(const char* pack_id) = 0;
    virtual bool touch_sticker(const char* sticker_id, int64_t now) = 0;
    // 更新贴纸描述（≤140 字；入库前调用方截断）
    virtual bool update_sticker_description(const char* sticker_id,
                                            const char* desc) = 0;
    virtual std::unique_ptr<StickerRow> get_sticker(const char* sticker_id) = 0;
    // 查询某贴纸行的软删态（id 即内容 sha1，全局主键）：
    //   -1=无该行   0=存活   1=软删（deleted=1）
    virtual int sticker_deleted_state(const char* sticker_id) = 0;
    // 复活被软删的贴纸：UPDATE stickers SET deleted=0（保留 emoji/描述/position/last_used）
    virtual bool restore_sticker(const char* sticker_id) = 0;
    // orderby 白名单见 StickerDbSync 实现；orderby 未知时回落"rowid DESC"。
    // limit=0 不限；offset 仅当 limit>0 时生效。
    // deleted: 0=仅存活(默认) 1=仅已删 -1=全部；emoji 非空时精确匹配。
    virtual std::vector<StickerRow> list_stickers(
        const char* pack_id,
        const char* orderby = "rowid DESC",
        int limit = 0, int offset = 0,
        int deleted = 0, const char* emoji = nullptr) = 0;
    virtual std::vector<StickerRow> list_recent_stickers(int limit = 30) = 0;
    virtual std::vector<StickerRow> search_stickers(const char* query) = 0;
    virtual int count_stickers(const char* pack_id = nullptr) = 0;
    virtual int countPacks() = 0;

    virtual bool begin_write_transaction() = 0;
    virtual bool commit_transaction() = 0;
};

class StickerDbSyncSafeInterface {
public:
    virtual ~StickerDbSyncSafeInterface() = default;
    virtual StickerDbSyncInterface& get() = 0;
};

std::shared_ptr<StickerDbSyncSafeInterface> create_sticker_db(
    std::shared_ptr<SqliteConnectionSafe> conn);

bool init_sticker_db(SqliteDb& db);
bool drop_sticker_db(SqliteDb& db);
