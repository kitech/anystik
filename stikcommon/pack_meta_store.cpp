#include "pack_meta_store.h"
#include "jsonl_lww_store.h"

#include "qstring_shim.h"         // QStringLiteral / qUtf8Printable
#include "qdatetime_shim.h"       // qNowMsecs()（Qt3）；QDateTime（Qt4+）

#ifdef QT3_BUILD
#include <qcstring.h>             // QCString（QString::utf8() 的返回类型）
#endif

#include <memory>                 // std::unique_ptr（sticker_db.h 已引，显式再引）
#include <string>
#include <vector>

namespace packmeta {

const char kCarrierName[]    = "_stikmeta.svg";
const char kConflictMarker[] = ".conflict";

namespace {

// QString → std::string（非 ASCII 安全：Qt3 utf8()、Qt4+ toUtf8()）。
std::string toStd(const QString& s)
{
#ifdef QT3_BUILD
    const QCString u = s.utf8();
    return std::string(u.data(), size_t(u.length()));
#else
    const QByteArray u = s.toUtf8();
    return std::string(u.constData(), size_t(u.size()));
#endif
}

QString fromStd(const std::string& s)
{
    return QString::fromUtf8(s.data(), int(s.size()));
}

bool isDigits(const std::string& s)
{
    if (s.empty()) {
        return false;
    }
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
    }
    return true;
}

// baseDir + rel；兼容历史遗留的绝对路径行（rel 以 '/' 开头）。
std::string absOf(const std::string& baseDir, const std::string& rel)
{
    if (!rel.empty() && rel[0] == '/') {
        return rel;
    }
    if (baseDir.empty()) {
        return rel;
    }
    return baseDir + "/" + rel;
}

// 该包全部行（含软删、含载体/冲突行）。
std::vector<StickerRow> rowsOf(StickerDbSyncInterface& db,
                               const std::string& packId)
{
    return db.list_stickers(packId.c_str(), "rowid DESC", 0, 0, -1, nullptr);
}

// 确保载体行存在（不存在则插一行；id 用稳定的载体相对路径）。
void ensureCarrierRow(StickerDbSyncInterface& db, const std::string& baseDir,
                      const std::string& packId, const std::string& rel)
{
    const std::vector<StickerRow> rows = rowsOf(db, packId);
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].file_path == rel) {
            return;
        }
    }
    StickerRow row;
    row.id        = rel;                     // 载体非内容寻址，用稳定路径作主键
    row.pack_id   = packId;
    row.file_path = rel;
    row.size      = int(jsonl_lww::readFileRaw(fromStd(absOf(baseDir, rel))).size());
    row.position  = db.count_stickers(packId.c_str());
    row.deleted   = 0;
    db.add_sticker(row);
}

// ── 可合并边车文件注册表（新增文件类型只加一行）──
const MergeableEntry kMergeables[] = {
    { kCarrierName, &jsonl_lww::merge, true },   // _stikmeta.svg：对 UI 隐藏
    // 未来：{ "tags.jsonl", &jsonl_lww::merge, true },
};
const int kMergeableCount = int(sizeof(kMergeables) / sizeof(kMergeables[0]));

} // namespace

std::string basenameOf(const std::string& rel)
{
    const size_t p = rel.find_last_of('/');
    return (p == std::string::npos) ? rel : rel.substr(p + 1);
}

std::string canonicalBase(const std::string& base)
{
    const size_t p = base.rfind(kConflictMarker);
    if (p == std::string::npos) {
        return base;
    }
    if (isDigits(base.substr(p + std::string(kConflictMarker).size()))) {
        return base.substr(0, p);
    }
    return base;
}

const MergeableEntry* mergeables() { return kMergeables; }
int mergeableCount() { return kMergeableCount; }

const MergeableEntry* findMergeable(const std::string& base)
{
    for (int i = 0; i < kMergeableCount; ++i) {
        if (base == kMergeables[i].basename) {
            return &kMergeables[i];
        }
    }
    return nullptr;
}

const MergeableEntry* findMergeableRel(const std::string& rel)
{
    return findMergeable(canonicalBase(basenameOf(rel)));
}

bool isMergeableBase(const std::string& base)
{
    return findMergeable(canonicalBase(base)) != nullptr;
}

bool isMergeableRel(const std::string& rel)
{
    return findMergeableRel(rel) != nullptr;
}

bool isHiddenFromUi(const std::string& base)
{
    const MergeableEntry* e = findMergeable(canonicalBase(base));
    return e != nullptr && e->hiddenFromUi;
}

bool isCarrierBase(const std::string& base)
{
    return isMergeableBase(base);
}

bool isInternalRel(const std::string& rel)
{
    return isHiddenFromUi(basenameOf(rel));
}

long long nowMsec()
{
#ifdef QT3_BUILD
    return static_cast<long long>(qNowMsecs());
#else
    return static_cast<long long>(QDateTime::currentMSecsSinceEpoch());
#endif
}

std::string carrierDir(StickerDbSyncInterface& db, const std::string& packId)
{
    const std::vector<StickerRow> rows = rowsOf(db, packId);
    for (size_t i = 0; i < rows.size(); ++i) {
        const std::string& fp = rows[i].file_path;
        if (isInternalRel(fp)) {
            continue;
        }
        const size_t p = fp.find_last_of('/');
        if (p != std::string::npos) {
            return fp.substr(0, p);        // pastes 或 packs/<T>
        }
    }
    const std::unique_ptr<StickerPackRow> pack = db.get_pack(packId.c_str());
    if (pack && !pack->title.empty()) {
        return "packs/" + pack->title;
    }
    return "packs/" + packId;
}

