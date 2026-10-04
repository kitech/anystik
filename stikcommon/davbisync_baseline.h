#ifndef DAVBISYNC_BASELINE_H
#define DAVBISYNC_BASELINE_H

#ifdef QT3_BUILD
// Qt3.5 无 CamelCase 转发头（QString/QMap 等只有小写头文件）
#include <qstring.h>
#include <qmap.h>
// Qt3.5 无 qint64（Qt4 引入），本头公开 API 用它承载字节数/epoch 毫秒
#include "qglobaltype_shim.h"
#else
#include <QString>
#include <QMap>
#endif
#include "qstring_shim.h"   // QStringLiteral(Qt<4.1) 垫片，本头的默认值初始化要用

namespace davbisync {

// 基线条目：相对云根的路径 → 大小 + mtime。判定 = size 不同，或 size 相同且
// 两侧 mtime 均有效(≠0) 且不同（第二判据，防 size-only 漏报）；任一侧 mtime 无效
// → 仅 size（无 mtime 服务器回退）。云端 last-modified 只读不改写，需要写 mtime
// 时仅作用于本地文件（下载对齐本地 mtime）
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