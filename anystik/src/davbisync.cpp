#include "davbisync.h"
#include "stickerstore.h"
#include "qwebdav.h"
#include "qwebdavdirparser.h"
#include "qwebdavitem.h"

#include <QDateTime>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QPair>
#include <QSet>
#include <QNetworkRequest>
#include <QNetworkReply>

namespace {

QString formatBytes(qint64 n)
{
    static const char* units[] = { "B", "KB", "MB", "GB" };
    double v = double(n);
    int u = 0;
    while (v >= 1024.0 && u < 3) {
        v /= 1024.0;
        ++u;
    }
    if (u == 0)
        return QStringLiteral("%1 B").arg(qint64(v));
    return QStringLiteral("%1 %2").arg(v, 0, 'f', 1)
        .arg(QString::fromLatin1(units[u]));
}

QString formatDuration(qint64 msec)
{
    const qint64 s = qMax<qint64>(0, msec) / 1000;
    const qint64 h = s / 3600;
    const qint64 m = (s % 3600) / 60;
    const qint64 ss = s % 60;
    if (h > 0) {
        return QStringLiteral("%1:%2:%3").arg(h)
            .arg(m, 2, 10, QLatin1Char('0')).arg(ss, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(m)
        .arg(ss, 2, 10, QLatin1Char('0'));
}

} // namespace

SyncEngine::SyncEngine(QObject* parent)
    : QObject(parent)
    , m_webdav(new QWebdav(this))
    , m_parser(new QWebdavDirParser(this))
{
    // 云端目录逐目录扫描状态机：parser 每次列一个目录，finished 后收集并调度下一目录
    connect(m_parser, &QWebdavDirParser::finished, this,
            [this]() {
                if (!m_running) {
                    return;
                }
                if (m_scanCurrentDir.isEmpty()) {
                    return;    // 非扫描阶段（空闲 finish，忽略）
                }
                collectCloudItems();
            });

    connect(m_parser, &QWebdavDirParser::errorChanged, this,
            [this](const QString& line) {
                log(davbisync::Warn, QStringLiteral("webdav"), line);
            });
}

SyncEngine::~SyncEngine() = default;

bool SyncEngine::isCloudAvailable() const
{
    return m_webdav != nullptr && m_parser != nullptr;
}

bool SyncEngine::setConnectionSettings(int connectionType,
                                       const QString& hostname,
                                       const QString& rootPath,
                                       const QString& username,
                                       const QString& password,
                                       int port,
                                       bool useSsl)
{
    m_host = hostname;
    m_rootPath = rootPath;
    m_username = username;
    m_password = password;
    m_port = port;
    m_useSsl = useSsl;

    const QWebdav::QWebdavConnectionType ctype =
        (connectionType == 2) ? QWebdav::HTTPS : QWebdav::HTTP;

    m_webdav->setConnectionSettings(ctype, hostname, rootPath,
                                    username, password, port);
    return true;
}

bool SyncEngine::startSync()
{
    if (!m_webdav || !m_parser) {
        return false;
    }
    if (m_running) {
        return false;
    }
    m_running = true;
    m_finished = false;
    m_percent = 0;
    m_startMsec = QDateTime::currentMSecsSinceEpoch();
    m_fileStartMsec = m_startMsec;
    m_lastFileMs = 0;

    m_cloudReady = false;
    m_cloudFiles.clear();
    m_localFiles.clear();
    m_cloudToLocal.clear();
    m_scannedCloudDirs.clear();
    m_pendingDirs.clear();
    m_scanCurrentDir.clear();
    m_cloudFileCount = 0;
    m_localFileCount = 0;
    m_localDirPacks.clear();
    m_diagnosis = davbisync::SyncDiagnosis();
    davbisync::baselineLoad(&m_base);
    m_uploadQueue.clear();
    m_downloadQueue.clear();
    m_pendingCloudRenames.clear();
    m_ensuredDirs.clear();
    m_uploadIndex = 0;
    m_uploadDone = 0;
    m_uploadSkipped = 0;
    m_downloadSkipped = 0;
    m_uploadTotal = 0;
    m_downloadDone = 0;
    m_downloadIndex = 0;
    m_mkdirChain.clear();
    m_activeReplies.clear();
    m_tempFiles.clear();

    emit progressUpdated(0, QStringLiteral("scan"),
                         QStringLiteral("listing local packs"));

    // 阶段 A：本地清单（同步，SQLite + stat）
    buildLocalListing();

    // 阶段 B：云端清单（异步逐目录）
    scanCloudDir(m_cloudRoot);
    return true;
}

void SyncEngine::abort()
{
    if (!m_running || m_finished) {
        return;
    }
    m_running = false;
    if (m_parser) {
        m_parser->abort();
    }
    // 取消在途网络请求（其 finished 回调会带 deleteLater 返回，无需额外处理）
    for (QNetworkReply* r : m_activeReplies) {
        if (r) {
            r->abort();
        }
    }
    m_activeReplies.clear();
    for (const QString& t : m_tempFiles) {
        QFile::remove(t);
    }
    m_tempFiles.clear();
    emit progressUpdated(m_percent, QStringLiteral("cancelled"),
                         QStringLiteral("cancel requested"));
    emit finished(2, QStringLiteral("cancelled by user"));
    m_finished = true;
}

void SyncEngine::setPollingDelay(int ms)
{
    Q_UNUSED(ms);
}

void SyncEngine::setPackPolicies(
    const QMap<QString, davbisync::PackPolicy>& policies)
{
    m_packPolicies = policies;
}

void SyncEngine::setRunning(bool running)
{
    m_running = running;
}

// ═══════════════════════════════════════════════════════════════════
// 阶段 A：本地清单
// ═══════════════════════════════════════════════════════════════════

void SyncEngine::buildLocalListing()
{
    m_localFiles.clear();
    m_cloudToLocal.clear();
    m_localDirPacks.clear();
    m_localFileCount = 0;

    const auto packs = StickerStore::instance()->packs(1, "title ASC");
    int dirs = 0;
    for (const auto& pack : packs) {
        if (StickerStore::instance()->isBuiltinSourcePack(pack.id)) {
            log(davbisync::Info, QStringLiteral("pack"),
                QStringLiteral("skip builtin-source pack: %1").arg(pack.title));
            continue;
        }
        // M5：包级策略（空 map = 默认全参与）
        const davbisync::PackPolicy policy =
            m_packPolicies.value(pack.id);
        if (m_packPolicies.contains(pack.id) && !policy.participates()) {
            log(davbisync::Info, QStringLiteral("pack"),
                QStringLiteral("skip by policy (pull&push off): %1")
                    .arg(pack.title));
            m_diagnosis.skipped.append(pack.title);
            continue;
        }
        const QString cloudRoot =
            m_packPolicies.contains(pack.id) ? policy.cloudRoot() : m_cloudRoot;
        const QString cloudDir =
            cloudRoot + QLatin1Char('/')
            + StickerStore::sanitizeDirName(pack.title);
        m_localDirPacks.insert(cloudDir, pack.id);
        if (m_packPolicies.contains(pack.id) && !policy.pushEnabled) {
            continue;    // 不上传（pull 方向仍可）
        }
        const auto stickers = StickerStore::instance()->stickers(pack.id);
        for (const auto& s : stickers) {
            const QString localAbs =
                StickerStore::instance()->resolveStickerPath(s.filePath);
            const QFileInfo fi(localAbs);
            if (!fi.exists() || !fi.isFile()) {
                continue;
            }
            const QString rel = cloudDir + QLatin1Char('/') + fi.fileName();
            davbisync::BaselineEntry e;
            e.size = fi.size();
            e.mtimeMsec = fi.lastModified().toMSecsSinceEpoch();
            m_localFiles.insert(rel, e);
            m_cloudToLocal.insert(rel, localAbs);
        }
        ++dirs;
    }
    m_localFileCount = m_localFiles.size();
    log(davbisync::Info, QStringLiteral("scan"),
        QStringLiteral("local listing: %1 packs, %2 files (builtin-source packs excluded)")
            .arg(dirs).arg(m_localFileCount));
}

// ═══════════════════════════════════════════════════════════════════
// 阶段 B：云端清单（逐目录 listDirectory）
// ═══════════════════════════════════════════════════════════════════

void SyncEngine::scanCloudDir(const QString& cloudRelDir)
{
    if (!m_running) {
        return;
    }
    QString dir = cloudRelDir;
    if (!dir.endsWith(QLatin1Char('/'))) {
        dir += QLatin1Char('/');
    }
    m_scanCurrentDir = dir;
    emit progressUpdated(0, QStringLiteral("scan"),
                         QStringLiteral("listing %1").arg(cloudRelDir));
    probeServer();   // 阶段 B 起点，轻量 OPTIONS，仅首目录触发一次
    if (!m_parser->listDirectory(m_webdav, dir, false)) {
        finishWithError(QStringLiteral("cloud listing failed to start: %1")
                            .arg(cloudRelDir));
    }
}

void SyncEngine::collectCloudItems()
{
    const QString dirRel = canonicalRel(m_scanCurrentDir);
    m_scanCurrentDir.clear();

    const auto items = m_parser->getList();
    for (const auto& item : items) {
        const QString rel = canonicalRel(item.path());
        if (rel.isEmpty()) {
            continue;
        }
        if (item.isDir()) {
            if (rel != dirRel && !m_scannedCloudDirs.contains(rel)) {
                m_pendingDirs.append(QPair<QString, QString>(rel, QString()));
            }
        } else {
            davbisync::BaselineEntry e;
            e.size = qint64(item.size());
            e.mtimeMsec = item.lastModified().toMSecsSinceEpoch();
            if (e.mtimeMsec > 0) {
                m_cloudMtimeAvail = true;
            }
            // 一个目录内的重复列出（服务端不一致）以首次为准（insert 不覆盖）
            if (!m_cloudFiles.contains(rel)) {
                m_cloudFiles.insert(rel, e);
                ++m_cloudFileCount;
            }
        }
    }
    m_scannedCloudDirs.insert(dirRel);

    log(davbisync::Info, QStringLiteral("scan"),
        QStringLiteral("cloud dir %1: %2 files").arg(dirRel)
            .arg(m_cloudFiles.size()));
    emit treeScanned(m_localFileCount + m_cloudFileCount,
                     m_scannedCloudDirs.size());

    processPendingCloudDirs();
}

void SyncEngine::processPendingCloudDirs()
{
    if (!m_running) {
        return;
    }
    while (!m_pendingDirs.isEmpty()) {
        const auto d = m_pendingDirs.takeFirst();
        if (m_scannedCloudDirs.contains(d.first)) {
            continue;
        }
        scanCloudDir(d.first);
        return;
    }
    finishCloudScan();
}

void SyncEngine::finishCloudScan()
{
    if (!m_running) {
        return;
    }
    m_cloudReady = true;
    if (m_cloudFileCount > 0) {
        m_cloudMtimeDecided = true;    // 云端有文件 → getlastmodified 结论可定
        updateRemoteFeature();
    }
    log(davbisync::Info, QStringLiteral("scan"),
        QStringLiteral("cloud listing done: %1 files in %2 dirs")
            .arg(m_cloudFiles.size()).arg(m_scannedCloudDirs.size()));

    // 阶段 C：diff + 冲突处置
    buildOpQueue();

    // 阶段 D：应用
    pushNext();
}

// ═══════════════════════════════════════════════════════════════════
// 阶段 C：diff（幂等 put/get；双侧皆变 → 云端旧版改名 .conflictN 保留）
// ═══════════════════════════════════════════════════════════════════

void SyncEngine::buildOpQueue()
{
    m_uploadQueue.clear();
    m_downloadQueue.clear();
    m_conflictRelCloud.clear();
    m_conflictNames.clear();

    // 统一双向 diff：以基线(状态文件)作参照，无状态文件 → 空列表(一切条目均为新增)。
    // 无任何模式/首次分支；空状态下凡「两侧都有且 size 不同」→ 双侧新增且不一致 → 冲突。
    davbisync::SyncBaseline& base = m_base;

    const auto mtimeDiffer = [](qint64 cur, qint64 baseMsec) {
        return cur > 0 && baseMsec > 0 && cur != baseMsec;
    };
    // 判据 = size 不同，或 size 相同但两侧 mtime 均有效且不同（第二判据，防
    // size-only 漏报）。任一侧 mtime 无效（0）→ 仅 size（无 mtime 服务器回退）。
    const auto localChanged = [&base, &mtimeDiffer](
        const QString& rel, qint64 size, qint64 mtimeMsec) {
        const auto it = base.local.constFind(rel);
        if (it == base.local.constEnd()) {
            return true;
        }
        return it.value().size != size
            || mtimeDiffer(mtimeMsec, it.value().mtimeMsec);
    };
    const auto cloudChanged = [&base, &mtimeDiffer](
        const QString& rel, qint64 size, qint64 mtimeMsec) {
        const auto it = base.cloud.constFind(rel);
        if (it == base.cloud.constEnd()) {
            return true;
        }
        return it.value().size != size
            || mtimeDiffer(mtimeMsec, it.value().mtimeMsec);
    };

    for (auto it = m_localFiles.cbegin(); it != m_localFiles.cend(); ++it) {
        const QString rel = it.key();
        const qint64 localSize = it.value().size;
        const auto cit = m_cloudFiles.constFind(rel);
        if (cit == m_cloudFiles.constEnd()) {
            // 云端无此文件 → 上传
            m_uploadQueue.append(QPair<QString, QString>(
                m_cloudToLocal.value(rel), rel));
            continue;
        }
        const qint64 cloudSize = cit.value().size;
        if (cloudSize == localSize) {
            const qint64 cloudMsec = cit.value().mtimeMsec;
            const qint64 localMsec = it.value().mtimeMsec;
            const bool mtimeEqual =
                cloudMsec > 0 && localMsec > 0 && cloudMsec == localMsec;
            if (cloudMsec <= 0 || localMsec <= 0 || mtimeEqual) {
                // 相等预检通过：size 同，且（一侧无 mtime → size-only 一致）
                // 或 mtime 亦同 → 跳过
                ++m_uploadSkipped;
                continue;
            }
            // size 相同但双侧 mtime 有效且不同 -> 落入下方 changed 判卷
            // （双侧皆变且当前不一致 → 冲突双保留）
        }
        const bool lc = localChanged(rel, localSize, it.value().mtimeMsec);
        const bool cc = cloudChanged(rel, cloudSize, cit.value().mtimeMsec);
        if (lc && cc) {
            // 双侧皆变且不一致 → 冲突：云端旧版改名 .conflictN 保留，本地新版本上传覆盖
            m_conflictRelCloud.append(rel);
            const QString dirBase = rel.section(QLatin1Char('/'), 0, -2);
            const QString fileBase = rel.section(QLatin1Char('/'), -1);
            const QString newRel = nextConflictName(dirBase, fileBase);
            m_pendingCloudRenames.append(QPair<QString, QString>(rel, newRel));
            m_uploadQueue.append(QPair<QString, QString>(
                m_cloudToLocal.value(rel), rel));
            m_diagnosis.conflicts
                .append(QStringLiteral("%1 (local %2 B vs cloud %3 B; cloud kept as %4)")
                            .arg(rel).arg(localSize).arg(cloudSize).arg(newRel));
            log(davbisync::Warn, QStringLiteral("conflict"),
                QStringLiteral("%1 changed on both sides; "
                               "keeping cloud version as %2, uploading local")
                    .arg(rel, newRel));
            continue;
        }
        if (lc) {
            // 仅本地变化 → 上传覆盖（云端旧版为基线一致版，rclone ->winner 语义）
            m_uploadQueue.append(QPair<QString, QString>(
                m_cloudToLocal.value(rel), rel));
            continue;
        }
        if (cc) {
            // 仅云端变化 → 下载
            m_downloadQueue.append(QPair<QString, QString>(
                rel, m_cloudToLocal.value(rel)));
            continue;
        }
        ++m_uploadSkipped;   // 理论不可达（size 不同则至少一侧 changed）
    }

    // 云端新增、本地无对应 → 下载（受包级 pullEnabled 控制）
    for (auto cit = m_cloudFiles.cbegin(); cit != m_cloudFiles.cend(); ++cit) {
        const QString rel = cit.key();
        if (m_localFiles.contains(rel)) {
            continue;
        }
        const QString dirRel = rel.section(QLatin1Char('/'), 0, -2);
        const QString packId = m_localDirPacks.value(dirRel);
        if (!packId.isEmpty() && m_packPolicies.contains(packId)) {
            const davbisync::PackPolicy p = m_packPolicies.value(packId);
            if (!p.pullEnabled) {
                log(davbisync::Info, QStringLiteral("pack"),
                    QStringLiteral("skip download by policy: %1").arg(rel));
                ++m_downloadSkipped;
                continue;
            }
        }
        m_downloadQueue.append(QPair<QString, QString>(rel, QString()));
    }

    m_uploadTotal = m_uploadQueue.size();
    log(davbisync::Info, QStringLiteral("scan"),
        QStringLiteral("op queue: %1 uploads, %2 downloads, %3 conflicts")
            .arg(m_uploadQueue.size()).arg(m_downloadQueue.size())
            .arg(m_conflictRelCloud.size()));
}

QString SyncEngine::nextConflictName(const QString& dirBase,
                                     const QString& fileBase)
{
    // rclone num 风格：file.conflict1 / file.conflict2 …（时间序自动编号）
    int n = 1;
    QString candidate = dirBase + QLatin1Char('/') + fileBase
                        + QStringLiteral(".conflict%1").arg(n);
    while (m_conflictNames.contains(candidate)
           || m_cloudFiles.contains(candidate)) {
        ++n;
        candidate = dirBase + QLatin1Char('/') + fileBase
                    + QStringLiteral(".conflict%1").arg(n);
    }
    m_conflictNames.append(candidate);
    return candidate;
}

// ═══════════════════════════════════════════════════════════════════
// 阶段 D：应用
// ═══════════════════════════════════════════════════════════════════

void SyncEngine::pushNext()
{
    if (!m_running) {
        return;
    }

    // 1) 冲突改名（先于上传，避免云端旧版被新上传覆盖后改名丢失）
    if (!m_pendingCloudRenames.isEmpty()) {
        const auto item = m_pendingCloudRenames.takeFirst();
        const QString from = item.first;
        const QString to = item.second;
        QNetworkReply* reply = m_webdav->move(from, to);
        m_activeReplies.append(reply);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, from, to]() {
                    removeActiveReply(reply);
                    if (!m_running) {
                        return;
                    }
                    if (reply->error() == QNetworkReply::NoError) {
                        m_cloudFiles.insert(to, m_cloudFiles.value(from));
                        m_cloudFiles.remove(from);
                        log(davbisync::Info, QStringLiteral("conflict"),
                            QStringLiteral("cloud rename %1 → %2")
                                .arg(from, to));
                    } else {
                        log(davbisync::Warn, QStringLiteral("conflict"),
                            QStringLiteral("cloud rename failed %1: %2")
                                .arg(from, reply->errorString()));
                    }
                    pushNext();
                });
        return;
    }

