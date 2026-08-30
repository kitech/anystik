#include "stickerstore.h"
#include "storage.h"
#include "sticker_db.h"
#include "androidutils.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QBuffer>
#include <QFileInfo>
#include <QImageReader>
#include <QClipboard>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QDateTime>
#include <QMutex>
#include <QDebug>
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
        b.coverPath = QString::fromStdString(row.cover_path);
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
        b.filePath = QString::fromStdString(row.file_path);
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
        b.filePath = QString::fromStdString(row.file_path);
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
        b.filePath = QString::fromStdString(row.file_path);
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
        QFileInfo fi(file);
        QImageReader reader(file);
        reader.setAutoTransform(true);
        const QSizeF imgSize = reader.size();
        const qint64 fileBytes = fi.size();

        StickerRow row;
        row.id = fileIdFor(file).toStdString();
        row.pack_id = packId.toUtf8().constData();
        row.file_path = file.toStdString();
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
bool StickerStore::pasteFromClipboard(QString* errorOut)
{
    if (!ensureInit()) {
        if (errorOut) *errorOut = QStringLiteral("storage init failed");
        return false;
    }

    QByteArray bytes;

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
            }
        }
    }
#else
    const QImage img = QGuiApplication::clipboard()->image();
    if (img.isNull()) {
        if (errorOut) *errorOut = QStringLiteral("剪贴板中没有图片");
        return false;
    }

    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!img.save(&buffer, "PNG")) {
        if (errorOut) *errorOut = QStringLiteral("图片编码失败");
        return false;
    }
#endif

    if (bytes.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("剪贴板中没有图片");
        return false;
    }
    return importImageBytes(bytes, errorOut);
}

// ── 图片字节入库（桌面剪贴板 / Android 剪贴板 / Android 分享 共用）──
bool StickerStore::importImageBytes(const QByteArray& bytes, QString* errorOut)
{
    if (bytes.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("empty image data");
        return false;
    }

    // 探测真实格式（Android 分享/粘贴可能是 jpeg/gif/webp），按内容定扩展名
    QBuffer probe;
    probe.setData(bytes);
    probe.open(QIODevice::ReadOnly);
    QImageReader reader(&probe);
    reader.setAutoTransform(true);
    const QSize imgSize = reader.size();
    const QByteArray fmt = reader.format();
    probe.close();
    if (!imgSize.isValid()) {
        if (errorOut) *errorOut = QStringLiteral("图片解码失败");
        return false;
    }

    QString ext = QStringLiteral(".png");
    if (fmt == "jpeg" || fmt == "jpg") ext = QStringLiteral(".jpg");
    else if (fmt == "gif")  ext = QStringLiteral(".gif");
    else if (fmt == "webp") ext = QStringLiteral(".webp");

    // 幂等 ID + 落盘路径
    const QString idHex = QString(QCryptographicHash::hash(
        bytes, QCryptographicHash::Sha1).toHex());
    const QString dataDir = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);

    QDir pasteDir(dataDir + QStringLiteral("/pastes"));
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
    row.file_path = filePath.toStdString();
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
    QImage img(filePath);
    if (img.isNull()) {
        qWarning() << "[StickerStore] failed to load image:" << filePath;
        return false;
    }
    QGuiApplication::clipboard()->setImage(img);
#endif
    return true;
}