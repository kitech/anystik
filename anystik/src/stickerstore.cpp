#include "stickerstore.h"
#include "storage.h"
#include "sticker_db.h"
#include "androidutils.h"
#include "macpasteboard.h"
#include "macgifconverter.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QBuffer>
#include <QFileInfo>
#include <QImageReader>
#include <QClipboard>
#include <QMimeData>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QDateTime>
#include <QByteArrayView>
#include <QMutex>
#include <QDebug>
#include <QMimeDatabase>
#include <QMimeType>
#include <QSettings>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QtConcurrent/QtConcurrent>
#include <QtCore/private/qzipreader_p.h>
#include <QTemporaryFile>
#include "../vendor/tangora_gif.h"
#include <string>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#include <QFuture>
#endif

StickerStore* StickerStore::s_instance = nullptr;
static QMutex s_initMutex;

StickerStore* StickerStore::instance()
{
    if (!s_instance) {
        s_instance = new StickerStore();
    }
    return s_instance;
}

StickerStore::StickerStore(QObject* parent)
    : QObject(parent)
{
}

bool StickerStore::ensureInit()
{
    QMutexLocker locker(&s_initMutex);
    if (m_initialized) {
        return true;
    }

    const QString dataDir = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    if (dataDir.isEmpty()) {
        qWarning() << "[StickerStore] no AppLocalDataLocation";
        return false;
    }

    qDebug() << "[StickerStore] init Storage at:" << dataDir;
    if (!Storage::instance().init(dataDir.toUtf8().constData())) {
        qWarning() << "[StickerStore] Storage::init failed";
        return false;
    }
    dropLegacyChatTables();
    m_initialized = true;
    return true;
}

// 聊天功能已移除：Storage::init 仍会创建聊天域的空表，这里启动时一次性清掉，
// 历史聊天消息记录随之删除（qldox 目录保持零改动）。
void StickerStore::dropLegacyChatTables()
{
    static const char* legacyTables[] = {
        "messages", "messages_fts", "reactions", "translations",
        "bookmarks", "channels", "peers", "pending_messages"
    };
    auto& db = Storage::instance().msgDb();
    for (const char* t : legacyTables) {
        db.exec((std::string("DROP TABLE IF EXISTS ") + t).c_str());
    }
    qDebug() << "[StickerStore] legacy chat tables dropped";
}

QString StickerStore::stickerBaseDir() const
{
    if (m_stickerBaseDir.isEmpty()) {
        // 持久化优先：若已显式切过存储根，则沿用；否则默认 app 私有目录。
        const QString saved = QSettings().value(
            QStringLiteral("storageRoot")).toString();
        if (!saved.isEmpty()
                && QDir(saved).exists()
                && QFileInfo(saved).isWritable()) {
            m_stickerBaseDir = saved;
        } else {
            m_stickerBaseDir = QStandardPaths::writableLocation(
                QStandardPaths::AppLocalDataLocation);
        }
        if (m_stickerBaseDir.isEmpty())
            m_stickerBaseDir = QDir::tempPath();
    }
    return m_stickerBaseDir;
}

QString StickerStore::storageRootPath(StorageRoot r) const
{
    switch (r) {
    case StorageRoot::AppPrivate: {
        QString p = QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation);
        return p.isEmpty() ? QDir::tempPath() : p;
    }
    case StorageRoot::Pictures: {
#ifdef Q_OS_ANDROID
        const QString p = androidPicturesStickerBaseDir();
        if (!p.isEmpty())
            return p;
#endif
        const QString pics = QStandardPaths::writableLocation(
            QStandardPaths::PicturesLocation);
        return pics.isEmpty() ? QString()
                              : pics + QStringLiteral("/anystik");
    }
    }
    return QString();
}

bool StickerStore::isCurrentStoragePictures() const
{
    const QString cur = QDir::cleanPath(stickerBaseDir());
    const QString pics = QDir::cleanPath(storageRootPath(StorageRoot::Pictures));
    return !pics.isEmpty() && cur == pics;
}

QString StickerStore::resolveStickerPath(const QString& stored)
{
    const QString base = stickerBaseDir();
    if (stored.isEmpty())
        return stored;
    // 以 '/' 开头的视为绝对路径（旧数据/外部导入透传），否则拼到当前 base
    if (stored.startsWith(QLatin1Char('/')))
        return stored;
    return base + QLatin1Char('/') + stored;
}

QString StickerStore::relativeToBase(const QString& abs)
{
    const QString base = stickerBaseDir();
    if (abs == base || abs.startsWith(base + QLatin1Char('/')))
        return abs.mid(base.size() + 1);
    // 不在 base 下 → 原样（如外部绝对路径）
    return abs;
}

namespace {
// 迁移结果（阶段一 工作线程产生，阶段二 GUI 线程消费）
struct MigrationResult {
    bool ok = false;
    bool sameRoot = false;            // 起止根相同（无需迁移）
    QString failDetail;               // 失败原因（中文）
    int totalFiles = 0;               // 计划迁移的文件数（进度分母）
    qint64 copiedBytes = 0;           // 已拷贝字节（仅展示，不参与进度）
    int externalSkipped = 0;          // 外部引用跳过条数
    QStringList created;              // 本次新建的目标侧文件（回滚时删除）
    QStringList relSkipped;           // 已在目标、未搬的记录（用于 phase2 相对化）
    QStringList relMoved;             // 从 fromRoot 搬入的记录（相对路径，phase2 转换）
    QStringList relExternal;          // 外部引用（非 base 下绝对，phase2 保留不动）
    QString fromRoot, toRoot;
};
}

bool StickerStore::switchStorageRoot(StorageRoot target, QString* errorOut)
{
    if (!ensureInit()) {
        if (errorOut) *errorOut = QStringLiteral("存储未初始化");
        return false;
    }
    if (m_migrating) {
        if (errorOut) *errorOut = QStringLiteral("正在迁移，请稍候");
        return false;
    }

    const QString fromRoot = QDir::cleanPath(stickerBaseDir());
    const QString toRoot = QDir::cleanPath(storageRootPath(target));
    if (toRoot.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("无法确定目标目录");
        return false;
    }
    if (fromRoot == toRoot) {
        // 目标即当前根：直接持久化该根并返回成功
        QSettings().setValue(QStringLiteral("storageRoot"), toRoot);
        return true;
    }
    if (!QDir().mkpath(toRoot)) {
        if (errorOut) *errorOut = QStringLiteral("无法创建目标目录：")
                                  + toRoot;
        return false;
    }

    // 开始两阶段异步迁移
    m_migrating = true;
    m_pendingStorageRoot = target;

    auto future = QtConcurrent::run([this, fromRoot, toRoot]() {
        // ── 阶段一：只复制，绝不触碰源与 DB ──
        MigrationResult r;
        r.fromRoot = fromRoot;
        r.toRoot = toRoot;

        // 收集 DB 全部记录路径（含软删）
        QVector<QPair<QString,QString>> rels;      // (file_path 原文, 相对或绝对分类)
        {
            auto& db = Storage::instance().msgDb();
            SqliteStatement stmt = db.prepare("SELECT file_path FROM stickers");
            if (stmt.isPrepared()) {
                while (stmt.stepRow()) {
                    const char* fp = stmt.columnText(0);
                    if (fp && *fp)
                        rels.append({QString::fromUtf8(fp), QString()});
                }
            }
        }

        // 构建去重的 from→to 映射，并分类
        QSet<QString> seenTo;
        struct Plan { QString from, to, rel; int kind; }; // kind 1=搬入 0=已在目标 2=外部
        QVector<Plan> plan;
        for (const auto& fp : rels) {
            const QString& stored = fp.first;
            if (stored.startsWith(QLatin1Char('/'))) {
                // 绝对路径
                if (stored.startsWith(fromRoot + QLatin1Char('/'))) {
                    const QString rel = stored.mid(fromRoot.size() + 1);
                    const QString to = toRoot + QLatin1Char('/') + rel;
                    if (!seenTo.contains(to)) {
                        seenTo.insert(to);
                        plan.append({QString(), to, rel, 1}); // from 由规则推导
                    }
                    r.relMoved.append(rel);
                } else if (stored.startsWith(toRoot + QLatin1Char('/'))) {
                    const QString rel = stored.mid(toRoot.size() + 1);
                    if (!seenTo.contains(stored)) {
                        seenTo.insert(stored);
                        plan.append({QString(), stored, rel, 0});
                    }
                    r.relSkipped.append(rel);
                } else {
                    r.relExternal.append(stored);   // 外部引用，保留不动
                    r.externalSkipped++;
                }
            } else {
                // 相对路径（相对 base）
                if (!seenTo.contains(stored)) {
                    seenTo.insert(stored);
                    plan.append({QString(), QString(), stored, 3}); // 相对，搬
                }
                r.relMoved.append(stored);
            }
        }

        r.totalFiles = plan.size();
        int done = 0;
        bool failed = false;
        for (auto& p : plan) {
            // 计算实际源/目标路径
            QString from, to;
            switch (p.kind) {
            case 1: from = fromRoot + QLatin1Char('/') + p.rel; to = p.to; break;
            case 0: from = p.to; to = p.to; break;               // 已在目标，无需搬
            case 2: continue;                                     // 不会进入 plan
            case 3: from = fromRoot + QLatin1Char('/') + p.rel;
                    to = toRoot + QLatin1Char('/') + p.rel; break;
            default: continue;
            }
            done++;
            if (p.kind == 0) {
                // 已在目标：无需复制
                emit migrationProgress(done, r.totalFiles, r.copiedBytes, p.to);
                continue;
            }
            if (QFile::exists(to)) {
                emit migrationProgress(done, r.totalFiles, r.copiedBytes, to);
                continue;   // 已存在 → 幂等跳过
            }
            if (!QDir().mkpath(QFileInfo(to).absolutePath())) {
                r.failDetail = QStringLiteral("无法创建子目录：")
                               + QFileInfo(to).absolutePath();
                failed = true; break;
            }
            if (!QFile::copy(from, to)) {
                r.failDetail = QStringLiteral("复制失败：")
                               + from + QStringLiteral(" → ") + to;
                failed = true; break;
            }
            r.created.append(to);
            r.copiedBytes += QFileInfo(to).size();
            emit migrationProgress(done, r.totalFiles, r.copiedBytes, to);
        }

        r.ok = !failed;
        if (failed && !r.created.isEmpty()) {
            // 回滚：仅删除本次新建的目标侧副本，绝不碰 fromRoot 源
            for (int i = r.created.size() - 1; i >= 0; --i)
                QFile::remove(r.created[i]);
        }
        return r;
    });

    future.then(this, [this, fromRoot, toRoot, target,
                 errorOut](const MigrationResult& r) {
        if (!r.ok) {
            m_migrating = false;
            if (errorOut) *errorOut = r.failDetail;
            emit migrationFinished(false, r.failDetail);
            return;
        }
        // ── 阶段二：全部复制成功后，转换 DB 绝对路径 → 相对（按 toRoot）+ 持久化 ──
        auto& db = Storage::instance().msgDb();
        db.beginTransaction();
        bool dbOk = true;
        {
            SqliteStatement sel = db.prepare("SELECT rowid,file_path FROM stickers");
            if (!sel.isPrepared()) dbOk = false;
            while (dbOk && sel.stepRow()) {
                const qint64 rowid = sel.columnInt64(0);
                const char* fp = sel.columnText(1);
                QString stored = fp ? QString::fromUtf8(fp) : QString();
                QString upd;
                if (stored.startsWith(toRoot + QLatin1Char('/'))) {
                    // 绝对且位于新 base 下 → 相对化
                    upd = stored.mid(toRoot.size() + 1);
                } else if (stored.startsWith(QLatin1Char('/'))) {
                    // 其他绝对（外部/旧）→ 保留不动
                    continue;
                } else {
                    // 已是相对 → 不变
                    continue;
                }
                SqliteStatement up = db.prepare(
                    "UPDATE stickers SET file_path=?1 WHERE rowid=?2");
                if (!up.isPrepared() || !up.bind(1, upd.toUtf8().constData())
                        || !up.bind(2, static_cast<int64_t>(rowid)) || !up.step()) {
                    dbOk = false; break;
                }
            }
        }
        if (dbOk) {
            db.commitTransaction();
        } else {
            db.rollbackTransaction();
        }

        if (!dbOk) {
            m_migrating = false;
            if (errorOut) *errorOut = QStringLiteral("数据库路径更新失败");
            emit migrationFinished(false, QStringLiteral("数据库路径更新失败"));
            return;
        }

        // 持久化新 base
        QSettings().setValue(QStringLiteral("storageRoot"), toRoot);
        m_stickerBaseDir = toRoot;
        m_migrating = false;

        // best-effort：重写 downloadedPackMeta/<id>.dir（若指向旧 base/packs）
        {
            QSettings s;
            // 通过 packs 接口无法拿到元数据键集合，这里按 id 无法枚举；
            // 改为遍历现有 pack 元数据键（downloadedPackMeta/<id>）估算不现实，
            // 简化：卸载删文件路径略旧，不影响数据正确性，此处不处理（注释说明）。
            Q_UNUSED(s)
        }

        // 说明：不做 MediaScanner 广播，Pictures/anystik 仅作文件存储，
        // 图片不会自动出现在系统相册索引中（属预期）。

        emit migrationFinished(true, QString());
        emit dataChanged();
        Q_UNUSED(fromRoot)
    });

    return true;
}

StickerDbSyncInterface& stickerDb()
{
    return *Storage::instance().stickerDb();
}

QVector<StickerPackBrief> StickerStore::packs(int installed, const char* orderby,
                                                 int limit, int offset)
{
    QVector<StickerPackBrief> result;
    if (!ensureInit()) {
        return result;
    }
    auto rows = stickerDb().list_packs(installed, orderby, limit, offset);
    for (const auto& row : rows) {
        StickerPackBrief b;
        b.id = QString::fromStdString(row.id);
        b.title = QString::fromUtf8(row.title.c_str());
        b.author = QString::fromUtf8(row.author.c_str());
        b.coverPath = resolveStickerPath(QString::fromStdString(row.cover_path));
        b.position = row.position;
        result.append(b);
    }
    return result;
}

QVector<StickerBrief> StickerStore::stickers(const QString& packId,
                                               const char* orderby,
                                               int limit, int offset,
                                               int deleted, const char* emoji)
{
    QVector<StickerBrief> result;
    if (!ensureInit()) {
        return result;
    }
    auto rows = stickerDb().list_stickers(packId.toUtf8().constData(),
                                          orderby, limit, offset, deleted, emoji);
    for (const auto& row : rows) {
        StickerBrief b;
        b.id = QString::fromStdString(row.id);
        b.packId = QString::fromStdString(row.pack_id);
        b.filePath = resolveStickerPath(QString::fromStdString(row.file_path));
        b.emoji = QString::fromUtf8(row.emoji.c_str());
        b.width = row.width;
        b.height = row.height;
        b.size = row.size;
        b.lastUsed = row.last_used;
        b.description = QString::fromUtf8(row.description.c_str());
        result.append(b);
    }
    return result;
}