    // 2) 上传（目录链未就位时先逐层 mkdir）
    if (m_uploadIndex < m_uploadQueue.size()) {
        const auto& item = m_uploadQueue.at(m_uploadIndex);
        const QString localPath = item.first;
        const QString cloudPath = item.second;
        const QString cloudDir = cloudPath.section(QLatin1Char('/'), 0, -2);
        if (m_mkdirChain.isEmpty() && !m_ensuredDirs.contains(cloudDir)) {
            // 构建「根目录 → cloudDir」的完整待建链，顶层在前
            m_mkdirChain.clear();
            QString cur;
            const auto parts = cloudDir.split(QLatin1Char('/'));
            for (const QString& seg : parts) {
                if (seg.isEmpty()) {
                    continue;
                }
                cur = (cur.isEmpty() ? seg : cur + QLatin1Char('/') + seg);
                if (!m_ensuredDirs.contains(cur)) {
                    m_mkdirChain.append(cur);
                }
            }
        }
        if (!m_mkdirChain.isEmpty()) {
            mkdirNextChain();
            return;
        }
        checkAndUpload(localPath, cloudPath);
        return;
    }

    // 3) 下载
    if (m_downloadIndex < m_downloadQueue.size()) {
        const auto& item = m_downloadQueue.at(m_downloadIndex);
        downloadFile(item.first, QString());
        return;
    }

