#ifndef STIKCOMMON_PACK_META_STORE_H
#define STIKCOMMON_PACK_META_STORE_H

// 包级「贴纸描述」元数据：以 _stikmeta.svg（内容为 JSONL 文本）为载体，
// 随普通 sticker 行走现有 WebDAV bisync（引擎两侧零改动）。宿主 DB 经
// StickerDbSyncInterface 窄接口访问，文件系统用 StickerStore 的 baseDir。
//
// 约定：
//   * 载体相对路径 = 该包云目录 + "/_stikmeta.svg"；普通包 = "packs/<T>"，
//     粘贴板包 = "pastes"（由包内任一 sticker 的 file_path 目录推导）。
//   * 记录 = {"rel":"<basename>","desc":"<≤140>","mtime":<epoch-ms>}。
//   * 冲突合并：*.conflict<digits> 载体内容并入主载体，**保留冲突文件本身**。
#include "sticker_db.h"           // StickerDbSyncInterface / StickerRow / kPacksAll

#include "qglobaltype_shim.h"     // qint64 / Qt 版本宏

#ifdef QT3_BUILD
#include <qstring.h>
#include <qmap.h>
#else
#include <QString>
#include <QMap>
#endif

#if QT_VERSION < 0x050000
#include "qjson_shim.h"
#else
#include <QJsonObject>
#endif

#include <string>

namespace packmeta {

extern const char kCarrierName[];         // "_stikmeta.svg"
extern const char kConflictMarker[];      // ".conflict"

// 载体所在目录（相对存储根）；无已入库 sticker 时回落 "packs/<title>"。
std::string carrierDir(StickerDbSyncInterface& db, const std::string& packId);
// 载体相对路径（相对存储根）：carrierDir + "/" + kCarrierName。
std::string carrierRel(StickerDbSyncInterface& db, const std::string& packId);

std::string basenameOf(const std::string& rel);
// base 是否载体本身或 *.conflict<digits>（应从 UI 隐藏）。
bool isCarrierBase(const std::string& base);
bool isInternalRel(const std::string& rel);

// ── 可合并边车文件注册表 ──
// 引擎同步、下载折叠、UI 隐藏三处均按此表分派；新增可自动合并的文件类型，
// 只需在 pack_meta_store.cpp 的 kMergeables[] 加一行。
//
// 合并策略：本地原始字节 + 云端原始字节 → 合并后字节（与文件格式解耦）。
typedef std::string (*MergeFn)(const std::string& local,
                               const std::string& cloud);

struct MergeableEntry {
    const char* basename;     // 规范基名（不含 .conflict<digits>）
    MergeFn     merge;        // 合并算法
    bool        hiddenFromUi; // 是否从贴纸列表隐藏
};

const MergeableEntry* mergeables();
int mergeableCount();
// 去掉尾随 .conflict<digits> 得到规范基名。
std::string canonicalBase(const std::string& base);
// 按规范基名查找；未注册返回 nullptr。
const MergeableEntry* findMergeable(const std::string& base);
// 按 rel 归一（basenameOf + canonicalBase）后查找；未注册返回 nullptr。
const MergeableEntry* findMergeableRel(const std::string& rel);
// base（可含 .conflictN）/ rel 是否已注册可合并。
bool isMergeableBase(const std::string& base);
bool isMergeableRel(const std::string& rel);
// 是否应从 UI 隐藏。
bool isHiddenFromUi(const std::string& base);

// 当前 epoch 毫秒（Qt3 走 qdatetime_shim 的 qNowMsecs）。
long long nowMsec();

// 写入该包全部描述（必要时新建载体行 + 原子写文件）。
// m 为空且文件不存在 → 不凭空造文件，直接成功。
bool saveAll(StickerDbSyncInterface& db, const std::string& baseDir,
             const std::string& packId,
             const QMap<QString, QJsonObject>& m);

// 单条增改 / 删除（内部读取 → 改动 → 整写）。
bool setDesc(StickerDbSyncInterface& db, const std::string& baseDir,
             const std::string& packId, const std::string& relKey,
             const std::string& desc, long long mtimeMsec);
// QString 便捷重载（两侧统一调它，调用点无需做 UTF-8 转换）。
bool setDescQ(StickerDbSyncInterface& db, const std::string& baseDir,
              const std::string& packId, const std::string& relKey,
              const QString& desc, long long mtimeMsec);
bool removeDesc(StickerDbSyncInterface& db, const std::string& baseDir,
                const std::string& packId, const std::string& relKey);

// 同步收尾：把载体内容回写 DB 描述（只改有差异者）；返回改动条数。
int applyPack(StickerDbSyncInterface& db, const std::string& baseDir,
              const std::string& packId);
int applyAll(StickerDbSyncInterface& db, const std::string& baseDir);

// 合并 *.conflict* 载体内容进主载体（保留冲突文件）；返回合并的冲突文件数。
int resolveConflicts(StickerDbSyncInterface& db, const std::string& baseDir,
                     const std::string& packId);
int resolveAllConflicts(StickerDbSyncInterface& db, const std::string& baseDir);

} // namespace packmeta

#endif // STIKCOMMON_PACK_META_STORE_H
