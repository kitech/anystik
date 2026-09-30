#ifndef DAVBISYNC_H
#define DAVBISYNC_H

#ifdef QT3_BUILD
// Qt 3.5 没有 QSet/QHash，且 QList 只是 `#define QList QPtrList` 的指针宏
// （qptrlist.h:189，存不了值类型）；头文件名也只有小写形式。
//
// ★ 包含顺序是硬要求：qmap_shim.h 末尾是
//   `#define QMap qlstik_qt3::QMapShim`（文本级替换），
//   必须排在**所有 Qt 头之后**，否则 Qt 头内部自己的 QMap
//   （如 QVariant::Private 的 QMap<QString,QVariant>）被改写而编不过。
//   实测：垫片放在 Qt 头之前 → qvariant.h:369 报 const_iterator 无法转换。
#include <qobject.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qurl.h>
#include <qdatetime.h>
#include <qmap.h>
#include <qcstring.h>   // Qt 3.5 的 QByteArray 在此，不是 qbytearray.h
#include <qvariant.h>
#include "qlist_shim.h"
#include "qset_shim.h"
#include "qmap_shim.h"
#include "qglobaltype_shim.h"
#include "qstring_shim.h"
#else
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QByteArray>
#include <QVariant>
#include <QSet>
#include <QHash>
#endif

#include "davbisync_baseline.h"

// 本移植版要用的 QMap 语义（value/constFind/cbegin/迭代器 value()/
// erase 返回下一迭代器）Qt 3.5 原生 QMap 全缺，qmap_shim.h 的 QMapShim 补齐。
// Qt3 走 QMapShim、Qt4+ 走原生 QMap，调用点无需版本分支。
//
// ★ 必须用宏，不能是 alias template 或模板 typedef：Qt 3.5 的 moc 两者都解析不了
//   （实测 `using X = ...` 与 `template<..> typedef ..` 均报 "Error: syntax error"
//   指向该行；moc 3.5 也不定义 Q_MOC_RUN，无法用 #ifndef 把它藏起来）。
//   宏形态 moc 实测可解析（它只做文本扫描，不求值类型）。类体下方 #undef 收口，
//   避免污染包含本头的其它 TU。
#ifdef QT3_BUILD
#define QMapLike qlstik_qt3::QMapShim
#else
#define QMapLike QMap
#endif

class QWebdavLite;
class QWebdavDirParserLite;
class QWebdavItemLite;
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
    QMapLike<QString, PackPolicy> packPolicies;
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
    void setPackPolicies(const QMapLike<QString, davbisync::PackPolicy>& policies);
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
    using FileEntryMap = QMapLike<QString, davbisync::BaselineEntry>;

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

    // ↓ 下面四个必须是 **槽**，不能是普通私有成员函数。
    //   Qt3 侧 createConnection/createParser 用 SIGNAL/SLOT 字符串接线
    //   （davbisync.cpp:376/425-428），而字符串形式只查 moc 的槽表 —— 没进
    //   slots: 段就查不到，connect 静默返回 false 且只在 stderr 打一行
    //   "No such slot"，编译期完全看不出来。后果最重的是 onParserFinished：
    //   它不触发 = 云端清单状态机不推进 = 整个同步永久挂住（0 个请求发出）。
    //   Qt6 侧走 PMF connect 的 lambda，不需要槽；两版共用同一份实现。
    //   （这个坑是 davsync_e2e 首跑「20s 未收尾 + 服务端 0 条请求」查出来的。）
private slots:
    // QWebdavLite 的错误上报落点：两版都经 subscribeErrorChanged 的 lambda 转发
    // 到此（QWebdavLite 无 Q_OBJECT，Qt3/Qt6 都不能用 PMF connect）。
    void onWebdavError(const QString& e);
    // parser 的 finished / errorChanged 落点。Qt3 侧必须是槽（SIGNAL/SLOT
    // 字符串形式不能接 lambda），Qt6 侧由 PMF connect 的 lambda 转发进来，
    // 两版共用同一份逻辑，不重复实现。
    void onParserFinished();
    void onParserError(const QString& line);
    // 子对象（QWebdavLite / QWebdavDirParserLite）析构时的调试日志。做成 Qt3 槽
    // 是因为 Qt3 的 connect 只有 SIGNAL/SLOT 字符串形式，而 &QObject::destroyed
    // 在 Qt3 下无 PMF 重载（qconnect_slots.h 只覆盖 QNetworkReply/QNAM 发送者）。
    void onChildDestroyed(QObject* w);

private:
    void createConnection();   // 每轮新建 QWebdavLite（连接池/认证归零，与重启等价）
    void createParser();       // 每轮新建 parser 并接线
    QUrl cloudUrl(const QString& dbRel) const;
    QString cloudPath(const QString& dbRel) const;  // 业务 dbRel → 传输路径（唯一入云拼根点）
    void persistBaseline();          // 成功操作后增量原子保存基线（防中断重传）
    void emitProgress();
    void setProgress(const QString& stage, qint64 fileDone, qint64 fileTotal);
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
    qint64 m_speedLastBytes = 0;   // 上次报告字节数
    qint64 m_speedLastMsec = 0;    // 上次报告时刻
    qreal m_speedBps = 0;          // EMA 平滑速度(B/s)
    QString m_speedStage;          // 当前速度所属阶段(upload/download)
    QString m_cloudRoot = QStringLiteral("anystik");

    // ── 扫描状态（阶段 A/B）──
    bool m_cloudReady = false;     // 云端清单是否已就绪
    bool m_serverProbed = false;   // OPTIONS 预检仅触发一次
    QSet<QString> m_scannedCloudDirs;   // 已列出过的云端目录
    QString m_scanCurrentDir;           // 本次正在列的目录
    QList<QPair<QString, QString>> m_pendingDirs; // 待展开子目录 (relDir, depthPath)
    FileEntryMap m_cloudFiles;      // cloudRelPath → 云端条目
    FileEntryMap m_localFiles;      // cloudRelPath → 本地条目
    // 只需 value/insert/clear，Qt3 走 QMapShim 才有 value(key)
    QMapLike<QString, QString> m_cloudToLocal; // cloudRelPath → 本地绝对路径
    QMapLike<QString, QString> m_localDirPacks; // 云目录 rel → 本地 packId（存在性）
    QMapLike<QString, davbisync::PackPolicy> m_packPolicies; // id → 策略（空=默认全参与）
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
    int m_totalJobs = 0;           // 本轮总量 = 上传+下载+改名
    int m_jobsDone = 0;            // 已完成/已跳过件数（只增，基准不回落）
    QWebdavLite *m_webdav = nullptr;
    QWebdavDirParserLite *m_parser = nullptr;
    QString m_rootPath = QStringLiteral("/");
    QString m_host;
    QString m_username;
    QString m_password;
    int m_port = 0;
    int m_connectionType = 1;
    bool m_useSsl = true;
};

#undef QMapLike

#endif // DAVBISYNC_H