    finishOk(QStringLiteral("uploaded %1, skipped %2, downloaded %3")
             .arg(m_uploadDone)
             .arg(m_uploadSkipped + m_downloadSkipped)
             .arg(m_downloadDone));
}

void SyncEngine::mkdirNextChain()
{
    if (m_mkdirChain.isEmpty()) {
        pushNext();
        return;
    }
    const QString dir = m_mkdirChain.constFirst();
    QNetworkReply* reply = m_webdav->mkdir(dir);
    m_activeReplies.append(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, dir]() {
                removeActiveReply(reply);
                if (!m_running) {
                    return;
                }
                if (reply->error() != QNetworkReply::NoError) {
                    log(davbisync::Warn, QStringLiteral("mkdir"),
                        QStringLiteral("%1: %2").arg(dir, reply->errorString()));
                }
                // 无论成败都标记 ensured（继续建子层；put 前真正缺失会以 HTTP 错误上报）
                m_ensuredDirs.append(dir);
                m_mkdirChain.removeFirst();
                pushNext();
            });
}

void SyncEngine::checkAndUpload(const QString& localPath, const QString& cloudPath)
{
    QNetworkRequest req(cloudUrl(cloudPath));
    QNetworkReply* reply = m_webdav->head(req);
    m_activeReplies.append(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, localPath, cloudPath]() {
                removeActiveReply(reply);
                if (!m_running) {
                    return;
                }
                if (reply->error() == QNetworkReply::ContentNotFoundError) {
                    uploadFile(localPath, cloudPath);      // 云端不存在 → 直接上传
                    return;
                }
                if (reply->error() != QNetworkReply::NoError) {
                    finishWithError(QStringLiteral("webdav check failed: %1 (%2)")
                                        .arg(cloudPath, reply->errorString()));
                    return;
                }
                const qint64 remoteSize =
                    reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
                const QFileInfo fi(localPath);
                // HEAD 幂等判定读 size + mtime 双值（与 buildOpQueue 同口径）：
                // skip 当且仅当「大小与两侧 mtime 均相对基线未变」。两侧各自与基线比
                //（云 mtime 不与本地 mtime 直接比——上传后云 last-modified 为服务器
                // 时刻，互比会每轮空转重传）。
                bool cloudUnchanged = false;
                const qint64 headMsec = [reply]() -> qint64 {
                    const QByteArray lm = reply->rawHeader("Last-Modified");
                    if (lm.isEmpty()) {
                        return 0;
                    }
                    const QDateTime t = QDateTime::fromString(
                        QString::fromLatin1(lm), Qt::RFC2822Date);
                    return t.isValid() ? t.toMSecsSinceEpoch() : 0;
                }();
                const qint64 baseCloudMsec =
                    m_cloudFiles.value(cloudPath).mtimeMsec;
                if (headMsec > 0) {
                    // 服务器回 mtime：与基线一致才视为云端未变；
                    // run 中途被第三方改动（mtime 漂移）→ 需重传
                    cloudUnchanged = (headMsec == baseCloudMsec);
                } else if (baseCloudMsec <= 0) {
                    // 纯 size-only 型服务器（两侧均无 mtime）→ 大小一致即视为一致
                    cloudUnchanged = true;
                } else {
                    // 扫描能拿到 mtime 但 HEAD 不回 → 无法确认，保守视为已变
                    cloudUnchanged = false;
                }
                bool localUnchanged = false;
                const qint64 localMsec = fi.lastModified().toMSecsSinceEpoch();
                const auto bit = m_base.local.constFind(cloudPath);
                if (!localMsec) {
                    localUnchanged = true;      // 本地 mtime 不可用 → size-only 语义
                } else if (bit == m_base.local.constEnd()) {
                    localUnchanged = false;     // 无本地基线 → 视为已变（dst 为准）
                } else {
                    localUnchanged = (localMsec == bit.value().mtimeMsec);
                }
                if (remoteSize == fi.size() && cloudUnchanged && localUnchanged) {
                    ++m_uploadSkipped;
                    ++m_uploadIndex;
                    m_fileStartMsec = QDateTime::currentMSecsSinceEpoch(); // 跳过不算文件用时
                    emitProgress();
                    pushNext();
                    return;
                }
                uploadFile(localPath, cloudPath);   // 缺失/大小不同/任一侧相对基线已变 → 上传覆盖
            });
}