QVector<StickerBrief> StickerStore::recent(int limit)
{
    QVector<StickerBrief> result;
    if (!ensureInit()) {
        return result;
    }
    auto rows = stickerDb().list_recent_stickers(limit);
    for (const auto& row : rows) {
        StickerBrief b;
        b.id = QString::fromStdString(row.id);
        b.packId = QString::fromStdString(row.pack_id);
        b.filePath = resolveStickerPath(QString::fromStdString(row.file_path));
        b.emoji = QString::fromUtf8(row.emoji.c_str());
        b.width = row.width;
        b.height = row.height;
        b.size = row.size;
        b.lastUsed = row.last_used;
        b.description = QString::fromUtf8(row.description.c_str());
        result.append(b);
    }
    return result;
}

QVector<StickerBrief> StickerStore::search(const QString& query)
{
    QVector<StickerBrief> result;
    if (!ensureInit() || query.isEmpty()) {
        return result;
    }
    auto rows = stickerDb().search_stickers(query.toUtf8().constData());
    for (const auto& row : rows) {
        StickerBrief b;
        b.id = QString::fromStdString(row.id);
        b.packId = QString::fromStdString(row.pack_id);
        b.filePath = resolveStickerPath(QString::fromStdString(row.file_path));
        b.emoji = QString::fromUtf8(row.emoji.c_str());
        b.width = row.width;
        b.height = row.height;
        b.size = row.size;
        b.lastUsed = row.last_used;
        b.description = QString::fromUtf8(row.description.c_str());
        result.append(b);
    }
    return result;
}

int StickerStore::countStickers(const QString& packId)
{
    if (!ensureInit()) {
        return 0;
    }
    return stickerDb().count_stickers(
        packId.isEmpty() ? nullptr : packId.toUtf8().constData());
}

// ── 目录导入：每个子目录 = 一个贴纸包，支持图片递归 ──
static bool isSupportedImage(const QString& suffix)
{
    static const QStringList exts = {
        "png", "jpg", "jpeg", "gif", "webp", "bmp", "svg",
    };
    return exts.contains(suffix.toLower());
}

static bool scanRecursive(QDir dir, QVector<QString>& files)
{
    bool ok = true;
    const auto infoList = dir.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);
    for (const auto& fi : infoList) {
        if (fi.isDir()) {
            if (!scanRecursive(QDir(fi.absoluteFilePath()), files)) {
                ok = false;
            }
        } else if (isSupportedImage(fi.suffix())) {
            files.append(fi.absoluteFilePath());
        }
    }
    return ok;
}

static QString fileIdFor(const QString& path)
{
    return QString(QCryptographicHash::hash(path.toUtf8(),
        QCryptographicHash::Sha1).toHex());
}

// ── PNG/APNG 深度探测：Qt PNG 插件不识别 APNG 动画帧（一律报 png / imageCount==1），
//    按 chunk 结构自检。acTL 必在首个 IDAT 前（APNG 规范），fdAT 紧随其后。──
struct PngChunkInfo {
    bool    validSignature = false;
    bool    hasAcTL   = false;
    quint32 acTlFrames = 0;
    int     fdAtCount = 0;
    int     fcTlCount = 0;         // fcTL 帧控制块计数（真 APNG 至少 1 个）
    bool    truncated = false;
    QByteArray summary;            // 诊断：前 12 个 chunk 类型 "IHDR|PLTE|IDAT|"
};

static PngChunkInfo probePngChunks(const QByteArray& bytes)
{
    PngChunkInfo info;
    static const uchar kSig[8] = {0x89,'P','N','G','\r','\n',0x1a,'\n'};
    if (bytes.size() < 8 || memcmp(bytes.constData(), kSig, 8) != 0) return info;
    info.validSignature = true;
    quint64 off = 8, total = quint64(bytes.size());   // quint64 偏移，杜绝 4GiB 溢出
    bool sawIDAT = false;
    int shown = 0, scanned = 0;
    while (off + 8 <= total) {
        const quint32 len = (quint32(uchar(bytes.at(off)))   << 24)
                          | (quint32(uchar(bytes.at(off+1))) << 16)
                          | (quint32(uchar(bytes.at(off+2))) <<  8)
                          |  quint32(uchar(bytes.at(off+3)));
        const quint64 dataStart = off + 8;
        if (dataStart + quint64(len) + 4 > total) { info.truncated = true; break; }  // +CRC
        const QByteArray type = bytes.mid(off + 4, 4);
        // acTL 依 APNG 规范必须在首个 IDAT 之前；其后出现视为无效
        if (!sawIDAT && type == "acTL") {
            info.hasAcTL = true;
            if (len >= 4)
                info.acTlFrames = (quint32(uchar(bytes.at(dataStart)))   << 24)
                                | (quint32(uchar(bytes.at(dataStart+1))) << 16)
                                | (quint32(uchar(bytes.at(dataStart+2))) <<  8)
                                |  quint32(uchar(bytes.at(dataStart+3)));
        }
        if (type == "fcTL") ++info.fcTlCount;   // 真 APNG 至少一个帧控制块
        if (sawIDAT && type == "fdAT") ++info.fdAtCount;
        if (type == "IDAT") sawIDAT = true;
        if (!sawIDAT && shown < 12) {
            if (!info.summary.isEmpty()) info.summary += '|';
            info.summary += type;
            ++shown;
        }
        if (type == "IEND") break;
        if (++scanned > 8192) { info.truncated = true; break; }   // 防超长/恶意图
        off = dataStart + quint64(len) + 4;
    }
    return info;
}

// APNG 帧数：acTL num_frames 权威；异常无 acTL 结构按 fdAT 计数（>0 即动画迹象）
static int apngFrameCount(const QByteArray& bytes)
{
    const PngChunkInfo c = probePngChunks(bytes);
    if (!c.validSignature) return 0;
    return c.hasAcTL ? int(c.acTlFrames) : c.fdAtCount;
}

// APNG 判定：依规范 acTL 已在首个 IDAT 前（probePngChunks 已强校验位置），
// 且存在至少一个 fcTL 帧控制块确认真动画；异常结构（无 acTL 但有 fdAT）仍按 fdAT 迹象识别
static bool isApng(const PngChunkInfo& c)
{
    return (c.hasAcTL && c.fcTlCount > 0) || c.fdAtCount > 0;
}

// WebP 帧数：Qt libwebp 插件支持动画但不下发帧数；按 RIFF/WEBP 结构自检验证，
// 统计 ANMF 帧块（ANIM+ANMF 齐则动画，返回帧数；否则 0）
static int webpFrameCount(const QByteArray& bytes)
{
    if (bytes.size() < 16) return 0;
    if (bytes.at(0) != 'R' || bytes.at(1) != 'I' || bytes.at(2) != 'F' || bytes.at(3) != 'F') return 0;
    if (bytes.at(8) != 'W' || bytes.at(9) != 'E' || bytes.at(10) != 'B' || bytes.at(11) != 'P') return 0;
    quint32 off = 12;
    const quint32 total = quint32(bytes.size());
    int anmf = 0, scanned = 0;
    while (off + 8 <= total) {
        const QByteArray four = bytes.mid(off, 4);
        const quint32 len =   (quint32(uchar(bytes.at(off + 4)))       )
                            | (quint32(uchar(bytes.at(off + 5))) << 8)
                            | (quint32(uchar(bytes.at(off + 6))) << 16)
                            | (quint32(uchar(bytes.at(off + 7))) << 24);
        const quint32 dataEnd = off + 8 + len;
        if (dataEnd > total) break;      // 截断：放弃
        if (four == "ANMF") ++anmf;      // 每帧一个 ANMF 块
        off = dataEnd + (len & 1);       // 校验字对齐（奇长补一字节）
        if (++scanned > 1024) break;
    }
    return anmf > 0 ? anmf : 0;
}

// 运行时实际可用的图片格式集合（缓存）。
// 白名单写死 vs 插件缺失可能产生「支持列表有但运行时尚无法解码 → 静默失败」，
// 这里以 QImageReader::supportedImageFormats() 为准做交集兜底。
// 映射：内部 normalized 名（apng/svgz）→ 真实插件名（png/svg）。
static QSet<QByteArray> runtimeSupportedImageFormats()
{
    static const QSet<QByteArray> kCache = [] {
        QSet<QByteArray> s;
        const auto fmts = QImageReader::supportedImageFormats();
        for (const QByteArray& f : fmts) {
            const QByteArray lo = f.trimmed().toLower();
            if (lo.isEmpty()) continue;
            s.insert(lo);
            if (lo == "png")  s.insert("apng");   // APNG 由 png 插件读取
            if (lo == "svg")  s.insert("svgz");   // gzip svg 同名插件
            if (lo == "jpeg") s.insert("jpg");
            if (lo == "tif")  s.insert("tiff");
        }
        return s;
    }();
    return kCache;
}

// 单次解码首帧（鲁棒版）：每个 QImageReader 只用一次 read()，绝不 seek 回绕复用。
// Qt 官方明确：QImageReader 生命周期内修改其 device 位置 = undefined results；
// 且 QJpegHandler 等内置插件在首次 read() 后进入 ReadingEnd 状态，二次 read() 必败。
// 失败时依次降级：关 autoTransform（EXIF/旋转元数据故障点）→ 显式定格式 → 组合。
// 返回成功 QImage（失败为 null），outErr/outErrStr 携带最终失败原由。
static QImage robustDecodeFirstFrame(const QByteArray& bytes,
                                     const QByteArray& fmt,
                                     QImageReader::ImageReaderError* outErr = nullptr,
                                     QString* outErrStr = nullptr)
{
    QByteArray readerFmt = fmt.trimmed().toLower();
    if (readerFmt == "apng") readerFmt = "png";  // APNG 由 png 插件读取
    if (readerFmt == "svgz") readerFmt = "svg";
    if (readerFmt == "jpg")  readerFmt = "jpeg";

    const auto attempt = [&](bool autoTransform, const QByteArray& forcedFmt) {
        QBuffer buf;
        buf.setData(bytes);
        if (!buf.open(QIODevice::ReadOnly)) return QImage();
        QImageReader r(&buf);
        r.setAutoTransform(autoTransform);
        if (!forcedFmt.isEmpty()) r.setFormat(forcedFmt);
        QImage img = r.read();
        if (outErr)   *outErr   = r.error();
        if (outErrStr) *outErrStr = r.errorString();
        return img.size().isValid() ? img : QImage();
    };

    QImage img = attempt(true, readerFmt);
    if (!img.isNull() || readerFmt.isEmpty()) return img;
    img = attempt(false, readerFmt);
    if (!img.isNull()) return img;
    img = attempt(true, QByteArray());   // 显式定格式失败 → 交还自动探测
    if (!img.isNull()) return img;
    img = attempt(false, QByteArray());
    return img;
}

// 统一的图片来源有效性/尺寸探测：
// 拦截级：空 / 不可读 / 尺寸无效 / 格式不在白名单 / read() 解码失败 → 返回 false
// 观测级：过小 → 仅 qWarning
static bool probeImageValidity(const QByteArray& bytes,
                               QByteArray* outFormat, QSize* outSize,
                               int* outFrames = nullptr)
{
    if (bytes.isEmpty()) {
        qWarning("[StickerPaste][probe] empty bytes");
        return false;
    }
    if (bytes.size() < 16) {
        qWarning("[StickerPaste][probe] suspiciously small size=%d", bytes.size());
    }

    QBuffer probe;
    probe.setData(bytes);
    probe.open(QIODevice::ReadOnly);
    QImageReader reader(&probe);
    reader.setAutoTransform(true);

    if (!reader.canRead()) {
        qWarning("[StickerPaste][probe] cannot read, size=%d", bytes.size());
        return false;
    }

    QByteArray fmt = reader.format().trimmed().toLower();
    if (fmt.isEmpty()) {
        fmt = QImageReader::imageFormat(&probe).trimmed().toLower();
    }

    int frames = 0;
    if (fmt == "png") {                    // APNG 归一：Qt 只会报 png
        const PngChunkInfo png = probePngChunks(bytes);
        if (isApng(png)) {
            fmt = "apng";
            frames = png.acTlFrames > 0 ? int(png.acTlFrames) : png.fdAtCount;
            if (png.truncated)
                qWarning("[StickerPaste][probe] apng truncated acTL=%u fdAT=%d fcTL=%d",
                         png.acTlFrames, png.fdAtCount, png.fcTlCount);
        }
    }

    static const QSet<QByteArray> kAllowed = {
        "png","apng",                      // apng 手动加入
        "jpg","jpeg","gif","webp","bmp","tif","tiff","tga",
        "xpm","xbm","ppm","pbm","pgm","wbmp","svg","svgz","avif"
    };
    if (!kAllowed.contains(fmt)) {
        qWarning("[StickerPaste][probe] format not in whitelist fmt=%s",
                 fmt.constData());
        return false;
    }
    // 运行时插件兜底：白名单写死 ≠ 当前运行时插件真实支持。
    // 若格式在 supportedImageFormats() 中缺失则明确拒绝（避免解码静默失败无日志）。
    if (!runtimeSupportedImageFormats().contains(fmt)) {
        qWarning("[StickerPaste][probe] format not supported by runtime plugins fmt=%s "
                 "(whitelisted but missing imageformats plugin)",
                 fmt.constData());
        return false;
    }

    const QSize size = reader.size();
    if (!size.isValid()) {
        qWarning("[StickerPaste][probe] size invalid fmt=%s", fmt.constData());
        return false;
    }

    // 终验：完整解码首帧（只校验，不用于入库尺寸）。
    // 用全新 QBuffer + 全新 QImageReader 独立 read() 一次 ——
    // 绝不在既有 reader 生命周期内对其 device 手动 seek 复用（Qt 官方 = undefined，
    // 内置插件二次 read() 必败，正是此前「部分图片解码失败」的根因）。
    QImageReader::ImageReaderError decodeErr = QImageReader::UnknownError;
    QString decodeErrStr;
    QImage first = robustDecodeFirstFrame(bytes, fmt, &decodeErr, &decodeErrStr);
    if (first.isNull() || !first.size().isValid()) {
        qWarning("[StickerPaste][probe] decode read failed fmt=%s err=%d errstr=%s size=%dx%d",
                 fmt.constData(), int(decodeErr), qPrintable(decodeErrStr),
                 size.width(), size.height());
        // 大图/内存限制兜底：Qt 因 allocation limit 拒绝（RawImageFormatError/InvalidDataError
        // 且错误串含 allocation）。贴纸导入超大图本不合理，明确拒绝并给出可读提示路径。
        if (decodeErrStr.contains("allocation", Qt::CaseInsensitive)
            || decodeErrStr.contains("memory", Qt::CaseInsensitive)) {
            qWarning("[StickerPaste][probe] image exceeds QImageReader allocation limit "
                     "(QT_IMAGEIO_MAXALLOC default 256MB) size=%dx%d bytes=%lld",
                     size.width(), size.height(), (qint64)bytes.size());
        }
        // 细诊断：区分 真APNG / 截断损坏 / Qt 重编码残片（IDAT 根因定位）
        if (fmt.contains("png")) {
            const PngChunkInfo png = probePngChunks(bytes);
            if (png.validSignature)
                qWarning("[StickerPaste][probe] png sig=ok chunks=[%s] acTL=%s(%u) fcTL=%d fdAT=%d trunc=%d",
                         png.summary.constData(), png.hasAcTL ? "yes" : "no",
                         png.acTlFrames, png.fcTlCount, png.fdAtCount, int(png.truncated));
            else
                qWarning("[StickerPaste][probe] png sig=bad bytes=0x%02x 0x%02x len=%d",
                         uchar(bytes.at(0)), uchar(bytes.at(1)), bytes.size());
        }
        return false;
    }

    if (outFrames) *outFrames = frames;
    if (outFormat) *outFormat = fmt;
    if (outSize)   *outSize = size;
    return true;
}

