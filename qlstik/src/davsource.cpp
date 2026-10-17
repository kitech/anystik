#include "davsource.h"

#include "compatcore34.h"          // qFromUtf8
#include "storage.h"               // Storage::instance().stickerDb()
#include "sticker_db.h"            // StickerDbSyncInterface / StickerPackRow / StickerRow
#include "pack_meta_store.h"       // packmeta::isMergeableBase
#include "builtinsources.h"        // builtinsrc::matches / sanitizeDirName / diag
#include "davlocalsource.h"        // DavLocalSource / setDavLocalSource
#include "qsettings34.h"           // 读 downloadedPackMeta/<id>（与 anystik 同文件）
#include "qvariant_shim.h"         // QVariantMap + qVariantMapValue
#include "qstring_shim.h"          // qToStdString / qToUtf8BA / qToLower / qTrimmed / QLatin1Char
#include "qfile_shim.h"            // qFileRename / qFileCopy / qFileInfoPath / qFileInfoSuffix
#include "qglobaltype_shim.h"      // qMkpath
#include "qstandardpaths_shim.h"   // QStandardPaths::writableLocation
#include "config.h"                // 照搬 stickerlistpage.cpp 的 storageRoot 读取

#ifdef QT3_BUILD
#include "qimagereader_shim.h"         // Qt3 的完整 QImageReader
#include "qcryptographichash_shim.h"   // Qt3 的 QCryptographicHash 垫片
#else
#include <QImageReader>
#include <QCryptographicHash>
#endif

#include <memory>
#include <vector>