void SyncEngine::uploadFile(const QString& localPath, const QString& cloudPath)
{
    m_fileStartMsec = QDateTime::currentMSecsSinceEpoch();
    const QFileInfo finfo(localPath);
    log(davbisync::Info, QStringLiteral("upload"),
        QStringLiteral("上传 %1 · %2 · 第 %3/%4")
            .arg(finfo.fileName())
            .arg(formatBytes(finfo.size()))
            .arg(m_uploadIndex + 1)
            .arg(m_uploadTotal));

    QFile* file = new QFile(localPath);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        finishWithError(QStringLiteral("cannot open local file: %1").arg(localPath));
        return;
    }
    // 上传不 set Date 头（远端 last-modified 只读不改写；dt 缺省为无效
    // QDateTime，qwebdav 内部 dt.isValid() 分支跳过），云 mtime 交服务器生成
    QNetworkReply* reply = m_webdav->put(cloudPath, file);
    m_activeReplies.append(reply);
    connect(reply, &QNetworkReply::uploadProgress, this,
            [this, reply](qint64 done, qint64 total) {
                Q_UNUSED(reply)
                if (m_running && total > 0) {
                    const double frac =
                        double(m_uploadIndex) / qMax(1, m_uploadQueue.size());
                    const double inc = (total > 0 ? double(done) / double(total) : 0.0)
                        / qMax(1, m_uploadQueue.size());
                    m_percent = int(100.0 * (frac + inc));
                    emit progressUpdated(m_percent, QStringLiteral("upload"), statDetail());
                }
            });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, file, localPath, cloudPath]() {
                removeActiveReply(reply);
                file->deleteLater();
                if (!m_running) {
                    return;
                }
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (reply->error() != QNetworkReply::NoError
                        || (status >= 400)) {
                    finishWithError(QStringLiteral("upload failed: %1 (%2 status %3)")
                                        .arg(localPath, reply->errorString()).arg(status));
                    return;
                }
                m_lastFileMs = QDateTime::currentMSecsSinceEpoch() - m_fileStartMsec;
                log(davbisync::Info, QStringLiteral("upload"),
                    QStringLiteral("完成 %1 · 用时 %2 s")
                        .arg(QFileInfo(localPath).fileName())
                        .arg(m_lastFileMs / 1000.0, 0, 'f', 1));
                // 更新云清单，保证 finishOk 的基线反映真实云端
                // 写后回读：PUT 响应若带 Last-Modified 头直接回填真实云 mtime
                // （远端时间戳只读不改写；解析失败则置 0 等下一轮扫描回填）
                davbisync::BaselineEntry e;
                e.size = QFileInfo(localPath).size();
                e.mtimeMsec = 0;
                const QByteArray lm = reply->rawHeader("Last-Modified");
                if (!lm.isEmpty()) {
                    const QDateTime t =
                        QDateTime::fromString(QString::fromLatin1(lm), Qt::RFC2822Date);
                    if (t.isValid()) {
                        e.mtimeMsec = t.toMSecsSinceEpoch();
                        m_putMtimeEcho = true;
                    }
                }
                if (!m_putEchoDecided) {
                    m_putEchoDecided = true;
                    updateRemoteFeature();
                }
                m_cloudFiles.insert(cloudPath, e);
                ++m_uploadDone;
                ++m_uploadIndex;
                emitProgress();
                pushNext();
            });
}

