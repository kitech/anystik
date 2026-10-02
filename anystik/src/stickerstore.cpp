// ⚠ include 顺序约束，勿把这段下移：Qt3 把 QList/QVector 定义成**指针容器宏**
//（qptrlist.h:189 `#define QList QPtrList`、qptrvector.h:113
// `#define QVector QPtrVector`）。stikcommon 的值容器 shim 要 #undef 宏后补
// 同名值容器类，所以必须让这两个 Qt 头先被解析，再引 shim。
// 原因：stickerstore.h:104 就声明了 `QVector<StickerPackBrief> packs(...)`，
// 若该头在 shim 之前被解析，声明处的 QVector 会展开成 QPtrVector<StickerPackBrief>，
// 而定义处（shim 之后）却是 shim 的 QVector<StickerBrief> —— 两者是**不同类型**，
// 编译器报 "no declaration matches"。
//
// ⚠ 不要在这里裸写 `#include <qptrlist.h>` / `<qptrvector.h>`：那是 Qt3 专有头，
//   Qt6 侧不存在（会 fatal error: No such file or directory）。两个 shim 已在
//   各自的 QT3_BUILD 守卫内自己 include 它们（qlist_shim.h:23、
//   qvector_shim.h），顺序语义不变，且 Qt4+ 分支改为引入原生容器。
#include "qlist_shim.h"
#include "qvector_shim.h"

#include "stickerstore.h"
#include "eifreader.h"
#include "storage.h"
#include "sticker_db.h"
// androidutils.h 是 Android 专用（头内用大写 <QString>，Qt3 只认小写
// <qstring.h>，故 Qt3 桌面包含它会直接编译失败）。本文件用到它的 4 处符号
// （androidPicturesStickerBaseDir / showAndroidToast ×3）全在 Q_OS_ANDROID
// 守卫内，故按平台条件包含——Qt6 行为不变：桌面不定义 Q_OS_ANDROID，本来也
// 用不到这些函数；Android 下照常包含。
#ifdef Q_OS_ANDROID
#include "androidutils.h"
#endif
// macgifconverter.h / macpasteboard.h 是 macOS 专用（头内用大写 <QByteArray>/
// <QList>/<QString>，Qt3 只认小写头，包含即编译失败）。本文件用它们的 4 处调用
// （ensureMacGifConverter ×2、macCollectPasteboard、macPasteboardData）全在
// Q_OS_MACOS 块内，故按平台条件包含；Qt6 行为不变（非 macOS 本就只调空实现）。
#if defined(Q_OS_MACOS)
#include "macgifconverter.h"
#include "macpasteboard.h"
#endif
#ifdef QT3_BUILD
// Qt3 无 QtCore/ 这类模块目录，vendor qnaturalsort.h 的 `#include <QtCore>`
// 直接 fatal error；且 Qt3 无 Qt::CaseSensitivity / QChar::isLower()。
// 用 stikcommon 垫片（算法逐行照搬 vendor，见该头内差异清单）。
#include "qnaturalsort_shim.h"
#else
#include "qnaturalsort.h"
#endif

#include <algorithm>
#include <string.h>   // memset()（zlib z_stream 清零）；不靠 qba_shim.h 等传递引入
#ifdef QT3_BUILD
// Qt3 头文件名全小写、无驼峰别名（/opt/qt338sh/include 下 344 个 .h 全是
// qstring.h / qdir.h 这种形式），且以下类在 Qt3 全不存在，各自走 stikcommon
// 垫片。逐项对应关系（依据见各垫片头内注释）：
//   QStandardPaths        → Qt 5.0 引入    → qstandardpaths_shim.h
//   QDirIterator          → Qt 4.2 引入    → qdir_shim.h
//   QDir 静态/成员缺口    → tempPath(4.2) / cleanPath(4.2) /
//                          relativeFilePath(4.2) / removeRecursively(5.0) → qdir_shim.h
//   QImageReader          → Qt 4.0 引入    → qimagereader_shim.h
//   QClipboard/QMimeData/QGuiApplication → Qt3 只有 QApplication +
//                          QClipboard + QMimeSource              → qclipboard_shim.h
//   QJsonDocument/Object  → Qt 5.0 引入    → qjson_shim.h（底层 cJSON）
//   QMimeDatabase/QMimeType → Qt 5.0 引入  → qmimedatabase_shim.h
//   QNetworkAccessManager/Reply/Request → Qt 4.6 引入（Qt3 只有 QHttp） → qnam_shim.h
//   QByteArrayView        → Qt 5.10 引入   → qbytearrayview_shim.h
//   QList/QVector/QSet     → Qt3 是 QPtrList/QPtrVector/QMap 派生  → 对应 *_shim.h
//   QIODevice 枚举 IO_*    → Qt3 用宏而非枚举成员               → qglobaltype_shim.h
//   QDebug 运算符重载      → Qt3 只有 qDebug()                 → qdebug_shim.h
//   QDateTime::toString 带格式名                            → qdatetime_shim.h
//   QStringLiteral/QString::split 语义差异等                 → qstring_shim.h
// 保留 Qt3 原生小写头的：QDir / QFile / QBuffer / QFileInfo / QDateTime /
// QMutex / QSettings / QUrl / QPainter。
#include <qdir.h>
#include <qfile.h>
#include <qbuffer.h>
#include <qfileinfo.h>
#include <qdatetime.h>
#include <qmutex.h>
#include <qsettings.h>
#include <qurl.h>
#include <qpainter.h>
#include "qstandardpaths_shim.h"
#include "qdir_shim.h"
#include "qimagereader_shim.h"
#include "qclipboard_shim.h"
#include "qjson_shim.h"
#include "qmimedatabase_shim.h"
#include "qnam_shim.h"
#include "qfile_shim.h"
#include "qtemporaryfile_shim.h"
#include "qdatetime_shim.h"
#include "qdebug_shim.h"
#include "qset_shim.h"
#include "qlist_shim.h"
#include "qvector_shim.h"
#include "qbytearray_shim.h"
#include "qglobaltype_shim.h"
#include "qzlib_shim.h"
#include "qba_shim.h"
#include "qbytearrayview_shim.h"
#include "qcryptographichash_shim.h"
#include "qzipreader_shim.h"
#else
#include <QStandardPaths>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QBuffer>
#include <QFileInfo>
#include <QImageReader>
#include <QClipboard>
#include <QMimeData>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QMutex>
#include <QDebug>
// 跨版本自由函数垫片（qbaConstData / qOpenReadOnly / qOpenWriteOnly /
// qBufferSetData / qAbsPath / qMkpath / qToStdString / qFromStdString 等）。
// 这些头内部逐项带 QT_VERSION 守卫，Qt6 分支只取原生实现，故 Qt6 侧也安全
// 可包含 —— 必须包含，因为下面的函数体是 Qt3/Qt6 **共用**的一份代码，
// 不按版本分叉（分叉会掩盖行为漂移）。
//
// 教训：qToStdString/qFromStdString 最初被我放进 qstring_shim.h 的
// `#if QT_VERSION < 0x040000` 块**内部**，Qt6 下因而未定义；同名调用
// 在 Qt3 侧编译通过、Qt6 侧 "was not declared in this scope"，
// 而 Qt6 正式构建里这块 TU 编译较晚，Qt3 语法门禁完全看不到这个洞。
#include "qba_shim.h"
#include "qglobaltype_shim.h"
#include "qdir_shim.h"   // qDirTempPath/qDirCleanPath/qDirRemoveRecursively/qDirRelativeFilePath
#include "qfile_shim.h"  // qFileCopy/qFileRename/qFileInfoSuffix/qFileInfoCompleteBaseName/...
// 这三个与上面同样属于「Qt3/Qt6 共用代码」的垫片，头内各函数都带版本守卫，
// Qt6 分支取原生实现，缺包含就会报 "was not declared in this scope"：
//   qclipboard_shim.h  → qUrlToLocalFile（stickerstore 的剪贴板贴纸路径）
//   qdatetime_shim.h   → qDateTimeEpochSecs
//   qbytearray_shim.h  → qNumberToByteArray
#include "qclipboard_shim.h"
#include "qdatetime_shim.h"
#include "qbytearray_shim.h"
#include <QMimeDatabase>
#include <QMimeType>
// qJsonParseObject()：跨版本自由函数（Qt3 走本 shim 的 cJSON 实现，
// Qt4.5+ 转调原生 QJsonDocument）。头内部逐项带版本守卫，两版本都安全可含，
// 且必须包含 —— 下面的调用点是 Qt3/Qt6 共用的一份代码。
#include "qjson_shim.h"
#include <QSettings>
#include <QUrl>
#include <QSet>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QtConcurrent/QtConcurrent>
#include <QCryptographicHash>
#include <QByteArrayView>
#include <QTemporaryFile>
// QPainter/QFont：makeTgsPlaceholder() 用 QPainter 画占位图并读 p.font()。
// 重构 include 块时误删了 Qt6 分支的这行（Qt3 分支已由 <qpainter.h> 覆盖），
// 导致 Qt6 报 "QPainter has initializer but incomplete type"。QGuiApplication
// 只前向声明 QPainter/QFont，不拉入完整定义，故必须显式包含。
#include <QPainter>
#include <QFont>
#include <QtCore/private/qzipreader_p.h>
#endif
// qToUtf8BA()：跨版本 QString→UTF-8 QByteArray（Qt3 走 QCString 中转，Qt4+
// 即 toUtf8）。QCryptographicHash 的 5 处入参用它替代 Qt6 专有的 .toUtf8()。
// 本垫片在两个分支都定义 qToUtf8BA，Qt6 侧不定义任何宏，故可无条件包含。
#include "qstring_shim.h"
// qVariantMapValue()：Qt3 的 QMap 没有 Qt4.0 才加的 value()/value(key,def)，
// 而 QVariantMap 就是 QMap<QString,QVariant>（qvariant.h:87）。⚠ 必须放在
// 上面这个**共用区**（两个版本分支的 #endif 之后）—— 放进任一分支都会让
// 另一版本报 "qVariantMapValue was not declared in this scope"。
#include "qvariant_shim.h"
// Qt3 无 QImage 的 Format 概念（构造参数是 int depth）、无 scaled()、
// 无 Format_RGBA8888/Format_ARGB32_Premultiplied、Qt3 32 位图内存是 BGRA
// 而非 RGBA。qimage_shim.h 把这些差异收进自由函数，Qt6 分支转调原生 API，
// 故下面所有调用点写法两端一致、Qt6 行为不变。
#include "qimage_shim.h"
#include "../vendor/tangora_gif.h"
#include <zlib.h>
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
    if (!Storage::instance().init(qUtf8Printable(dataDir))) {
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
            m_stickerBaseDir = qDirTempPath();
    }
    return m_stickerBaseDir;
}

QString StickerStore::storageRootPath(StorageRoot r) const
{
    switch (r) {
    case StorageRoot::AppPrivate: {
        QString p = QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation);
        return p.isEmpty() ? qDirTempPath() : p;
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
    const QString cur = qDirCleanPath(stickerBaseDir());
    const QString pics = qDirCleanPath(storageRootPath(StorageRoot::Pictures));
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
        return abs.mid(base.length() + 1);
    // 不在 base 下 → 原样（如外部绝对路径）
    return abs;
}

void StickerStore::cleanupMigrationSource(const QString& fromRoot)
{
    if (fromRoot.isEmpty())
        return;
    // 只删旧 base 下的贴纸文件目录（packs/ 包、pastes/ 散图）；
    // 绝不动 fromRoot 根下 cache.db/message.db/cache_fs 等非贴纸数据。
    // 不用 {...} 初始化列表：Qt3 的 QStringList（QValueList<QString> 子类）
    // 没有 initializer_list 构造，会报 "could not convert from
    // <brace-enclosed initializer list>"。qStringListBuild 走 magic static。
    const QStringList subDirs =
            qStringListBuild(QStringLiteral("packs"), QStringLiteral("pastes"));
    for (const QString& sub : subDirs) {
        const QString p = fromRoot + QLatin1Char('/') + sub;
        QDir d(p);
        if (d.exists()) {
            // best-effort：失败仅残留旧副本，不影响迁移成功的正确性
            if (!qDirRemoveRecursively(QDir(d.path())))
                qWarning("[StickerStore] 清理旧目录失败: %s",
                         qPrintable(p));
        }
    }
}

// ── CRC64（ECMA-182）用于迁移时文件完整性校验 ──────────────────────
static quint64 s_crc64Table[256];

static void ensureCrc64Table()
{
    static const bool done = []() {
        for (quint64 n = 0; n < 256; ++n) {
            quint64 c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0x95ac9329ac4bc9b5ULL ^ (c >> 1)) : (c >> 1);
            s_crc64Table[n] = c;
        }
        return true;
    }();
    Q_UNUSED(done);
}

