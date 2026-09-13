#ifndef DAVBISYNC_H
#define DAVBISYNC_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QByteArray>
#include <QVariant>

#include "davbisync_baseline.h"

class QWebdav;
class QWebdavDirParser;
class QWebdavItem;
class QNetworkReply;

namespace davbisync {

enum LogLevel
{
    Info = 0,
    Warn = 1,
    Error = 2
};

enum FinishCode
{
    FinishOk = 0,        // 同步全部成功
    FinishError = 1,     // 出错中止
    FinishCancelled = 2  // 用户取消
};

struct PackPolicy
{
    bool pullEnabled = true;
    bool pushEnabled = true;
    QString stickerDirRel;   // 服务器端相对云盘根的目录；空 = 不参与
    QString packDesc;
    bool overwriteRemote = false;

    bool participates() const { return pullEnabled || pushEnabled; }
    QString cloudRoot() const
    {
        return !stickerDirRel.isEmpty()
            ? stickerDirRel : QStringLiteral("anystik");
    }
};

struct SyncDiagnosis
{
    QStringList conflicts;
    QStringList skipped;
    QMap<QString, PackPolicy> packPolicies;
    bool engineReady = true;
    QString engineError;

    int conflictCount() const { return conflicts.count(); }
    int pendingCount() const
    {
        return conflicts.count() + skipped.count();
    }
};

} // namespace davbisync

class SyncEngine : public QObject
{
    Q_OBJECT

public:
    explicit SyncEngine(QObject* parent = nullptr);
    ~SyncEngine() override;

    bool isCloudAvailable() const;
    bool setConnectionSettings(int connectionType,
                               const QString& hostname,
                               const QString& rootPath,
                               const QString& username,
                               const QString& password,
                               int port = 0,
                               bool useSsl = true);

    bool startSync();
    void abort();
    bool isRunning() const { return m_running; }

    int progressPercent() const { return m_percent; }

    // ── M5 PackPolicy 接入 ──
    // key = 贴纸包 id（本地 StickerPackBrief.id）。空 map = 全包参与（默认）。
    // pushEnabled=false → 该包不上传；pullEnabled=false → 该包不下载；
    // participates()=两者皆关 → 整包跳过（记入诊断 skipped）。
    void setPackPolicies(const QMap<QString, davbisync::PackPolicy>& policies);
    const davbisync::SyncDiagnosis& diagnosis() const { return m_diagnosis; }

signals:
    void progressUpdated(int percent, const QString& step, const QString& detail);
    void syncLog(int level, const QString& tag, const QString& line);
    void finished(int exitCode, const QString& summaryAfterSkipped);
    void treeScanned(int files, int dirs);
    void remoteFeature(const QString& summary);

public slots:
    void setPollingDelay(int ms);

private:
    void setRunning(bool running);

    // ── 双向同步（本地⇄云端；只增改 → put/get/rename/mkdir，不做删除方向）──
    // 业务 rel（dbRel）= DB file_path 直传的相对路径（pastes/x、packs/<T>/x），
    // 不含云根；云根 anystik/ 仅由 cloudPath()/cloudUrl() 在传输边界临时拼出，
    // 从不写入 DB/基线/内存键。键 = 统一 tet dbRel。
    using FileEntryMap = QMap<QString, davbisync::BaselineEntry>;

    // 阶段 A：本地清单
    void buildLocalListing();
    // 阶段 B：云端清单（逐目录 listDirectory 状态机；入参为业务 dbRel）
    void scanCloudDir(const QString& dbRelDir);
    void collectCloudItems();            // parser finished 后收集当前目录并调度下一目录
    void processPendingCloudDirs();
    void finishCloudScan();
    // 阶段 C：diff + 冲突处置（幂等 put/get，双侧皆变 → 云端旧版改名 .conflictN 保留）
    void buildOpQueue();
    // 阶段 D：应用
    void pushNext();
    void mkdirNextChain();          // 逐层串行 mkdir，直到完整目录链就位
    void checkAndUpload(const QString& localPath, const QString& cloudRel);
    void uploadFile(const QString& localPath, const QString& cloudRel);
    void downloadFile(const QString& cloudRel, const QString& localAbs);
    void removeActiveReply(QNetworkReply* reply);   // 注销并销毁 reply
    QUrl cloudUrl(const QString& dbRel) const;
    QString cloudPath(const QString& dbRel) const;  // 业务 dbRel → 传输路径（唯一入云拼根点）
    void persistBaseline();          // 成功操作后增量原子保存基线（防中断重传）
    void emitProgress();
    void finishOk(const QString& summary);
    void finishWithError(const QString& msg);
    void log(davbisync::LogLevel level, const QString& tag, const QString& line);
    QString statDetail() const;
    void probeServer();
    void updateRemoteFeature();
    static QString canonicalRel(const QString& raw);
    QString nextConflictName(const QString& dirBase, const QString& fileBase);