void SyncEngine::downloadFile(const QString& cloudRel, const QString& _localAbs)
{
    Q_UNUSED(_localAbs)
    m_fileStartMsec = QDateTime::currentMSecsSinceEpoch();
    const QString baseName = cloudRel.section(QLatin1Char('/'), -1);

    // 云端目录（StickerPacks/<title>）→ 本地包标题
    const QString relDir = cloudRel.section(QLatin1Char('/'), 0, -2);
    const QString title = relDir.section(QLatin1Char('/'), -1);
    // B：内置源排除不区分上传/下载侧，按云目录名对称判定（上传侧整包不列出、
    // 下载侧对属于内置源包的云目录整体跳过），在 ensurePack 前拦截，不为其建包
    if (StickerStore::instance()->builtinSourceCloudDirs().contains(title)) {
        log(davbisync::Info, QStringLiteral("pack"),
            QStringLiteral("skip download into builtin-source pack: %1")
                .arg(cloudRel));
        m_diagnosis.skipped.append(cloudRel);
        ++m_downloadSkipped;
        ++m_downloadIndex;
        emitProgress();
        pushNext();
        return;
    }
    const QString packId = StickerStore::instance()->ensurePack(title);
    if (packId.isEmpty()) {
        finishWithError(QStringLiteral("ensurePack failed for cloud dir: %1")
                            .arg(relDir));
        return;
    }

    // 临时文件，下载完成后 importStickerFile 移入 base/packs/<title>/
    const QString tmp = QDir::tempPath() + QLatin1Char('/')
                        + QStringLiteral("anystik_dl_")
                        + QString::number(m_downloadIndex)
                        + QLatin1Char('_') + baseName;
    QFile* out = new QFile(tmp);
    if (!out->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        delete out;
        finishWithError(QStringLiteral("cannot create temp file: %1").arg(tmp));
        return;
    }
    m_tempFiles.append(tmp);

    log(davbisync::Info, QStringLiteral("download"),
        QStringLiteral("下载 %1 · 第 %2/%3")
            .arg(baseName).arg(m_downloadIndex + 1)
            .arg(m_downloadQueue.size()));

    QNetworkReply* reply = m_webdav->get(cloudRel, out);
    m_activeReplies.append(reply);
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, reply](qint64 done, qint64 total) {
                Q_UNUSED(reply)
                if (m_running && total > 0) {
                    const double frac =
                        double(m_downloadIndex) / qMax(1, m_downloadQueue.size());
                    const double inc = (total > 0 ? double(done) / double(total) : 0.0)
                        / qMax(1, m_downloadQueue.size());
                    m_percent = int(100.0 * (frac + inc));
                    emit progressUpdated(m_percent, QStringLiteral("download"),
                                         statDetail());
                }
            });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, out, tmp, packId, cloudRel, title]() {
                removeActiveReply(reply);
                out->deleteLater();
                if (!m_running) {
                    return;
                }
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (reply->error() != QNetworkReply::NoError
                        || (status >= 400)) {
                    m_tempFiles.removeOne(tmp);
                    QFile::remove(tmp);
                    finishWithError(QStringLiteral("download failed: %1 (%2 status %3)")
                                        .arg(cloudRel, reply->errorString()).arg(status));
                    return;
                }
                QString err;
                if (!StickerStore::instance()->importStickerFile(packId, tmp, &err)) {
                    m_tempFiles.removeOne(tmp);
                    QFile::remove(tmp);
                    finishWithError(QStringLiteral("import cloud file failed: %1 (%2)")
                                        .arg(cloudRel, err));
                    return;
                }
                m_tempFiles.removeOne(tmp);
                QFile::remove(tmp);
                // 更新本地清单以便 finishOk 基线（云端目录名即本地包标题）
                const QString localAbs =
                    StickerStore::instance()->resolveStickerPath(
                        QStringLiteral("packs/") + title + QLatin1Char('/')
                        + cloudRel.section(QLatin1Char('/'), -1));
                // 远端 last-modified 只读：把本地副本 mtime 对齐为云端值（只改本地），
                // 下一轮不会因下载改了本地时间戳而误判为空转上传
                const qint64 cloudMsec =
                    m_cloudFiles.value(cloudRel).mtimeMsec;
                if (cloudMsec > 0) {
                    QFile f(localAbs);
                    f.setFileTime(
                        QDateTime::fromMSecsSinceEpoch(cloudMsec, Qt::UTC),
                        QFileDevice::FileModificationTime);
                }
                m_lastFileMs = QDateTime::currentMSecsSinceEpoch() - m_fileStartMsec;
                log(davbisync::Info, QStringLiteral("download"),
                    QStringLiteral("完成 %1 · 用时 %2 s")
                        .arg(cloudRel.section(QLatin1Char('/'), -1))
                        .arg(m_lastFileMs / 1000.0, 0, 'f', 1));
                QFileInfo fi(localAbs);
                davbisync::BaselineEntry e;
                e.size = fi.size();
                e.mtimeMsec = fi.lastModified().toMSecsSinceEpoch();
                m_localFiles.insert(cloudRel, e);
                m_cloudToLocal.insert(cloudRel, localAbs);
                ++m_downloadDone;
                ++m_downloadIndex;
                emitProgress();
                pushNext();
            });
}