// GIF 帧数：多帧动画返回 >1，单帧/不可确定返回 <=1（QBuffer 上 QImageReader::imageCount）
static int gifFrameCount(const QByteArray& bytes)
{
    if (bytes.isEmpty()) return 0;
    QBuffer probe;
    probe.setData(bytes);
    probe.open(QIODevice::ReadOnly);
    QImageReader reader(&probe);
    if (!reader.canRead()) return 0;
    return reader.imageCount();
}

// TIFF 帧数：多页 TIFF 由 Qt tiff 插件按页读；imageCount 可能返回 -1/1 不可靠，
// 用「读帧→跳下一页」循环实测（GIF 走 gifFrameCount，不经此路径）。
static int tiffFrameCount(const QByteArray& bytes)
{
    if (bytes.isEmpty()) return 0;
    QBuffer probe;
    probe.setData(bytes);
    probe.open(QIODevice::ReadOnly);
    QImageReader reader(&probe);
    if (!reader.canRead()) return 0;
    int n = 0;
    while (n < 1024) {
        const QImage frame = reader.read();
        if (frame.isNull() || !frame.size().isValid()) break;
        ++n;
        if (!reader.jumpToNextImage()) break;
    }
    return n;
}

// QQ 磁盘文件去混淆：CloneWith 逆向出 QQNT marketface 为
// 「30 字节原样 + 20 字节 ^0xFF」的循环结构；emoji-recv 缓存疑似同构混淆。
// 产出三个变体供调用方 probe 验证（命中多帧动画才采用，正常图片绝不误伤）。
static QList<QByteArray> deobfuscateQQ(const QByteArray& in)
{
    const auto block = [&in](int keep, int xored) {
        QByteArray r = in;
        int pos = 0;
        const int n = r.size();
        while (pos < n) {
            const int kEnd = qMin(pos + keep, n);
            pos = kEnd;
            const int xEnd = qMin(pos + xored, n);
            for (int i = pos; i < xEnd; ++i)
                r[i] = char(uchar(r.at(i)) ^ 0xFF);
            pos = xEnd;
        }
        return r;
    };
    QList<QByteArray> out;
    out << block(30, 20);    // 变体A：保留30+异或20 循环（CloneWith 实测结构）
    out << block(20, 30);    // 变体B：对调块长
    QByteArray all = in;     // 变体C：全量 ^0xFF
    for (int i = 0; i < all.size(); ++i)
        all[i] = char(uchar(all.at(i)) ^ 0xFF);
    out << all;
    return out;
}

// 读本地图片候选：直读 probe 通过即返回；失败再试 QQ 去混淆变体
// （仅当解出的是多帧动画才采用），全程留诊断日志（消除静默失败）。
static bool loadLocalImageCandidate(const QString& path,
                                    QByteArray* outBytes,
                                    QByteArray* outFmt,
                                    QSize* outSize,
                                    int* outFrames)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("[StickerPaste] uri read fail path=%s", qPrintable(path));
        return false;
    }
    const QByteArray fb = f.readAll();
    QByteArray fmt;
    QSize s;
    int frames = 0;
    if (probeImageValidity(fb, &fmt, &s, &frames)) {
        *outBytes = fb; *outFmt = fmt; *outSize = s; *outFrames = frames;
        return true;
    }
    // 直读失败：留诊断（前 32 字节 hex + 长度），再试去混淆
    {
        QByteArray hex;
        for (int i = 0; i < qMin(fb.size(), 32); ++i)
            hex += QString("%1").arg(uchar(fb.at(i)), 2, 16, QLatin1Char('0')).toLatin1();
        qWarning("[StickerPaste] uri probe fail path=%s len=%lld head0=%s deobf-try",
                 qPrintable(path), (qint64)fb.size(), hex.constData());
    }
    const auto variants = deobfuscateQQ(fb);
    for (int vi = 0; vi < variants.size(); ++vi) {
        const QByteArray& v = variants.at(vi);
        QByteArray vf;
        QSize vs;
        int vfr = 0;
        if (probeImageValidity(v, &vf, &vs, &vfr)
                && (vf == "gif" || vf == "apng" || vf == "webp")
                && vfr > 1) {
            *outBytes = v; *outFmt = vf; *outSize = vs; *outFrames = vfr;
            qInfo("[StickerPaste] source=uri-deobf variant=%d path=%s fmt=%s size=%dx%d bytes=%lld frames=%d",
                  vi + 1, qPrintable(path), vf.constData(), vs.width(), vs.height(),
                  (qint64)v.size(), vfr);
            return true;
        }
    }
    qWarning("[StickerPaste] uri probe+deobf all fail path=%s", qPrintable(path));
    return false;
}

// 统一动画帧数：按 fmt 分派（gif 走 Qt imageCount；apng/webp 走 chunk 自检；tiff 走逐页实测）
static int imageAnimationFrames(const QByteArray& bytes, const QByteArray& fmt)
{
    if (fmt == "gif")  return gifFrameCount(bytes);
    if (fmt == "apng") return apngFrameCount(bytes);
    if (fmt == "webp") return webpFrameCount(bytes);
    if (fmt == "tif" || fmt == "tiff") return tiffFrameCount(bytes);
    return 0;
}

// 缩放自验证：缩放结果写临时文件保留，与原图对比格式+帧数，不匹配则 qWarning。
// 保原格式语义：GIF→GIF、APNG→APNG 应一致；WebP→APNG 属既有预期转换，豁免不误报。
static void verifyScaledResult(const QByteArray& srcRaw,
                               const QByteArray& scaledBytes,
                               const char* tag)
{
    if (srcRaw.isEmpty() || scaledBytes.isEmpty()) return;

    QByteArray srcFmt, scaleFmt;
    QSize srcSize, scaleSize;
    int srcFrames = 0, scaleFrames = 0;
    probeImageValidity(srcRaw, &srcFmt, &srcSize, &srcFrames);
    probeImageValidity(scaledBytes, &scaleFmt, &scaleSize, &scaleFrames);
    if (srcFrames == 0)
        srcFrames = imageAnimationFrames(srcRaw, srcFmt);
    if (scaleFrames == 0)
        scaleFrames = imageAnimationFrames(scaledBytes, scaleFmt);

    QTemporaryFile tmp(QDir::tempPath()
                       + QStringLiteral("/anystik_scale_verify_XXXXXX"));
    QString tmpPath;
    if (tmp.open()) {
        tmp.write(scaledBytes);
        tmp.flush();
        tmpPath = tmp.fileName();
    }

    const bool fmtConvert = (srcFmt == "webp" && scaleFmt == "apng");
    const bool fmtOk = (srcFmt == scaleFmt) || fmtConvert;
    const bool framesOk = (srcFrames == scaleFrames);
    if (!fmtOk || !framesOk) {
        qWarning("[StickerScale][%s] MISMATCH orig=fmt:%s frames:%d %dx%d "
                 "vs scaled=fmt:%s frames:%d %dx%d tmp=%s",
                 tag, srcFmt.constData(), srcFrames, srcSize.width(), srcSize.height(),
                 scaleFmt.constData(), scaleFrames, scaleSize.width(), scaleSize.height(),
                 qPrintable(tmpPath));
    } else {
        qInfo("[StickerScale][%s] verify ok fmt=%s frames=%d %dx%d->%dx%d",
              tag, scaleFmt.constData(), scaleFrames,
              srcSize.width(), srcSize.height(),
              scaleSize.width(), scaleSize.height());
    }
}

// ── APNG 组装（多页 TIFF → APNG 重编码用）───────────────────────────
static quint32 s_crcTable[256];

static void ensureCrcTable()
{
    static const bool done = []() {
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
            s_crcTable[n] = c;
        }
        return true;
    }();
    Q_UNUSED(done);
}

static quint32 pngCrc(const QByteArray& type, const QByteArray& data)
{
    ensureCrcTable();
    quint32 c = 0xffffffffu;
    const auto feed = [&c](char ch) {
        const unsigned x = unsigned(uchar(ch));
        c = s_crcTable[(c ^ x) & 0xff] ^ (c >> 8);
    };
    for (int i = 0; i < type.size(); ++i) feed(type.at(i));
    for (int i = 0; i < data.size(); ++i) feed(data.at(i));
    return c ^ 0xffffffffu;
}

static QByteArray pngChunk(const QByteArray& type, const QByteArray& data)
{
    if (type.size() != 4) return QByteArray();
    QByteArray out;
    out.reserve(12 + data.size());
    const quint32 len = quint32(data.size());
    out += char(len >> 24);
    out += char(len >> 16);
    out += char(len >> 8);
    out += char(len);
    out += type;
    out += data;
    const quint32 crc = pngCrc(type, data);
    out += char(crc >> 24);
    out += char(crc >> 16);
    out += char(crc >> 8);
    out += char(crc);
    return out;
}

// APNG 自检：chunk 结构（acTL 帧数 / fdAT 数）+ 首帧可解码
static bool apngSelfCheck(const QByteArray& png, int frames)
{
    const PngChunkInfo ver = probePngChunks(png);
    QByteArray vf;
    QSize vs;
    if (!ver.validSignature || ver.acTlFrames != quint32(frames)
        || ver.fdAtCount != frames - 1
        || !probeImageValidity(png, &vf, &vs)) {
        qWarning("[StickerScale] apng self-check fail acTL=%u fdAT=%d frames=%d",
                 ver.acTlFrames, ver.fdAtCount, frames);
        return false;
    }
    return true;
}

// 帧 QImage(RGBA8888) 列表 → 手拼 APNG（IHDR/acTL/fcTL/IDAT/fdAT/IEND）。
// zlib 走 qCompress（filter=none 逐行）；非空且 delayMs>0 时按帧延迟写 fcTL（cs/9）。
// 自检通过才返回；失败空（调用方保守落静态首帧）。
static QByteArray buildApngFromFrames(const QList<QImage>& frames,
                                      const QVector<int>& delayMs = QVector<int>())
{
    if (frames.size() < 2) return QByteArray();

    const int w = frames.first().width();
    const int h = frames.first().height();
    const auto be32 = [](quint32 v) {
        QByteArray b(4, '\0');
        b[0] = char(v >> 24); b[1] = char(v >> 16); b[2] = char(v >> 8); b[3] = char(v);
        return b;
    };
    const auto be16 = [](quint16 v) {
        QByteArray b(2, '\0');
        b[0] = char(v >> 8); b[1] = char(v);
        return b;
    };
    // 逐行 filter=none + zlib
    const auto scanlinesZ = [](const QImage& im) {
        QByteArray raw;
        const int bpl = im.width() * 4;
        raw.reserve(im.height() * (bpl + 1));
        for (int y = 0; y < im.height(); ++y) {
            raw += char(0);
            raw += QByteArray::fromRawData(
                reinterpret_cast<const char*>(im.constScanLine(y)), bpl);
        }
        return qCompress(raw, 6);
    };

    QByteArray png;
    {
        const char sig[8] = {char(0x89), 'P', 'N', 'G', '\r', '\n', char(0x1a), '\n'};
        png.append(sig, 8);
    }
    QByteArray ihdr;
    ihdr += be32(quint32(w));
    ihdr += be32(quint32(h));
    ihdr += char(8);                 // bit depth
    ihdr += char(6);                 // color type RGBA
    ihdr += char(0);                 // compression
    ihdr += char(0);                 // filter
    ihdr += char(0);                 // interlace
    png += pngChunk(QByteArrayLiteral("IHDR"), ihdr);

    QByteArray actl;
    actl += be32(quint32(frames.size()));
    actl += be32(0);                 // 无限循环
    png += pngChunk(QByteArrayLiteral("acTL"), actl);

    quint32 seq = 0;
    const bool hasDelay = delayMs.size() == frames.size();
    for (int i = 0; i < frames.size(); ++i) {
        const QImage& fr = frames.at(i);
        quint16 dnum = 1, dden = 10;           // 默认 100ms
        if (hasDelay) {
            int ms = delayMs.at(i);
            if (ms < 1) ms = 10;               // 0/非法延迟回落
            if (ms > 6553) ms = 6553;          // 帧延迟上限（cs 域能表达的倒数）
            dnum = quint16(qBound(1, (ms * 10 + 9) / 10, 6553)); // 四舍五入到百分秒
            dden = 100;
        }
        QByteArray fctl;
        fctl += be32(seq++);
        fctl += be32(quint32(fr.width()));
        fctl += be32(quint32(fr.height()));
        fctl += be32(0);             // x 偏移
        fctl += be32(0);             // y 偏移
        fctl += be16(dnum);
        fctl += be16(dden);
        fctl += char(0);             // dispose_op: none
        fctl += char(0);             // blend_op: source
        png += pngChunk(QByteArrayLiteral("fcTL"), fctl);

        if (i == 0) {
            png += pngChunk(QByteArrayLiteral("IDAT"), scanlinesZ(fr));
        } else {
            QByteArray fdat;
            fdat += be32(seq++);
            fdat += scanlinesZ(fr);
            png += pngChunk(QByteArrayLiteral("fdAT"), fdat);
        }
    }
    png += pngChunk(QByteArrayLiteral("IEND"), QByteArray());

    if (!apngSelfCheck(png, frames.size())) return QByteArray();
    qInfo("[StickerScale] apng ok frames=%d size=%dx%d bytes=%lld",
          frames.size(), w, h, (qint64)png.size());
    return png;
}

