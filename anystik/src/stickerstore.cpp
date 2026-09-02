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
#include <QSettings>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QtConcurrent/QtConcurrent>
#include <QtCore/private/qzipreader_p.h>
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

// 统一的图片来源有效性/尺寸探测：
// 拦截级：空 / 不可读 / 尺寸无效 / 格式不在白名单 / read() 解码失败 → 返回 false
// 观测级：过小 → 仅 qWarning
static bool probeImageValidity(const QByteArray& bytes,
                               QByteArray* outFormat, QSize* outSize)
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

    static const QSet<QByteArray> kAllowed = {
        "png","jpg","jpeg","gif","webp","bmp","tif","tiff","tga",
        "xpm","xbm","ppm","pbm","pgm","wbmp","svg","svgz","avif"
    };
    if (!kAllowed.contains(fmt)) {
        qWarning("[StickerPaste][probe] format not in whitelist fmt=%s",
                 fmt.constData());
        return false;
    }

    const QSize size = reader.size();
    if (!size.isValid()) {
        qWarning("[StickerPaste][probe] size invalid fmt=%s", fmt.constData());
        return false;
    }

    // 终验：完整解码首帧（只校验，不用于入库尺寸）
    if (!probe.seek(0)) {
        qWarning("[StickerPaste][probe] seek failed fmt=%s", fmt.constData());
        return false;
    }
    QImage first = reader.read();
    if (first.isNull() || !first.size().isValid()) {
        qWarning("[StickerPaste][probe] decode read failed fmt=%s", fmt.constData());
        return false;
    }

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
    // 顺序：动画优先（gif/原始 UTI/webp/apng），静态兜底置后。
    // mac 上 Qt 通常不把 com.compuserve.gif/public.gif 映射为 image/gif，
    // 也不保证其原始 UTI 出现在 formats()（未注册的 UTI 不暴露）。
    // 此处按名 best-effort 取原始字节；mac 默认经 ensureMacGifConverter()(QUtiMimeConverter)
    // 权威读取，旧 macPasteboardData 直读随 ANYS_USE_MM_PASTEBOARD 运行时切换。
    for (const char* fmt : {"image/gif",
                            "com.compuserve.gif", "public.gif", "image/x-gif",
                            "image/webp",
                            "image/apng", "image/png",
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
            bytes = raw;            // 保留原始格式（GIF 动画随之保留）
#if defined(Q_OS_MACOS)
            if (gifBranch)
                // frames= 供核对剪贴板 GIF 是否已被源端重编码成单帧
                qInfo("[StickerPaste] source=mime type=%s fmt=%s size=%dx%d bytes=%d frames=%d backend=%s",
                      fmt, f.constData(), s.width(), s.height(), raw.size(),
                      gifFrameCount(raw),
                      useMmPasteboard() ? "NSPasteboard native (.mm)" : "QUtiMimeConverter (Qt)");
            else
#endif
                qInfo("[StickerPaste] source=mime type=%s fmt=%s size=%dx%d bytes=%d",
                      fmt, f.constData(), s.width(), s.height(), raw.size());
            break;
        }
#if defined(Q_OS_MACOS)
        if (gifBranch)
            // 无效 GIF 也显示后端与来源（多处候选时逐条出现）
            qInfo("[StickerPaste] source=mime type=%s bytes=%d backend=%s decode-failed",
                  fmt, raw.size(),
                  useMmPasteboard() ? "NSPasteboard native (.mm)" : "QUtiMimeConverter (Qt)");
#endif
    }
    // 单帧 GIF→uri 原始多帧文件回退（mac 源端常把动画重编码成单帧塞进 image/gif，
    // 同时 text/uri-list 指向原始多帧文件；仅此病理触发，命中也只改读本地多帧 GIF）。
    // 提取为 lambda：MIME 循环产物与 macpb 直读产物两处字节落地后共用。
    auto rescueSingleFrameGif = [](QByteArray& raw, const QMimeData* md) -> void {
        if (raw.isEmpty()) return;
        QByteArray fmtG;
        QSize sizeG;
        if (!(probeImageValidity(raw, &fmtG, &sizeG) && fmtG == "gif"
                && gifFrameCount(raw) <= 1)) {
            return;
        }
        const QList<QUrl> urls = md ? md->urls() : QList<QUrl>();
        for (const QUrl& url : urls) {
            if (!url.isLocalFile()) continue;              // 只取本地文件
            const QString p = url.toLocalFile();
            if (p.isEmpty() || !QFileInfo::exists(p)) continue;
            if (!QFileInfo(p).isFile()) continue;
            if (QImageReader::imageFormat(p) != "gif") continue;  // 只救 GIF
            QFile f(p);
            if (f.open(QIODevice::ReadOnly)) {
                QByteArray fb = f.readAll();
                QByteArray fmt2;
                QSize s2;
                if (probeImageValidity(fb, &fmt2, &s2)
                        && gifFrameCount(fb) > 1) {        // 原始文件确为多帧
                    raw = fb;
                    qInfo("[StickerPaste] source=uri-gif-fallback path=%s fmt=%s size=%dx%d bytes=%lld frames=%d",
                          qPrintable(p), fmt2.constData(), s2.width(), s2.height(),
                          (qint64)fb.size(), gifFrameCount(fb));
                    break;
                }
            }
        }
    };
    rescueSingleFrameGif(bytes, mime);
    if (bytes.isEmpty()) {
        // text/uri-list：文件管理器复制文件引用 → 读本地可读图片原始字节（保各格式/动画）
        const QList<QUrl> urls = mime ? mime->urls() : QList<QUrl>();
        for (const QUrl& url : urls) {
            if (!url.isLocalFile()) continue;          // 只取本地文件
            const QString p = url.toLocalFile();
            if (p.isEmpty() || !QFileInfo::exists(p)) continue;
            if (!QFileInfo(p).isFile()) continue;
            if (QImageReader::imageFormat(p).isEmpty()) continue;  // 前置：非可读图片
            QFile f(p);
            if (f.open(QIODevice::ReadOnly)) {
                QByteArray fb = f.readAll();
                QByteArray fmt2;
                QSize s2;
                if (probeImageValidity(fb, &fmt2, &s2)) {
                    bytes = fb;
                    qInfo("[StickerPaste] source=uri path=%s fmt=%s size=%dx%d bytes=%lld",
                          qPrintable(p), fmt2.constData(), s2.width(), s2.height(),
                          (qint64)fb.size());
                    break;
                }
            }
        }
    }
