#ifndef QLSTIK_QIMAGE_SHIM_H
#define QLSTIK_QIMAGE_SHIM_H

// Qt3.5 与 Qt6 的 QImage 差异垫片（stickerstore.cpp 图像管线专用）。
//
// ── 需求来源（anystik/src/stickerstore.cpp 的实测用量）────────────────────
//   · QImage::Format_RGBA8888 / convertToFormat()   —— :1375 :1406 :2647 :2791
//     （解帧后统一转 RGBA8888 再缩放/重编码）
//   · QImage::Format_ARGB32_Premultiplied            —— :672（TGS 占位图）
//   · scaled(size, Qt::KeepAspectRatio, Smooth)       —— :2644 :2780 :2789 :2871
//   · constBits() / sizeInBytes()                     —— :1122（"帧与第 1 帧互异" 判定）
//   · constScanLine()                                 —— :1293（APNG 组装逐行取像素）
//
// ── 为什么 Qt3 侧不做「假装有 Format_RGBA8888」────────────────────────────
// Qt3 的 QImage 是 32 位 depth + `setAlphaBuffer(bool)`，**没有 Format 概念**
// （qimage.h:74 ctor 形参是 `int depth`）。且 32 位内存布局**是本机字节序下的
// BGRA**：实测 /opt/qt338sh 小端主机上 `fill(qRgba(0x11,0x22,0x33,0xff))`
// 得 row0 = `33 22 11 ff`（qimage.h:125 bits()、Qt6 Format_RGBA8888 是
// `ff 00 00 ff`）。也就是说 Qt3 **无法**造出内存即 RGBA 的 QImage。
//
// 故分两层：
//   ① qImageRgba()      —— 给「要继续缩放 / 显示 / 存盘」的帧用，返回本平台
//                          原生 32 位带 alpha 图（Qt6 仍为 Format_RGBA8888）。
//   ② qImageRgbaBytes() —— 给「要按字节喂编码器」的帧用，返回 RGBA 字节序
//                          （R,G,B,A）的 QByteArray。GIF/APNG 编码器只认字节
//                          序，不认 QImage 的内部格式。
// 调用点原本直接吃 constBits()/constScanLine()，现改吃 ②，行为在两端一致。

#include <qglobal.h>

#if QT_VERSION < 0x040000

#include <qimage.h>
#include <qcstring.h>
#include <qsize.h>
#include <string.h>

// Qt3 32 位 QImage 内存是 BGBA（见文件头实测），转成 RGBA 字节序。
inline QByteArray qImageRgbaBytes(const QImage& im)
{
    QByteArray out;
    if (im.isNull() || im.width() <= 0 || im.height() <= 0) return out;
    // 统一先转 32 位带 alpha，避免 8/24 位调色板图逐像素分支
    QImage src = im;
    if (src.depth() != 32) src = src.convertDepth(32);
    if (!src.hasAlphaBuffer()) {
        src = src.copy();
        src.setAlphaBuffer(true);
    }
    const int w = src.width(), h = src.height();
    const int inStride = src.bytesPerLine();
    out.resize(w * h * 4);
    char* dst = out.data();
    const uchar* base = src.bits();
    for (int y = 0; y < h; ++y) {
        const uchar* p = base + (size_t)y * inStride;
        char* q = dst + (size_t)y * w * 4;
        for (int x = 0; x < w; ++x) {
            q[0] = char(p[2]);   // B
            q[1] = char(p[1]);   // G
            q[2] = char(p[0]);   // R
            q[3] = char(p[3]);   // A
            p += 4;
            q += 4;
        }
    }
    return out;
}

// 「转成 RGBA8888」的 Qt3 等价物：本平台原生 32 位带 alpha 图。
inline QImage qImageRgba(const QImage& im)
{
    if (im.isNull()) return im;
    QImage r = im.depth() == 32 ? im.copy() : im.convertDepth(32);
    if (!r.hasAlphaBuffer()) r.setAlphaBuffer(true);
    return r;
}

// Qt6 侧：三个函数转调原生 API，Qt6 行为一字不变。
#else

#include <QImage>

inline QByteArray qImageRgbaBytes(const QImage& im)
{
    if (im.isNull() || im.width() <= 0 || im.height() <= 0) return QByteArray();
    const QImage c = im.convertToFormat(QImage::Format_RGBA8888);
    return QByteArray(reinterpret_cast<const char*>(c.constBits()), int(c.sizeInBytes()));
}

inline QImage qImageRgba(const QImage& im)
{
    return im.isNull() ? im : im.convertToFormat(QImage::Format_RGBA8888);
}

#endif // QT_VERSION < 0x040000

#endif // QLSTIK_QIMAGE_SHIM_H