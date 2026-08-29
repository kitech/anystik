#ifndef STICKER_STORE_H
#define STICKER_STORE_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QVector>

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
};

class StickerStore : public QObject
{
    Q_OBJECT
public:
    static StickerStore* instance();

    // 惰性初始化 Storage（message.db/cache.db + sticker 表），线程安全单次执行
    bool ensureInit();

    // ── 查询（全部走 StickerDbSyncInterface，GUI 线程直接调用）──
    QVector<StickerPackBrief> packs();
    QVector<StickerBrief> stickers(const QString& packId);
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

    // 复制图片到剪贴板（Desktop: QClipboard 位图；Android: 剪贴板写文件路径 + toast）
    bool copyStickerToClipboard(const QString& filePath);

Q_SIGNALS:
    void dataChanged();

private:
    StickerStore(QObject* parent = nullptr);

    void dropLegacyChatTables();

    static StickerStore* s_instance;
    bool m_initialized = false;
};

#endif // STICKER_STORE_H