static quint64 crc64File(const QString& path)
{
    QFile f(path);
    if (!qOpenReadOnly(f))
        return 0;
    ensureCrc64Table();
    quint64 crc = 0xFFFFFFFFFFFFFFFFULL;
    char buf[65536];
    while (!f.atEnd()) {
        const qint64 n = qIODeviceRead(f, buf, sizeof(buf));
        if (n <= 0) break;
        for (qint64 i = 0; i < n; ++i)
            crc = s_crc64Table[(crc ^ uchar(buf[i])) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFFFFFFFFFULL;
}

namespace {
// 迁移结果（阶段一 工作线程产生，阶段二 GUI 线程消费）
struct MigrationResult {
    bool ok = false;
    bool sameRoot = false;            // 起止根相同（无需迁移）
    QString failDetail;               // 失败原因（中文）
    int totalFiles = 0;               // 计划迁移的文件数（进度分母）
    int copiedFiles = 0;              // 实际执行拷贝的文件数
    int skippedFiles = 0;             // CRC64+大小校验一致跳过的文件数
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

    const QString fromRoot = qDirCleanPath(stickerBaseDir());
    const QString toRoot = qDirCleanPath(storageRootPath(target));
    if (toRoot.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("无法确定目标目录");
        return false;
    }
    if (fromRoot == toRoot) {
        // 目标即当前根：直接持久化该根并返回成功
        QSettings().setValue(QStringLiteral("storageRoot"), toRoot);
        return true;
    }
    if (!qMkpath(toRoot)) {
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
                    const QString rel = stored.mid(fromRoot.length() + 1);
                    const QString to = toRoot + QLatin1Char('/') + rel;
                    if (!seenTo.contains(to)) {
                        seenTo.insert(to);
                        plan.append({QString(), to, rel, 1}); // from 由规则推导
                    }
                    r.relMoved.append(rel);
                } else if (stored.startsWith(toRoot + QLatin1Char('/'))) {
                    const QString rel = stored.mid(toRoot.length() + 1);
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
                r.skippedFiles++;
                emit migrationProgress(done, r.totalFiles, r.copiedBytes,
                                       r.copiedFiles, r.skippedFiles, p.to);
                continue;
            }
            if (QFile::exists(to)) {
                // 目标已存在 → CRC64 + 大小校验
                QFileInfo toInfo(to);
                QFileInfo fromInfo(from);
                if (toInfo.size() == fromInfo.size()
                    && crc64File(to) == crc64File(from)) {
                    // 完全一致，跳过
                    r.skippedFiles++;
                    emit migrationProgress(done, r.totalFiles, r.copiedBytes,
                                           r.copiedFiles, r.skippedFiles, to);
                    continue;
                }
                // 不一致 → 删除旧文件，重新拷贝
                QFile::remove(to);
            }
            if (!qMkpath(qAbsPath(QFileInfo(to)))) {
                r.failDetail = QStringLiteral("无法创建子目录：")
                               + qFileInfoAbsolutePath(QFileInfo(to));
                failed = true; break;
            }
            if (!qFileCopy(from, to)) {
                r.failDetail = QStringLiteral("复制失败：")
                               + from + QStringLiteral(" → ") + to;
                failed = true; break;
            }
            r.created.append(to);
            r.copiedFiles++;
            r.copiedBytes += QFileInfo(to).size();
            emit migrationProgress(done, r.totalFiles, r.copiedBytes,
                                   r.copiedFiles, r.skippedFiles, to);
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
                    upd = stored.mid(toRoot.length() + 1);
                } else if (stored.startsWith(QLatin1Char('/'))) {
                    // 其他绝对（外部/旧）→ 保留不动
                    continue;
                } else {
                    // 已是相对 → 不变
                    continue;
                }
                SqliteStatement up = db.prepare(
                    "UPDATE stickers SET file_path=?1 WHERE rowid=?2");
                if (!up.isPrepared() || !up.bind(1, qUtf8Printable(upd))
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

        // 迁移完全成功（DB 已转、base 已切）后清理旧 base 的贴纸文件，避免两份拷贝。
        // 只删 packs/、pastes/；绝不动 fromRoot 根下 cache.db/message.db/cache_fs 等。
        cleanupMigrationSource(fromRoot);

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
        b.id = qFromStdString(row.id);
        b.title = QString::fromUtf8(row.title.c_str());
        b.author = QString::fromUtf8(row.author.c_str());
        b.coverPath = resolveStickerPath(qFromStdString(row.cover_path));
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
    auto rows = stickerDb().list_stickers(qUtf8Printable(packId),
                                          orderby, limit, offset, deleted, emoji);
    for (const auto& row : rows) {
        StickerBrief b;
        b.id = qFromStdString(row.id);
        b.packId = qFromStdString(row.pack_id);
        b.filePath = resolveStickerPath(qFromStdString(row.file_path));
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
        b.id = qFromStdString(row.id);
        b.packId = qFromStdString(row.pack_id);
        b.filePath = resolveStickerPath(qFromStdString(row.file_path));
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
    auto rows = stickerDb().search_stickers(qUtf8Printable(query));
    for (const auto& row : rows) {
        StickerBrief b;
        b.id = qFromStdString(row.id);
        b.packId = qFromStdString(row.pack_id);
        b.filePath = resolveStickerPath(qFromStdString(row.file_path));
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
        packId.isEmpty() ? nullptr : qUtf8Printable(packId));
}

// ── 目录导入：每个子目录 = 一个贴纸包，支持图片递归 ──
static bool isSupportedImage(const QString& suffix)
{
    // ⚠ 用 fromLatin1 而非 fromAscii：后者 Qt5 起弃用、Qt6 已移除，而
    //   fromLatin1 在 Qt3（qstring.h:660）与 Qt6 都在；对纯 ASCII 字面量两者等价。
    static const QStringList exts = qStringListBuild(
        QString::fromLatin1("png"),  QString::fromLatin1("jpg"),
        QString::fromLatin1("jpeg"), QString::fromLatin1("gif"),
        QString::fromLatin1("webp"), QString::fromLatin1("bmp"),
        QString::fromLatin1("svg"),  QString::fromLatin1("tgs"));
    return exts.contains(qToLower(suffix));
}

// ── TGS 支持：Telegram 官方动图贴纸是 gzip 压缩的 Lottie JSON ──
// 本应用不内置 Lottie 渲染，导入时解压读取画布尺寸，用占位图入库。
static bool gunzipTgs(const QByteArray& in, QByteArray* raw)
{
    if (raw) qbaClear(*raw);
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, 15 + 16) != Z_OK) return false;   // 31: 自动识别 gzip/zlib
    zs.next_in  = reinterpret_cast<Bytef*>(const_cast<char*>(qbaConstData(in)));
    zs.avail_in = uInt(in.size());
    char buf[65536];
    int ret = Z_OK;
    do {
        zs.next_out = reinterpret_cast<Bytef*>(buf);
        zs.avail_out = sizeof(buf);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&zs);
            return false;
        }
        if (raw) qbaAppend(*raw, buf, (int)(sizeof(buf) - zs.avail_out));
    } while (ret != Z_STREAM_END);
    inflateEnd(&zs);
    return true;
}

static bool parseTgsSize(const QByteArray& raw, int* w, int* h)
{
    bool parsed = false;
    const QJsonObject o = qJsonParseObject(raw, &parsed);
    if (!parsed) return false;
    if (!o.contains("w") || !o.contains("h")) return false;
    const QJsonValue wv = o.value("w"), hv = o.value("h");
    if (!wv.isDouble() || !hv.isDouble()) return false;
    const int wi = int(wv.toDouble()), hi = int(hv.toDouble());
    if (wi <= 0 || hi <= 0 || wi > 8192 || hi > 8192) return false;
    if (w) *w = wi;
    if (h) *h = hi;
    return true;
}

// TGS 占位图（文件名缺预览图时显示）。
//
// Qt3 分支不用 QPainter：Qt3 的 QImage **不继承 QPaintDevice**（qimage.h:68
// `class Q_EXPORT QImage` 无基类；Qt4 起才继承），故 QPainter 无法以 QImage
// 为画布（实测 "no matching function for call to ‘QPainter::QPainter(QImage*)’"）。
// 中转 QPixmap 也不行：Qt3 只有 QPixmap(const QImage&) 单向构造
// （qpixmap.h:68），**没有** QPixmap→QImage 的反向转换，画完拿不回 QImage。
// 故 Qt3 下改为 setPixel 手绘：圆角矩形边框 + 中心一条横杠代替文字。
// 这是**有意的视觉降级**（无抗锯齿、无文字），仅影响 tgs 缺预览时的占位
// 缩略图，不影响任何数据路径；Qt6 分支仍是 QPainter 画完整带文字版本。
#if QT_VERSION >= 0x040000
static QImage makeTgsPlaceholder(const QString& name, int w, int h)
{
    QImage img = qImageNew32(w, h);
    img.fill(Qt::transparent);
    QPainter p(&img);
    qPainterSetAntialiasing(p);
    p.setBrush(qColorRgba(90, 140, 220, 90));
    p.setPen(Qt::NoPen);
    qPainterDrawRoundedRect(p, QRect(0, 0, w, h), w * 0.04, h * 0.04);
    QFont f = p.font();
    f.setPixelSize(qMax(12, qMin(w, h) / 8));
    p.setFont(f);
    p.setPen(qColorRgba(255, 255, 255, 230));
    p.drawText(QRect(0, 0, w, h), Qt::AlignCenter,
               name.isEmpty() ? QStringLiteral("TGS 动图") : name);
    p.end();
    return img;
}
#else
static QImage makeTgsPlaceholder(const QString& name, int w, int h)
{
    Q_UNUSED(name);
    QImage img = qImageNew32(w, h);
    qImageFillTransparent(img);
    // 圆角边框：半径取 w*0.04，与 Qt6 分支 qPainterDrawRoundedRect 的
    // rx = w*0.04 保持一致的观感；边框粗细取 max(1, w/64)。
    const int r = qMax(1, int(w * 0.04));
    const int bw = qMax(1, w / 64);
    const QRgb edge = qRgba(90, 140, 220, 90);
    const QRgb dash = qRgba(255, 255, 255, 230);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // 圆角判定：四角切掉 r×r 的方块（切角内不画）
            int cx = 0, cy = 0;
            if (x < r)            cx = r - x;
            else if (x >= w - r)  cx = x - (w - r - 1);
            if (y < r)            cy = r - y;
            else if (y >= h - r)  cy = y - (h - r - 1);
            if (cx > 0 && cy > 0 && cx * cx + cy * cy > r * r) continue;
            const bool onEdge = (x < bw || x >= w - bw || y < bw || y >= h - bw);
            if (onEdge) {
                img.setPixel(x, y, edge);
            } else {
                // 中心横杠代替文字（TGS 字样）
                const int my = h / 2;
                if (y >= my - bw && y < my + bw && x >= w / 4 && x < w - w / 4)
                    img.setPixel(x, y, dash);
            }
        }
    }
    return img;
}
#endif

static bool scanRecursive(QDir dir, QVector<QString>& files)
{
    bool ok = true;
    // Qt3 的 QDir 无 NoDotAndDotDot（qdir.h:61 FilterSpec 无此项，Qt4.2 才加），
    // 且**去掉该位并不等价**：实测 Qt3 entryInfoList(Files|Dirs|Readable) 会把
    // "." 与 ".." 一并返回，且二者 isDir() 皆为真（探针实测 count=3，
    // [0]="." [1]=".." [2]=真实子目录）。这会让下面的 fi.isDir() 分支顺着
    // ".." 无限向上递归。故此处按名字显式排除，等价于 Qt4+ 的
    // NoDotAndDotDot。
    const QList<QFileInfo> infoList =
        qDirEntryInfoList(dir, QDir::Files | QDir::Dirs | QDir::Readable);
    for (const auto& fi : infoList) {
        const QString entryName = fi.fileName();
        if (entryName == QLatin1String(".") || entryName == QLatin1String(".."))
            continue;
        if (fi.isDir()) {
            if (!scanRecursive(QDir(qFileInfoAbsoluteFilePath(fi)), files)) {
                ok = false;
            }
        } else if (isSupportedImage(qFileInfoSuffix(fi))) {
            files.append(qFileInfoAbsoluteFilePath(fi));
        }
    }
    return ok;
}

