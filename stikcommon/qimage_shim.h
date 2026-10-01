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

// scaled(size, KeepAspectRatio, SmoothTransformation) 的 Qt3 等价物。
// Qt3 无 scaled()，只有返回副本的 scale()/smoothScale()（qimage.h:158/165），
// 且 aspect 枚举名是 ScaleMin/ScaleMax：
//   ScaleMin == Qt6 Qt::KeepAspectRatio（等比缩放到装得下，尺寸可小于请求）
//   ScaleMax == Qt6 Qt::KeepAspectRatioByExpanding（等比缩放填满，允许超出）
// 实测 4x2 → 请求 3x3：ScaleMin 得 3x1，与 Qt6 KeepAspectRatio 同为 3x1。
inline QImage qImageScaledKeepAspectSmooth(const QImage& im, const QSize& s)
{
    return im.smoothScale(s, QImage::ScaleMin);
}

// 半透明 QColor。Qt3 的 QColor 只有三参构造（qcolor.h:82），无四参、无
// setAlpha、无 Qt::transparent，且 QPainter 根本没有抗锯齿概念（qpainter.h
// 无 setRenderHint/RenderHit 声明）。这三个函数只被 makeTgsPlaceholder 用到，
// Qt3 侧退化为不透明色 + 无抗锯齿占位图，不影响任何解码/编码路径。
inline QColor qColorRgba(int r, int g, int b, int a)
{
    Q_UNUSED(a);
    return QColor(r, g, b);
}

// QPainter 抗锯齿开关（Qt3 无 RenderHint，no-op）。
inline void qPainterSetAntialiasing(QPainter&)
{
}

// drawRoundedRect 的 Qt3 等价物是 drawRoundRect(QRect,int,int)（qpainter.h:203）。
// Qt6 的 drawRoundedRect 半径参数是 qreal 且按半径计；Qt3 的是 int 且以
// 直径计（drawRoundRect 的 xRnd/yRnd 语义为 0..100 的百分比直径），故这里
// 把 qreal 半径换算成 Qt3 的「直径 = 2*半径」整数像素。
inline void qPainterDrawRoundedRect(QPainter& p, const QRect& r,
                                     qreal rx, qreal ry)
{
    p.drawRoundRect(r, int(qRound(rx * 2)), int(qRound(ry * 2)));
}

// 新建 w×h 的 32 位带 alpha 图（QImage::Format_ARGB32_Premultiplied 的 Qt3
// 等价物）。Qt3 的 QImage 无 Format 概念，构造参数是 int depth（qimage.h:74）。
inline QImage qImageNew32(int w, int h)
{
    QImage im(w, h, 32);
    if (!im.isNull()) im.setAlphaBuffer(true);
    return im;
}

// 填全透明。Qt3 无 Qt::transparent（随 Qt4.6 的 QColor alpha 引入），也无
// QColor::setAlpha（qcolor.h 只有三参构造 82 行与 qAlpha 60 行），
// 只能靠 qRgba 的 alpha 字节写 0。
inline void qImageFillTransparent(QImage& im)
{
    im.fill(qRgba(0, 0, 0, 0));
}

// 两个图是否像素完全一致（替代 memcmp(constBits()) 判重）。
inline bool qImageSameRgba(const QImage& a, const QImage& b)
{
    if (a.isNull() || b.isNull()) return a.isNull() && b.isNull();
    if (a.size() != b.size()) return false;
    return qImageRgbaBytes(a) == qImageRgbaBytes(b);
}

// 逐行取 RGBA 字节（替代 constScanLine + QByteArray::fromRawData）。
// Qt3 的 bytesPerLine() 含 4 字节对齐填充，直接 fromRawData(constScanLine(y),
// w*4) 在 w 不是 4 的倍数时会把下一行的填充字节当成像素，必须整块取出再切片。
inline QByteArray qImageScanlineRgba(const QImage& im, int y)
{
    if (im.isNull() || y < 0 || y >= im.height()) return QByteArray();
    const int w = im.width();
    const QByteArray all = qImageRgbaBytes(im);
    if (all.isEmpty()) return all;
    // ⚠ Qt3 的 QByteArray 是 QMemArray<char>，**没有 mid()**（Qt 4.2 才有），
    //   必须自己 resize + memcpy 切片。
    QByteArray row(w * 4);
    if (!row.isNull())
        ::memcpy(row.data(), all.data() + (size_t)y * w * 4, (size_t)(w * 4));
    return row;
}

// Qt6 侧：全部转调原生 API，Qt6 行为与原调用点一字不差。
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

inline QImage qImageScaledKeepAspectSmooth(const QImage& im, const QSize& s)
{
    return im.scaled(s, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

inline QImage qImageNew32(int w, int h)
{
    return QImage(w, h, QImage::Format_ARGB32_Premultiplied);
}

inline void qImageFillTransparent(QImage& im)
{
    im.fill(Qt::transparent);
}

inline bool qImageSameRgba(const QImage& a, const QImage& b)
{
    if (a.isNull() || b.isNull()) return a.isNull() && b.isNull();
    if (a.size() != b.size()) return false;
    return a.constBits() && b.constBits()
        && ::memcmp(a.constBits(), b.constBits(),
                    std::size_t(a.sizeInBytes())) == 0;
}

inline QByteArray qImageScanlineRgba(const QImage& im, int y)
{
    if (im.isNull() || y < 0 || y >= im.height()) return QByteArray();
    return QByteArray(reinterpret_cast<const char*>(im.constScanLine(y)),
                      im.width() * 4);
}

#endif // QT_VERSION < 0x040000

#endif // QLSTIK_QIMAGE_SHIM_H