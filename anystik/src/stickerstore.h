#ifndef STICKER_STORE_H
#define STICKER_STORE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QVector>
#include <QHash>
#include <QVariantMap>
#include <QDateTime>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QUrl;

struct StickerPackBrief {
    QString id;
    QString title;
    QString author;
    QString coverPath;
    int     position = 0;
};

struct StickerBrief {
    QString id;
    QString packId;
    QString filePath;
    QString emoji;
    int     width  = 0;
    int     height = 0;
    int     size   = 0;
    qint64  lastUsed = 0;
    QString description;   // 图片描述，不超过 140 字
    int     isPublic = 0;   // 是否公开（默认不公开）
};

// 贴纸图片元信息（由文件探测：类型/大小/长宽/帧数/更新时间）
struct StickerMeta {
    QString typeLabel;     // 人类可读类型，如 "GIF (image/gif)"
    QString mime;          // 原始 MIME，如 "image/gif"
    bool    animated = false;
    int     frames = 0;    // >1 即动画
    int     width = 0;
    int     height = 0;
    qint64  sizeBytes = 0; // 文件字节数
    QDateTime modified;    // 文件修改时间
};
// 拼成多行展示/拷贝文本（类型/大小/尺寸/帧数/更新时间），预览与菜单共用
QString formatStickerMeta(const StickerMeta& meta);

// 内置下载源唯一表。approxSize 为预告约值（静态，非运行时所得）；-1 = 无实测
struct BuiltinSource {
    const char* name;
    const char* url;
    qint64 approxSize;
};
extern const BuiltinSource kBuiltinSources[];
extern const unsigned kBuiltinSourceCount;

class StickerStore : public QObject
{
    Q_OBJECT
public:
    static StickerStore* instance();

    // 惰性初始化 Storage（message.db/cache.db + sticker 表），线程安全单次执行
    bool ensureInit();

    // ── 查询（全部走 StickerDbSyncInterface，GUI 线程直接调用）──
    QVector<StickerPackBrief> packs(int installed = 1,
                                    const char* orderby = "created_at DESC",
                                    int limit = 0, int offset = 0);
    QVector<StickerBrief> stickers(const QString& packId,
                                   const char* orderby = "rowid DESC",
                                   int limit = 0, int offset = 0,
                                   int deleted = 0, const char* emoji = nullptr);
    QVector<StickerBrief> recent(int limit = 60);
    QVector<StickerBrief> search(const QString& query);
    int countStickers(const QString& packId);

    // ── 写操作 ──
    bool importDirectory(const QString& dir, QString* errorOut = nullptr);
    bool pasteFromClipboard(QString* errorOut = nullptr);
    // 由任意字节源（桌面剪贴板 PNG / Android 剪贴板或分享读取的原始图片字节）入库到「粘贴板」，
    // 内部按内容探测格式、sha1 幂等去重
    bool importImageBytes(const QByteArray& bytes, QString* errorOut = nullptr);
    bool renamePack(const QString& packId, const QString& newTitle);
    bool deletePack(const QString& packId);
    bool deleteSticker(const QString& stickerId);
    void touchSticker(const QString& stickerId);

    // 复制图片到剪贴板（Desktop: QClipboard 位图；Android: FileProvider content URI(image/*) + toast）
    bool copyStickerToClipboard(const QString& filePath);
    // 由文件探测元信息（类型/大小/长宽/帧数/更新时间）
    StickerMeta stickerMeta(const QString& filePath) const;
    // 按 scale 缩放后复制（静态→QImage scaled 位图；动画帧逐帧缩放，GIF 源保 GIF
    // 经 gif-h 重编码、其余动画源转 APNG；放大结果最长边 >4096 等比封顶）
    bool copyStickerScaledToClipboard(const QString& filePath, qreal scale);
    // 出向分享：Android 经 ShareActivity 拉起系统分享面板（ACTION_SEND + FileProvider）；
    // 非 Android 返回 false（调用方 toast 提示）
    bool shareStickerFile(const QString& filePath);