std::string carrierRel(StickerDbSyncInterface& db, const std::string& packId)
{
    return carrierDir(db, packId) + "/" + kCarrierName;
}

bool saveAll(StickerDbSyncInterface& db, const std::string& baseDir,
             const std::string& packId,
             const QMap<QString, QJsonObject>& m)
{
    const std::string rel = carrierRel(db, packId);
    const std::string abs = absOf(baseDir, rel);
    if (m.isEmpty() && jsonl_lww::readFileRaw(fromStd(abs)).empty()) {
        return true;                       // 无描述且无载体：不凭空造文件
    }
    if (!jsonl_lww::writeFile(fromStd(abs), m)) {
        return false;
    }
    ensureCarrierRow(db, baseDir, packId, rel);
    return true;
}

bool setDesc(StickerDbSyncInterface& db, const std::string& baseDir,
             const std::string& packId, const std::string& relKey,
             const std::string& desc, long long mtimeMsec)
{
    const std::string rel = carrierRel(db, packId);
    QMap<QString, QJsonObject> m =
        jsonl_lww::parse(jsonl_lww::readFileRaw(fromStd(absOf(baseDir, rel))));
    QJsonObject o;
    o.insert(QStringLiteral("rel"), QJsonValue(fromStd(relKey)));
    o.insert(QStringLiteral("desc"), QJsonValue(fromStd(desc)));
    o.insert(QStringLiteral("mtime"), QJsonValue(double(mtimeMsec)));
    m.insert(fromStd(relKey), o);
    return saveAll(db, baseDir, packId, m);
}

bool setDescQ(StickerDbSyncInterface& db, const std::string& baseDir,
              const std::string& packId, const std::string& relKey,
              const QString& desc, long long mtimeMsec)
{
    return setDesc(db, baseDir, packId, relKey, toStd(desc), mtimeMsec);
}

bool removeDesc(StickerDbSyncInterface& db, const std::string& baseDir,
                const std::string& packId, const std::string& relKey)
{
    const std::string rel = carrierRel(db, packId);
    const std::string abs = absOf(baseDir, rel);
    QMap<QString, QJsonObject> m =
        jsonl_lww::parse(jsonl_lww::readFileRaw(fromStd(abs)));
    if (!m.contains(fromStd(relKey))) {
        return true;
    }
    m.remove(fromStd(relKey));
    return saveAll(db, baseDir, packId, m);
}

int applyPack(StickerDbSyncInterface& db, const std::string& baseDir,
              const std::string& packId)
{
    const std::string rel = carrierRel(db, packId);
    const std::string raw =
        jsonl_lww::readFileRaw(fromStd(absOf(baseDir, rel)));
    if (raw.empty()) {
        return 0;
    }
    const QMap<QString, QJsonObject> m = jsonl_lww::parse(raw);

    const std::vector<StickerRow> rows = rowsOf(db, packId);
    int changed = 0;
    for (QMap<QString, QJsonObject>::const_iterator it = m.constBegin();
         it != m.constEnd(); ++it) {
        const std::string base = toStd(it.key());
        const std::string desc =
            toStd((*it).value(QStringLiteral("desc")).toString());
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].file_path.empty()) {
                continue;
            }
            if (basenameOf(rows[i].file_path) != base) {
                continue;
            }
            if (rows[i].description != desc) {
                db.update_sticker_description(rows[i].id.c_str(), desc.c_str());
                ++changed;
            }
            break;
        }
    }
    return changed;
}

int applyAll(StickerDbSyncInterface& db, const std::string& baseDir)
{
    const std::vector<StickerPackRow> packs = db.list_packs(kPacksAll);
    int total = 0;
    for (size_t i = 0; i < packs.size(); ++i) {
        total += applyPack(db, baseDir, packs[i].id);
    }
    return total;
}

int resolveConflicts(StickerDbSyncInterface& db, const std::string& baseDir,
                     const std::string& packId)
{
    QMap<QString, QJsonObject> m;
    int merged = 0;
    const std::vector<StickerRow> rows = rowsOf(db, packId);
    for (size_t i = 0; i < rows.size(); ++i) {
        const std::string base = basenameOf(rows[i].file_path);
        if (!isCarrierBase(base)) {
            continue;                      // 只吃载体本身 + *.conflict<digits>
        }
        const std::string raw = jsonl_lww::readFileRaw(
            fromStd(absOf(baseDir, rows[i].file_path)));
        if (raw.empty()) {
            continue;
        }
        jsonl_lww::foldInto(m, raw);
        if (base != kCarrierName) {
            ++merged;                      // 冲突文件内容已并入
        }
    }
    if (merged > 0) {
        saveAll(db, baseDir, packId, m);    // 回写主载体（**不删**冲突文件）
    }
    return merged;
}

int resolveAllConflicts(StickerDbSyncInterface& db, const std::string& baseDir)
{
    const std::vector<StickerPackRow> packs = db.list_packs(kPacksAll);
    int total = 0;
    for (size_t i = 0; i < packs.size(); ++i) {
        total += resolveConflicts(db, baseDir, packs[i].id);
    }
    return total;
}

} // namespace packmeta