    QList<QPair<QString, QString>> m_uploadQueue; // (本地绝对路径, 业务 dbRel)
    QList<QPair<QString, QString>> m_downloadQueue; // (业务 dbRel, 本地绝对路径)
    QList<QPair<QString, QString>> m_pendingCloudRenames; // (旧 dbRel, 新 dbRel) 冲突改名
    QStringList m_ensuredDirs;                    // 已确保存在的云端目录
    QStringList m_mkdirChain;                     // 待逐层 mkdir 的目录（顶层在前）
    QList<QNetworkReply*> m_activeReplies;        // 在途请求（abort 时逐一取消）
    QStringList m_tempFiles;                      // 在途下载临时文件（abort 时清理）
    int m_uploadIndex = 0;
    int m_uploadDone = 0;
    int m_uploadSkipped = 0;
    int m_downloadSkipped = 0;
    int m_uploadTotal = 0;
    int m_downloadDone = 0;
    int m_downloadIndex = 0;
    int m_downloadFailed = 0;      // 单文件内容失败（图片解码失败等）跳过数
    qint64 m_startMsec = 0;        // 本次同步开始时刻（startSync）
    qint64 m_fileStartMsec = 0;    // 当前文件开始时刻（uploadFile）
    qint64 m_lastFileMs = 0;       // 最近完成的单个文件用时
    QString m_cloudRoot = QStringLiteral("anystik");

    // ── 扫描状态（阶段 A/B）──
    bool m_cloudReady = false;     // 云端清单是否已就绪
    bool m_serverProbed = false;   // OPTIONS 预检仅触发一次
    QSet<QString> m_scannedCloudDirs;   // 已列出过的云端目录
    QString m_scanCurrentDir;           // 本次正在列的目录
    QList<QPair<QString, QString>> m_pendingDirs; // 待展开子目录 (relDir, depthPath)
    FileEntryMap m_cloudFiles;      // cloudRelPath → 云端条目
    FileEntryMap m_localFiles;      // cloudRelPath → 本地条目
    QHash<QString, QString> m_cloudToLocal; // cloudRelPath → 本地绝对路径
    QMap<QString, QString> m_localDirPacks; // 云目录 rel → 本地 packId（存在性）
    QMap<QString, davbisync::PackPolicy> m_packPolicies; // id → 策略（空=默认全参与）
    davbisync::SyncDiagnosis m_diagnosis;   // 本次运行诊断（冲突/跳过清单）
    davbisync::SyncBaseline m_base;         // 本次运行基线（startSync 载入，diff/HEAD 共用）
    int m_cloudFileCount = 0;
    int m_localFileCount = 0;

    // ── 远程能力检测（零阻断；仅影响判定精确度与展示）──
    bool m_cloudMtimeAvail = false;    // PROPFIND getlastmodified 可用
    bool m_putMtimeEcho = false;       // PUT 响应回读 Last-Modified 可用
    bool m_cloudMtimeDecided = false;  // 云端非空已完成判定
    bool m_putEchoDecided = false;     // 首个上传已完成判定
    QString m_serverName;              // OPTIONS Server 头
    QString m_davCap;                  // OPTIONS DAV: 头
    QString m_allowMethods;            // OPTIONS Allow 头

    // ── 差异（阶段 C 产物）──
    QList<QString> m_conflictRelCloud;  // 双侧皆变 → 云端旧版改名保留的路径
    QStringList m_conflictNames;        // 已分配的 .conflictN 云端名

    bool m_running = false;
    bool m_finished = false;      // 已 emit finished，防重复收尾
    int m_percent = 0;
    QWebdav *m_webdav = nullptr;
    QWebdavDirParser *m_parser = nullptr;
    QString m_rootPath = QStringLiteral("/");
    QString m_host;
    QString m_username;
    QString m_password;
    int m_port = 0;
    bool m_useSsl = true;
};

#endif // DAVBISYNC_H
