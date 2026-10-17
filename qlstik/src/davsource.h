#ifndef QLSTIK_DAVSOURCE_H
#define QLSTIK_DAVSOURCE_H

// qlstik 的 DavLocalSource 适配层（批次 P3）。
//
// 把 qlstik 现有数据层接到 stikcommon/davlocalsource.h 的九方法窄接口，供
// stikcommon/davbisync.cpp 的 SyncEngine 使用：
//   qlappcom/storage.h        → Storage::instance().stickerDb()
//   qlappcom/sticker_db.h     → StickerDbSyncInterface / StickerPackRow / StickerRow
//   stikcommon/pack_meta_store.h + builtinsources.h
//   qlcomp/qsettings34.h      → 读 downloadedPackMeta/<id>（与 anystik 同一配置文件）
//
// 生命周期照 anystik 的 StickerStore::instance()（stickerstore.cpp:192）：进程级
// 单例、懒建、无显式注册点。故本模块在静态初始化期自我注册到 stikcommon 的
// setDavLocalSource()，main.cpp 零改动。
//
// ⚠ 本单元在 Qt4 下**不参与编译**（见 qlstik.pro 的挂载条件）：它派生的
//    DavLocalSource 由 stikcommon.pri:250-256 仅在 Qt3 / Qt≥5 编入
//    davlocalsource.cpp，Qt4 下缺基类 vtable。
//
// 实现细节（相对路径基目录）逐字照搬 stickerlistpage.cpp:447-467 的
// stickerBaseDir()/stickerResolvePath()：显示侧（贴纸网格）与同步侧必须同源，
// 同一份 storageRoot 逻辑，不另造。

#include "davlocalsource.h"

#endif // QLSTIK_DAVSOURCE_H