namespace {

// ── 基目录 / 相对路径：逐字照搬 stickerlistpage.cpp:447-467 ──
// 显示侧（贴纸网格）与同步侧（本适配层）必须同源，同一份 storageRoot 逻辑。
QString stickerBaseDir()
{
    // anysk：显式配置的根优先，非法则回落 AppLocalDataLocation。
    // qlstik 走 Config（现为 QSettings34，与 anystik 共用 anystik.conf）。
    const QString saved = Config::value(QString::fromUtf8("storageRoot"));
    if (!saved.isEmpty() && QDir(saved).exists()) {
        return saved;
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

QString stickerResolvePath(const QString& stored)
{
    if (stored.isEmpty()) {
        return stored;
    }
    if (stored.startsWith("/")) {
        return stored;                    // 绝对路径透传
    }
    return stickerBaseDir() + "/" + stored;
}

// 内容稳定 ID：Sha1(path) 的 40 位小写 hex。与 anystik 的 fileIdFor 等价。
QString fileIdFor(const QString& path)
{
    return QString(QCryptographicHash::hash(qToUtf8BA(path),
        QCryptographicHash::Sha1).toHex());
}

// 读 downloadedPackMeta/<id> 映射（anystik StickerStore::packMeta 等价物）。
QVariantMap qlstikPackMeta(const QString& packId)
{
    QSettings34 settings;   // 默认 org=fedlet / app=anystik
    return settings.value(
        QString::fromLatin1("downloadedPackMeta/") + packId).toMap();
}

class QlstikDavLocalSource : public DavLocalSource
{
public:
    QlstikDavLocalSource() { setDavLocalSource(this); }

    std::vector<DavLocalPack> installedPacksByTitleAsc()
    {
        std::vector<DavLocalPack> out;
        StickerDbSyncInterface* db = Storage::instance().stickerDb();
        if (!db)
            return out;
        const std::vector<StickerPackRow> rows = db->list_packs(1, "title ASC");
        for (size_t i = 0; i < rows.size(); ++i) {
            DavLocalPack p;
            p.id = qFromUtf8(rows[i].id);
            p.title = qFromUtf8(rows[i].title);
            out.push_back(p);
        }
        return out;
    }

    std::vector<DavLocalSticker> stickersOfPack(const QString& packId)
    {
        std::vector<DavLocalSticker> out;
        StickerDbSyncInterface* db = Storage::instance().stickerDb();
        if (!db)
            return out;
        const std::vector<StickerRow> rows =
            db->list_stickers(qToStdString(packId).c_str());
        for (size_t i = 0; i < rows.size(); ++i) {
            DavLocalSticker s;
            s.filePath = stickerResolvePath(qFromUtf8(rows[i].file_path));
            out.push_back(s);
        }
        return out;
    }

    bool isBuiltinSourcePack(const QString& packId, const QString& title)
    {
        const QString url =
            qVariantMapValue(qlstikPackMeta(packId),
                             QString::fromLatin1("url")).toString();
        return builtinsrc::matches(url, title);
    }

    QString builtinSourceDiag(const QString& packId, const QString& title)
    {
        const QString url =
            qVariantMapValue(qlstikPackMeta(packId),
                             QString::fromLatin1("url")).toString();
        return builtinsrc::diag(packId, url, title);
    }

    QStringList builtinSourceCloudDirs()
    {
        QStringList dirs;
        const std::vector<DavLocalPack> packs = installedPacksByTitleAsc();
        for (size_t i = 0; i < packs.size(); ++i) {
            if (isBuiltinSourcePack(packs[i].id, packs[i].title))
                dirs << builtinsrc::sanitizeDirName(packs[i].title);
        }
        return dirs;
    }

    QString relativeToBase(const QString& abs)
    {
        const QString base = stickerBaseDir();
        if (abs == base || abs.startsWith(base + "/"))
            return abs.mid(base.length() + 1);
        return abs;                       // 不在 base 下 → 原样
    }

    QString resolveStickerPath(const QString& stored)
    {
        return stickerResolvePath(stored);
    }

    QString ensurePack(const QString& title)
    {
        if (qTrimmed(title).isEmpty())
            return QString();
        StickerDbSyncInterface* db = Storage::instance().stickerDb();
        if (!db)
            return QString();

        // 按标题复用
        const std::vector<StickerPackRow> existing = db->list_packs(1);
        for (size_t i = 0; i < existing.size(); ++i) {
            if (qFromUtf8(existing[i].title) == title)
                return qFromUtf8(existing[i].id);
        }
        const QString packId =
            QString::fromLatin1("pack_") + fileIdFor(title).left(12);
        StickerPackRow pack;
        pack.id = qToStdString(packId);
        pack.title = qToStdString(title);
        pack.author = std::string();
        pack.position = int(db->list_packs(1).size());
        if (!db->add_pack(pack))
            return QString();
        return packId;
    }

    bool importStickerFile(const QString& packId, const QString& srcAbs,
                           QString* errorOut, const QString& dstName,
                           const QString& targetRel)
    {
        StickerDbSyncInterface* db = Storage::instance().stickerDb();
        if (!db) {
            if (errorOut) *errorOut = QString::fromUtf8("storage init failed");
            return false;
        }
        const QFileInfo si(srcAbs);
        if (!si.exists() || !si.isFile()) {
            if (errorOut) *errorOut = QString::fromUtf8("src file missing");
            return false;
        }
        const std::unique_ptr<StickerPackRow> pack =
            db->get_pack(qToStdString(packId).c_str());
        if (!pack) {
            if (errorOut) *errorOut = QString::fromUtf8("pack not found");
            return false;
        }

        // 目标相对路径：下载侧直传业务 dbRel（落盘 = 键一致）；为空则按包标题
        // base/packs/<title>。
        const QString rel = targetRel.isEmpty()
            ? QString::fromLatin1("packs/") + qFromUtf8(pack->title)
            : targetRel;
        const QString targetDir =
            stickerBaseDir() + "/" + qFileInfoPath(QFileInfo(rel));
        if (!qMkpath(targetDir)) {
            if (errorOut) *errorOut = QString::fromUtf8("无法创建包目录");
            return false;
        }

        // 目标文件名：下载侧显式传云端原始 basename；其余沿用源文件名。
        const QString fileName = targetRel.isEmpty()
            ? (dstName.isEmpty() ? si.fileName() : dstName)
            : rel.section(QLatin1Char('/'), -1);
        const QString dst = targetDir + "/" + fileName;

        // 幂等：同包同相对路径已有行 → 已导入（可合并边车目标缺失时例外，重落地）。
        const bool mergeableOk = packmeta::isMergeableBase(qToStdString(fileName));
        const std::vector<StickerRow> existing =
            db->list_stickers(qToStdString(packId).c_str());
        for (size_t i = 0; i < existing.size(); ++i) {
            if (qFromUtf8(existing[i].file_path) == rel) {
                if (mergeableOk && !QFileInfo(dst).isFile())
                    break;
                return true;
            }
        }

        // 解码预检（svg 例外：QVariant 依赖平台 qsvg 插件；可合并边车为文本非图）。
        const bool svgOk =
            qToLower(qFileInfoSuffix(QFileInfo(fileName)))
                == QString::fromLatin1("svg");
        QImageReader probe(srcAbs);
        probe.setAutoTransform(true);
        if (!svgOk && !mergeableOk && !probe.size().isValid()) {
            if (errorOut) *errorOut = QString::fromUtf8("图片解码失败");
            return false;
        }
        const QSize imgSize = probe.size();

        // 移动落地（src 为云端 get 的临时文件）；rename 失败（跨设备）退回拷贝+清理。
        // 不覆盖：qFileRename/qFileCopy 目标已存在即返回 false。
        if (dst != srcAbs) {
            if (!qFileRename(srcAbs, dst)) {
                if (!qFileCopy(srcAbs, dst)) {
                    if (errorOut) *errorOut = QString::fromUtf8("文件落地失败");
                    return false;
                }
                QFile::remove(srcAbs);
            }
        }

        const QFileInfo fi(dst);
        StickerRow row;
        // 载体用稳定业务 rel 作 id，与 packmeta::ensureCarrierRow 对齐
        row.id = mergeableOk ? qToStdString(rel) : qToStdString(fileIdFor(dst));
        row.pack_id = qToStdString(packId);
        row.file_path = qToStdString(rel);
        row.emoji = std::string();
        row.width = int(imgSize.width());
        row.height = int(imgSize.height());
        row.size = int(fi.size());
        row.last_used = 0;
        row.position = db->count_stickers(qToStdString(packId).c_str());
        return db->add_sticker(row);
    }
};

// 进程级单例（对应 anystik StickerStore::instance 模式）：命名空间静态对象在
// 动态初始化期构造，构造函数自我注册。构造本身不触碰 Qt 对象，早于/晚于
// QApplication 均无副作用；数据访问全部惰性到方法调用时。
QlstikDavLocalSource g_davSource;

} // namespace