QUrl SyncEngine::cloudUrl(const QString& relPath) const
{
    QUrl u;
    u.setScheme(m_webdav->isSSL() ? QStringLiteral("https") : QStringLiteral("http"));
    u.setHost(m_webdav->hostname());
    u.setPort(m_webdav->port());
    QString p = m_webdav->rootPath();
    if (p.isEmpty()) {
        p = QStringLiteral("/");
    }
    if (!p.endsWith(QLatin1Char('/'))) {
        p += QLatin1Char('/');
    }
    p += relPath;
    u.setPath(p);
    return u;
}

void SyncEngine::emitProgress()
{
    const int total =
        qMax(1, m_uploadTotal + m_downloadQueue.size() + m_pendingCloudRenames.size());
    const int done = m_uploadIndex + m_downloadDone;
    m_percent = int(100.0 * double(done) / double(total));
    m_percent = qBound(0, m_percent, 100);
    emit progressUpdated(m_percent, QStringLiteral("sync"), statDetail());
}

QString SyncEngine::statDetail() const
{
    QStringList parts;
    if (m_uploadTotal > 0) {
        parts << QStringLiteral("上传 %1/%2")
                     .arg(m_uploadIndex).arg(m_uploadTotal);
    }
    if (!m_downloadQueue.isEmpty()) {
        parts << QStringLiteral("下载 %1/%2")
                     .arg(m_downloadDone).arg(m_downloadQueue.size());
    }
    if (!m_conflictRelCloud.isEmpty()) {
        parts << QStringLiteral("冲突 %1").arg(m_conflictRelCloud.size());
    }

    QString curText;
    QString curSizeText;
    if (!m_pendingCloudRenames.isEmpty()) {
        curText = QStringLiteral("重命名 %1").arg(m_pendingCloudRenames.size());
    } else if (m_uploadIndex < m_uploadQueue.size()) {
        const auto& item = m_uploadQueue.at(m_uploadIndex);
        curText = QStringLiteral("上传 %1")
            .arg(QFileInfo(item.first).fileName());
        curSizeText = formatBytes(QFileInfo(item.first).size());
    } else if (m_downloadIndex < m_downloadQueue.size()) {
        const QString rel = m_downloadQueue.at(m_downloadIndex).first;
        curText = QStringLiteral("下载 %1").arg(rel.section(QLatin1Char('/'), -1));
        const auto cit = m_cloudFiles.constFind(rel);
        if (cit != m_cloudFiles.constEnd()) {
            curSizeText = formatBytes(cit.value().size);
        }
    } else {
        curText = QStringLiteral("收尾");
    }

    const int total = qMax(1, m_uploadTotal + m_downloadQueue.size()
                              + m_pendingCloudRenames.size());
    const int done = m_uploadIndex + m_downloadDone;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double elapsedSec = qMax(1.0, double(now - m_startMsec) / 1000.0);

    QStringList line;
    line << curText;
    if (!curSizeText.isEmpty()) {
        line << curSizeText;
    }
    line << parts;
    if (m_lastFileMs > 0) {
        line << QStringLiteral("本次 %1").arg(formatDuration(m_lastFileMs));
    }
    line << QStringLiteral("已用 %1").arg(formatDuration(m_startMsec > 0
        ? now - m_startMsec : 0));
    if (done > 0 && total > done) {
        line << QStringLiteral("ETA %1").arg(formatDuration(
            qint64(elapsedSec * double(total - done) / double(done)) * 1000));
    }
    return line.join(QStringLiteral(" · "));
}

