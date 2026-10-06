#ifndef QLSTIK_STICKERCLIPBOARD_H
#define QLSTIK_STICKERCLIPBOARD_H

// 贴纸复制的统一管线（移植计划.md §18）。
//
// 存在理由：四个复制入口（单击 / 右键 / 预览动作栏 / 4 档缩放）此前各走
// `QImageReader::read()` 读**首帧** + `QClipboard::setImage()`，动图被拍成静图。
// 本类把「读字节 → 嗅探 → 分流」三件事收在一处，入口只管调它、toast。
//
// 两条路径（§18.4 / §18.5）：
//   copyOriginal()  非缩放：**原始字节直通**，完全不经过解码 →
//                   帧数/delay/loop count 逐字节无损。这条是实测结论不是推测：
//                   3 帧 GIF 经 QMimeData 往返，Qt3/Qt6 两端取回字节与原始
//                   memcmp 逐字节相同、实读帧数仍为 3（§18.10 第 2 项）。
//                   ⚠ 因此**不要**在这里走 setImageData()/setImage()，那会
//                   经过 QImage 而丢多帧。
//   copyScaled()    缩放：解帧 → 逐帧缩放 → 同格式重编码。同格式没有编码器时
//                   回退 PNG 并把 *fellBackToPng 置 true，供调用方选
//                   `copied_scale_fallback` 文案（§18.8）。
//
// ⚠ 已知不在本批范围：GIF/APNG/WebP 三个动图编码器分别在批次 4/5/6。
//   本批这三类走 PNG 回退分支（copyScaled 的 out 参数会如实报 true），
//   等编码器落地后自动变成同格式输出，控制流不用再改。

#include "compat34.h"

#ifdef QT3_BUILD
#include <qstring.h>
#else
#include <QString>
#endif

class StickerClipboard {
public:
    // 原始字节直通。文件不存在/读不出/嗅探不出格式返回 false。
    static bool copyOriginal(const QString& filePath);

    // 缩放复制。*fellBackToPng 可为 0（不关心）；非 0 时回写「是否发生 PNG 回退」。
    // 文件不存在、scale <= 0、解不出帧、编码器全失败返回 false。
    static bool copyScaled(const QString& filePath, double scale,
                           bool* fellBackToPng);
};

#endif // QLSTIK_STICKERCLIPBOARD_H