#ifdef Q_OS_MACOS
    if (useMmPasteboard()) {
        // 旧方案（运行时切回，ANYS_USE_MM_PASTEBOARD=1）：直接查系统 NSPasteboard
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
                    qInfo("[StickerPaste] source=macpb type=%s fmt=%s size=%dx%d bytes=%d frames=%d backend=NSPasteboard native (.mm)",
                          t, f.constData(), s.width(), s.height(), raw.size(),
                          gifFrameCount(raw));
                    break;
                }
                qInfo("[StickerPaste] source=macpb type=%s bytes=%d backend=NSPasteboard native (.mm) decode-failed",
                      t, raw.size());
            }
        }
        // macpb 产物同样可能为源端单帧重编码：此处补同一回退（修复旧路径漏网）
        rescueSingleFrameGif(bytes, mime);
    } else {
        // 默认方案：转换器已在 MIME 循环前 ensureMacGifConverter() 注册，
        // image/gif 已由循环取出；此处 bytes 仍为空才顺延位图兜底。
    }
#endif
    if (bytes.isEmpty()) {
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
        qInfo("[StickerPaste] source=bitmap fmt=png size=%dx%d bytes=%lld",
              img.width(), img.height(), (qint64)bytes.size());
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

    QString ext = QStringLiteral(".png");
    if (fmt == "jpeg" || fmt == "jpg") ext = QStringLiteral(".jpg");
    else if (fmt == "gif")  ext = QStringLiteral(".gif");
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
    // GIF：以 MIME 携带原始字节（保动画），附位图供不支持 GIF 的应用回退
    if (QImageReader::imageFormat(filePath).toLower() == "gif") {
        QFile f(filePath);
        if (f.open(QIODevice::ReadOnly)) {
            QMimeData* mime = new QMimeData;
            mime->setData("image/gif", f.readAll());
            mime->setImageData(QImage(filePath));   // PNG 位图回退
            QGuiApplication::clipboard()->setMimeData(mime);
            return true;
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
    const QString dataDir = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    return dataDir + QStringLiteral("/packs/.download/")
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
    const QString dataDir = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);

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

    const QString targetDir = dataDir + QStringLiteral("/packs/") + title;
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
    const QString dataDir = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
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