static QString fileIdFor(const QString& path)
{
    return QString(QCryptographicHash::hash(qToUtf8BA(path),
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
    if (bytes.size() < 8 || memcmp(qbaConstData(bytes), kSig, 8) != 0) return info;
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
        const QByteArray type = qbaMid(bytes, off + 4, 4);
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
        const QByteArray four = qbaMid(bytes, off, 4);
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
            const QByteArray lo = qbaToLower(qbaTrimmed(f));
            if (lo.isEmpty()) continue;
            s.insert(lo);
            if (lo == "png")  s.insert(qbaLit("apng"));   // APNG 由 png 插件读取
            if (lo == "svg")  s.insert(qbaLit("svgz"));   // gzip svg 同名插件
            if (lo == "jpeg") s.insert(qbaLit("jpg"));
            if (lo == "tif")  s.insert(qbaLit("tiff"));
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
    QByteArray readerFmt = qbaToLower(qbaTrimmed(fmt));
    if (readerFmt == "apng") readerFmt = qbaLit("png");  // APNG 由 png 插件读取
    if (readerFmt == "svgz") readerFmt = qbaLit("svg");
    if (readerFmt == "jpg")  readerFmt = qbaLit("jpeg");

    const auto attempt = [&](bool autoTransform, const QByteArray& forcedFmt) {
        QBuffer buf;
        qBufferSetData(buf, bytes);
        if (!qOpenReadOnly(buf)) return QImage();
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
    qBufferSetData(probe, bytes);
    qOpenReadOnly(probe);
    QImageReader reader(&probe);
    reader.setAutoTransform(true);

    if (!reader.canRead()) {
        qWarning("[StickerPaste][probe] cannot read, size=%d", bytes.size());
        return false;
    }

    QByteArray fmt = qbaToLower(qbaTrimmed(reader.format()));
    if (fmt.isEmpty()) {
        fmt = qbaToLower(qbaTrimmed(QImageReader::imageFormat(&probe)));
    }

    int frames = 0;
    if (fmt == "png") {                    // APNG 归一：Qt 只会报 png
        const PngChunkInfo png = probePngChunks(bytes);
        if (isApng(png)) {
            fmt = qbaLit("apng");
            frames = png.acTlFrames > 0 ? int(png.acTlFrames) : png.fdAtCount;
            if (png.truncated)
                qWarning("[StickerPaste][probe] apng truncated acTL=%u fdAT=%d fcTL=%d",
                         png.acTlFrames, png.fdAtCount, png.fcTlCount);
        }
    }

    // Qt3 的 QSet 无 initializer_list 构造（见 qByteArraySetBuildAscii 注记）。
    static const char* const kFmtItems[] = {
        "png","apng",                      // apng 手动加入
        "jpg","jpeg","gif","webp","bmp","tif","tiff","tga",
        "xpm","xbm","ppm","pbm","pgm","wbmp","svg","svgz","avif"
    };
    static const QSet<QByteArray> kAllowed =
            qByteArraySetBuildAscii(kFmtItems, (int)(sizeof(kFmtItems)/sizeof(kFmtItems[0])));
    if (!kAllowed.contains(fmt)) {
        qWarning("[StickerPaste][probe] format not in whitelist fmt=%s",
                 qbaConstData(fmt));
        return false;
    }
    // 运行时插件兜底：白名单写死 ≠ 当前运行时插件真实支持。
    // 若格式在 supportedImageFormats() 中缺失则明确拒绝（避免解码静默失败无日志）。
    if (!runtimeSupportedImageFormats().contains(fmt)) {
        qWarning("[StickerPaste][probe] format not supported by runtime plugins fmt=%s "
                 "(whitelisted but missing imageformats plugin)",
                 qbaConstData(fmt));
        return false;
    }

    const QSize size = reader.size();
    if (!size.isValid()) {
        qWarning("[StickerPaste][probe] size invalid fmt=%s", qbaConstData(fmt));
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
                 qbaConstData(fmt), int(decodeErr), qPrintable(decodeErrStr),
                 size.width(), size.height());
        // 大图/内存限制兜底：Qt 因 allocation limit 拒绝（RawImageFormatError/InvalidDataError
        // 且错误串含 allocation）。贴纸导入超大图本不合理，明确拒绝并给出可读提示路径。
        if (qStringContainsNoCase(decodeErrStr, QStringLiteral("allocation"))
            || qStringContainsNoCase(decodeErrStr, QStringLiteral("memory"))) {
            qWarning("[StickerPaste][probe] image exceeds QImageReader allocation limit "
                     "(QT_IMAGEIO_MAXALLOC default 256MB) size=%dx%d bytes=%lld",
                     size.width(), size.height(), (qint64)bytes.size());
        }
        // 细诊断：区分 真APNG / 截断损坏 / Qt 重编码残片（IDAT 根因定位）
        if (qbaIndexOf(fmt, qbaLit("png")) >= 0) {
            const PngChunkInfo png = probePngChunks(bytes);
            if (png.validSignature)
                qWarning("[StickerPaste][probe] png sig=ok chunks=[%s] acTL=%s(%u) fcTL=%d fdAT=%d trunc=%d",
                         qbaConstData(png.summary), png.hasAcTL ? "yes" : "no",
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
    qBufferSetData(probe, bytes);
    qOpenReadOnly(probe);
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
    qBufferSetData(probe, bytes);
    qOpenReadOnly(probe);
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
    if (!qOpenReadOnly(f)) {
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
        for (int i = 0; i < qMin((int)fb.size(), 32); ++i)
            hex += qToLatin1BA(QString("%1").arg(uchar(fb.at(i)), 2, 16, QLatin1Char('0')));
        qWarning("[StickerPaste] uri probe fail path=%s len=%lld head0=%s deobf-try",
                 qPrintable(path), (qint64)fb.size(), qbaConstData(hex));
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
                  vi + 1, qPrintable(path), qbaConstData(vf), vs.width(), vs.height(),
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

// 全量解帧探针：把缩放/直通字节按 Qt 一直 read 到 null，统计实读帧数与"与第 1 帧互异"
// 的帧数（QImage::copy 深拷贝帧 0 为基准逐字节 memcmp）。用于戳穿两类假动画：
// ①图像描述块计数虚高、但真实解码仅能出首帧；②40 帧像素完全相同（退化静止）。
// 仅对 Qt 可多帧解码的格式（GIF）有意义；APNG 走 chunk 计数，不落此探针。
static void fullDecodeScaledFrames(const QByteArray& bytes,
                                   int* outOkFrames,
                                   int* outDistinctFrames)
{
    *outOkFrames = 0;
    *outDistinctFrames = 1;
    if (bytes.isEmpty()) return;

    QBuffer probe;
    qBufferSetData(probe, bytes);
    qOpenReadOnly(probe);
    QImageReader reader(&probe);
    reader.setAutoTransform(true);
    if (!reader.canRead()) return;

    const bool animated = reader.supportsOption(QImageIOHandler::Animation);
    QImage ref;
    int n = 0;
    for (int i = 0; i < 600; ++i) {
        const QImage frame = reader.read();
        if (frame.isNull() || !frame.size().isValid()) break;
        if (ref.isNull()) {
            ref = frame.copy();
            *outDistinctFrames = 1;
        } else if (!qImageSameRgba(frame, ref)) {
            // 原为 memcmp(constBits()) 判重。Qt3 侧 constBits 是 BGRA、
            // sizeInBytes 是 stride×h（含对齐填充），跨图 memcmp 语义不可靠，
            // 故改走按 RGBA 字节序比较的自由函数，两端语义一致。
            ++(*outDistinctFrames);
        }
        ++n;
        if (!animated && !reader.jumpToNextImage()) break;
    }
    *outOkFrames = n;
    // 兜底：distinct 不超实读帧数
    if (*outDistinctFrames > n) *outDistinctFrames = qMax(1, n);
}

// 缩放自验证（硬门槛）：对整份缩放结果解析一次，与原图对比格式+帧数；
// 不一致→MISMATCH 日志 + 返回 false（剪贴板不上可疑动画字节），临时产物保留人工比对。
// 保原格式语义：GIF→GIF、APNG→APNG 应一致；WebP→APNG / TIFF 多页→APNG 属既有预期转换。
static bool verifyScaledResult(const QByteArray& srcRaw,
                               const QByteArray& scaledBytes,
                               const char* tag)
{
    if (srcRaw.isEmpty() || scaledBytes.isEmpty()) return false;

    QByteArray srcFmt, scaleFmt;
    QSize srcSize, scaleSize;
    int srcFrames = 0, scaleFrames = 0;
    probeImageValidity(srcRaw, &srcFmt, &srcSize, &srcFrames);
    probeImageValidity(scaledBytes, &scaleFmt, &scaleSize, &scaleFrames);
    if (srcFrames == 0)
        srcFrames = imageAnimationFrames(srcRaw, srcFmt);
    if (scaleFrames == 0)
        scaleFrames = imageAnimationFrames(scaledBytes, scaleFmt);

    // 真动画判据：GIF 全量解帧实读（实读>=2 且互异>=2 才算动画）；
    // APNG/WebP 走 chunk 计数（acTL/fcTL/fdAT），Qt PNG 插件只扁平解析、实读恒 1 帧，不落此判据。
    int okFrames = 0, distinctFrames = 0;
    bool decodeOk = true;
    if (scaleFmt == "gif") {
        fullDecodeScaledFrames(scaledBytes, &okFrames, &distinctFrames);
        decodeOk = (okFrames >= 2 && distinctFrames >= 2);
    }

    // 缩放结果落盘常驻（setAutoRemove(false)），日志给路径供外部播放器复核；
    // 后缀随输出格式（gif→.gif；apng/png→.png；webp→.webp）。
    const QByteArray scaleSuffix = (scaleFmt == "gif") ? qbaLit(".gif")
            : (scaleFmt == "webp") ? qbaLit(".webp") : qbaLit(".png");
    QTemporaryFile tmp(qDirTempPath()
                       + QStringLiteral("/anystik_scale_verify_XXXXXX")
                       + QLatin1String(qbaConstData(scaleSuffix)));
    tmp.setAutoRemove(false);
    QString tmpPath;
    if (tmp.open()) {
        qIODeviceWrite(tmp, qbaConstData(scaledBytes), scaledBytes.size());
        tmp.flush();
        tmpPath = tmp.fileName();
    }

    // 允许的重编码映射：WebP→APNG（已知转换）、TIFF 多页→APNG（paste 侧 multipageTiffToApng）
    const bool fmtConvert = (srcFmt == "webp" && scaleFmt == "apng")
            || ((srcFmt == "tif" || srcFmt == "tiff") && scaleFmt == "apng");
    const bool fmtOk = (srcFmt == scaleFmt) || fmtConvert;
    const bool framesOk = (srcFrames == scaleFrames);
    if (!fmtOk || !framesOk || !decodeOk) {
        qWarning("[StickerScale][%s] MISMATCH orig=fmt:%s frames:%d %dx%d "
                 "vs scaled=fmt:%s frames:%d %dx%d decode(ok=%d/distinct=%d) tmp=%s",
                 tag, qbaConstData(srcFmt), srcFrames, srcSize.width(), srcSize.height(),
                 qbaConstData(scaleFmt), scaleFrames, scaleSize.width(), scaleSize.height(),
                 okFrames, distinctFrames,
                 qPrintable(tmpPath));
        return false;
    }
    qInfo("[StickerScale][%s] verify ok fmt=%s frames=%d %dx%d->%dx%d "
          "decode(ok=%d/distinct=%d) tmp=%s",
          tag, qbaConstData(scaleFmt), scaleFrames,
          srcSize.width(), srcSize.height(),
          scaleSize.width(), scaleSize.height(),
          okFrames, distinctFrames, qPrintable(tmpPath));
    return true;
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
    qbaReserve(out, 12 + data.size());
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
        QByteArray b = qbaUninit(4);   // 4 字节全覆写，用未初始化缓冲即可（Qt3 无 (n,ch) 构造）
        b[0] = char(v >> 24); b[1] = char(v >> 16); b[2] = char(v >> 8); b[3] = char(v);
        return b;
    };
    const auto be16 = [](quint16 v) {
        QByteArray b = qbaUninit(2);
        b[0] = char(v >> 8); b[1] = char(v);
        return b;
    };
    // 逐行 filter=none + zlib
    const auto scanlinesZ = [](const QImage& im) {
        QByteArray raw;
        const int bpl = im.width() * 4;
        qbaReserve(raw, im.height() * (bpl + 1));
        for (int y = 0; y < im.height(); ++y) {
            raw += char(0);
            raw += qImageScanlineRgba(im, y);
        }
        return qCompress(raw, 6);
    };

    QByteArray png;
    {
        const char sig[8] = {char(0x89), 'P', 'N', 'G', '\r', '\n', char(0x1a), '\n'};
        qbaAppend(png, sig, 8);
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
            // 用 operator[] 而非 at()：Qt3 的 QValueList::at() 返回 NodePtr
            // （链表节点指针，qvaluelist.h:245），不是 int&，赋给 int 会报
            // "invalid conversion from ‘int*’ to ‘int’"。Qt4+ 的 at() 返回 int&，
            // 但 [] 两版本都返回引用，语义一致。
            int ms = delayMs[i];
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
    qBufferSetData(probe, tiffBytes);
    qOpenReadOnly(probe);
    QImageReader reader(&probe);
    reader.setAutoTransform(true);
    if (!reader.canRead()) return QByteArray();

    QList<QImage> frames;
    while (frames.size() < 1024) {
        const QImage frame = reader.read();
        if (frame.isNull() || !frame.size().isValid()) break;
        frames << qImageRgba(frame);
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
    qBufferSetData(probe, bytes);
    qOpenReadOnly(probe);
    QImageReader reader(&probe);
    reader.setAutoTransform(true);
    if (!reader.canRead()) return false;

    // 动画格式（GIF/动画 WebP）：read() 自动推进帧并读到 null 收尾，绝不能
    // 再调 jumpToNextImage()（其 handler 未重写→基类恒 false→循环断在第 1 帧）。
    // 非动画多帧（多页 TIFF）：Qt tiff 插件 read() 不翻页，jumpToNextImage() 负责推进。
    const bool animated = reader.supportsOption(QImageIOHandler::Animation);
    QList<QImage> frames;
    QVector<int> delays;
    const int kMaxFrames = 600;
    while (frames.size() < kMaxFrames) {
        const QImage frame = reader.read();
        if (frame.isNull() || !frame.size().isValid()) break;
        frames << qImageRgba(frame);
        delays << qMax(1, reader.nextImageDelay());
        if (!animated && !reader.jumpToNextImage()) break;
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

    QTemporaryFile tmp(qDirTempPath() + QStringLiteral("/anystik_scaled_XXXXXX.gif"));
    if (!tmp.open()) return QByteArray();
    const QString path = tmp.fileName();
    tmp.close();                       // GifBegin 需要独占创建文件

    // Qt nextImageDelay() 为毫秒；gif-h delay 为百分秒（GIF GCE Delay Time 1/100s）。
    // 不换算动画会慢 10 倍；cs 域 1~65535。
    const auto toGifDelay = [](int ms) -> uint32_t {
        return uint32_t(qBound(1, (ms + 5) / 10, 65535));
    };

    int delay = 10;                    // 默认 100ms（cs）
    if (delayMs.size() == frames.size()) {
        delay = int(toGifDelay(delayMs.first()));
    }

    const int w = frames.first().width();
    const int h = frames.first().height();
    GifWriter writer;
    if (!GifBegin(&writer, qUtf8Printable(path), uint32_t(w), uint32_t(h),
                  uint32_t(delay), 8, true)) {
        return QByteArray();
    }

    bool ok = true;
    for (int i = 0; i < frames.size(); ++i) {
        const QImage& fr = frames.at(i);
        int delayCs = delay;
        if (delayMs.size() == frames.size()) {
            delayCs = int(toGifDelay(delayMs.at(i)));
        }
        // gif-h 要求 RGBA8888 字节序。Qt3 的 QImage 内存是 BGRA，必须先换通道；
        // Qt6 侧 frames 本就是 Format_RGBA8888，该步等价于拷贝。
        const QByteArray rgba = qImageRgbaBytes(fr);
        // tangora_gif.h:827 首参是 const uint8_t*，而 qbaConstData() 返回
        // const char* —— char*→uint8_t* 不是标准隐式转换，Qt6 侧会报
        // "invalid conversion ... [-fpermissive]"（原代码同样有此问题）。
        // 字节值逐位透传，故用 reinterpret_cast 显式表达同一意图；
        // 不新建 QByteArray/QCString，避免一次多余拷贝。
        ok = GifWriteFrame(&writer, reinterpret_cast<const uint8_t*>(qbaConstData(rgba)),
                           uint32_t(w), uint32_t(h),
                           uint32_t(delayCs), 8, true);
        if (!ok) break;
    }
    GifEnd(&writer);
    if (!ok) return QByteArray();

    QFile f(path);
    if (!qOpenReadOnly(f)) return QByteArray();
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
              qPrintable(c.type), qbaConstData(f), s.width(), s.height(),
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
              qPrintable(c.type), qbaConstData(f), s.width(), s.height(),
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
                  qPrintable(p), qbaConstData(f), s.width(), s.height(), (qint64)fb.size(), fr);
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
              qPrintable(c.type), qbaConstData(f), s.width(), s.height(), (qint64)bytes.size());
        if (srcType) *srcType = c.type;
    } else if (!fileStatic.isEmpty()) {
        bytes = fileStatic;
        QByteArray f;
        QSize s;
        probeImageValidity(bytes, &f, &s);
        qInfo("[StickerPaste] source=pb-file-static path=%s fmt=%s size=%dx%d bytes=%lld backend=NSPasteboard",
              qPrintable(fileUrls.first()), qbaConstData(f), s.width(), s.height(),
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
            return qFromStdString(p.id);
        }
    }

    packId = QString("pack_%1").arg(QString(
        QCryptographicHash::hash(qToUtf8BA(packTitle),
            QCryptographicHash::Sha1).toHex().left(12)));
    StickerPackRow pack;
    pack.id = qToStdString(packId);
    pack.title = qUtf8Printable(packTitle);
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
    if (!qMkpath(targetDir)) {
        if (errorOut) *errorOut = QStringLiteral("无法创建包目录");
        return false;
    }
    const QString rootAbs = qDirAbsolutePath(root);

    // 按相对根目录的路径做自然排序（数字感知、稳定保序），
    // 使 eif 组内序号、多分组 1/2/10、zip 层级与手动目录导入顺序全部确定化
    std::stable_sort(files.begin(), files.end(),
                     [&rootAbs](const QString& a, const QString& b) {
        return QNaturalSort::naturalCompare(
                   qDirRelativeFilePath(QDir(rootAbs), a),
                   qDirRelativeFilePath(QDir(rootAbs), b)) < 0;
    });

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
        const QString rel = qDirRelativeFilePath(QDir(rootAbs), file);
        const QString dst = targetDir + QLatin1Char('/') + rel;

        // TGS 分支：Telegram 动图贴纸（gzip Lottie JSON）。
        // 解压取画布尺寸，原始 .tgs 字节保留，同目录生成占位 PNG 入库。
        if (qToLower(qFileInfoSuffix(QFileInfo(file))) == QLatin1String("tgs")) {
            QFile f(file);
            if (!qOpenReadOnly(f)) continue;
            const QByteArray bytes = f.readAll();
            f.close();
            int tw = 512, th = 512;
            QByteArray raw;
            const bool gzOk = bytes.size() >= 2
                && uchar(bytes.at(0)) == 0x1f && uchar(bytes.at(1)) == 0x8b
                && gunzipTgs(bytes, &raw);
            if (!gzOk || !parseTgsSize(raw, &tw, &th)) {
                tw = th = 512;
            }

            // 原始 .tgs 字节保留（供未来真渲染），仍按相对层级复制
            if (!QFile::exists(dst)) {
                if (!qMkpath(qAbsPath(QFileInfo(dst)))) continue;
                if (!qFileCopy(file, dst)) continue;
            }
            // 占位 PNG：同目录 <stem>.tgs_preview.png
            // （带 .tgs_preview 后缀避免与并存同 stem 真图冲突）
            const QString stem = qFileInfoCompleteBaseName(QFileInfo(file));
            const QString pngRel = stem + QStringLiteral(".tgs_preview.png");
            const QString pngDst = QFileInfo(dst).dir().filePath(pngRel);
            const QImage ph = makeTgsPlaceholder(stem, tw, th);
            if (ph.isNull()) continue;
            ph.save(pngDst, "PNG");
            if (!QFileInfo(pngDst).exists()) continue;

            StickerRow row;
            row.id = qToStdString(fileIdFor(pngDst));
            row.pack_id = qUtf8Printable(packId);
            row.file_path = qToStdString(relativeToBase(pngDst));
            row.emoji = "";
            row.width = tw;
            row.height = th;
            row.size = int(QFileInfo(dst).size());   // 原始 tgs 字节数
            row.last_used = 0;
            row.position = pos++;
            if (db.add_sticker(row)) importedAny = true;
            continue;
        }

        // 解码预检：svg 例外（canRead 依赖平台 qsvg 插件，保持旧行为）；
        // 其它格式解析不出尺寸（损坏/截断/不支持）→ 跳过，不复制不入库。
        const bool svgOk = qToLower(qFileInfoSuffix(QFileInfo(file)))
                           == QLatin1String("svg");
        QImageReader probe(file);
        probe.setAutoTransform(true);
        if (!svgOk && !probe.size().isValid()) {
            continue;
        }
        const QSize imgSize = probe.size();   // 预检通过的尺寸，直接入库

        if (!QFile::exists(dst)) {
            if (!qMkpath(qAbsPath(QFileInfo(dst)))) {
                if (errorOut) *errorOut = QStringLiteral("无法创建包子目录");
                continue;
            }
            if (!qFileCopy(file, dst)) {
                if (errorOut) *errorOut = QStringLiteral("文件复制失败：")
                                          + file;
                continue;
            }
        }

        QFileInfo fi(dst);
        const qint64 fileBytes = fi.size();

        StickerRow row;
        row.id = qToStdString(fileIdFor(file));
        row.pack_id = qUtf8Printable(packId);
        row.file_path = qToStdString(relativeToBase(dst));
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
    if (!qOpenReadOnly(f))
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
            const QString ext = qToLower(qFileInfoSuffix(fi));
            if (!ext.isEmpty()) {
                meta.mime = QStringLiteral("image/%1").arg(ext);
                meta.typeLabel = qToUpper(ext) + QStringLiteral(" (") + meta.mime + QLatin1Char(')');
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
            QByteArray rFmt = qbaToLower(qbaTrimmed(reader.format()));
            if (!rFmt.isEmpty() && meta.mime.isEmpty()) {
                meta.mime = QStringLiteral("image/%1").arg(QString::fromLatin1(rFmt));
                meta.typeLabel = QString::fromLatin1(qbaToUpper(rFmt))
                                 + QStringLiteral(" (") + meta.mime + QLatin1Char(')');
            }
        }
        return meta;
    }

    // probe 成功：正常流程
    if (frames < 2)
        frames = imageAnimationFrames(raw, fmt);
    const QString lower = qToLower(QString::fromLatin1(fmt));

    QString human = qToUpper(lower);
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

bool StickerStore::pasteFromClipboard(QString* errorOut, bool* dup,
                                      QString* resurrectId)
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
              qUtf8Printable(mime->formats().join(QStringLiteral("|"))));
#if defined(Q_OS_MACOS)
    // 只读诊断：核对源端原始文件引用（public.file-url⇔file:// 可回退 / public.url⇔https 源端限制）
    if (mime) {
        const QByteArray uriRaw = mime->data("text/uri-list");
        qInfo("[StickerPaste] text/uri-list=%s",
              uriRaw.trimmed().isEmpty() ? "<none>" : qbaConstData(uriRaw.trimmed()));
        for (const QUrl& u : mime->urls())
            qInfo("[StickerPaste] uri[%s]%s",
                  u.isLocalFile() ? "file" : "remote",
                  qUtf8Printable(u.toString()));
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
                      fmt, qbaConstData(f), s.width(), s.height(), raw.size(),
                      imageAnimationFrames(raw, f),
                      useMmPasteboard() ? "NSPasteboard native (.mm)" : "QUtiMimeConverter (Qt)");
            else
#endif
                qInfo("[StickerPaste] source=mime type=%s fmt=%s size=%dx%d bytes=%d",
                      fmt, qbaConstData(f), s.width(), s.height(), raw.size());
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
            const QString p = qUrlToLocalFile(url);
            if (p.isEmpty() || !qFileInfoExists(p)) continue;
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
                      qPrintable(p), qbaConstData(fmtG), qbaConstData(fmt2),
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
            const QString p = qUrlToLocalFile(url);
            if (p.isEmpty() || !qFileInfoExists(p)) continue;
            if (!QFileInfo(p).isFile()) continue;
            QByteArray fb;
            QByteArray fmt2;
            QSize s2;
            int frames2 = 0;
            if (loadLocalImageCandidate(p, &fb, &fmt2, &s2, &frames2)) {
                bytes = fb;
                srcType = QStringLiteral("uri:") + p;
                qInfo("[StickerPaste] source=uri path=%s fmt=%s size=%dx%d bytes=%lld frames=%d",
                      qPrintable(p), qbaConstData(fmt2), s2.width(), s2.height(),
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
                          t, qbaConstData(f), s.width(), s.height(), raw.size(),
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
        qInfo("[StickerPaste] bitmap source qimage-format=%d", qImageFormatTag(img));

        // 自检修复：RGB555 等格式 encode 出的 PNG 可能无有效 IDAT（此前复现
        // chunks=[IHDR|pHYs] + libpng IDAT failure），先归一 ARGB32 再编码并 probe 自验；
        // 逐级降级格式，最后 BMP 兜底（Qt BMP 编码不含 zlib，必成）。
        // qImageFormat/qImageConvertToFormat 是 qimage_shim.h 的跨版本入口
        // （Qt3 无 QImage::Format 枚举也无 convertToFormat，见该头注释）。
        const qImageFormat kFormats[] = {
            qFmtArgb32,
            qFmtRgb32,
            qFmtRgba8888,
        };
        bool done = false;
        for (qImageFormat fmt : kFormats) {
            const QImage c = qImageConvertToFormat(img, fmt);
            QByteArray trial;
            QBuffer b;
            qBufferMake(b, trial);
            if (!qOpenWriteOnly(b)) continue;
            if (!c.save(&b, "PNG")) continue;
            b.close();
            // 两版本 qBufferMake 都别名到 trial 这块内存（Qt3 的 setBuffer 虽
            // 按值传参，但 QByteArray 带引用计数、共享存储），写完 trial 即可见；
            // 这里再 qBufferTake 一次是为让写法不依赖该别名语义，纯拷贝。
            trial = qBufferTake(b);
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
            const QImage c = qImageConvertToFormat(img, qFmtArgb32);
            srcType = QStringLiteral("bitmap");
            QBuffer b;
            qBufferMake(b, bytes);
            if (!qOpenWriteOnly(b) || !c.save(&b, "BMP")) {
                if (errorOut) *errorOut = QStringLiteral("图片编码失败");
                return false;
            }
            b.close();
            bytes = qBufferTake(b);   // 同上：纯拷贝，非正确性必需

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
                                                    : qToUtf8BA(srcType);
        qInfo("[StickerPaste] verify src=%s final-fmt=%s size=%dx%d bytes=%lld frames=%d probe-ok=%d",
              qbaConstData(srcUtf),
              vok ? qbaConstData(vf) : "?",
              vok ? vs.width() : 0, vok ? vs.height() : 0,
              (qint64)bytes.size(),
              vok ? imageAnimationFrames(bytes, vf) : 0, int(vok));
    }
    return importImageBytes(bytes, errorOut, dup, resurrectId);
}

// ── 图片字节入库（桌面剪贴板 / Android 剪贴板 / Android 分享 共用）──
bool StickerStore::importImageBytes(const QByteArray& bytes, QString* errorOut,
                                    bool* dup, QString* resurrectId)
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
          qbaConstData(fmt), imgSize.width(), imgSize.height(),
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
    if (!pasteDir.exists() && !qMkpath(qDirAbsolutePath(pasteDir))) {
        if (errorOut) *errorOut = QStringLiteral("无法创建 pastes 目录");
        return false;
    }

    const QString filePath = pasteDir.filePath(idHex + ext);
    if (!QFile::exists(filePath)) {
        QFile file(filePath);
        if (!qOpenWriteOnly(file)
            || qIODeviceWrite(file, qbaConstData(bytes), bytes.size()) != bytes.size()) {
            if (errorOut) *errorOut = QStringLiteral("图片保存失败");
            return false;
        }
    } else {
        // 同内容已在：不重写文件、不 REPLACE、不挪位、不隐式复活，直接视为成功。
        // 行态三态：-1=无行(孤儿文件)  0=存活  1=软删（可询问还原）。
        const int st = stickerDb().sticker_deleted_state(qUtf8Printable(idHex));
        if (resurrectId && st == 1) {
            *resurrectId = idHex;
        }
        if (dup) {
            *dup = (st != 1);
        }
        return true;
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
    row.id = qToStdString(idHex);
    row.pack_id = qUtf8Printable(packId);
    row.file_path = qToStdString(QStringLiteral("pastes/") + idHex + ext);
    row.emoji = "";
    row.width = imgSize.width();
    row.height = imgSize.height();
    row.size = int(bytes.size());
    row.last_used = 0;
    row.position = int(db.count_stickers(qUtf8Printable(packId)));

    const bool ok = db.add_sticker(row);
    db.commit_transaction();

    if (ok) {
        emit dataChanged();
    } else if (errorOut) {
        *errorOut = QStringLiteral("贴纸入库失败");
    }
    return ok;
}

// ── 双向同步下行落地（只增改，不做删除方向）──────────────────

QString StickerStore::sanitizeDirName(const QString& name)
{
    const QString illegal = QStringLiteral("/\\:*?\"<>|");
    QString out;
    out.reserve(name.length());
    // 索引循环而非 for (QChar c : name)：Qt3 的 QString 无 begin()/end()。
    for (int ci = 0; ci < name.length(); ++ci) {
        const QChar c = name.at(ci);
        if (!c.isPrint())
            continue;
        if (illegal.contains(c))
            continue;
        if (c < QLatin1Char(' '))
            continue;
        out.append(c);
    }
    if (out.isEmpty())
        out = QStringLiteral("pack");
    return out;
}

QString StickerStore::ensurePack(const QString& title)
{
    if (!ensureInit() || qTrimmed(title).isEmpty()) {
        return QString();
    }
    auto& db = stickerDb();
    QString err;
    return findOrCreatePack(db, title, int(db.list_packs(1).size()), &err);
}

QStringList StickerStore::builtinSourceCloudDirs()
{
    QStringList dirs;
    const auto packs = this->packs(1, "title ASC");
    for (const auto& p : packs) {
        if (isBuiltinSourcePack(p.id, p.title)) {
            dirs << sanitizeDirName(p.title);
        }
    }
    return dirs;
}

bool StickerStore::importStickerFile(const QString& packId,
                                     const QString& srcAbs,
                                     QString* errorOut,
                                     const QString& dstName,
                                     const QString& targetRel)
{
    if (m_migrating) {
        if (errorOut) *errorOut = QStringLiteral("正在迁移，请稍候再导入");
        return false;
    }
    if (!ensureInit()) {
        if (errorOut) *errorOut = QStringLiteral("storage init failed");
        return false;
    }
    const QFileInfo si(srcAbs);
    if (!si.exists() || !si.isFile()) {
        if (errorOut) *errorOut = QStringLiteral("src file missing");
        return false;
    }

    auto& db = stickerDb();
    auto pack = db.get_pack(qUtf8Printable(packId));
    if (!pack) {
        if (errorOut) *errorOut = QStringLiteral("pack not found");
        return false;
    }
    // 目标相对路径：下载侧直传业务 dbRel（落盘 = 键一致）；为空则按包标题
    // base/packs/<title>（其余调用）。剪贴板 pastes/<f> → base/pastes/<f>，
    // 不再误入 packs/ 下；普通包 packs/<T>/<f> → base/packs/<T>/<f>。
    const QString rel = targetRel.isEmpty()
        ? QStringLiteral("packs/") + QString::fromUtf8(pack->title.c_str())
        : targetRel;
    const QString targetDir = stickerBaseDir() + QLatin1Char('/')
        + qFileInfoPath(QFileInfo(rel));
    if (!qMkpath(targetDir)) {
        if (errorOut) *errorOut = QStringLiteral("无法创建包目录");
        return false;
    }

    // 目标文件名：下载侧显式传云端原始 basename；其余调用沿用源文件名
    const QString fileName = targetRel.isEmpty()
        ? (dstName.isEmpty() ? si.fileName() : dstName)
        : rel.section(QLatin1Char('/'), -1);
    const QString dst = targetDir + QLatin1Char('/') + fileName;

    // 幂等：同包同相对路径已有行 → 已导入（双向按 size 判 same，不会重入）
    const auto existing = db.list_stickers(qUtf8Printable(packId));
    for (const auto& e : existing) {
        if (qFromStdString(e.file_path) == rel) {
            return true;
        }
    }

    // 解码预检（保持 importDirectory 的 svg 例外：QVariant 依赖平台 qsvg 插件）
    const bool svgOk = qToLower(qFileInfoSuffix(QFileInfo(srcAbs)))
                       == QLatin1String("svg");
    QImageReader probe(srcAbs);
    probe.setAutoTransform(true);
    if (!svgOk && !probe.size().isValid()) {
        if (errorOut) *errorOut = QStringLiteral("图片解码失败");
        return false;
    }
    const QSize imgSize = probe.size();

    // 移动落地（src 为云端 get 的临时文件）。
    // ⚠ 原注释写"目标已存在则覆盖（rename 的 POSIX 覆盖语义）"是**错的**，已
    //   按实测更正：Qt 6.7.3 的 QFile::rename 在目标已存在时返回 false 且
    //   **不覆盖**（探针实测：目标内容保持 OLD，源文件仍在），与 POSIX
    //   rename(2) 的覆盖语义不同 —— Qt 显式先做了存在性检查。qFileRename
    //   已按此对齐（目标存在直接返回 false）。
    // 实际不会走到"目标已存在"：上方幂等检查（同包同相对路径已入库即返回）
    // 已提前返回，此处 dst 必为新路径。
    if (dst != srcAbs) {
        if (!qFileRename(srcAbs, dst)) {
            // rename 失败（如跨设备）→ 退回拷贝 + 清理源。
            // 注意 qFileCopy 同样拒绝已存在的目标，故此处目标必不存在。
            if (!qFileCopy(srcAbs, dst)) {
                if (errorOut) *errorOut = QStringLiteral("文件落地失败");
                return false;
            }
            QFile::remove(srcAbs);
        }
    }

    QFileInfo fi(dst);
    StickerRow row;
    row.id = qToStdString(fileIdFor(dst));
    row.pack_id = pack->id;
    row.file_path = qToStdString(rel);
    row.emoji = "";
    row.width = int(imgSize.width());
    row.height = int(imgSize.height());
    row.size = int(fi.size());
    row.last_used = 0;
    row.position = int(db.count_stickers(qUtf8Printable(packId)));

    const bool ok = db.add_sticker(row);
    if (ok) {
        emit dataChanged();
    } else if (errorOut) {
        *errorOut = QStringLiteral("贴纸入库失败");
    }
    return ok;
}

bool StickerStore::renameStickerFile(const QString& packId,
                                     const QString& oldFileName,
                                     const QString& newFileName)
{
    if (!ensureInit() || packId.isEmpty() || oldFileName.isEmpty()
            || newFileName.isEmpty() || oldFileName == newFileName) {
        return false;
    }
    auto& db = stickerDb();
    auto pack = db.get_pack(qUtf8Printable(packId));
    if (!pack) {
        return false;
    }
    const QString title = QString::fromUtf8(pack->title.c_str());
    const QString base = stickerBaseDir();
    const QString oldAbs = base + QStringLiteral("/packs/") + title
        + QLatin1Char('/') + oldFileName;
    const QString newAbs = base + QStringLiteral("/packs/") + title
        + QLatin1Char('/') + newFileName;

    if (QFile::exists(oldAbs) && oldAbs != newAbs) {
        if (QFile::exists(newAbs)) {
            if (!QFile::remove(newAbs)) {
                return false;
            }
        }
        if (!qFileRename(oldAbs, newAbs)) {
            return false;
        }
    }

    const QString oldRel = relativeToBase(oldAbs);
    const QString newRel = relativeToBase(newAbs);

    SqliteStatement stmt = Storage::instance().msgDb().prepare(
        "UPDATE stickers SET file_path=?1 WHERE pack_id=?2 AND file_path=?3");
    if (!stmt.isPrepared()) {
        return false;
    }
    const QByteArray newRelU = qToUtf8BA(newRel);
    const QByteArray packU = qToUtf8BA(packId);
    const QByteArray oldRelU = qToUtf8BA(oldRel);
    if (!stmt.bind(1, qbaConstData(newRelU))) return false;
    if (!stmt.bind(2, qbaConstData(packU))) return false;
    if (!stmt.bind(3, qbaConstData(oldRelU))) return false;
    const bool ok = stmt.step();
    if (ok) emit dataChanged();
    return ok;
}

bool StickerStore::renamePack(const QString& packId, const QString& newTitle)
{
    if (!ensureInit() || qTrimmed(newTitle).isEmpty()) {
        return false;
    }
    auto& db = Storage::instance().msgDb();
    SqliteStatement stmt = db.prepare(
        "UPDATE sticker_packs SET title=?1 WHERE id=?2");
    if (!stmt.isPrepared()) {
        return false;
    }
    QByteArray titleUtf8 = qToUtf8BA(qTrimmed(newTitle));
    QByteArray packUtf8 = qToUtf8BA(packId);
    if (!stmt.bind(1, qbaConstData(titleUtf8))) return false;
    if (!stmt.bind(2, qbaConstData(packUtf8))) return false;
    const bool ok = stmt.step();
    if (ok) emit dataChanged();
    return ok;
}

bool StickerStore::deletePack(const QString& packId)
{
    if (!ensureInit()) {
        return false;
    }
    const bool ok = stickerDb().delete_pack(qUtf8Printable(packId));
    // 子表情由外键策略级联清除
    if (ok) emit dataChanged();
    return ok;
}

bool StickerStore::deleteSticker(const QString& stickerId)
{
    if (!ensureInit()) {
        return false;
    }
    const bool ok = stickerDb().delete_sticker(qUtf8Printable(stickerId));
    if (ok) emit dataChanged();
    return ok;
}

bool StickerStore::restoreSticker(const QString& stickerId)
{
    if (!ensureInit()) {
        return false;
    }
    const bool ok = stickerDb().restore_sticker(qUtf8Printable(stickerId));
    if (ok) emit dataChanged();
    return ok;
}

bool StickerStore::setStickerDescription(const QString& stickerId,
                                         const QString& description)
{
    if (!ensureInit()) {
        return false;
    }
    const bool ok = stickerDb().update_sticker_description(
        qUtf8Printable(stickerId), qUtf8Printable(description));
    if (ok) emit dataChanged();
    return ok;
}

void StickerStore::touchSticker(const QString& stickerId)
{
    if (!ensureInit()) {
        return;
    }
    stickerDb().touch_sticker(qUtf8Printable(stickerId),
        qDateTimeEpochSecs());
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
            ? qbaLit("image/gif") : qbaLit("image/apng");
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
        qOpenWriteOnly(pb);
        if (pngFallback.save(&pb, "PNG"))
            mime->setData("public.png", pb.data());
    }
    if (!filePath.isEmpty())
        mime->setUrls({QUrl::fromLocalFile(filePath)}); // public.file-url：文件粘贴方取原始多帧
#endif
    mime->setImageData(pngFallback);        // PNG 位图回退（缩放结果）
    QGuiApplication::clipboard()->setMimeData(mime);
    qInfo("[StickerCopy] fmt=%s bytes=%d frames=%d",
          qbaConstData(fmt), bytes.size(), imageAnimationFrames(bytes, fmt));
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
    qListReserve(scaled, frames.size());
    for (const QImage& fr : frames) {
        QImage s = qImageScaledKeepAspectSmooth(fr, targetSize);
        if (s.isNull()) return false;
        scaled << qImageRgba(s);
    }

    if (srcFmt == "gif") {
        const QByteArray gifBytes = buildGifBytes(scaled, delayMs);
        if (!gifBytes.isEmpty()
                && verifyScaledResult(srcRaw, gifBytes, "desktop-gif")) {
            return stashAnimationClipboard(qbaLit("gif"), gifBytes, srcPath, scaled.first());
        }
    } else {
        const QByteArray apng = buildApngFromFrames(scaled, delayMs);
        if (!apng.isEmpty()
                && verifyScaledResult(srcRaw, apng, "desktop-apng")) {
            return stashAnimationClipboard(qbaLit("apng"), apng, srcPath, scaled.first());
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
        if (qOpenReadOnly(pb)) pRaw = pb.readAll();
        QByteArray pFmt; QSize pSize; int pFrames = 0;
        if (probeImageValidity(pRaw, &pFmt, &pSize, &pFrames)
                && ((pFmt == "gif") || (pFmt == "apng" && pFrames > 1)))
            animFmt = pFmt;
    }
    if (!animFmt.isEmpty()) {
        QFile f(filePath);
        if (qOpenReadOnly(f)) {
            const QByteArray raw = f.readAll();
            if (verifyScaledResult(raw, raw, "desktop-copy"))
                return stashAnimationClipboard(animFmt, raw, filePath, QImage(filePath));
            qWarning("[StickerCopy] animation passthrough verify failed fmt=%s",
                     qbaConstData(animFmt));
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
    if (!qOpenWriteOnly(out)) return false;   // Qt3 IO_WriteOnly 本身即截断（已实测）
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
        if (!qOpenReadOnly(pb)) return false;
        raw = pb.readAll();
        QByteArray fmt; QSize size; int cnt = 0;
        probeImageValidity(raw, &fmt, &size, &cnt);
        srcFmt = fmt;
        decodeAllFrames(raw, &frames, &delays);
    }
    if (frames.size() < 2) {
        QImage img(filePath);
        if (img.isNull()) return false;
        img = qImageScaledKeepAspectSmooth(img, cappedScaledSize(img.size(), scale));
        return storeScaledTmpThenCopy(img);
    }

    const QSize target = cappedScaledSize(frames.first().size(), scale);
    QList<QImage> scaled;
    qListReserve(scaled, frames.size());
    for (const QImage& fr : frames) {
        QImage s = qImageScaledKeepAspectSmooth(fr, target);
        if (s.isNull()) return false;
        scaled << qImageRgba(s);
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
        qOpenWriteOnly(wb);
        if (!first.save(&wb, "PNG")) return false;
        bytes = wb.data();
        ext = QStringLiteral(".png");
    }
    if (bytes.isEmpty()) return false;

    // 拷贝前核对：格式+帧数须与原图一致；不一致→MISMATCH 日志并回退静态 PNG
    if (!verifyScaledResult(raw, bytes, "android-anim")) {
        const QImage first = scaled.first();
        QBuffer wb;
        qOpenWriteOnly(wb);
        if (!first.save(&wb, "PNG")) return false;
        bytes = wb.data();
        ext = QStringLiteral(".png");
    }

    const QString tmpPath = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation) + QStringLiteral("/scaled") + ext;
    QFile out(tmpPath);
    if (!qOpenWriteOnly(out)) return false;   // Qt3 IO_WriteOnly 本身即截断（已实测）
    qIODeviceWrite(out, qbaConstData(bytes), bytes.size());
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
        qWarning() << "[StickerStore] android scaled copy failed:" << tmpPath;
    return copied;

#else
    QFile pb(filePath);
    if (!qOpenReadOnly(pb)) return false;
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
    img = qImageScaledKeepAspectSmooth(img, cappedScaledSize(img.size(), scale));
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
    // 仅替换文件系统危险字符与换行，保留中文/空格/括号/大小写，
    // 保证安装后分组名与源显示名一致可读。
    QString out = in;
    const QString dangerous = QStringLiteral("\\/:*?\"<>|");
    // 索引循环 + qStringRefAt：Qt3 的 QString 无 begin()/end()，且
    // operator[] 按值返回 QChar（真正可写的只有 QString::ref()）。
    for (int ci = 0; ci < out.length(); ++ci) {
        QChar& c = qStringRefAt(out, ci);
        if (c == QLatin1Char('\n') || c == QLatin1Char('\r')
            || dangerous.contains(c)) {
            c = QLatin1Char('_');
        }
    }
    if (qTrimmed(out).isEmpty()) {
        out = QStringLiteral("pack");
    }
    return out;
}

static QString urlHex(const QString& url)
{
    return QString(QCryptographicHash::hash(qToUtf8BA(url),
        QCryptographicHash::Md5).toHex()).left(16);
}

// 远程 GitHub 源解析（内置源统一走 gh-proxy 前缀加速，不使用 codeload.github.com）：
//   https://gh-proxy.{com,org}/https://github.com/<owner>/<repo>/archive/<40位sha>.zip   → 整包，sha 固定
//   https://gh-proxy.{com,org}/https://raw.githubusercontent.com/<owner>/<repo>/<sha>/<path>
//   https://raw.githubusercontent.com/<owner>/<repo>/<sha>/<path>   （兼容无前缀直连）
// 双前缀剥除，兼容历史 .com 代理与现行 .org 代理
enum class RemoteKind { None, ArchiveZip, RawFile };
static RemoteKind matchRemoteRepo(const QString& url,
                                  QString* owner, QString* repo, QString* branch)
{
    QString inner = url;
    static const QStringList kGhProxyPrefixes = qStringListBuild(
        QStringLiteral("https://gh-proxy.com/https://"),
        QStringLiteral("https://gh-proxy.org/https://"));
    for (const auto& prefix : kGhProxyPrefixes) {
        if (inner.startsWith(prefix)) {
            inner = inner.mid(prefix.length());
            break;
        }
    }
    if (inner.startsWith(QLatin1String("github.com/"))) {
        inner.remove(0, QStringLiteral("github.com/").length());
    } else if (inner.startsWith(QLatin1String("raw.githubusercontent.com/"))) {
        inner.remove(0, QStringLiteral("raw.githubusercontent.com/").length());
    } else {
        return RemoteKind::None;
    }

    const QStringList parts = qStringSplitSkipEmpty(inner, QLatin1Char('/'));
    // <owner>/<repo>/archive/<40位sha>.zip
    if (parts.size() == 4 && qStringListAt(parts, 2) == QLatin1String("archive")) {
        QString rev = qStringListAt(parts, 3);
        if (rev.endsWith(QLatin1String(".zip"))) {
            rev = rev.left(rev.length() - 4);
        }
        const QByteArray dec = qbaFromHex(qToLatin1BA(rev));
        if (dec.size() == 20 && rev.length() == 40) {
            if (owner) *owner = qStringListAt(parts, 0);
            if (repo) *repo = qStringListAt(parts, 1);
            if (branch) *branch = rev;
            return RemoteKind::ArchiveZip;
        }
    }
    // <owner>/<repo>/<40位sha>/<path...>
    if (parts.size() >= 4) {
        const QString rev = qStringListAt(parts, 2);
        const QByteArray dec = qbaFromHex(qToLatin1BA(rev));
        if (dec.size() == 20 && rev.length() == 40) {
            if (owner) *owner = qStringListAt(parts, 0);
            if (repo) *repo = qStringListAt(parts, 1);
            if (branch) *branch = rev;
            return RemoteKind::RawFile;
        }
    }
    return RemoteKind::None;
}

static QString urlDisplayName(const QString& url)
{
    QString owner, repo, branch;
    if (matchRemoteRepo(url, &owner, &repo, &branch) != RemoteKind::None) {
        return repo;
    }
    QUrl u(url);
    QString base = u.fileName();
    if (base.isEmpty()) {
        const QStringList parts = qStringSplitSkipEmpty(u.path(), QLatin1Char('/'));
        if (!parts.isEmpty()) base = parts.last();
    }
    if (base.isEmpty() || qStringEqualsNoCase(base, QStringLiteral("main"))
        || qStringEqualsNoCase(base, QStringLiteral("master"))
        || qStringEqualsNoCase(base, QStringLiteral("head"))) {
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
    { "DeepSeek酱 鲸鱼娘 梗图 preview(19)",
      "https://gh-proxy.org/https://github.com/the-beating-light-of-the-nail/deepseek-chan-meme-pack/archive/3678bd997602446aedb9b2854ebee7400c808138.zip",
      715185L,    // 2026-09-15 实测 GET 全量字节（仓库 archive，含 19 张 preview webp + README）
      "https://github.com/the-beating-light-of-the-nail/deepseek-chan-meme-pack",
      "2026-09-15", true },
    { "奶龙 抽象梗图",
      "https://gh-proxy.org/https://github.com/GGGeeeooorrrgggeee/nailong-memes/archive/ada8a505e1bfcd40e0c0ad9962b45d20ffda0599.zip",
      153480410L,  // 2026-09-16 实测 GET 全量字节：141 文件 / 136 张 gif+jpg（均在 gif/ ），sha 钉死
      "https://github.com/GGGeeeooorrrgggeee/nailong-memes",
      "2026-09-16", true },
    { "中国社交媒体平台表情合集 (emoji-chinese)",
      "https://gh-proxy.org/https://github.com/YiJio/emoji-chinese/archive/79c2292b778cf93cb5850f70c3168bcbfe39aeeb.zip",
      16108164L,   // 2026-09-16 API 树求和（≈15.4MB，1280 png + 698 gif，QQ/微信/抖音/B站/微博等多平台默认表情）
      "https://github.com/YiJio/emoji-chinese",
      "2026-09-16", true },
    { "EmojiPackage 中文斗图配文合集",
      "https://gh-proxy.org/https://github.com/getActivity/EmojiPackage/archive/6110519a8340b36ae3497c4b062292ca25291b61.zip",
      202812407L,   // 2026-09-16 API 树求和（≈193.4MB，302 gif / 1537 jpg / 129 png / 15 webp）
      "https://github.com/getActivity/EmojiPackage",
      "2026-09-16", true },
    { "smiles 聊天动态表情 (AC娘等)",
      "https://gh-proxy.org/https://github.com/LiuJi-Jim/smiles/archive/e710ecc1d22e012231175fb88dbc60291de4ce83.zip",
      30089460L,    // 2026-09-16 API 树求和（≈28.7MB，219 gif / 137 jpg / 13 png）
      "https://github.com/LiuJi-Jim/smiles",
      "2026-09-16", true },
    { "QQ 官方表情全系 (QFace)",
      "https://gh-proxy.org/https://github.com/koishijs/QFace/archive/f835d447fb5eb4aa9ae733f31d90cdd6d5589093.zip",
      143277322L,  // 2026-09-16 API 树求和（≈136.6MB，94 表情 / 949 资源，含 gif/webp/png）
      "https://koishi.js.org/QFace/",
      "2026-09-16", true },
    { "QQ 超级表情 (37张)",
      "https://gh-proxy.org/https://github.com/Iamliuxiaozhen/QQemoji/archive/571be5c0f9c304dd2c34511578aa7233637e0c4d.zip",
      7164746L,   // 2026-09-16 API 树求和（≈6.8MB，super/ 37 张 png）
      "https://github.com/Iamliuxiaozhen/QQemoji",
      "2026-09-16", true },
    { "4chan 表情包 (2022)",
      "https://gh-proxy.org/https://github.com/Sundowner8/4chanmotes.github.io/archive/6ac1b8072cff9a0f92c3e86a236c234953c324c8.zip",
      590793L,    // 2026-09-16 API 树求和（≈0.6MB，emotes/ 约 175 张 png/gif）
      "https://github.com/Sundowner8/4chanmotes.github.io",
      "2026-09-16", true },
    { "blobs.gg 梗图 GIF (英文)",
      "https://gh-proxy.org/https://github.com/Raymo111/emoji/archive/ec4fa3b7d74dfd299570cb63ab6ce9d74e4b6f01.zip",
      24625769L,  // 2026-09-16 API 树求和（≈23.5MB，600+ 张 gif/webp）
      "https://github.com/Raymo111/emoji",
      "2026-09-16", true },
    { "Discord/Slack 自制表情 (skullface)",
      "https://gh-proxy.org/https://github.com/skullface/emotes/archive/36915096b337f98e9e7581f95fec46ed2b39f807.zip",
      4271880L,   // 2026-09-16 API 树求和（≈4.1MB，emoji/ 114 张 png/gif）
      "https://github.com/skullface/emotes",
      "2026-09-16", true },
    { "WhatsApp 官方示例贴纸 (SDK)",
      "https://gh-proxy.org/https://github.com/WhatsApp/stickers/archive/06144a1f6077bbb346e1230032fc4e0bce996d03.zip",
      13163057L,   // 8/30 selftest5 整包实测（约值，随 commit 变化）
      "https://github.com/WhatsApp/stickers",
      "2025-11-07", true },
    { "Animals (Telegram)",
      "https://gh-proxy.org/https://raw.githubusercontent.com/kanelai/stickerapp/b84a74ab09db90e33bd6eaee7135bf96a00fb5dd/Animals.stickerpack",
      1088205L,    // 本会话 Range 206 实测 content-range: bytes 0-0/1088205
      "https://github.com/kanelai/stickerapp",
      "2019-02-18", true },
    { "LINE 贴纸包 (GitHub 镜像)",
      "https://gh-proxy.org/https://raw.githubusercontent.com/porridgebrother/line-stickers/8480a02a3b74914d70b8763cef77c2a9a86769a5/stickers.zip",
      1490697L,   // 本会话 Range 实测 content-range: bytes 0-0/1490697（raw 偶发超时,重试即可）
      "https://github.com/porridgebrother/line-stickers",
      "2017-09-18", true },
    { "wyv 表情包 (Volpeon)",
      "https://strapi.volpeon.ink/uploads/wyv_10ef1cb106.zip",
      680387L,    // 2026-09-19 HEAD 实测 content-length（≈0.68 MB，31 张 png 根目录直达，含 LICENSE/meta.json）
      "https://volpeon.ink/emojis/wyv/",
      "2026-09-19", true },
    { "ChineseBQB 梗图包",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/001Funny_%E6%BB%91%E7%A8%BD%E5%A4%A7%E4%BD%AC%F0%9F%98%8FBQB.zip",
      4691509L,
      "https://v2fy.com/p/001Funny_%E6%BB%91%E7%A8%BD%E5%A4%A7%E4%BD%AC%F0%9F%98%8FBQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },   // 本会话 Range 实测 content-range: bytes 0-0/4691509（raw 源偶发超时,重试即可）
    { "LINE 贴纸 2938",
      "https://stickershop.line-scdn.net/stickershop/v1/product/2938/iphone/stickers@2x.zip",
      797156L,         // 真机 HEAD 实测 content-length（≈0.76 MB）
      "https://store.line.me/stickershop/product/2938/zh-Hans",
      "2026-09-19", true },
    { "LINE 动态 18060",
      "https://stickershop.line-scdn.net/stickershop/v1/product/18060/iphone/stickerpack@2x.zip",
      7246424L,         // 真机 HEAD 实测 content-length（≈6.9 MB）
"https://store.line.me/stickershop/product/18060/zh-Hans",
      "2026-09-19", true },
     { "小红书表情包（社交平台合集）",
       "https://gh-proxy.org/https://github.com/Augenstern-O/Stickers/archive/92767d783cc80d3e729217b5897752035da75fc9.zip",
       320394593L,          // 本会话实测下载（≈320.0 MB）
       "https://github.com/Augenstern-O/Stickers",
      "2026-08-01", true },
     { "Twitter 官方 Emoji (Twemoji)",
       "https://gh-proxy.org/https://github.com/twitter/twemoji/archive/bad3bceeafc901ace42a3dfe0421db6388daafb9.zip",
       33554432L,         // 指定 32M
       "https://github.com/twitter/twemoji",
      "2026-07-07", true },
     { "贴吧表情全收集（滑稽等）",
       "https://gh-proxy.org/https://github.com/KeikoAyano/Tieba-Emoji/archive/aabbbbfaa2ccd6540cb5f13bc09b0cd1c64089c5.zip",
       17447147L,         // API 求和（≈16.6 MB）
       "https://github.com/KeikoAyano/Tieba-Emoji",
      "2025-11-02", true },
     { "抖音表情包（默认/合成全收集）",
       "https://gh-proxy.org/https://github.com/rento666/douyin-emoji/archive/46f5cba582a70206d79f492908cf8509d8784398.zip",
       35398057L,         // API 求和（≈33.8 MB）
       "https://github.com/rento666/douyin-emoji",
      "2025-11-27", true },
{ "B站表情全归档（ccmuyuu）",
       "https://gh-proxy.org/https://github.com/ccmuyuu/bilibili-emotes/archive/db3972317ee029f5acf28ba7bbe971cbbc4fd4e9.zip",
       4365762560L,       // 首次预估=API size 4263440KB（≈4.07 GB）
       "https://github.com/ccmuyuu/bilibili-emotes",
      "2026-09-06", true },
     { "B站贴纸存档（amtoaer）",
       "https://gh-proxy.org/https://github.com/amtoaer/bilibili-stickers/archive/abab458b399659f4ae6d91f3633c3a8365b147ac.zip",
       1686083L,          // API 求和（≈1.6 MB）
       "https://github.com/amtoaer/bilibili-stickers",
      "2023-05-21", true },
{ "B站表情gif全图（rtransformation）",
        "https://gh-proxy.org/https://github.com/rtransformation/Bilibili-emoticon-collection/archive/a2f0c55a04f689a23382a594791957d8b01aff70.zip",
        3546883L,          // API 求和（≈3.4 MB）
        "https://github.com/rtransformation/Bilibili-emoticon-collection",
      "2022-01-25", true },
     { "brd 表情包 (Volpeon)",
        "https://strapi.volpeon.ink/uploads/brd_560ac976ea.zip",
        692745L,          // 2026-09-19 HEAD 实测 content-length（≈0.68 MB）
        "https://volpeon.ink/emojis/brd/",
      "2026-09-19", true },
     { "wlf 表情包 (Volpeon)",
        "https://strapi.volpeon.ink/uploads/wlf_6b2776ad62.zip",
        841722L,          // 2026-09-19 HEAD 实测 content-length（≈0.84 MB）
        "https://volpeon.ink/emojis/wlf/",
      "2026-09-19", true },
     { "Clannad 团子表情 (NaiJi/udongein)",
        "https://udongein.xyz/emoji-page/dangos/dangos.zip",
        147315L,          // 2026-09-19 HEAD 实测 content-length
        "https://udongein.xyz/emoji-page/",
      "2026-09-19", true },
     { "雀魂表情 (NaiJi/udongein)",
        "https://udongein.xyz/emoji-page/mahjong-soul/mahjong-soul.zip",
        2487739L,         // 2026-09-19 HEAD 实测 content-length（≈2.5 MB）
        "https://udongein.xyz/emoji-page/",
      "2026-09-19", true },
     { "moule 角色包 HYPERHYENA",
        "https://moule.world/media/images/emojis/HYPERHYENA.zip",
        1024677L,         // 2026-09-19 HEAD 实测 content-length（≈1.0 MB）
        "https://moule.world/",
      "2026-09-19", true },
     { "moule 角色包 PWNZR",
        "https://moule.world/media/images/emojis/PWNZR.zip",
        719654L,          // 2026-09-19 HEAD 实测 content-length（≈0.72 MB）
        "https://moule.world/",
      "2026-09-19", true },
     { "mutant 表情 (fedi-emojis)",
        "https://gh-proxy.org/https://raw.githubusercontent.com/SnenxyTengoku/fedi-emojis/main/mutant.zip",
        21171683L,        // 2026-09-19 HEAD 实测 content-length（≈21 MB）
        "https://github.com/SnenxyTengoku/fedi-emojis",
      "2026-09-19", true },
     { "mutantremix (fedi-emojis)",
        "https://gh-proxy.org/https://raw.githubusercontent.com/SnenxyTengoku/fedi-emojis/main/mutantremix.zip",
        255372L,          // 2026-09-19 HEAD 实测 content-length（≈0.25 MB）
        "https://github.com/SnenxyTengoku/fedi-emojis",
      "2026-09-19", true },
     { "blobbee (olivvybee)",
        "https://github.com/olivvybee/emojis/releases/download/2026.08.16.1/blobbee.zip",
        1836542L,         // 2026-09-19 HEAD 实测 content-length（≈1.8 MB）
        "https://github.com/olivvybee/emojis",
      "2026-09-19", true },
     { "neobread (olivvybee)",
        "https://github.com/olivvybee/emojis/releases/download/2026.08.16.1/neobread.zip",
        1298158L,         // 2026-09-19 HEAD 实测 content-length（≈1.3 MB）
        "https://github.com/olivvybee/emojis",
      "2026-09-19", true },
      // ── ChineseBQB 精选包（来源 zhaoolee/ChineseBQB 仓库 README 直链；本会话并行 HEAD 实测 content-length）──
    { "ChineseBQB 002 可爱的女孩纸👧",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/002CuteGirl_%E5%8F%AF%E7%88%B1%E7%9A%84%E5%A5%B3%E5%AD%A9%E7%BA%B8%F0%9F%91%A7BQB.zip",
      59028547L,
      "https://v2fy.com/p/002CuteGirl_%E5%8F%AF%E7%88%B1%E7%9A%84%E5%A5%B3%E5%AD%A9%E7%BA%B8%F0%9F%91%A7BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 003 可爱男孩纸👶",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/003CuteBoy_%E5%8F%AF%E7%88%B1%E7%94%B7%E5%AD%A9%E7%BA%B8%F0%9F%91%B6BQB.zip",
      10535808L,
      "https://v2fy.com/p/003CuteBoy_%E5%8F%AF%E7%88%B1%E7%94%B7%E5%AD%A9%E7%BA%B8%F0%9F%91%B6BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 006 仓鼠🐹",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/006Hamster_%E4%BB%93%E9%BC%A0%F0%9F%90%B9BQB.zip",
      1984744L,
      "https://v2fy.com/p/006Hamster_%E4%BB%93%E9%BC%A0%F0%9F%90%B9BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 007 胖虎🐯",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/007Tiger_%E8%83%96%E8%99%8E%F0%9F%90%AFBQB.zip",
      443084L,
      "https://v2fy.com/p/007Tiger_%E8%83%96%E8%99%8E%F0%9F%90%AFBQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 009 熊本熊🐻",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/009KumamotoBear_%E7%86%8A%E6%9C%AC%E7%86%8A%F0%9F%90%BBBQB.zip",
      4822035L,
      "https://v2fy.com/p/009KumamotoBear_%E7%86%8A%E6%9C%AC%E7%86%8A%F0%9F%90%BBBQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 010 是喵星人啦🐱",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/010Cat_%E6%98%AF%E5%96%B5%E6%98%9F%E4%BA%BA%E5%95%A6%F0%9F%90%B1BQB.zip",
      24245100L,
      "https://v2fy.com/p/010Cat_%E6%98%AF%E5%96%B5%E6%98%9F%E4%BA%BA%E5%95%A6%F0%9F%90%B1BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 011 狗🐶",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/011Dog_%E7%8B%97%F0%9F%90%B6BQB.zip",
      19551423L,
      "https://v2fy.com/p/011Dog_%E7%8B%97%F0%9F%90%B6BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 013 小猪佩奇👑",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/013PigPecs_%E5%B0%8F%E7%8C%AA%E4%BD%A9%E5%A5%87%F0%9F%91%91BQB.zip",
      3866090L,
      "https://v2fy.com/p/013PigPecs_%E5%B0%8F%E7%8C%AA%E4%BD%A9%E5%A5%87%F0%9F%91%91BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 026 小黄鸡🐔",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/026Chicken_%E5%B0%8F%E5%B9%BA%E9%B8%A1%F0%9F%90%94BQB.zip",
      943786L,
      "https://v2fy.com/p/026Chicken_%E5%B0%8F%E5%B9%BA%E9%B8%A1%F0%9F%90%94BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 031 沙雕企鹅🐧",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/031Penguin_%E6%B2%99%E9%9B%95%E4%BC%81%E9%B9%85%F0%9F%90%A7BQB.zip",
      1645177L,
      "https://v2fy.com/p/031Penguin_%E6%B2%99%E9%9B%95%E4%BC%81%E9%B9%85%F0%9F%90%A7BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 035 猫和老鼠",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/035TomAndJerry_%E7%8C%AB%E5%92%8C%E8%80%81%E9%BC%A0BQB.zip",
      40770934L,
      "https://v2fy.com/p/035TomAndJerry_%E7%8C%AB%E5%92%8C%E8%80%81%E9%BC%A0BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 036 皮卡丘",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/036Pikachu_%E7%9A%AE%E5%8D%A1%E4%B8%98BQB.zip",
      31541L,
      "https://v2fy.com/p/036Pikachu_%E7%9A%AE%E5%8D%A1%E4%B8%98BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", false },
    { "ChineseBQB 039 姚明（三巨头）",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/039YaoMing%E8%A1%A8%E6%83%85%E5%8C%85%E4%B8%89%E5%B7%A8%E5%A4%B4_%E5%A7%9A%E6%98%8EBQB.zip",
      70017L,
      "https://v2fy.com/p/039YaoMing%E8%A1%A8%E6%83%85%E5%8C%85%E4%B8%89%E5%B7%A8%E5%A4%B4_%E5%A7%9A%E6%98%8EBQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", false },
    { "ChineseBQB 040 花泽香菜（三巨头）",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/040HanazawaKana%E8%A1%A8%E6%83%85%E5%8C%85%E4%B8%89%E5%B7%A8%E5%A4%B4_%E8%8A%B1%E6%B3%BD%E9%A6%99%E8%8F%9CBQB.zip",
      24276L,
      "https://v2fy.com/p/040HanazawaKana%E8%A1%A8%E6%83%85%E5%8C%85%E4%B8%89%E5%B7%A8%E5%A4%B4_%E8%8A%B1%E6%B3%BD%E9%A6%99%E8%8F%9CBQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", false },
    { "ChineseBQB 048 海绵宝宝",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/048SpongeBob_%E6%B5%B7%E7%BB%B5%E5%AE%9D%E5%AE%9DBQB.zip",
      217974L,
      "https://v2fy.com/p/048SpongeBob_%E6%B5%B7%E7%BB%B5%E5%AE%9D%E5%AE%9DBQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 052 杰尼龟",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/052Squirtle_%E6%9D%B0%E5%B0%BC%E9%BE%9FBQB.zip",
      2775654L,
      "https://v2fy.com/p/052Squirtle_%E6%9D%B0%E5%B0%BC%E9%BE%9FBQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 056 哆啦A梦",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/056Doraemon_%E5%93%86%E5%95%A6A%E6%A2%A6BQB.zip",
      6476953L,
      "https://v2fy.com/p/056Doraemon_%E5%93%86%E5%95%A6A%E6%A2%A6BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 060 Mur猫😺",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/060MurCat_Mur%E7%8C%AB%F0%9F%98%BABQB.zip",
      2761270L,
      "https://v2fy.com/p/060MurCat_Mur%E7%8C%AB%F0%9F%98%BABQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 062 蔡徐坤🏀",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/062CaiXvKun_%E8%94%A1%E5%BE%90%E5%9D%A4%F0%9F%8F%80BQB.zip",
      3759098L,
      "https://v2fy.com/p/062CaiXvKun_%E8%94%A1%E5%BE%90%E5%9D%A4%F0%9F%8F%80BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 064 特朗普",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/064Trump_%E7%89%B9%E6%9C%97%E6%99%AEBQB.zip",
      561200L,
      "https://v2fy.com/p/064Trump_%E7%89%B9%E6%9C%97%E6%99%AEBQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", false },
    { "ChineseBQB 065 旅行青蛙🐸",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/065TravelFrog_%E6%97%85%E8%A1%8C%E9%9D%92%E8%9B%99%F0%9F%90%B8BQB.zip",
      320710L,
      "https://v2fy.com/p/065TravelFrog_%E6%97%85%E8%A1%8C%E9%9D%92%E8%9B%99%F0%9F%90%B8BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 070 JOJO的奇妙冒险",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/070JOJO%E7%9A%84%E5%A5%87%E5%A6%99%E5%86%92%E9%99%A9BQB.zip",
      223508L,
      "https://v2fy.com/p/070JOJO%E7%9A%84%E5%A5%87%E5%A6%99%E5%86%92%E9%99%A9BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 089 饮茶哥",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/089TeaBoy_%E9%A5%AE%E8%8C%B6%E5%93%A5_BQB.zip",
      1774698L,
      "https://v2fy.com/p/089TeaBoy_%E9%A5%AE%E8%8C%B6%E5%93%A5_BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 095 原神",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/095GenShin_%E5%8E%9F%E7%A5%9E_BQB.zip",
      16702253L,
      "https://v2fy.com/p/095GenShin_%E5%8E%9F%E7%A5%9E_BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 101 电锯人",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/101ChainsawMan_%E7%94%B5%E9%94%AF%E4%BA%BA_BQB.zip",
      846057L,
      "https://v2fy.com/p/101ChainsawMan_%E7%94%B5%E9%94%AF%E4%BA%BA_BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 105 黑神话悟空🐒",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/105_BlackMythWuKong_%E9%BB%91%E7%A5%9E%E8%AF%9D%E6%82%9F%E7%A9%BA%F0%9F%90%92_BQB.zip",
      1180957L,
      "https://v2fy.com/p/105_BlackMythWuKong_%E9%BB%91%E7%A5%9E%E8%AF%9D%E6%82%9F%E7%A9%BA%F0%9F%90%92_BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 106 芙莉莲🪄",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/106_Frieren_%E8%8A%99%E8%8E%89%E8%8E%B2%F0%9F%AA%84_BQB.zip",
      6236255L,
      "https://v2fy.com/p/106_Frieren_%E8%8A%99%E8%8E%89%E8%8E%B2%F0%9F%AA%84_BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
{ "新飞飞QQ表情包 (eif)",
       "https://cz.197942.com/dl/lx/xinff.zip",
       167841L,    // 2013-01-04 cr173 页面更新日；zip 内含 新飞飞表情包.eif（1602050B，74 张）
       "https://www.cr173.com/soft/53605.html",
       "2013-01-04", true },
     { "Telegram 官方动图贴纸样例 (TGS)",
       "https://codeload.github.com/kuronekowen/Telegram-Sticker-Sample/zip/"
       "334150dbf20b010dbb6393baccc1f596f046a1f3",
       778380L,       // 2026-09-25 实测下载字节；zip 内含 6 张 tgs + 6 份 Lottie JSON
       "https://github.com/kuronekowen/Telegram-Sticker-Sample",
       "2021-11-13", true },
};
const unsigned kBuiltinSourceCount =
    sizeof(kBuiltinSources) / sizeof(kBuiltinSources[0]);

static QByteArray fileMd5(const QString& path)
{
    QFile f(path);
    if (!qOpenReadOnly(f)) {
        return QByteArray();
    }
    QCryptographicHash hash(QCryptographicHash::Md5);
    QByteArray buf;
    buf.resize(64 * 1024);
    qint64 got = 0;
    while ((got = qIODeviceRead(f, qbaData(buf), buf.size())) > 0) {
        hash.addData(QByteArrayView(buf.data(), int(got)));
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

    QString branch;
    if (matchRemoteRepo(url, nullptr, nullptr, &branch) == RemoteKind::ArchiveZip) {
        // 整包已固定到具体 commit；版本即该 sha，无需请求网络。
        const QString ver = QStringLiteral("commit ") + branch.left(7);
        auto hint = dlHint(url);
        hint.insert("version", ver);
        hint.insert("versionRaw", branch);
        setDlHint(url, hint);
        emit probeDone(url, -1, ver, branch, true, QString());
        return;
    }

    auto* reply = m_nam->head(makeRequest(QUrl(url)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, branch]() {
        const bool ok = (reply->error() == QNetworkReply::NoError);
        QString err;
        qint64 size = -1;
        QString raw, ver;
        if (ok) {
            if (reply->hasRawHeader("Content-Length")) {
                size = qbaToLongLong(reply->rawHeader("Content-Length"));
            }
            if (branch.length() == 40) {
                raw = branch;                   // raw/文件源：已知 commit sha 作版本
                ver = QStringLiteral("commit ") + branch.left(7);
            } else {
                raw = QString::fromLatin1(
                    reply->rawHeader("ETag")).remove(QLatin1Char('"'));
                if (raw.isEmpty()) {
                    raw = QString::fromLatin1(reply->rawHeader("Last-Modified"));
                }
                ver = raw.isEmpty() ? QStringLiteral("未知") : raw;
            }
        } else {
            err = reply->errorString();
        }
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
    return qVariantMapValue(dlHint(url), QStringLiteral("realSize"), -1).toLongLong();
}

qint64 StickerStore::cachedApproxSize(const QString& url) const
{
    return qVariantMapValue(dlHint(url), QStringLiteral("approxSize"), -1).toLongLong();
}

void StickerStore::seedBuiltinApproxSizes()
{
    for (unsigned i = 0; i < kBuiltinSourceCount; ++i) {
        if (!kBuiltinSources[i].enabled)
            continue;                                  // 预留源不上种子表
        const QString url = QString::fromUtf8(kBuiltinSources[i].url);
        auto hint = dlHint(url);
        if (qVariantMapValue(hint, QStringLiteral("approxSize")).toLongLong() > 0)
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
    task->total = qVariantMapValue(hint, QStringLiteral("total"), -1).toLongLong();
    if (task->total < 0) {
        task->total = -1;
    }
    task->name = qVariantMapValue(hint, QStringLiteral("name")).toString();
    if (task->name.isEmpty()) {
        // 内置源优先其展示名，保证「待安装列表 ↔ 安装后分组」一一对应
        QString builtinName;
        for (unsigned i = 0; i < kBuiltinSourceCount; ++i) {
            if (url == QString::fromUtf8(kBuiltinSources[i].url)) {
                builtinName = QString::fromUtf8(kBuiltinSources[i].name);
                break;
            }
        }
        task->name = builtinName.isEmpty() ? urlDisplayName(url) : builtinName;
    }

    qMkpath(qAbsPath(QFileInfo(partPath)));
    auto* file = new QFile(partPath);
    // 断点续传：offset>0 走追加，否则整文件重写（Qt3 IO_WriteOnly 本身即截断）
    const bool opened = (offset > 0)
        ? qOpenWriteOnlyAppend(*file)
        : qOpenWriteOnly(*file);
    if (!opened) {
        delete task;
        delete file;
        emit downloadFinished(url, false, "无法写入下载目录");
        return;
    }
    task->out = file;

    QNetworkRequest req = makeRequest(QUrl(url));
    if (offset > 0) {
        req.setRawHeader("Range", "bytes=" + qNumberToByteArray(offset) + "-");
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
        if (qIODeviceWrite(*task->out, qbaConstData(chunk), chunk.size())
            != chunk.size()) {
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
                const int slash = qLastIndexOf(cr, QLatin1Char('/'));
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
        const int sp = qIndexOf(cr, QLatin1Char(' '));
        int dash = qIndexOf(cr, QLatin1Char('-'), sp >= 0 ? sp : 0);
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
            task->total = qbaToLongLong(reply->rawHeader("Content-Length"));
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
    if (m_migrating) {
        // 迁移中不启动安装：保留 .part 续传状态（rename 尚未发生），
        // 迁移结束后 UI 显示「继续」，用户可点继续重新安装。
        const QString url = task->url;
        m_tasks.remove(url);
        delete task;
        emit downloadFinished(url, false,
            QStringLiteral("正在迁移存储位置，下载已完成，请稍后点击「继续」"));
        return;
    }
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
    const QString zipPath = partPath.left(partPath.length() - 5)
                            + QStringLiteral(".zip");
    const QString base = stickerBaseDir();

    auto failNow = [&](const QString& msg, bool removeZip) -> InstallResult {
        if (removeZip) QFile::remove(zipPath);
        InstallResult r;
        r.ok = false;
        r.message = msg;
        return r;
    };

    if (!qFileRename(partPath, zipPath)) {
        return failNow(QStringLiteral("下载文件无法落盘"), false);
    }

    // MD5：全量重读 zip（跨续传会话也一致）
    const QByteArray md5 = fileMd5(zipPath);

    // EIF 分流：QQ 表情包 .eif 是 OLE 复合文档（CFB 魔数），
    // 与 zip 包按头部魔数判别后走不同安装链路。
    QFile headF(zipPath);
    QByteArray head8;
    if (qOpenReadOnly(headF)) {
        head8 = qIODeviceReadN(headF, 8);   // zip 魔数，只需前 8 字节
        headF.close();
    }
    if (eifreader::isEifFile(head8)) {
        return runInstallEif(task, zipPath, md5);
    }
    // TGS 分流：单个 .tgs（gzip 压缩 Lottie）裸文件也走专门安装链路。
    // 注：zip 包内的 .tgs 由 importDirectory 的 tgs 分支处理，不需在此分流。
    if (head8.size() >= 2 && uchar(head8.at(0)) == 0x1f
            && uchar(head8.at(1)) == 0x8b) {
        return runInstallTgs(task, zipPath, md5);
    }

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
        const QStringList comps = qStringSplitSkipEmpty(p, QLatin1Char('/'));
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

    // 标题优先「源显示名」（内置源 = 列表展示名，自定义 = URL 派生名），
    // 保证「待安装列表 ↔ 安装后分组」一一对应；非内置且 zip 有唯一顶层目录
    // 时才回退使用目录名。
    QStringList tops;
    for (const auto& fi : infoList) {
        if (!fi.isValid()) continue;
        const int slash = qIndexOf(fi.filePath, QLatin1Char('/'));
        const QString first = slash < 0
            ? fi.filePath : fi.filePath.left(slash);
        if (!tops.contains(first)) tops.append(first);
    }
    QString title = task->name;
    if (title.isEmpty()) {
        title = (tops.size() == 1) ? tops.first() : QString();
        if (title.endsWith(QLatin1Char('/'))) {
            qStringChop(title, 1);
        }
    }
    if (title.isEmpty()) {
        title = urlDisplayName(url);
    }
    title = sanitizeToken(title);

    const QString targetDir = base + QStringLiteral("/packs/") + title;
    if (QFile::exists(targetDir)) {
        if (!qDirRemoveRecursively(QDir(targetDir))) {
            zip.close();
            return failNow(QStringLiteral("无法清理旧包目录"), true);
        }
    }
    if (!qMkpath(targetDir)) {
        zip.close();
        return failNow(QStringLiteral("无法创建包目录"), true);
    }
    if (!zip.extractAll(targetDir)) {
        zip.close();
        return failNow(QStringLiteral("解压失败"), true);
    }
    zip.close();

    QString err;
    bool imported = importDirectory(targetDir, &err);
    if (!imported) {
        // 兼容「zip 包裹 eif」：QQ 表情离线包常见把 .eif 再压一层 zip。
        // 直接导入找不到图片时，扫描 targetDir 内的 *.eif 展开后再导。
        QDirIterator eifIt(targetDir,
            QStringList() << QStringLiteral("*.eif")
                          << QStringLiteral("*.EIF"),
            // Qt3 无 NoDotAndDotDot；此处去掉该位是**行为等价**的：调用点是
            // "*.eif"/"*.EIF" 名字过滤，Qt3 下 "."/".." 本就不匹配该通配
            // （已用探针实测：垫片仅返回 a.eif/b.EIF/sub/deep.eif，无 . 与
            // ..），且 QDir::Files 已排除目录。与上面 scanRecursive 的
            // Files|Dirs 情形不同 —— 那里没有名字过滤，才必须显式跳过。
            QDir::Files,
            QDirIterator::Subdirectories);
        bool expandedAny = false;
        const QStringList eifs = [&] {
            QStringList list;
            while (eifIt.hasNext()) list.append(eifIt.next());
            return list;
        }();
        for (const QString& e : eifs) {
            const QString stem = sanitizeToken(qFileInfoCompleteBaseName(QFileInfo(e)));
            const QString out = targetDir + QLatin1Char('/') + stem;
            QString e2;
            if (eifreader::extractEif(e, out, &e2)) {
                expandedAny = true;
                QFile::remove(e);   // 已展开的 eif 源文件摘除，避免误导入
            }
        }
        if (expandedAny) {
            imported = importDirectory(targetDir, &err);
        }
    }
    if (!imported) {
        return failNow(QStringLiteral("导入失败：") + (err.isEmpty()
                 ? QStringLiteral("无可用图片") : err), true);
    }

    // 取 packId：importDirectory 以标题复用/新建，标题==targetDir 名
    QString packId;
    for (const auto& p : stickerDb().list_packs(-1)) {
        if (QString::fromUtf8(p.title.c_str()) == title) {
            packId = qFromStdString(p.id);
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
    r.md5Hex = QString::fromLatin1(qbaToHex(md5));

    // A2 内容变化检测：同源同包且 md5 变化 → 附加提示
    const QVariantMap oldMeta = QSettings().value(
        QStringLiteral("downloadedPackMeta/") + packId).toMap();
    const QString oldMd5 = qVariantMapValue(oldMeta, QStringLiteral("md5")).toString();
    if (!oldMd5.isEmpty()
            && qVariantMapValue(oldMeta, QStringLiteral("url")).toString() == url
            && oldMd5 != r.md5Hex) {
        r.note = QStringLiteral("（远端内容已变化，已覆盖安装）");
    }

    QFile::remove(zipPath);
    return r;
}

StickerStore::InstallResult StickerStore::runInstallEif(
    DownloadTask* task, const QString& eifPath, const QByteArray& md5)
{
    const QString url = task->url;
    const QString base = stickerBaseDir();

    // 标题同 zip 链路：源显示名优先，回退 URL 派生名
    QString title = task->name;
    if (title.isEmpty()) {
        title = urlDisplayName(url);
    }
    title = sanitizeToken(title);

    const QString targetDir = base + QStringLiteral("/packs/") + title;
    if (QFile::exists(targetDir)) {
        if (!qDirRemoveRecursively(QDir(targetDir))) {
            return {false, QStringLiteral("无法清理旧包目录"), {}, {},
                    {}, {}, {}};
        }
    }
    if (!qMkpath(targetDir)) {
        return {false, QStringLiteral("无法创建包目录"), {}, {}, {}, {}, {}};
    }

    QString err;
    int imageCount = 0;
    if (!eifreader::extractEif(eifPath, targetDir, &err, &imageCount)) {
        QFile::remove(eifPath);
        return {false, QStringLiteral("导入失败：") + (err.isEmpty()
                 ? QStringLiteral("无可用图片") : err), {}, {},
                 {}, {}, {}};
    }

    if (!importDirectory(targetDir, &err)) {
        QFile::remove(eifPath);
        return {false, QStringLiteral("导入失败：") + (err.isEmpty()
                 ? QStringLiteral("无可用图片") : err), {}, {},
                 {}, {}, {}};
    }

    QString packId;
    for (const auto& p : stickerDb().list_packs(-1)) {
        if (QString::fromUtf8(p.title.c_str()) == title) {
            packId = qFromStdString(p.id);
            break;
        }
    }
    if (packId.isEmpty()) {
        QFile::remove(eifPath);
        return {false, QStringLiteral("分组标识丢失"), {}, {}, {}, {}, {}};
    }

    InstallResult r;
    r.ok = true;
    r.message = title;
    r.packId = packId;
    r.dir = targetDir;
    r.total = QFileInfo(eifPath).size();
    r.md5Hex = QString::fromLatin1(qbaToHex(md5));

    const QVariantMap oldMeta = QSettings().value(
        QStringLiteral("downloadedPackMeta/") + packId).toMap();
    const QString oldMd5 = qVariantMapValue(oldMeta, QStringLiteral("md5")).toString();
    if (!oldMd5.isEmpty()
            && qVariantMapValue(oldMeta, QStringLiteral("url")).toString() == url
            && oldMd5 != r.md5Hex) {
        r.note = QStringLiteral("（远端内容已变化，已覆盖安装）");
    }

    QFile::remove(eifPath);
    return r;
}

StickerStore::InstallResult StickerStore::runInstallTgs(
    DownloadTask* task, const QString& tgsPath, const QByteArray& md5)
{
    const QString url = task->url;
    const QString base = stickerBaseDir();

    // 标题同 zip 链路：源显示名优先，回退 URL 派生名
    QString title = task->name;
    if (title.isEmpty()) {
        title = urlDisplayName(url);
    }
    title = sanitizeToken(title);

    const QString targetDir = base + QStringLiteral("/packs/") + title;
    if (QFile::exists(targetDir)) {
        if (!qDirRemoveRecursively(QDir(targetDir))) {
            return {false, QStringLiteral("无法清理旧包目录"), {}, {},
                    {}, {}, {}};
        }
    }
    if (!qMkpath(targetDir)) {
        return {false, QStringLiteral("无法创建包目录"), {}, {}, {}, {}, {}};
    }

    // 单文件 tgs 落盘，随后的 importDirectory 会命中 tgs 分支生成占位
    const QString tgsDst = targetDir + QStringLiteral("/") + title
                           + QStringLiteral(".tgs");
    if (!qFileCopy(tgsPath, tgsDst)) {
        return {false, QStringLiteral("导入失败：tgs 文件无法落盘"), {}, {},
                {}, {}, {}};
    }

    QString err;
    if (!importDirectory(targetDir, &err)) {
        return {false, QStringLiteral("导入失败：") + (err.isEmpty()
                 ? QStringLiteral("无可用图片") : err), {}, {},
                 {}, {}, {}};
    }

    QString packId;
    for (const auto& p : stickerDb().list_packs(-1)) {
        if (QString::fromUtf8(p.title.c_str()) == title) {
            packId = qFromStdString(p.id);
            break;
        }
    }
    if (packId.isEmpty()) {
        QFile::remove(tgsPath);
        return {false, QStringLiteral("分组标识丢失"), {}, {}, {}, {}, {}};
    }

    InstallResult r;
    r.ok = true;
    r.message = title;
    r.packId = packId;
    r.dir = targetDir;
    r.total = QFileInfo(tgsPath).size();
    r.md5Hex = QString::fromLatin1(qbaToHex(md5));

    const QVariantMap oldMeta = QSettings().value(
        QStringLiteral("downloadedPackMeta/") + packId).toMap();
    const QString oldMd5 = qVariantMapValue(oldMeta, QStringLiteral("md5")).toString();
    if (!oldMd5.isEmpty()
            && qVariantMapValue(oldMeta, QStringLiteral("url")).toString() == url
            && oldMd5 != r.md5Hex) {
        r.note = QStringLiteral("（远端内容已变化，已覆盖安装）");
    }

    QFile::remove(tgsPath);
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
        meta.insert("version", qVariantMapValue(hint, QStringLiteral("version"), QStringLiteral("未知")));
        meta.insert("versionRaw", qVariantMapValue(hint, QStringLiteral("versionRaw")));
        meta.insert("md5", r.md5Hex);
        meta.insert("dir", r.dir);
        meta.insert("dl_time", qDateTimeEpochSecs());
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
        qUtf8Printable(packId), installed ? 1 : 0);
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
        dir = qVariantMapValue(packMeta(packId), QStringLiteral("dir")).toString();
    }
    const bool ok = stickerDb().delete_pack(qUtf8Printable(packId));
    if (ok) {
        if (removeFiles && !dir.isEmpty()) {
            qDirRemoveRecursively(QDir(dir));
        }
        QSettings settings;
        QStringList list = settings.value(
            QStringLiteral("downloadedPacks")).toStringList();
        // 卸载（保留文件）也从已下载列表移除；元数据仅彻底删除时清除
        qStringListRemoveAll(list, packId);
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
        const QStringList parts =
            qDirEntryList(dlDir, QStringLiteral("*.part"), QDir::Files);
        for (const QString& name : parts) {
            const QString key = name.left(name.length() - 5); // 去掉 .part
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
        qUtf8Printable(packId), "rowid DESC", 0, 0, 0, nullptr);
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

namespace {
// 归一化内置源 URL：剥任意 gh-proxy 代理前缀（com/org），防改代理导致匹配失效
QString normalizeSourceUrl(const QString& url)
{
    static const QStringList kGhProxyPrefixes = qStringListBuild(
        QStringLiteral("https://gh-proxy.com/"),
        QStringLiteral("https://gh-proxy.org/"));
    for (const auto& prefix : kGhProxyPrefixes) {
        if (url.startsWith(prefix))
            return url.mid(prefix.length());
    }
    return url;
}
} // namespace

bool StickerStore::isBuiltinSourcePack(const QString& packId,
                                       const QString& title) const
{
    const QString url = qVariantMapValue(packMeta(packId), QStringLiteral("url")).toString();
    const QString urlNorm = normalizeSourceUrl(url);
    for (unsigned i = 0; i < kBuiltinSourceCount; ++i) {
        // 保持现有：下载安装写入的 url 元数据
        const bool urlHit = !url.isEmpty()
            && urlNorm == normalizeSourceUrl(
                   QString::fromUtf8(kBuiltinSources[i].url));
        // 新增：自带 name 匹配当前目录标题
        const QString name = QString::fromUtf8(kBuiltinSources[i].name);
        const bool nameHit = title == name || title == sanitizeDirName(name);
        if (urlHit || nameHit)
            return true;
    }
    return false;
}

QString StickerStore::builtinSourceDiag(const QString& packId,
                                        const QString& title) const
{
    static const QSet<QString> s_builtinUrls = []() {   // 与 isBuiltinSourcePack 同口径
        QSet<QString> set;
        for (unsigned i = 0; i < kBuiltinSourceCount; ++i)
            set.insert(normalizeSourceUrl(QString::fromUtf8(kBuiltinSources[i].url)));
        return set;
    }();

    const QString url = qVariantMapValue(packMeta(packId), QStringLiteral("url")).toString();
    const QString urlNorm = normalizeSourceUrl(url);
    const bool urlHit = !url.isEmpty() && s_builtinUrls.contains(urlNorm);

    bool titleHit = false;
    for (unsigned i = 0; i < kBuiltinSourceCount; ++i) {
        const QString name = QString::fromUtf8(kBuiltinSources[i].name);
        if (title == name || title == sanitizeDirName(name)) {
            titleHit = true;
            break;
        }
    }

    return QStringLiteral("id=%1 title=%2 meta.url=%3 urlNorm=%4 urlHit=%5 builtinTitleHit=%6")
        .arg(packId)
        .arg(title)
        .arg(url.isEmpty() ? QStringLiteral("(empty)") : url)
        .arg(urlNorm.isEmpty() ? QStringLiteral("(empty)") : urlNorm)
        .arg(urlHit ? QStringLiteral("YES") : QStringLiteral("NO"))
        .arg(titleHit ? QStringLiteral("YES") : QStringLiteral("NO"));
}