// 多页 TIFF → APNG 重编码：逐页 QImage(RGBA8888) → buildApngFromFrames。
// 失败返回空，调用方保守落静态首帧。
static QByteArray multipageTiffToApng(const QByteArray& tiffBytes)
{
    QBuffer probe;
    probe.setData(tiffBytes);
    probe.open(QIODevice::ReadOnly);
    QImageReader reader(&probe);
    reader.setAutoTransform(true);
    if (!reader.canRead()) return QByteArray();

    QList<QImage> frames;
    while (frames.size() < 1024) {
        const QImage frame = reader.read();
        if (frame.isNull() || !frame.size().isValid()) break;
        frames << frame.convertToFormat(QImage::Format_RGBA8888);
        if (!reader.jumpToNextImage()) break;
    }
    if (frames.size() < 2) return QByteArray();       // 单页 TIFF 不做重编码
    return buildApngFromFrames(frames);
}

// 通用多帧解码：GIF/APNG/WebP/TIFF 等，Qt 逐帧读取并保留每帧延迟(ms)。
// 上限 600 帧防内存爆炸；不足 2 帧返回空（静态图不走此路径）。
static bool decodeAllFrames(const QByteArray& bytes,
                            QList<QImage>* outFrames,
                            QVector<int>* outDelayMs = nullptr)
{
    if (bytes.isEmpty()) return false;
    QBuffer probe;
    probe.setData(bytes);
    probe.open(QIODevice::ReadOnly);
    QImageReader reader(&probe);
    reader.setAutoTransform(true);
    if (!reader.canRead()) return false;

    QList<QImage> frames;
    QVector<int> delays;
    const int kMaxFrames = 600;
    while (frames.size() < kMaxFrames) {
        const QImage frame = reader.read();
        if (frame.isNull() || !frame.size().isValid()) break;
        frames << frame.convertToFormat(QImage::Format_RGBA8888);
        delays << qMax(1, reader.nextImageDelay());
        if (!reader.jumpToNextImage()) break;
    }
    if (frames.size() < 2) return false;
    *outFrames = frames;
    if (outDelayMs) *outDelayMs = delays;
    return true;
}

// 缩放尺寸：round(src*scale)，任一边超过 4096 则等比缩至 4096（放大封顶）。
static QSize cappedScaledSize(const QSize& src, qreal scale)
{
    if (!src.isValid()) return QSize();
    constexpr int kMaxDim = 4096;
    qreal w = qRound(qreal(src.width()) * scale);
    qreal h = qRound(qreal(src.height()) * scale);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (qMax(w, h) > kMaxDim) {
        const qreal f = qreal(kMaxDim) / qMax(w, h);
        w = qRound(w * f);
        h = qRound(h * f);
    }
    return QSize(int(w), int(h));
}

// 帧列表 → GIF 字节（gif-h 编码，RGBA8888 自带量化+抖动，保留帧延迟）。
// 走临时文件再读回；probeImageValidity 自检通过才返回。
static QByteArray buildGifBytes(const QList<QImage>& frames,
                                const QVector<int>& delayMs = QVector<int>())
{
    if (frames.size() < 2) return QByteArray();

    QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/anystik_scaled_XXXXXX.gif"));
    if (!tmp.open()) return QByteArray();
    const QString path = tmp.fileName();
    tmp.close();                       // GifBegin 需要独占创建文件

    int delay = 10;
    if (delayMs.size() == frames.size()) {
        delay = delayMs.first();
        if (delay < 1) delay = 10;
    }

    const int w = frames.first().width();
    const int h = frames.first().height();
    GifWriter writer;
    if (!GifBegin(&writer, path.toUtf8().constData(), uint32_t(w), uint32_t(h),
                  uint32_t(delay), 8, true)) {
        return QByteArray();
    }

    bool ok = true;
    for (int i = 0; i < frames.size(); ++i) {
        const QImage& fr = frames.at(i);
        if (delayMs.size() == frames.size()) {
            int ms = delayMs.at(i);
            if (ms < 1) ms = 10;
            delay = ms;
        }
        ok = GifWriteFrame(&writer, fr.constBits(), uint32_t(w), uint32_t(h),
                           uint32_t(delay), 8, true);
        if (!ok) break;
    }
    GifEnd(&writer);
    if (!ok) return QByteArray();

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
    const QByteArray bytes = f.readAll();
    f.close();

    QByteArray vf;
    QSize vs;
    if (!probeImageValidity(bytes, &vf, &vs)) {
        qWarning("[StickerScale] gif self-check fail size=%d", bytes.size());
        return QByteArray();
    }
    qInfo("[StickerScale] gif ok frames=%d size=%dx%d bytes=%lld",
          frames.size(), w, h, (qint64)bytes.size());
    return bytes;
}

#if defined(Q_OS_MACOS)
// macOS 权威读取（默认/旧 native 两模式统一）：NSPasteboard 全 flavor 直读原始字节。
// Qt 预定义 UTI 表没有 GIF/PNG/APNG/WebP/JPEG（仅 public.tiff→application/x-qt-image）；
// 这里按魔数收集候选，优先级：多帧动画（GIF/APNG/WebP）→ 多页 TIFF→APNG →
// file-url 原文件多帧（Finder 粘贴=复制原文件）→ 单帧静态。
static void macCollectPasteboard(QByteArray& bytes, QString* srcType = nullptr)
{
    const auto cands = macPasteboardCollect();
    if (cands.isEmpty()) return;

    QList<const MacPasteCandidate*> anim;      // 多帧 GIF/APNG/WebP
    QList<const MacPasteCandidate*> statics;   // 单帧候选
    const MacPasteCandidate* tiffMulti = nullptr;
    QStringList fileUrls;

    for (const auto& c : cands) {
        if (c.isFileUrl) {
            fileUrls << c.filePath;
            continue;
        }
        QByteArray f;
        QSize s;
        int fr = 0;
        if (!probeImageValidity(c.data, &f, &s, &fr)) {
            qWarning("[StickerPaste] pb-cand probe fail type=%s len=%lld",
                     qPrintable(c.type), (qint64)c.data.size());
            continue;
        }
        const int frames = imageAnimationFrames(c.data, f);
        qInfo("[StickerPaste] pb-cand type=%s fmt=%s size=%dx%d bytes=%lld frames=%d",
              qPrintable(c.type), f.constData(), s.width(), s.height(),
              (qint64)c.data.size(), frames);
        if (frames > 1) {
            if (f == "tif" || f == "tiff") {
                if (!tiffMulti) tiffMulti = &c;
            } else {
                anim << &c;
            }
        } else {
            statics << &c;
        }
    }

    // 1) 多帧动画优先（GIF/APNG/WebP 原始字节直接可用）
    if (!anim.isEmpty()) {
        const MacPasteCandidate& c = *anim.first();
        bytes = c.data;
        QByteArray f;
        QSize s;
        probeImageValidity(bytes, &f, &s);
        qInfo("[StickerPaste] source=pb-collect-anim type=%s fmt=%s size=%dx%d bytes=%lld frames=%d backend=NSPasteboard",
              qPrintable(c.type), f.constData(), s.width(), s.height(),
              (qint64)bytes.size(), imageAnimationFrames(bytes, f));
        if (srcType) *srcType = c.type;
        return;
    }
    // 2) 多页 TIFF → APNG 重编码（Apple 生态动画载体）
    if (tiffMulti) {
        const QByteArray apng = multipageTiffToApng(tiffMulti->data);
        if (!apng.isEmpty()) {
            bytes = apng;
            qInfo("[StickerPaste] source=pb-tiff-apng type=%s bytes=%lld backend=NSPasteboard",
                  qPrintable(tiffMulti->type), (qint64)apng.size());
            if (srcType) *srcType = tiffMulti->type + QLatin1String("->apng");
            return;
        }
        qWarning("[StickerPaste] pb-tiff-apng convert fail, fallback static type=%s len=%lld",
                 qPrintable(tiffMulti->type), (qint64)tiffMulti->data.size());
        statics.prepend(tiffMulti);  // 保守：多页 TIFF 当静态首帧入库
    }
    // 3) file-url 原文件副本（直读失败自动试 QQ 去混淆）
    QByteArray fileStatic;
    for (const QString& p : fileUrls) {
        QByteArray fb;
        QByteArray f;
        QSize s;
        int fr = 0;
        if (!loadLocalImageCandidate(p, &fb, &f, &s, &fr)) continue;
        if (fr > 1 && (f == "gif" || f == "apng" || f == "webp")) {
            bytes = fb;
            qInfo("[StickerPaste] source=pb-file path=%s fmt=%s size=%dx%d bytes=%lld frames=%d backend=NSPasteboard",
                  qPrintable(p), f.constData(), s.width(), s.height(), (qint64)fb.size(), fr);
            if (srcType) *srcType = QStringLiteral("file:") + p;
            return;
        }
        if (fileStatic.isEmpty())
            fileStatic = fb;
    }
    // 4) 单帧静态：优先剪贴板静态字节，其次 file-url 静态
    if (!statics.isEmpty()) {
        const MacPasteCandidate& c = *statics.first();
        bytes = c.data;
        QByteArray f;
        QSize s;
        probeImageValidity(bytes, &f, &s);
        qInfo("[StickerPaste] source=pb-collect-static type=%s fmt=%s size=%dx%d bytes=%lld backend=NSPasteboard",
              qPrintable(c.type), f.constData(), s.width(), s.height(), (qint64)bytes.size());
        if (srcType) *srcType = c.type;
    } else if (!fileStatic.isEmpty()) {
        bytes = fileStatic;
        QByteArray f;
        QSize s;
        probeImageValidity(bytes, &f, &s);
        qInfo("[StickerPaste] source=pb-file-static path=%s fmt=%s size=%dx%d bytes=%lld backend=NSPasteboard",
              qPrintable(fileUrls.first()), f.constData(), s.width(), s.height(),
              (qint64)bytes.size());
        if (srcType) *srcType = QStringLiteral("file:") + fileUrls.first();
    }
}
#endif // Q_OS_MACOS

// 按标题复用或新建贴纸包，返回 packId；失败返回空串
static QString findOrCreatePack(StickerDbSyncInterface& db,
                                const QString& packTitle,
                                int packCount,
                                QString* errorOut)
{
    QString packId;
    const auto existingPacks = db.list_packs(1);
    for (const auto& p : existingPacks) {
        if (QString::fromUtf8(p.title.c_str()) == packTitle) {
            return QString::fromStdString(p.id);
        }
    }

    packId = QString("pack_%1").arg(QString(
        QCryptographicHash::hash(packTitle.toUtf8(),
            QCryptographicHash::Sha1).toHex().left(12)));
    StickerPackRow pack;
    pack.id = packId.toStdString();
    pack.title = packTitle.toUtf8().constData();
    pack.author = "";
    pack.position = packCount;
    if (!db.add_pack(pack)) {
        if (errorOut) *errorOut = QStringLiteral("add pack failed");
        return QString();
    }
    return packId;
}

bool StickerStore::importDirectory(const QString& dir, QString* errorOut)
{
    if (!ensureInit()) {
        if (errorOut) *errorOut = QStringLiteral("storage init failed");
        return false;
    }
    if (m_migrating) {
        if (errorOut) *errorOut = QStringLiteral("正在迁移，请稍候再导入");
        return false;
    }

    QDir root(dir);
    if (!root.exists()) {
        if (errorOut) *errorOut = QStringLiteral("directory not exists");
        return false;
    }

    auto& db = stickerDb();

    // 收集所有图片文件
    QVector<QString> files;
    scanRecursive(root, files);
    if (files.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("no image found");
        return false;
    }

    // 以目录名建立包（重复导入时复用已有包）
    QString packTitle = root.dirName();
    if (packTitle.isEmpty()) packTitle = QStringLiteral("sticker pack");

    // 一律复制进 base 下：base/packs/<标题>/。若源目录本身已在 base/packs 下
    // （如 Android 解压安装后调用），复制到自身会因目标已存在而跳过，幂等。
    const QString base = stickerBaseDir();
    const QString targetDir = base + QStringLiteral("/packs/") + packTitle;
    if (!QDir().mkpath(targetDir)) {
        if (errorOut) *errorOut = QStringLiteral("无法创建包目录");
        return false;
    }
    const QString rootAbs = root.absolutePath();

    db.begin_write_transaction();

    const QString packId = findOrCreatePack(db, packTitle,
        int(db.list_packs(1).size()), errorOut);
    if (packId.isEmpty()) {
        db.commit_transaction();
        return false;
    }

    int pos = 0;
    bool importedAny = false;
    for (const auto& file : files) {
        // 相对源根的相对子路径，保持目录层级复制到目标
        const QString rel = QDir(rootAbs).relativeFilePath(file);
        const QString dst = targetDir + QLatin1Char('/') + rel;
        if (!QFile::exists(dst)) {
            if (!QDir().mkpath(QFileInfo(dst).absolutePath())) {
                if (errorOut) *errorOut = QStringLiteral("无法创建包子目录");
                continue;
            }
            if (!QFile::copy(file, dst)) {
                if (errorOut) *errorOut = QStringLiteral("文件复制失败：")
                                          + file;
                continue;
            }
        }

        QFileInfo fi(dst);
        QImageReader reader(dst);
        reader.setAutoTransform(true);
        const QSizeF imgSize = reader.size();
        const qint64 fileBytes = fi.size();

        StickerRow row;
        row.id = fileIdFor(file).toStdString();
        row.pack_id = packId.toUtf8().constData();
        row.file_path = relativeToBase(dst).toStdString();
        row.emoji = "";
        row.width = int(imgSize.width());
        row.height = int(imgSize.height());
        row.size = int(fileBytes);
        row.last_used = 0;
        row.position = pos++;

        if (db.add_sticker(row)) {
            importedAny = true;
        }
    }

    db.commit_transaction();

    if (importedAny) {
        emit dataChanged();
    } else if (errorOut) {
        *errorOut = QStringLiteral("no sticker imported");
    }
    return importedAny;
}

// ── 粘贴添加：读系统剪贴板图片 → 存本地 → 归入「粘贴板」分组 ──

namespace {
// mac GIF 读取双方案（均编译进 mac 构建）：
//   ANYS_USE_MM_PASTEBOARD=1 → 旧 .mm(NSPasteboard) 直读;
//   否则（默认）→ 纯 C++ QUtiMimeConverter。其它平台恒 false。
bool useMmPasteboard()
{
#if defined(Q_OS_MACOS)
    static bool v = (qEnvironmentVariableIntValue("ANYS_USE_MM_PASTEBOARD") != 0);
    return v;
#else
    return false;
#endif
}
}