void SyncEngine::probeServer()
{
    if (m_serverProbed || !m_webdav) {
        return;
    }
    m_serverProbed = true;
    QNetworkReply* reply = m_webdav->options(m_cloudRoot);
    if (!reply) {
        return;
    }
    m_activeReplies.append(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        removeActiveReply(reply);
        m_serverName = QString::fromLatin1(reply->rawHeader("Server"));
        m_davCap = QString::fromLatin1(reply->rawHeader("DAV"));
        m_allowMethods = QString::fromLatin1(reply->rawHeader("Allow"));
        if (reply->error() == QNetworkReply::NoError
                && !m_davCap.contains(QLatin1Char('1'))) {
            log(davbisync::Warn, QStringLiteral("probe"),
                QStringLiteral("非标准 WebDAV（DAV=%1），部分功能可能失败")
                    .arg(m_davCap));
        }
        updateRemoteFeature();
    });
}

void SyncEngine::updateRemoteFeature()
{
    QStringList lines;
    if (m_serverProbed) {
const QString davNames = [this]() {
        QStringList parts;
        if (m_davCap.contains(QLatin1Char('1'))) {
            parts << QStringLiteral("1(文件读写)");
        }
        if (m_davCap.contains(QLatin1Char('2'))) {
            parts << QStringLiteral("2(文件锁)");
        }
        return parts.isEmpty()
            ? (m_davCap.isEmpty() ? QStringLiteral("?") : m_davCap)
            : parts.join(QStringLiteral("+"));
    }();
    lines << QStringLiteral("服务器软件: %1 · WebDAV 等级: %2 · 可用方法: %3")
                  .arg(m_serverName.isEmpty() ? QStringLiteral("?") : m_serverName)
                  .arg(davNames)
                  .arg(m_allowMethods.isEmpty() ? QStringLiteral("?") : m_allowMethods);
    }
    if (m_cloudMtimeDecided || m_putEchoDecided) {
lines << QStringLiteral("文件时间: %1 · 上传回读: %2 · 判定: %3")
                  .arg(m_cloudMtimeAvail ? QStringLiteral("支持✓")
                                         : QStringLiteral("不支持✗"))
                  .arg(m_putMtimeEcho ? QStringLiteral("✓")
                                      : QStringLiteral("✗(下轮回填)"))
                  .arg(m_cloudMtimeAvail ? QStringLiteral("大小＋时间")
                                         : QStringLiteral("仅大小"));
    }
    const QString text = lines.isEmpty()
        ? QStringLiteral("远程特征: 检测中...")
        : QStringLiteral("远程特征: ") + lines.join(QStringLiteral(" | "));
    emit remoteFeature(text);
    log(davbisync::Info, QStringLiteral("probe"), text);
}