    // ── 下载包（自带地址）──
    // 成品落 dataDir/packs/<标题>/**；元数据(url/版本commit/MD5)持久化在
    // QSettings: downloadedPacks(QStringList) + downloadedPackMeta/<packId>(map)；
    // 下载中磁盘仅留唯一的 <md5(url)16>.part，无临时元文件。
    bool setPackInstalled(const QString& packId, bool installed);
    bool uninstallPack(const QString& packId, bool removeFiles);
    qint64 packDiskSize(const QString& packId);
    QVariantMap packMeta(const QString& packId) const;

    void probeRemote(const QString& url);
    // 动态精确大小：获取 HEAD 成功且带 Content-Length 时写入的 realSize；无则 -1
    qint64 cachedRealSize(const QString& url) const;
    // 预告填入的约值大小：seedBuiltinApproxSizes 预写入的 approxSize；无则 -1
    qint64 cachedApproxSize(const QString& url) const;
    // 预告填入：首启把内置源 approxSize(含 -1) 缺省写入元数据，零网络、幂等
    void seedBuiltinApproxSizes();
    void downloadPack(const QString& url);
    void cancelDownload(const QString& url);
    bool hasPartialDownload(const QString& url) const;
    // 清理下载区残留：删除指纹不属于 knownUrls 的 *.part，并清除对应 dlProgress 死条目
    void cleanupAbandonedDownloads(const QStringList& knownUrls);

    // 图片描述长度上限（140 字）；写入/编辑描述时按此截断
    static constexpr int MaxDescriptionLength = 140;

Q_SIGNALS:
    void dataChanged();
    // size: -1 = 未知(如 codeload zip 无 Content-Length)
    // version/versionRaw: commit sha / ETag / Last-Modified / 未知
    void probeDone(const QString& url, qint64 size, const QString& version,
                   const QString& versionRaw, bool ok, const QString& error);
    void progressChanged(const QString& url, qint64 done, qint64 total);
    void downloadFinished(const QString& url, bool ok, const QString& error);

private:
    StickerStore(QObject* parent = nullptr);

    void dropLegacyChatTables();

    struct DownloadTask {
        QString url;
        QString partPath;
        QFile* out = nullptr;
        QNetworkReply* reply = nullptr;
        qint64 offset = 0;
        qint64 total = -1;
        QString name;
        bool cancelled = false;
        bool installing = false;   // 安装阶段（工作线程），期间不可取消
    };

    // install 工作线程的返回结果；GUI 线程收尾时据此写元数据并发信号
    struct InstallResult {
        bool ok = false;
        QString message;           // 成功=标题；失败=错误文案
        QString packId;
        QString note;              // 成功附加提示（如「远端内容已变化」）
        QString dir;               // dataDir/packs/<sanitized 标题>
        qint64  total = 0;         // zip 字节数
        QString md5Hex;
    };

    QNetworkRequest makeRequest(const QUrl& url);
    QString downloadPartPath(const QString& url) const;
    void ensureNam();
    void startDownload(const QString& url, bool noRange);
    void handleDownloadFinished(DownloadTask* task);
    void finishIfComplete(DownloadTask* task);
    // 安装三段式：runInstall(GUI 线程启动) → runInstallWork(工作线程执行)
    // → finalizeInstall(GUI 线程收尾：QSettings + 信号 + task 清理)
    void runInstall(DownloadTask* task);
    InstallResult runInstallWork(DownloadTask* task);
    void finalizeInstall(DownloadTask* task, const InstallResult& r);
    void closeOut(DownloadTask* task);

    static StickerStore* s_instance;
    bool m_initialized = false;
    QNetworkAccessManager* m_nam = nullptr;
    QHash<QString, DownloadTask*> m_tasks;
};

#endif // STICKER_STORE_H