QString formatStickerMeta(const StickerMeta& meta)
{
    auto fmtSize = [](qint64 bytes) -> QString {
        if (bytes < 1024)
            return QString::number(bytes) + QStringLiteral(" B");
        if (bytes < 1024 * 1024)
            return QString::number(double(bytes) / 1024.0, 'f', 1)
                 + QStringLiteral(" KB");
        return QString::number(double(bytes) / (1024.0 * 1024.0), 'f', 2)
             + QStringLiteral(" MB");
    };

    QStringList lines;
    lines << QString::fromUtf8("类型: ") + meta.typeLabel;
    lines << QString::fromUtf8("大小: ") + fmtSize(meta.sizeBytes);
    lines << QString::fromUtf8("尺寸: %1 × %2")
                .arg(meta.width).arg(meta.height);
    lines << QString::fromUtf8("帧数: %1").arg(meta.frames);
    lines << QString::fromUtf8("更新时间: %1")
                .arg(meta.modified.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));
    return lines.join(QLatin1Char('\n'));
}

StickerMeta StickerStore::stickerMeta(const QString& filePath) const
{
    StickerMeta meta;
    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile())
        return meta;
    meta.sizeBytes = fi.size();
    meta.modified = fi.lastModified();

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return meta;
    const QByteArray raw = f.readAll();
    f.close();

    // 优先用 probeImageValidity（精确格式检测 + 首帧解码校验）
    QByteArray fmt;
    QSize size;
    int frames = 0;
    bool probed = probeImageValidity(raw, &fmt, &size, &frames);

    if (!probed) {
        // probe 失败：宽松回退，用 QMimeDatabase + QFileInfo suffix 推断
        QMimeDatabase mdb;
        const QMimeType st = mdb.mimeTypeForFile(fi);
        if (st.isValid() && st.name().startsWith(QLatin1String("image/"))) {
            meta.mime = st.name();
            meta.typeLabel = st.comment() + QStringLiteral(" (") + st.name() + QLatin1Char(')');
        } else {
            // 按扩展名兜底
            const QString ext = fi.suffix().toLower();
            if (!ext.isEmpty()) {
                meta.mime = QStringLiteral("image/%1").arg(ext);
                meta.typeLabel = ext.toUpper() + QStringLiteral(" (") + meta.mime + QLatin1Char(')');
            } else {
                meta.typeLabel = QStringLiteral("未知 (unknown)");
            }
        }

        // 尝试用 QImageReader 直接从磁盘读尺寸和帧数
        QImageReader reader(filePath);
        reader.setAutoTransform(true);
        if (reader.canRead()) {
            QSize sz = reader.size();
            if (sz.isValid()) {
                meta.width = sz.width();
                meta.height = sz.height();
            }
            const int cnt = reader.imageCount();
            if (cnt > 0)
                meta.frames = cnt;
            meta.animated = cnt > 1;
            // 用 reader 精化格式名（比 suffix 更准确）
            QByteArray rFmt = reader.format().trimmed().toLower();
            if (!rFmt.isEmpty() && meta.mime.isEmpty()) {
                meta.mime = QStringLiteral("image/%1").arg(QString::fromLatin1(rFmt));
                meta.typeLabel = rFmt.toUpper() + QStringLiteral(" (") + meta.mime + QLatin1Char(')');
            }
        }
        return meta;
    }

    // probe 成功：正常流程
    if (frames < 2)
        frames = imageAnimationFrames(raw, fmt);
    const QString lower = QString::fromLatin1(fmt).toLower();

    QString human = lower.toUpper();
    if (lower == QLatin1String("jpg") || lower == QLatin1String("jpeg"))
        human = QStringLiteral("JPEG");
    else if (lower == QLatin1String("apng"))
        human = QStringLiteral("APNG");
    else if (lower == QLatin1String("svgz"))
        human = QStringLiteral("SVGZ");

    QString mime = QStringLiteral("image/%1").arg(lower);
    QMimeDatabase mdb;
    const QMimeType st = mdb.mimeTypeForFile(fi);
    if (st.isValid() && st.name().startsWith(QLatin1String("image/")))
        mime = st.name();

    meta.typeLabel = human + QStringLiteral(" (") + mime + QLatin1Char(')');
    meta.mime = mime;
    meta.animated = frames > 1;
    meta.frames = frames;
    meta.width = size.width();
    meta.height = size.height();
    return meta;
}

bool StickerStore::pasteFromClipboard(QString* errorOut)
{
    if (!ensureInit()) {
        if (errorOut) *errorOut = QStringLiteral("storage init failed");
        return false;
    }
    if (m_migrating) {
        if (errorOut) *errorOut = QStringLiteral("正在迁移，请稍候再粘贴");
        return false;
    }

    QByteArray bytes;
    QString srcType;

#ifdef Q_OS_ANDROID
    // Java 读系统剪贴板图片字节（厂商剪贴板图片一般以 content:// uri 存放）
    QJniObject jbytes = QJniObject::callStaticObjectMethod(
        "io/fedlet/mobutil/ShareActivity", "readClipboardImageBytes",
        "(Landroid/content/Context;)[B",
        QNativeInterface::QAndroidApplication::context().object());
    if (jbytes.isValid()) {
        QJniEnvironment env;
        JNIEnv* je = env.jniEnv();
        const jbyteArray ja = static_cast<jbyteArray>(jbytes.object());
        if (je && ja) {
            const jsize len = je->GetArrayLength(ja);
            if (len > 0) {
                bytes.resize(int(len));
                je->GetByteArrayRegion(ja, 0, len,
                    reinterpret_cast<jbyte*>(bytes.data()));
                srcType = QStringLiteral("android-clip");
            }
        }
    }
#else
    // 优先取剪贴板原始图像字节（保留 GIF 动画）；失败回退位图编码 PNG。
    // 浏览器/文件管理器复制 GIF 通常以 image/gif MIME 提供原始字节。
#if defined(Q_OS_MACOS)
    if (!useMmPasteboard())
        ensureMacGifConverter();   // 先注册，再取 mime/打日志，保证 diagnostics 含 image/gif
#endif
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    if (mime)
        qInfo("[StickerPaste] mime formats: %s",
              mime->formats().join('|').toUtf8().constData());
#if defined(Q_OS_MACOS)
    // 只读诊断：核对源端原始文件引用（public.file-url⇔file:// 可回退 / public.url⇔https 源端限制）
    if (mime) {
        const QByteArray uriRaw = mime->data("text/uri-list");
        qInfo("[StickerPaste] text/uri-list=%s",
              uriRaw.trimmed().isEmpty() ? "<none>" : uriRaw.trimmed().constData());
        for (const QUrl& u : mime->urls())
            qInfo("[StickerPaste] uri[%s]%s",
                  u.isLocalFile() ? "file" : "remote",
                  u.toString().toUtf8().constData());
    }
#endif
    // 顺序：多帧动画优先（apng/webp/gif），静态兜底置后。
    // 同一剪贴板可有多个 representation（如 QQ：image/gif 单帧 + image/apng 多帧），
    // apng 排最前保证读到多帧原稿，与 Finder 保存 .png 的行为一致。
    // mac 上 Qt 通常不把 com.compuserve.gif/public.gif 映射为 image/gif，
    // 也不保证其原始 UTI 出现在 formats()（未注册的 UTI 不暴露）。
    // 此处按名 best-effort 取原始字节；mac 默认经 ensureMacGifConverter()(QUtiMimeConverter)
    // 权威读取，旧 macPasteboardData 直读随 ANYS_USE_MM_PASTEBOARD 运行时切换。
#if defined(Q_OS_MACOS)
    // 权威读取①（默认/旧 native 两模式统一）：NSPasteboard 全 flavor 直读原始字节。
    // Qt 预定义 UTI 表没有 GIF/PNG/APNG/WebP/JPEG（仅 public.tiff→application/x-qt-image），
    // 此处按魔数收集候选，优先多帧 / 多页 TIFF→APNG / file-url 原文件 / 单帧静态。
    macCollectPasteboard(bytes, &srcType);
#endif
    if (bytes.isEmpty()) {
    for (const char* fmt : {"image/apng", "image/webp",
                            "image/gif",
                            "com.compuserve.gif", "public.gif", "image/x-gif",
                            "image/png",
                            "image/jpeg", "image/tiff", "image/bmp",
                            "image/avif", "image/x-png"}) {
        QByteArray raw = mime ? mime->data(QLatin1String(fmt)) : QByteArray();
        if (raw.isEmpty()) continue;
#if defined(Q_OS_MACOS)
        // GIF 分支：无论有效与否都带上所用后端（Qt 方案 / mac native 方案）
        const bool gifBranch = qstrcmp(fmt, "image/gif") == 0
                || qstrcmp(fmt, "com.compuserve.gif") == 0
                || qstrcmp(fmt, "public.gif") == 0
                || qstrcmp(fmt, "image/x-gif") == 0;
#endif
        QByteArray f;
        QSize s;
        if (probeImageValidity(raw, &f, &s)) {
            bytes = raw;            // 保留原始格式（GIF/APNG 动画随之保留）
            srcType = QLatin1String(fmt);
#if defined(Q_OS_MACOS)
            if (gifBranch || f == "apng" || f == "webp")
                // frames= 供核对剪贴板动画是否已被源端重编码成单帧
                qInfo("[StickerPaste] source=mime type=%s fmt=%s size=%dx%d bytes=%d frames=%d backend=%s",
                      fmt, f.constData(), s.width(), s.height(), raw.size(),
                      imageAnimationFrames(raw, f),
                      useMmPasteboard() ? "NSPasteboard native (.mm)" : "QUtiMimeConverter (Qt)");
            else
#endif
                qInfo("[StickerPaste] source=mime type=%s fmt=%s size=%dx%d bytes=%d",
                      fmt, f.constData(), s.width(), s.height(), raw.size());
            break;
        }
#if defined(Q_OS_MACOS)
        if (gifBranch || qstrcmp(fmt, "image/apng") == 0 || qstrcmp(fmt, "image/webp") == 0)
            // 无效动画也显示后端与来源（多处候选时逐条出现）
            qInfo("[StickerPaste] source=mime type=%s bytes=%d backend=%s decode-failed",
                  fmt, raw.size(),
                  useMmPasteboard() ? "NSPasteboard native (.mm)" : "QUtiMimeConverter (Qt)");
#endif
    }
    }
    // 单帧动画→uri 原始多帧文件回退（mac 源端常把动画重编码成单帧塞进剪贴板，
    // 同时 text/uri-list 指向原始多帧文件；仅此病理触发，命中也只改读本地多帧文件）。
    // 同格式（GIF救GIF/APNG救APNG）或剪贴板为 GIF 时跨格式救回（QQ 场景：剪贴板单帧
    // GIF、uri 实为 APNG 原稿）。提取为 lambda：MIME 循环产物与 macpb 直读产物共用，
    // 文件读取走 loadLocalImageCandidate（直读失败自动试 QQ 去混淆）。
    auto rescueSingleFrameAnimation = [&srcType](QByteArray& raw, const QMimeData* md) -> void {
        if (raw.isEmpty()) return;
        QByteArray fmtG;
        QSize sizeG;
        int framesG = 0;
        if (!(probeImageValidity(raw, &fmtG, &sizeG, &framesG)
                && (fmtG == "gif" || fmtG == "apng") && framesG <= 1)) {
            return;
        }
        const QList<QUrl> urls = md ? md->urls() : QList<QUrl>();
        for (const QUrl& url : urls) {
            if (!url.isLocalFile()) continue;              // 只取本地文件
            const QString p = url.toLocalFile();
            if (p.isEmpty() || !QFileInfo::exists(p)) continue;
            if (!QFileInfo(p).isFile()) continue;
            QByteArray fb;
            QByteArray fmt2;
            QSize s2;
            int frames2 = 0;
            if (loadLocalImageCandidate(p, &fb, &fmt2, &s2, &frames2)
                    && (fmt2 == fmtG || fmtG == "gif")   // 同格式，或剪贴板为 GIF（跨 GIF↔APNG）
                    && (fmt2 == "gif" || fmt2 == "apng" || fmt2 == "webp")
                    && frames2 > 1                        // 目标是多帧动画
                    && s2 == sizeG) {                     // 同尺寸守卫，防误救无关文件
                raw = fb;
                srcType = QStringLiteral("uri-gif-fallback:") + p;
                qInfo("[StickerPaste] source=uri-gif-fallback path=%s clip=%s->file=%s size=%dx%d bytes=%lld frames=%d",
                      qPrintable(p), fmtG.constData(), fmt2.constData(),
                      s2.width(), s2.height(),
                      (qint64)fb.size(), frames2);
                break;
            }
        }
    };
    rescueSingleFrameAnimation(bytes, mime);
    if (bytes.isEmpty()) {
        // text/uri-list：文件管理器复制文件引用 → 读本地可读图片原始字节
        // （保各格式/动画；直读失败再试 QQ 去混淆）
        const QList<QUrl> urls = mime ? mime->urls() : QList<QUrl>();
        for (const QUrl& url : urls) {
            if (!url.isLocalFile()) continue;          // 只取本地文件
            const QString p = url.toLocalFile();
            if (p.isEmpty() || !QFileInfo::exists(p)) continue;
            if (!QFileInfo(p).isFile()) continue;
            QByteArray fb;
            QByteArray fmt2;
            QSize s2;
            int frames2 = 0;
            if (loadLocalImageCandidate(p, &fb, &fmt2, &s2, &frames2)) {
                bytes = fb;
                srcType = QStringLiteral("uri:") + p;
                qInfo("[StickerPaste] source=uri path=%s fmt=%s size=%dx%d bytes=%lld frames=%d",
                      qPrintable(p), fmt2.constData(), s2.width(), s2.height(),
                      (qint64)fb.size(), frames2);
                break;
            }
        }
    }
#ifdef Q_OS_MACOS
    if (useMmPasteboard()) {
        // 旧方案（运行时切回，ANYS_USE_MM_PASTEBOARD=1）：硬编码 UTI 直读兜底；
        // 全 flavor 枚举已由 macCollectPasteboard 在权威读取①完成，此处只补老路径。
        if (bytes.isEmpty()) {
            static const char* kMacTypes[] = {
                "com.compuserve.gif", "public.gif"
            };
            for (const char* t : kMacTypes) {
                QByteArray raw = macPasteboardData(t);
                if (raw.isEmpty()) continue;
                QByteArray f;
                QSize s;
                if (probeImageValidity(raw, &f, &s)) {
                    bytes = raw;
                    srcType = QLatin1String(t);
                    qInfo("[StickerPaste] source=macpb type=%s fmt=%s size=%dx%d bytes=%d frames=%d backend=NSPasteboard native (.mm)",
                          t, f.constData(), s.width(), s.height(), raw.size(),
                          gifFrameCount(raw));
                    break;
                }
                qInfo("[StickerPaste] source=macpb type=%s bytes=%d backend=NSPasteboard native (.mm) decode-failed",
                      t, raw.size());
            }
        }
        // macpb 产物同样可能为源端单帧重编码：补同一回退（修复旧路径漏网）
        rescueSingleFrameAnimation(bytes, mime);
    }
#endif
    if (bytes.isEmpty()) {
        const QImage img = QGuiApplication::clipboard()->image();
        if (img.isNull()) {
            if (errorOut) *errorOut = QStringLiteral("剪贴板中没有图片");
            return false;
        }
        qInfo("[StickerPaste] bitmap source qimage-format=%d", int(img.format()));

        // 自检修复：RGB555 等格式 encode 出的 PNG 可能无有效 IDAT（此前复现
        // chunks=[IHDR|pHYs] + libpng IDAT failure），先归一 ARGB32 再编码并 probe 自验；
        // 逐级降级格式，最后 BMP 兜底（Qt BMP 编码不含 zlib，必成）。
        const QImage::Format kFormats[] = {
            QImage::Format_ARGB32,
            QImage::Format_RGB32,
            QImage::Format_RGBA8888,
        };
        bool done = false;
        for (QImage::Format fmt : kFormats) {
            const QImage c = img.convertToFormat(fmt);
            QByteArray trial;
            QBuffer b(&trial);
            if (!b.open(QIODevice::WriteOnly)) continue;
            if (!c.save(&b, "PNG")) continue;
            QByteArray tf;
            QSize ts;
            if (probeImageValidity(trial, &tf, &ts)) {
                bytes = trial;
                srcType = QStringLiteral("bitmap");
                qInfo("[StickerPaste] source=bitmap fmt=png size=%dx%d bytes=%lld encode-ok qformat=%d",
                      c.width(), c.height(), (qint64)bytes.size(), int(fmt));
                done = true;
                break;
            }
            qWarning("[StickerPaste] bitmap png self-check fail qformat=%d", int(fmt));
        }
        if (!done) {
            const QImage c = img.convertToFormat(QImage::Format_ARGB32);
            srcType = QStringLiteral("bitmap");
            QBuffer b(&bytes);
            if (!b.open(QIODevice::WriteOnly) || !c.save(&b, "BMP")) {
                if (errorOut) *errorOut = QStringLiteral("图片编码失败");
                return false;
            }
            QByteArray tf;
            QSize ts;
            if (!probeImageValidity(bytes, &tf, &ts)) {
                if (errorOut) *errorOut = QStringLiteral("图片编码失败");
                return false;
            }
            qInfo("[StickerPaste] source=bitmap fmt=bmp size=%dx%d bytes=%lld bmp-fallback",
                  c.width(), c.height(), (qint64)bytes.size());
        }
    }
#endif

    if (bytes.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("剪贴板中没有图片");
        return false;
    }
    {
        QByteArray vf;
        QSize vs;
        const bool vok = probeImageValidity(bytes, &vf, &vs);
        const QByteArray srcUtf = srcType.isEmpty() ? QByteArrayLiteral("?")
                                                    : srcType.toUtf8();
        qInfo("[StickerPaste] verify src=%s final-fmt=%s size=%dx%d bytes=%lld frames=%d probe-ok=%d",
              srcUtf.constData(),
              vok ? vf.constData() : "?",
              vok ? vs.width() : 0, vok ? vs.height() : 0,
              (qint64)bytes.size(),
              vok ? imageAnimationFrames(bytes, vf) : 0, int(vok));
    }
    return importImageBytes(bytes, errorOut);
}

