#ifndef QLSTIK_STICKERPASTE_H
#define QLSTIK_STICKERPASTE_H

// 贴纸粘贴（顶栏「粘贴」按钮）：读系统剪贴板图片 → 入库「粘贴板」分组。
//
// 对应 anystik 链路：StickerHomePage::requestPasteSticker（stickerhomepage.cpp:1530）
// → StickerStore::pasteFromClipboard（stickerstore.cpp:2109）→ importImageBytes（:2403）。
//
// 兼容契约（移植计划.md §19.2，破坏即与 anystik 双端互认失效）：
//   1. 「粘贴板」包 id = "pack_" + sha1(UTF8("粘贴板")).hex 前 12 位；先按
//      title=="粘贴板" 查重、有则复用。
//   2. 贴纸幂等 id = sha1(粘贴图片原始字节) 40 位小写 hex；文件名
//      pastes/<id>.<ext>、sticker.id = <id>。
//   3. 行字段语义照搬 anystik；经同一 StickerDbSyncInterface 写入。
//
// 实现镜像 stickerclipboard.cpp 的跨版本纪律（Qt3/4/6，见 stickerclipboard.h 文件头
// 与移植计划.md §18）：
//   ⚠ **不** include qlcomp 的 compat34.h（其会引入二进制读被文本模式破坏的
//     qOpenReadOnly(QFile&)，见 stickerclipboard.h:27-35 与 §18.12）。
//   ⚠ include 顺序：Qt 原生头 → 各 shim → qlist_shim.h 收尾（它会在 Qt3 分支
//     undef/替换 QList 宏，必须排在所有 Qt 原生头之后）。与本文件同构的
//     stickerclipboard.cpp:3-46 是现成范本。
//
// 引用实现取舍（§19.4）：
//   · 探测用 qSniffImageFormat（qformatsniff_shim.h，区分 apng）＋
//     QImageReader 解码校验 ＋ supportedImageFormats() 运行时白名单；
//   · 读剪贴板垫片已实测（qclipboard_shim.h:30-33，探针 paste_probe_qt3 复验）；
//   · sha1 用 qlcomp/sha1.c（steve reid 公有领域，qlstik.pro 挂载 ../qlcomp/sha1.c）。

#ifdef QT3_BUILD
#include <qstring.h>
#else
#include <QString>
#endif

class StickerPaste {
public:
    // 读系统剪贴板图片 → 存入「粘贴板」。返回 false 时 *err 给人类可读文案（非空）。
    // dup: 内容已在「粘贴板」（同 id 文件/行存在且未软删）；此时不重写、不挪位。
    // resurrectId: 该内容行此前被软删（sticker_deleted_state==1）→ 非空，调用方
    //              可弹窗询问还原（restore_sticker）。
    static bool pasteFromClipboard(bool* dup, QString* resurrectId, QString* err);
};

#endif // QLSTIK_STICKERPASTE_H