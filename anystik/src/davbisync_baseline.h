#ifndef DAVBISYNC_BASELINE_H
#define DAVBISYNC_BASELINE_H

#include <QString>
#include <QMap>

namespace davbisync {

// 基线条目：相对「云盘根/StickerPacks」的云路径 → 大小（mtime 仅记录，云端 mtime
// 未必可靠，双向判定以 size 为准，参照 rclone --size-only 语义）
struct BaselineEntry
{
    qint64 size = -1;      // 文件字节数；-1 = 未知
    qint64 mtimeMsec = 0;  // 本地条目 = 文件 mtime；云端条目 = 服务端 last-modified
};

struct SyncBaseline
{
    int schemaVersion = 1;
    QString lastMode = QStringLiteral("incremental"); // 统一双向同步，固定 incremental
    qint64 lastRunMsec = 0;    // 本次基线固化时刻
    QMap<QString, BaselineEntry> local;   // 本地侧快照（云路径 → 条目）
    QMap<QString, BaselineEntry> cloud;   // 云端侧快照（云路径 → 条目）
};

// 读/写基线 JSON（AppLocalDataLocation/davbisync_baseline.json）。
// 写入用 QSaveFile 原子提交：tmp + rename，中断/崩溃不毁旧基线
// （对应 rclone --recover 反向的安全语义：abort 不毁基线）。
bool baselineLoad(SyncBaseline* out);
bool baselineSave(const SyncBaseline& b);

} // namespace davbisync

#endif // DAVBISYNC_BASELINE_H