// ── 图片字节入库（桌面剪贴板 / Android 剪贴板 / Android 分享 共用）──
bool StickerStore::importImageBytes(const QByteArray& bytes, QString* errorOut)
{
    if (m_migrating) {
        if (errorOut) *errorOut = QStringLiteral("正在迁移，请稍候再添加");
        return false;
    }
    if (bytes.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("empty image data");
        return false;
    }

    // 探测真实格式与尺寸（统一入口：各来源共用，按内容定扩展名）
    QByteArray fmt;
    QSize imgSize;
    if (!probeImageValidity(bytes, &fmt, &imgSize)) {
        if (errorOut) *errorOut = QStringLiteral("图片解码失败");
        return false;
    }
    qInfo("[StickerPaste] import ok fmt=%s size=%dx%d bytes=%lld",
          fmt.constData(), imgSize.width(), imgSize.height(),
          (qint64)bytes.size());
    qInfo("[StickerPaste] import frames=%d", imageAnimationFrames(bytes, fmt));

    QString ext = QStringLiteral(".png");
    if (fmt == "jpeg" || fmt == "jpg") ext = QStringLiteral(".jpg");
    else if (fmt == "gif")  ext = QStringLiteral(".gif");
    // apng：刻意保留 .png —— 字节原样落盘、Qt 首帧可读、预览/位图兜底兼容
    else if (fmt == "webp") ext = QStringLiteral(".webp");
    else if (fmt == "bmp")  ext = QStringLiteral(".bmp");
    else if (fmt == "tif" || fmt == "tiff") ext = QStringLiteral(".tif");
    else if (fmt == "tga")  ext = QStringLiteral(".tga");
    else if (fmt == "xpm")  ext = QStringLiteral(".xpm");
    else if (fmt == "xbm")  ext = QStringLiteral(".xbm");
    else if (fmt == "ppm")  ext = QStringLiteral(".ppm");
    else if (fmt == "pbm")  ext = QStringLiteral(".pbm");
    else if (fmt == "pgm")  ext = QStringLiteral(".pgm");
    else if (fmt == "wbmp") ext = QStringLiteral(".wbmp");
    else if (fmt == "svg" || fmt == "svgz") ext = QStringLiteral(".svg");
    else if (fmt == "avif") ext = QStringLiteral(".avif");

    // 幂等 ID + 落盘路径
    const QString idHex = QString(QCryptographicHash::hash(
        bytes, QCryptographicHash::Sha1).toHex());
    const QString base = stickerBaseDir();

    QDir pasteDir(base + QStringLiteral("/pastes"));
    if (!pasteDir.exists() && !pasteDir.mkpath(".")) {
        if (errorOut) *errorOut = QStringLiteral("无法创建 pastes 目录");
        return false;
    }

    const QString filePath = pasteDir.filePath(idHex + ext);
    if (!QFile::exists(filePath)) {
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
            if (errorOut) *errorOut = QStringLiteral("图片保存失败");
            return false;
        }
    }

    auto& db = stickerDb();
    db.begin_write_transaction();

    const int packCount = int(db.list_packs(1).size());
    const QString packId = findOrCreatePack(db,
        QStringLiteral("粘贴板"), packCount, errorOut);
    if (packId.isEmpty()) {
        db.commit_transaction();
        return false;
    }

    StickerRow row;
    row.id = idHex.toStdString();
    row.pack_id = packId.toUtf8().constData();
    row.file_path = (QStringLiteral("pastes/") + idHex + ext).toStdString();
    row.emoji = "";
    row.width = imgSize.width();
    row.height = imgSize.height();
    row.size = int(bytes.size());
    row.last_used = 0;
    row.position = int(db.count_stickers(packId.toUtf8().constData()));

    const bool ok = db.add_sticker(row);
    db.commit_transaction();

    if (ok) {
        emit dataChanged();
    } else if (errorOut) {
        *errorOut = QStringLiteral("贴纸入库失败");
    }
    return ok;
}

bool StickerStore::renamePack(const QString& packId, const QString& newTitle)
{
    if (!ensureInit() || newTitle.trimmed().isEmpty()) {
        return false;
    }
    auto& db = Storage::instance().msgDb();
    SqliteStatement stmt = db.prepare(
        "UPDATE sticker_packs SET title=?1 WHERE id=?2");
    if (!stmt.isPrepared()) {
        return false;
    }
    QByteArray titleUtf8 = newTitle.trimmed().toUtf8();
    QByteArray packUtf8 = packId.toUtf8();
    if (!stmt.bind(1, titleUtf8.constData())) return false;
    if (!stmt.bind(2, packUtf8.constData())) return false;
    const bool ok = stmt.step();
    if (ok) emit dataChanged();
    return ok;
}

bool StickerStore::deletePack(const QString& packId)
{
    if (!ensureInit()) {
        return false;
    }
    const bool ok = stickerDb().delete_pack(packId.toUtf8().constData());
    // 子表情由外键策略级联清除
    if (ok) emit dataChanged();
    return ok;
}

bool StickerStore::deleteSticker(const QString& stickerId)
{
    if (!ensureInit()) {
        return false;
    }
    const bool ok = stickerDb().delete_sticker(stickerId.toUtf8().constData());
    if (ok) emit dataChanged();
    return ok;
}

void StickerStore::touchSticker(const QString& stickerId)
{
    if (!ensureInit()) {
        return;
    }
    stickerDb().touch_sticker(stickerId.toUtf8().constData(),
        QDateTime::currentSecsSinceEpoch());
}

bool StickerStore::shareStickerFile(const QString& filePath)
{
    if (filePath.isEmpty()) {
        return false;
    }
#ifdef Q_OS_ANDROID
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([filePath]() {
        QJniObject context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid()) return;
        QJniObject::callStaticMethod<void>(
            "io/fedlet/mobutil/ShareActivity", "shareLocalImage",
            "(Landroid/content/Context;Ljava/lang/String;)V",
            context.object(), QJniObject::fromString(filePath).object());
    });
    return true;
#else
    Q_UNUSED(filePath)
    return false;
#endif
}

// Desktop 剪贴板写动画字节：以 MIME 携带（保动画），附位图供不支持的应用回退。
// fmt 为 "gif"/"apng"；filePath 供 macOS public.file-url 别名引用。返回写入成功与否。
#ifndef Q_OS_ANDROID
static bool stashAnimationClipboard(const QByteArray& fmt,
                                    const QByteArray& bytes,
                                    const QString& filePath,
                                    const QImage& pngFallback)
{
    const QByteArray mimeType = (fmt == "gif")
            ? QByteArray("image/gif") : QByteArray("image/apng");
    QMimeData* mime = new QMimeData;
    mime->setData(mimeType, bytes);           // Qt 自回读：动画字节
#if defined(Q_OS_MACOS)
    if (fmt == "gif") {
        ensureMacGifConverter();            // 注册 GIF UTI 转换器
        mime->setData("public.gif", bytes);   // 别名 UTI（mac 原生兼容）
    } else {
        // APNG：macOS 未声明规范 UTI，写入自定义类型串（自回读走 image/apng）
        mime->setData("org.kde.anystik.apng", bytes);
        // public.png 别名：APNG 默认图像是合法独立 PNG，原生查看器可显示静态首帧
        QBuffer pb;
        pb.open(QIODevice::WriteOnly);
        if (pngFallback.save(&pb, "PNG"))
            mime->setData("public.png", pb.data());
    }
    if (!filePath.isEmpty())
        mime->setUrls({QUrl::fromLocalFile(filePath)}); // public.file-url：文件粘贴方取原始多帧
#endif
    mime->setImageData(pngFallback);        // PNG 位图回退（缩放结果）
    QGuiApplication::clipboard()->setMimeData(mime);
    qInfo("[StickerCopy] fmt=%s bytes=%d frames=%d",
          fmt.constData(), bytes.size(), imageAnimationFrames(bytes, fmt));
    return true;
}

// 帧缩放复制到剪贴板（Desktop）：逐帧 Smooth scaled → GIF/APNG/静态位图。
// scale>0；动画 GIF 源保 GIF（gif-h 重编码），其余动画源转 APNG，失败回退静态首帧。
static bool copyScaledFramesToClipboard(const QList<QImage>& frames,
                                        const QVector<int>& delayMs,
                                        const QByteArray& srcFmt,
                                        const QSize& targetSize,
                                        const QString& srcPath,
                                        const QByteArray& srcRaw)
{
    QList<QImage> scaled;
    scaled.reserve(frames.size());
    for (const QImage& fr : frames) {
        QImage s = fr.scaled(targetSize, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
        if (s.isNull()) return false;
        scaled << s.convertToFormat(QImage::Format_RGBA8888);
    }

    if (srcFmt == "gif") {
        const QByteArray gifBytes = buildGifBytes(scaled, delayMs);
        if (!gifBytes.isEmpty()) {
            verifyScaledResult(srcRaw, gifBytes, "desktop-gif");
            return stashAnimationClipboard("gif", gifBytes, srcPath, scaled.first());
        }
    } else {
        const QByteArray apng = buildApngFromFrames(scaled, delayMs);
        if (!apng.isEmpty()) {
            verifyScaledResult(srcRaw, apng, "desktop-apng");
            return stashAnimationClipboard("apng", apng, srcPath, scaled.first());
        }
    }

    // 重编码失败：保守落静态首帧位图
    if (!scaled.isEmpty()) {
        QGuiApplication::clipboard()->setImage(scaled.first());
        return !scaled.first().isNull();
    }
    return false;
}
#endif

bool StickerStore::copyStickerToClipboard(const QString& filePath)
{
    if (filePath.isEmpty()) {
        return false;
    }
#ifdef Q_OS_ANDROID
    showAndroidToast(QStringLiteral("已复制到剪贴板"));
    bool copied = false;
    auto future = QNativeInterface::QAndroidApplication::runOnAndroidMainThread(
        [&copied, filePath]() {
            QJniObject context = QNativeInterface::QAndroidApplication::context();
            if (!context.isValid()) return;
            QJniObject jpath = QJniObject::fromString(filePath);
            copied = QJniObject::callStaticMethod<jboolean>(
                "io/fedlet/mobutil/ShareActivity", "copyImageToClipboard",
                "(Landroid/content/Context;Ljava/lang/String;)Z",
                context.object(), jpath.object());
        });
    future.waitForFinished();
    if (!copied) {
        qWarning() << "[StickerStore] android copy to clipboard failed:" << filePath;
    }
#else
    // GIF/APNG：以 MIME 携带原始字节（保动画），附位图供不支持的应用回退
    QByteArray animFmt;
    {
        QFile pb(filePath);
        QByteArray pRaw;
        if (pb.open(QIODevice::ReadOnly)) pRaw = pb.readAll();
        QByteArray pFmt; QSize pSize; int pFrames = 0;
        if (probeImageValidity(pRaw, &pFmt, &pSize, &pFrames)
                && ((pFmt == "gif") || (pFmt == "apng" && pFrames > 1)))
            animFmt = pFmt;
    }
    if (!animFmt.isEmpty()) {
        QFile f(filePath);
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray raw = f.readAll();
            return stashAnimationClipboard(animFmt, raw, filePath, QImage(filePath));
        }
    }
    QImage img(filePath);
    if (img.isNull()) {
        qWarning() << "[StickerStore] failed to load image:" << filePath;
        return false;
    }
    QGuiApplication::clipboard()->setImage(img);
#endif
    return true;
}

