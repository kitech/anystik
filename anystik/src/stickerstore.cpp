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
            hint.insert("approxSize", size);      // HEAD 即有长度(LINE)
        setDlHint(url, hint);
        reply->deleteLater();
        emit probeDone(url, size, ver, raw, ok, err);
    });
}

qint64 StickerStore::cachedApproxSize(const QString& url) const
{
    return dlHint(url).value(QStringLiteral("approxSize"), -1).toLongLong();
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