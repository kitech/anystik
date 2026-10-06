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
// ⚠ 动图三兄弟均已落地专用编码器：GIF（批次 4，tangora gif-h）、APNG（批次 5，
//   手拼 PNG chunk）、WebP（批次 6，libwebp WebPAnimEncoder，保 WebP 不转 APNG）。
//   某格式编码失败时走 PNG 回退分支（copyScaled 的 out 参数会如实报 true）。
//   Qt6 上 APNG 被原生 PNG 插件拍平（imageCount==1、read() 只出首帧），根本到不了
//   APNG 分支，自然回退 PNG —— 与 §18.1 第 6 条一致，不是本处逻辑。

// ⚠ 刻意**不** include qlcomp 的 compat34.h（本类用不到其中的任何符号）。
//   原因：compat34.h → compatcore34.h 会声明 qlcomp 的
//   `qOpenReadOnly(QFile&)`，它在 Qt4+ 分支是
//   `open(QIODevice::ReadOnly | QIODevice::Text)` —— 文本模式读二进制会在
//   Qt6 上吞掉每个 0x0D 字节（实测 260 字节样本丢 3 字节）。对本 TU 而言它
//   比 stikcommon 的二进制版 `qOpenReadOnly(QIODevice&)`（qglobaltype_shim.h）
//   更精确匹配，会把后者遮蔽，导致 .cpp 里读贴纸原始字节 / 读回临时 GIF 被
//   静默破坏。anystik 的 stickerstore.cpp 同样只 include qglobaltype_shim.h、
//   从不 include compat34.h，故其 buildGifBytes 走的是二进制版 —— 这里对齐它。
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