#ifdef Q_OS_ANDROID
static bool storeScaledTmpThenCopy(const QImage& scaled)
{
    const QString tmpPath = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation) + QStringLiteral("/scaled.png");
    QFile out(tmpPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    scaled.save(&out, "PNG");
    out.close();

    showAndroidToast(QStringLiteral("已复制到剪贴板"));
    bool copied = false;
    auto future = QNativeInterface::QAndroidApplication::runOnAndroidMainThread(
        [&copied, tmpPath]() {
            QJniObject context = QNativeInterface::QAndroidApplication::context();
            if (!context.isValid()) return;
            QJniObject jpath = QJniObject::fromString(tmpPath);
            copied = QJniObject::callStaticMethod<jboolean>(
                "io/fedlet/mobutil/ShareActivity", "copyImageToClipboard",
                "(Landroid/content/Context;Ljava/lang/String;)Z",
                context.object(), jpath.object());
        });
    future.waitForFinished();
    if (!copied)
        qWarning() << "[StickerStore] android scaled static copy failed:" << tmpPath;
    return copied;
}
#endif

bool StickerStore::copyStickerScaledToClipboard(const QString& filePath, qreal scale)
{
    if (filePath.isEmpty() || scale <= 0.0) {
        return false;
    }
#ifdef Q_OS_ANDROID
    // 读取原图，逐帧缩放后写 AppLocalDataLocation 临时文件（扩展名按动画类型），
    // 复用 ShareActivity.copyImageToClipboard（按扩展名给 image/gif|png MIME）。
    QList<QImage> frames;
    QVector<int> delays;
    QByteArray srcFmt;
    QByteArray raw;
    {
        QFile pb(filePath);
        if (!pb.open(QIODevice::ReadOnly)) return false;
        raw = pb.readAll();
        QByteArray fmt; QSize size; int cnt = 0;
        probeImageValidity(raw, &fmt, &size, &cnt);
        srcFmt = fmt;
        decodeAllFrames(raw, &frames, &delays);
    }
    if (frames.size() < 2) {
        QImage img(filePath);
        if (img.isNull()) return false;
        img = img.scaled(cappedScaledSize(img.size(), scale), Qt::KeepAspectRatio,
                         Qt::SmoothTransformation);
        return storeScaledTmpThenCopy(img);
    }

    const QSize target = cappedScaledSize(frames.first().size(), scale);
    QList<QImage> scaled;
    scaled.reserve(frames.size());
    for (const QImage& fr : frames) {
        QImage s = fr.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (s.isNull()) return false;
        scaled << s.convertToFormat(QImage::Format_RGBA8888);
    }

    QString ext;
    QByteArray bytes;
    if (srcFmt == "gif") {
        bytes = buildGifBytes(scaled, delays);
        ext = QStringLiteral(".gif");
    } else {
        bytes = buildApngFromFrames(scaled, delays);
        ext = QStringLiteral(".png");
    }
    if (bytes.isEmpty() && !scaled.isEmpty()) {
        bytes = QByteArray();
        const QImage first = scaled.first();
        QBuffer wb;
        wb.open(QIODevice::WriteOnly);
        if (!first.save(&wb, "PNG")) return false;
        bytes = wb.data();
        ext = QStringLiteral(".png");
    }
    if (bytes.isEmpty()) return false;

    const QString tmpPath = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation) + QStringLiteral("/scaled") + ext;
    QFile out(tmpPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    out.write(bytes);
    out.close();

    verifyScaledResult(raw, bytes, "android-anim");

    showAndroidToast(QStringLiteral("已复制到剪贴板"));
    bool copied = false;
    auto future = QNativeInterface::QAndroidApplication::runOnAndroidMainThread(
        [&copied, tmpPath]() {
            QJniObject context = QNativeInterface::QAndroidApplication::context();
            if (!context.isValid()) return;
            QJniObject jpath = QJniObject::fromString(tmpPath);
            copied = QJniObject::callStaticMethod<jboolean>(
                "io/fedlet/mobutil/ShareActivity", "copyImageToClipboard",
                "(Landroid/content/Context;Ljava/lang/String;)Z",
                context.object(), jpath.object());
        });
    future.waitForFinished();
    if (!copied)
        qWarning() << "[StickerStore] android scaled copy failed:" << tmpPath;
    return copied;

#else
    QFile pb(filePath);
    if (!pb.open(QIODevice::ReadOnly)) return false;
    const QByteArray raw = pb.readAll();
    pb.close();

    QByteArray fmt; QSize origSize; int frameCount = 0;
    probeImageValidity(raw, &fmt, &origSize, &frameCount);
    const bool isAnim = (fmt == "gif")
            || (fmt == "apng" && frameCount > 1);

    QList<QImage> frames;
    QVector<int> delays;
    if (isAnim) {
        if (decodeAllFrames(raw, &frames, &delays))
            return copyScaledFramesToClipboard(frames, delays, fmt,
                                               cappedScaledSize(frames.first().size(), scale),
                                               filePath, raw);
        // 解码失败：回退为静态单帧
    }

    QImage img(filePath);
    if (img.isNull()) return false;
    img = img.scaled(cappedScaledSize(img.size(), scale), Qt::KeepAspectRatio,
                     Qt::SmoothTransformation);
    QGuiApplication::clipboard()->setImage(img);
    return true;
#endif
}

// ═══════════════════════════════════════════════════════════════════
// 下载包（自带地址）：probe / 断点续传 / 安装 / 元数据
// 磁盘仅留 <md5(url)16>.part；url/版本commit/MD5 持久化于 QSettings。
// ═══════════════════════════════════════════════════════════════════

static QString sanitizeToken(const QString& in)
{
    QString out = in;
    for (QChar& c : out) {
        const QChar ch = c;
        if (!(ch.isLetterOrNumber() || ch == QLatin1Char('.')
              || ch == QLatin1Char('_') || ch == QLatin1Char('-'))) {
            c = QLatin1Char('_');
        }
    }
    if (out.trimmed().isEmpty()) {
        out = QStringLiteral("pack");
    }
    return out;
}

static QString urlHex(const QString& url)
{
    return QString(QCryptographicHash::hash(url.toUtf8(),
        QCryptographicHash::Md5).toHex()).left(16);
}

// codeload: /<owner>/<repo>/zip/refs/heads/<branch>
static bool matchCodeloadRepo(const QString& url,
                              QString* owner, QString* repo, QString* branch)
{
    const QUrl u(url);
    if (u.host() != QLatin1String("codeload.github.com")) {
        return false;
    }
    const QStringList parts = u.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() == 6 && parts.at(2) == QLatin1String("zip")
        && parts.at(3) == QLatin1String("refs")
        && parts.at(4) == QLatin1String("heads")) {
        if (owner) *owner = parts.at(0);
        if (repo) *repo = parts.at(1);
        if (branch) *branch = parts.at(5);
        return true;
    }
    return false;
}

static QString urlDisplayName(const QString& url)
{
    QString owner, repo, branch;
    if (matchCodeloadRepo(url, &owner, &repo, &branch)) {
        return repo;
    }
    QUrl u(url);
    QString base = u.fileName();
    if (base.isEmpty()) {
        const QStringList parts = u.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (!parts.isEmpty()) base = parts.last();
    }
    if (base.isEmpty() || base.compare("main", Qt::CaseInsensitive) == 0
        || base.compare("master", Qt::CaseInsensitive) == 0
        || base.compare("head", Qt::CaseInsensitive) == 0) {
        base = QStringLiteral("pack");
    }
    return base;
}

static QVariantMap dlHint(const QString& url)
{
    return QSettings().value(
        QStringLiteral("dlProgress/") + urlHex(url)).toMap();
}

static void setDlHint(const QString& url, const QVariantMap& hint)
{
    QSettings().setValue(QStringLiteral("dlProgress/") + urlHex(url), hint);
}

// 内置下载源唯一表（唯一改源点）。approxSize 为预告约值，非运行时所得
// 如暂未获取到approxSize则-1
const BuiltinSource kBuiltinSources[] = {
    { "WhatsApp 官方示例贴纸 (SDK)",
      "https://codeload.github.com/WhatsApp/stickers/zip/refs/heads/main",
      13163057L },   // 8/30 selftest5 整包实测（约值，随 commit 变化）
    { "Animals (Telegram)",
      "https://raw.githubusercontent.com/kanelai/stickerapp/master/Animals.stickerpack",
      1088205L },    // 本会话 Range 206 实测 content-range: bytes 0-0/1088205
    { "LINE 贴纸包 (GitHub 镜像)",
      "https://raw.githubusercontent.com/porridgebrother/line-stickers/master/stickers.zip",
      1490697L },   // 本会话 Range 实测 content-range: bytes 0-0/1490697（raw 偶发超时,重试即可）
    { "ChineseBQB 梗图包",
      "https://raw.githubusercontent.com/zhaoolee/ChineseBQB/master/001Funny_%E6%BB%91%E7%A8%BD%E5%A4%A7%E4%BD%AC%F0%9F%98%8FBQB.zip",
      4691509L },   // 本会话 Range 实测 content-range: bytes 0-0/4691509（raw 源偶发超时,重试即可）
    { "LINE 贴纸 2938",
      "https://stickershop.line-scdn.net/stickershop/v1/product/2938/iphone/stickers@2x.zip",
      797156L },         // 真机 HEAD 实测 content-length（≈0.76 MB）
    { "LINE 动态 18060",
      "https://stickershop.line-scdn.net/stickershop/v1/product/18060/iphone/stickerpack@2x.zip",
      7246424L },         // 真机 HEAD 实测 content-length（≈6.9 MB）
};
const unsigned kBuiltinSourceCount =
    sizeof(kBuiltinSources) / sizeof(kBuiltinSources[0]);

static QByteArray fileMd5(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    QCryptographicHash hash(QCryptographicHash::Md5);
    QByteArray buf;
    buf.resize(64 * 1024);
    qint64 got = 0;
    while ((got = f.read(buf.data(), buf.size())) > 0) {
        hash.addData(QByteArrayView(buf.constData(), int(got)));
    }
    return hash.result();
}

QString StickerStore::downloadPartPath(const QString& url) const
{
    return stickerBaseDir() + QStringLiteral("/packs/.download/")
           + urlHex(url) + QStringLiteral(".part");
}

QNetworkRequest StickerStore::makeRequest(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setTransferTimeout(20000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "anystik/1.0");
    return req;
}

void StickerStore::ensureNam()
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
    }
}

void StickerStore::probeRemote(const QString& url)
{
    if (!ensureInit()) {
        emit probeDone(url, -1, QString(), QString(), false, "初始化失败");
        return;
    }
    ensureNam();

    QString owner, repo, branch;
    if (matchCodeloadRepo(url, &owner, &repo, &branch)) {
        // codeload zip 无 Content-Length，大小未知(以下载实计)；
        // 版本 = 分支最新 commit sha。
        const QString api = "https://api.github.com/repos/" + owner + "/"
                            + repo + "/commits/" + branch;
        auto* reply = m_nam->get(makeRequest(QUrl(api)));
        connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
            QString sha, err;
            bool ok = (reply->error() == QNetworkReply::NoError);
            if (ok) {
                const QJsonDocument doc =
                    QJsonDocument::fromJson(reply->readAll());
                sha = doc.object().value("sha").toString();
            } else {
                err = reply->errorString();
            }
            const QString ver = sha.isEmpty() ? QStringLiteral("未知")
                                              : QStringLiteral("commit ") + sha.left(7);
            auto hint = dlHint(url);
            hint.insert("version", ver);
            hint.insert("versionRaw", sha);
            setDlHint(url, hint);
            reply->deleteLater();
            emit probeDone(url, -1, ver, sha, ok, err);
        });
        return;
    }

    auto* reply = m_nam->head(makeRequest(QUrl(url)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        const bool ok = (reply->error() == QNetworkReply::NoError);
        QString err;
        qint64 size = -1;
        QString raw, ver;
        if (ok) {
            if (reply->hasRawHeader("Content-Length")) {
                size = reply->rawHeader("Content-Length").toLongLong();
            }
            raw = QString::fromLatin1(
                reply->rawHeader("ETag")).remove(QLatin1Char('"'));
            if (raw.isEmpty()) {
                raw = QString::fromLatin1(reply->rawHeader("Last-Modified"));
            }
        } else {
            err = reply->errorString();
        }
        ver = raw.isEmpty() ? QStringLiteral("未知") : raw;
        auto hint = dlHint(url);
        hint.insert("version", ver);
        hint.insert("versionRaw", raw);
        if (size >= 0)
            hint.insert("realSize", size);        // HEAD 带 Content-Length 的精确实测
        setDlHint(url, hint);
        reply->deleteLater();
        emit probeDone(url, size, ver, raw, ok, err);
    });
}

qint64 StickerStore::cachedRealSize(const QString& url) const
{
    return dlHint(url).value(QStringLiteral("realSize"), -1).toLongLong();
}

qint64 StickerStore::cachedApproxSize(const QString& url) const
{
    return dlHint(url).value(QStringLiteral("approxSize"), -1).toLongLong();
}

void StickerStore::seedBuiltinApproxSizes()
{
    for (unsigned i = 0; i < kBuiltinSourceCount; ++i) {
        const QString url = QString::fromUtf8(kBuiltinSources[i].url);
        auto hint = dlHint(url);
        if (hint.value(QStringLiteral("approxSize")).toLongLong() > 0)
            continue;                                  // 有效正值不动；-1/缺失则升级填入
        hint.insert(QStringLiteral("approxSize"), kBuiltinSources[i].approxSize);
        setDlHint(url, hint);
    }
}

void StickerStore::downloadPack(const QString& url)
{
    if (!ensureInit()) {
        emit downloadFinished(url, false, "初始化失败");
        return;
    }
    if (m_tasks.contains(url)) {
        return;
    }
    startDownload(url, /*noRange=*/false);
}

void StickerStore::cancelDownload(const QString& url)
{
    auto it = m_tasks.find(url);
    if (it == m_tasks.end() || (*it)->installing) {
        return;
    }
    (*it)->cancelled = true;
    if ((*it)->reply) {
        (*it)->reply->abort();
    }
}

bool StickerStore::hasPartialDownload(const QString& url) const
{
    return QFileInfo(downloadPartPath(url)).size() > 0;
}