void SyncEngine::removeActiveReply(QNetworkReply* reply)
{
    if (reply) {
        m_activeReplies.removeOne(reply);
        reply->deleteLater();
    }
}

void SyncEngine::finishOk(const QString& summary)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    m_running = false;
    m_percent = 100;

    // 成功后固化基线（原子写，中断不毁旧基线）
    // 统一双向同步：状态文件固定以 incremental 记录（无模式区分，字段仅作兼容占位）
    davbisync::SyncBaseline bl;
    bl.lastMode = QStringLiteral("incremental");
    bl.lastRunMsec = QDateTime::currentMSecsSinceEpoch();
    bl.local = m_localFiles;
    bl.cloud = m_cloudFiles;
    davbisync::baselineSave(bl);

    QString s = summary;
    if (!m_diagnosis.conflicts.isEmpty()) {
        s += QStringLiteral(", conflicts %1").arg(m_diagnosis.conflicts.size());
    }
    log(davbisync::Info, QStringLiteral("sync"), s);
    emit progressUpdated(100, QStringLiteral("done"), s);
    emit finished(0, s);
}

void SyncEngine::finishWithError(const QString& msg)
{
    if (m_finished || !m_running) {
        return;
    }
    m_finished = true;
    m_running = false;
    log(davbisync::Error, QStringLiteral("sync"), msg);
    log(davbisync::Error, QStringLiteral("sync"),
        QStringLiteral("aborted; completed transfers kept, rerun will re-check by size"));
    emit progressUpdated(m_percent, QStringLiteral("aborted"), msg);
    emit finished(1, msg);
}

void SyncEngine::log(davbisync::LogLevel level, const QString& tag, const QString& line)
{
    emit syncLog(int(level), tag, line);
}

QString SyncEngine::canonicalRel(const QString& raw)
{
    QString r = raw;
    while (r.startsWith(QLatin1Char('/'))) {
        r.remove(0, 1);
    }
    while (r.endsWith(QLatin1Char('/'))) {
        r.chop(1);
    }
    return r;
}