void StickerStore::startDownload(const QString& url, bool noRange)
{
    ensureNam();
    const QString partPath = downloadPartPath(url);
    const qint64 partSize = QFileInfo(partPath).size();
    const qint64 offset = (noRange || partSize <= 0) ? 0 : partSize;

    auto* task = new DownloadTask;
    task->url = url;
    task->partPath = partPath;
    task->offset = offset;

    const QVariantMap hint = dlHint(url);
    task->total = hint.value("total", -1).toLongLong();
    if (task->total < 0) {
        task->total = -1;
    }
    task->name = hint.value("name").toString();
    if (task->name.isEmpty()) {
        task->name = urlDisplayName(url);
    }

    QDir().mkpath(QFileInfo(partPath).absolutePath());
    auto* file = new QFile(partPath);
    const QIODevice::OpenMode mode = (offset > 0)
        ? (QIODevice::Append | QIODevice::WriteOnly)
        : (QIODevice::WriteOnly | QIODevice::Truncate);
    if (!file->open(mode)) {
        delete task;
        delete file;
        emit downloadFinished(url, false, "无法写入下载目录");
        return;
    }
    task->out = file;

    QNetworkRequest req = makeRequest(QUrl(url));
    if (offset > 0) {
        req.setRawHeader("Range", "bytes=" + QByteArray::number(offset) + "-");
    }
    task->reply = m_nam->get(req);
    m_tasks.insert(url, task);

    connect(task->reply, &QNetworkReply::readyRead, this, [this, task]() {
        if (!task->out) {
            return;
        }
        const QByteArray chunk = task->reply->readAll();
        if (chunk.isEmpty()) {
            return;
        }
        if (task->out->write(chunk) != chunk.size()) {
            task->reply->abort();
        }
    });

    connect(task->reply, &QNetworkReply::downloadProgress, this,
            [this, task](qint64 done, qint64 total) {
        const qint64 overallDone = task->offset + done;
        qint64 overallTotal = -1;
        if (total > 0) {
            if (task->reply) {
                const QString cr = QString::fromLatin1(
                    task->reply->rawHeader("Content-Range"));
                const int slash = cr.lastIndexOf(QLatin1Char('/'));
                if (slash >= 0) {
                    overallTotal = cr.mid(slash + 1).toLongLong();
                }
            }
            if (overallTotal <= 0) {
                overallTotal = task->offset + total;
            }
        }
        task->total = overallTotal;
        auto hint = dlHint(task->url);
        hint.insert("total", overallTotal);
        hint.insert("name", task->name);
        setDlHint(task->url, hint);
        emit progressChanged(task->url, overallDone, overallTotal);
    });

    connect(task->reply, &QNetworkReply::finished, this,
            [this, task]() { handleDownloadFinished(task); });
}

void StickerStore::closeOut(DownloadTask* task)
{
    if (task->out) {
        task->out->close();
        delete task->out;
        task->out = nullptr;
    }
}

void StickerStore::handleDownloadFinished(DownloadTask* task)
{
    const QString url = task->url;
    QNetworkReply* reply = task->reply;
    const int status = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError err = reply->error();

    if (task->cancelled) {
        reply->deleteLater();
        task->reply = nullptr;
        closeOut(task);
        m_tasks.remove(url);
        delete task;
        emit downloadFinished(url, false, "已取消（进度已保留）");
        return;
    }

    if (err != QNetworkReply::NoError) {
        reply->deleteLater();
        task->reply = nullptr;
        closeOut(task);
        m_tasks.remove(url);
        const bool notFound = (err == QNetworkReply::ContentNotFoundError
                               || status == 404);
        delete task;
        emit downloadFinished(url, false,
            notFound ? "地址不存在(404)" : reply->errorString());
        return;
    }

    if (status == 206) {
        // A4：校验服务端实际续传的起始偏移；不从请求的 offset 开始则丢弃重下
        qint64 startByte = -1;
        const QString cr = QString::fromLatin1(reply->rawHeader("Content-Range"));
        const int sp = cr.indexOf(QLatin1Char(' '));
        int dash = cr.indexOf(QLatin1Char('-'), sp >= 0 ? sp : 0);
        if (sp >= 0 && dash > sp) {
            startByte = cr.mid(sp + 1, dash - sp - 1).toLongLong();
        }
        if (startByte >= 0 && startByte != task->offset) {
            reply->deleteLater();
            task->reply = nullptr;
            closeOut(task);
            QFile::remove(task->partPath);
            m_tasks.remove(url);
            delete task;
            startDownload(url, /*noRange=*/false);
            return;
        }
        reply->deleteLater();
        task->reply = nullptr;
        closeOut(task);
        finishIfComplete(task);
        return;
    }

    if (status == 200) {
        if (task->offset > 0) {
            // 服务端忽略 Range：完整内容已被 append 在旧 .part 之后 → 丢弃重下
            reply->deleteLater();
            task->reply = nullptr;
            closeOut(task);
            QFile::remove(task->partPath);
            m_tasks.remove(url);
            delete task;
            startDownload(url, /*noRange=*/true);
            return;
        }
        if (reply->hasRawHeader("Content-Length")) {
            task->total = reply->rawHeader("Content-Length").toLongLong();
        }
        reply->deleteLater();
        task->reply = nullptr;
        closeOut(task);
        finishIfComplete(task);
        return;
    }

    if (status == 416) {
        // 偏移失效，复位重下
        reply->deleteLater();
        task->reply = nullptr;
        closeOut(task);
        QFile::remove(task->partPath);
        m_tasks.remove(url);
        delete task;
        startDownload(url, /*noRange=*/false);
        return;
    }

    reply->deleteLater();
    task->reply = nullptr;
    closeOut(task);
    m_tasks.remove(url);
    delete task;
    emit downloadFinished(url, false, QStringLiteral("HTTP %1").arg(status));
}

void StickerStore::finishIfComplete(DownloadTask* task)
{
    const QString url = task->url;
    const qint64 fsize = QFileInfo(task->partPath).size();
    const bool complete = (task->total < 0) ? true : (fsize >= task->total);
    if (!complete) {
        m_tasks.remove(url);
        delete task;
        emit downloadFinished(url, false,
            QStringLiteral("下载未完整(%1/%2)，可继续")
                .arg(fsize).arg(task->total));
        return;
    }
    runInstall(task);
}

// ═══ 安装三段式（A1：解压/导入移至工作线程，避免 GUI 卡顿）═══

void StickerStore::runInstall(DownloadTask* task)
{
    task->installing = true;
    auto future = QtConcurrent::run([this, task]() {
        return runInstallWork(task);
    });
    future.then(this, [this, task](const InstallResult& r) {
        finalizeInstall(task, r);
    });
}

StickerStore::InstallResult StickerStore::runInstallWork(DownloadTask* task)
{
    const QString url = task->url;
    const QString partPath = task->partPath;
    const QString zipPath = partPath.left(partPath.size() - 5)
                            + QStringLiteral(".zip");
    const QString base = stickerBaseDir();

    auto failNow = [&](const QString& msg, bool removeZip) -> InstallResult {
        if (removeZip) QFile::remove(zipPath);
        InstallResult r;
        r.ok = false;
        r.message = msg;
        return r;
    };

    if (!QFile::rename(partPath, zipPath)) {
        return failNow(QStringLiteral("下载文件无法落盘"), false);
    }

    // MD5：全量重读 zip（跨续传会话也一致）
    const QByteArray md5 = fileMd5(zipPath);

    QZipReader zip(zipPath);
    if (!zip.exists() || !zip.isReadable()) {
        zip.close();
        return failNow(QStringLiteral("不是有效的贴纸包(zip)"), true);
    }

    const auto infoList = zip.fileInfoList();

    // B1 安全预扫：拒绝符号链接、绝对路径、父目录穿越、盘符冒号
    for (const auto& fi : infoList) {
        if (!fi.isValid()) continue;
        if (fi.isSymLink) {
            zip.close();
            return failNow(QStringLiteral("包内含符号链接，已停止"), true);
        }
        const QString p = fi.filePath;
        if (p.isEmpty()) continue;
        bool unsafe = p.startsWith(QLatin1Char('/'));
        const QStringList comps = p.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        for (const QString& c : comps) {
            if (c == QLatin1String("..") || c.contains(QLatin1Char(':'))) {
                unsafe = true;
                break;
            }
        }
        if (unsafe) {
            zip.close();
            return failNow(QStringLiteral("包内含不安全路径，已停止"), true);
        }
    }

    // 标题 = zip 顶层唯一目录名，否则 URL-derived 名
    QStringList tops;
    for (const auto& fi : infoList) {
        if (!fi.isValid()) continue;
        const int slash = fi.filePath.indexOf(QLatin1Char('/'));
        const QString first = slash < 0
            ? fi.filePath : fi.filePath.left(slash);
        if (!tops.contains(first)) tops.append(first);
    }
    QString title = (tops.size() == 1) ? tops.first() : QString();
    if (title.endsWith(QLatin1Char('/'))) {
        title.chop(1);
    }
    if (title.isEmpty()) {
        title = task->name;
    }
    title = sanitizeToken(title);

    const QString targetDir = base + QStringLiteral("/packs/") + title;
    if (QFile::exists(targetDir)) {
        if (!QDir(targetDir).removeRecursively()) {
            zip.close();
            return failNow(QStringLiteral("无法清理旧包目录"), true);
        }
    }
    if (!QDir().mkpath(targetDir)) {
        zip.close();
        return failNow(QStringLiteral("无法创建包目录"), true);
    }
    if (!zip.extractAll(targetDir)) {
        zip.close();
        return failNow(QStringLiteral("解压失败"), true);
    }
    zip.close();

    QString err;
    if (!importDirectory(targetDir, &err)) {
        return failNow(QStringLiteral("导入失败：") + (err.isEmpty()
                 ? QStringLiteral("无可用图片") : err), true);
    }

    // 取 packId：importDirectory 以标题复用/新建，标题==targetDir 名
    QString packId;
    for (const auto& p : stickerDb().list_packs(-1)) {
        if (QString::fromUtf8(p.title.c_str()) == title) {
            packId = QString::fromStdString(p.id);
            break;
        }
    }
    if (packId.isEmpty()) {
        return failNow(QStringLiteral("分组标识丢失"), true);
    }

    InstallResult r;
    r.ok = true;
    r.message = title;
    r.packId = packId;
    r.dir = targetDir;
    r.total = QFileInfo(zipPath).size();
    r.md5Hex = QString::fromLatin1(md5.toHex());

    // A2 内容变化检测：同源同包且 md5 变化 → 附加提示
    const QVariantMap oldMeta = QSettings().value(
        QStringLiteral("downloadedPackMeta/") + packId).toMap();
    const QString oldMd5 = oldMeta.value("md5").toString();
    if (!oldMd5.isEmpty() && oldMeta.value("url").toString() == url
            && oldMd5 != r.md5Hex) {
        r.note = QStringLiteral("（远端内容已变化，已覆盖安装）");
    }

    QFile::remove(zipPath);
    return r;
}

void StickerStore::finalizeInstall(DownloadTask* task, const InstallResult& r)
{
    const QString url = task->url;
    if (r.ok) {
        const QVariantMap hint = dlHint(url);
        QSettings settings;
        QVariantMap meta = settings.value(
            QStringLiteral("downloadedPackMeta/") + r.packId).toMap();
        meta.insert("url", url);
        meta.insert("name", r.message);
        meta.insert("total", r.total);
        meta.insert("version", hint.value("version", QStringLiteral("未知")));
        meta.insert("versionRaw", hint.value("versionRaw"));
        meta.insert("md5", r.md5Hex);
        meta.insert("dir", r.dir);
        meta.insert("dl_time", qint64(QDateTime::currentSecsSinceEpoch()));
        settings.setValue(QStringLiteral("downloadedPackMeta/") + r.packId, meta);

        QStringList list = settings.value(
            QStringLiteral("downloadedPacks")).toStringList();
        if (!list.contains(r.packId)) {
            list.append(r.packId);
            settings.setValue(QStringLiteral("downloadedPacks"), list);
        }
        settings.remove(QStringLiteral("dlProgress/") + urlHex(url));
    }

    m_tasks.remove(url);
    delete task;
    emit dataChanged();
    if (r.ok) {
        emit downloadFinished(url, true, r.message + r.note);
    } else {
        emit downloadFinished(url, false, r.message);
    }
}

// ── 下载包管理：启用/停用、卸载、彻底删除、占用大小、元数据 ──

bool StickerStore::setPackInstalled(const QString& packId, bool installed)
{
    if (!ensureInit()) {
        return false;
    }
    const bool ok = stickerDb().update_pack_installed(
        packId.toUtf8().constData(), installed ? 1 : 0);
    if (ok) {
        emit dataChanged();
    }
    return ok;
}

bool StickerStore::uninstallPack(const QString& packId, bool removeFiles)
{
    if (!ensureInit()) {
        return false;
    }
    QString dir;
    if (removeFiles) {
        dir = packMeta(packId).value("dir").toString();
    }
    const bool ok = stickerDb().delete_pack(packId.toUtf8().constData());
    if (ok) {
        if (removeFiles && !dir.isEmpty()) {
            QDir(dir).removeRecursively();
        }
        QSettings settings;
        QStringList list = settings.value(
            QStringLiteral("downloadedPacks")).toStringList();
        // 卸载（保留文件）也从已下载列表移除；元数据仅彻底删除时清除
        list.removeAll(packId);
        settings.setValue(QStringLiteral("downloadedPacks"), list);
        if (removeFiles) {
            settings.remove(QStringLiteral("downloadedPackMeta/") + packId);
        }
        emit dataChanged();
    }
    return ok;
}

void StickerStore::cleanupAbandonedDownloads(const QStringList& knownUrls)
{
    const QString dataDir = stickerBaseDir();
    if (dataDir.isEmpty()) {
        return;
    }
    QSet<QString> keep;
    for (const QString& u : knownUrls) {
        keep.insert(urlHex(u));
    }

    // 删除指纹不属于已知源的 *.part
    QDir dlDir(dataDir + QStringLiteral("/packs/.download"));
    if (dlDir.exists()) {
        const auto parts = dlDir.entryList(QStringList() << "*.part", QDir::Files);
        for (const QString& name : parts) {
            const QString key = name.left(name.size() - 5); // 去掉 .part
            if (!keep.contains(key)) {
                QFile::remove(dlDir.filePath(name));
            }
        }
    }

    // 清理不在已知源内的 dlProgress 死条目
    QSettings settings;
    settings.beginGroup(QStringLiteral("dlProgress"));
    const auto keys = settings.childKeys();
    for (const QString& k : keys) {
        if (!keep.contains(k)) {
            settings.remove(k);
        }
    }
    settings.endGroup();
}

qint64 StickerStore::packDiskSize(const QString& packId)
{
    if (!ensureInit()) {
        return 0;
    }
    qint64 total = 0;
    const auto rows = stickerDb().list_stickers(
        packId.toUtf8().constData(), "rowid DESC", 0, 0, 0, nullptr);
    for (const auto& row : rows) {
        total += row.size;
    }
    return total;
}

QVariantMap StickerStore::packMeta(const QString& packId) const
{
    return QSettings().value(
        QStringLiteral("downloadedPackMeta/") + packId).toMap();
}
