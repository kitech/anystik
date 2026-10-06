// qimagesmoothscale.cpp —— Qt3 的快速平滑缩放（Imlib2 面积采样算法）。
//
// 背景：Qt3 的 QImage::smoothScale() 实现基于 NetPBM 的 pnmscale.c，实测
// （240x240/154 帧 GIF 缩到 120x120）比 Qt6 的 scaled(...,SmoothTransformation)
// 慢约 38 倍（3690ms vs 96ms）。Qt6 的快路径是 qSmoothScaleImage()：Daniel M.
// Duley 从 Imlib2 移植进 Qt 的 C 算法。本文件把该算法移植回 Qt3，使两端
// 逐像素一致（GIF 为 1-bit alpha，直存/预乘等价），并消除该性能差。
//
// 来源：qtbase/src/gui/painting/qimagescale.cpp（Qt 6.7.3），版权与许可见其头：
//   Copyright (C) 2004, 2005 Daniel M. Duley
//   SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR
//                            GPL-2.0-only OR GPL-3.0-only
// 移植范围：仅 32 位带 alpha 的 AARGBA 路径（up_xy / up_x_down_y /
//   down_x_up_y / down_xy），去掉 64 位/浮点/SSE/NEON/多线程分支。
//
// ⚠ 只对 Qt3 编译；Qt6 侧 qImageSmoothScale 是头内 inline（原生 scaled）。

#include <qglobal.h>

#if QT_VERSION < 0x040000

#include <qimage.h>
#include <qcolor.h>

namespace {

struct QImageScaleInfo {
    int *xpoints;
    const unsigned int **ypoints;
    int *xapoints;
    int *yapoints;
    int xup_yup;
    int sw;
    int sh;
};

const unsigned int **qimageCalcYPoints(const unsigned int *src,
                                       int sw, int sh, int dh)
{
    const unsigned int **p;
    int j = 0, rv = 0;
    Q_INT64 val, inc;

    if (dh < 0) {
        dh = -dh;
        rv = 1;
    }
    p = new const unsigned int *[dh + 1];

    int up = (dh > sh || dh == sh);           // qAbs(dh) >= sh
    val = up ? 0x8000 * sh / dh - 0x8000 : 0;
    inc = (((Q_INT64)sh) << 16) / dh;
    for (int i = 0; i < dh; i++) {
        Q_INT64 v = val >> 16;
        p[j++] = src + (v > 0 ? v : 0) * sw;
        val += inc;
    }
    if (rv) {
        for (int i = dh / 2; --i >= 0; ) {
            const unsigned int *tmp = p[i];
            p[i] = p[dh - i - 1];
            p[dh - i - 1] = tmp;
        }
    }
    return p;
}

int *qimageCalcXPoints(int sw, int dw)
{
    int *p, j = 0, rv = 0;
    Q_INT64 val, inc;

    if (dw < 0) {
        dw = -dw;
        rv = 1;
    }
    p = new int[dw + 1];

    int up = (dw > sw || dw == sw);           // qAbs(dw) >= sw
    val = up ? 0x8000 * sw / dw - 0x8000 : 0;
    inc = (((Q_INT64)sw) << 16) / dw;
    for (int i = 0; i < dw; i++) {
        Q_INT64 v = val >> 16;
        p[j++] = int(v > 0 ? v : 0);
        val += inc;
    }

    if (rv) {
        for (int i = dw / 2; --i >= 0; ) {
            int tmp = p[i];
            p[i] = p[dw - i - 1];
            p[dw - i - 1] = tmp;
        }
    }
    return p;
}

int *qimageCalcApoints(int s, int d, int up)
{
    int *p, j = 0, rv = 0;

    if (d < 0) {
        rv = 1;
        d = -d;
    }
    p = new int[d];

    if (up) {
        /* scaling up */
        Q_INT64 val = 0x8000 * s / d - 0x8000;
        Q_INT64 inc = (((Q_INT64)s) << 16) / d;
        for (int i = 0; i < d; i++) {
            int pos = int(val >> 16);
            if (pos < 0)
                p[j++] = 0;
            else if (pos >= (s - 1))
                p[j++] = 0;
            else
                p[j++] = int((val >> 8) - ((val >> 8) & 0xffffff00LL));
            val += inc;
        }
    } else {
        /* scaling down */
        Q_INT64 val = 0;
        Q_INT64 inc = (((Q_INT64)s) << 16) / d;
        int Cp = (((d << 14) + s - 1) / s);
        for (int i = 0; i < d; i++) {
            int ap = int(((0x10000 - (val & 0xffff)) * Cp) >> 16);
            p[j] = ap | (Cp << 16);
            j++;
            val += inc;
        }
    }
    if (rv) {
        int tmp;
        for (int i = d / 2; --i >= 0; ) {
            tmp = p[i];
            p[i] = p[d - i - 1];
            p[d - i - 1] = tmp;
        }
    }
    return p;
}

QImageScaleInfo *qimageFreeScaleInfo(QImageScaleInfo *isi)
{
    if (isi) {
        delete[] isi->xpoints;
        delete[] isi->ypoints;
        delete[] isi->xapoints;
        delete[] isi->yapoints;
        delete isi;
    }
    return 0;
}

QImageScaleInfo *qimageCalcScaleInfo(const QImage &img,
                                     int sw, int sh, int dw, int dh, char aa)
{
    QImageScaleInfo *isi;
    int scw, sch;

    scw = int(dw * Q_INT64(img.width()) / sw);
    sch = int(dh * Q_INT64(img.height()) / sh);

    isi = new QImageScaleInfo;
    if (!isi)
        return 0;
    isi->sh = sh;
    isi->sw = sw;

    int adw = dw < 0 ? -dw : dw;
    int adh = dh < 0 ? -dh : dh;
    isi->xup_yup = (adw >= sw) + ((adh >= sh) << 1);

    isi->xpoints = qimageCalcXPoints(img.width(), scw);
    if (!isi->xpoints)
        return qimageFreeScaleInfo(isi);
    isi->ypoints = qimageCalcYPoints((const unsigned int *)img.scanLine(0),
                                     img.bytesPerLine() / 4, img.height(), sch);
    if (!isi->ypoints)
        return qimageFreeScaleInfo(isi);
    if (aa) {
        isi->xapoints = qimageCalcApoints(img.width(), scw, isi->xup_yup & 1);
        if (!isi->xapoints)
            return qimageFreeScaleInfo(isi);
        isi->yapoints = qimageCalcApoints(img.height(), sch, isi->xup_yup & 2);
        if (!isi->yapoints)
            return qimageFreeScaleInfo(isi);
    }
    return isi;
}

inline void qimageScaleAARGBA_helper(const unsigned int *pix, int xyap, int Cxy,
                                     int step, int &r, int &g, int &b, int &a)
{
    r = qRed(*pix)   * xyap;
    g = qGreen(*pix) * xyap;
    b = qBlue(*pix)  * xyap;
    a = qAlpha(*pix) * xyap;
    int j;
    for (j = (1 << 14) - xyap; j > Cxy; j -= Cxy) {
        pix += step;
        r += qRed(*pix)   * Cxy;
        g += qGreen(*pix) * Cxy;
        b += qBlue(*pix)  * Cxy;
        a += qAlpha(*pix) * Cxy;
    }
    pix += step;
    r += qRed(*pix)   * j;
    g += qGreen(*pix) * j;
    b += qBlue(*pix)  * j;
    a += qAlpha(*pix) * j;
}

static inline unsigned int INTERPOLATE_PIXEL_256(unsigned int x, unsigned int a,
                                                 unsigned int y, unsigned int b)
{
    Q_UINT64 t = (((Q_UINT64(x)) | ((Q_UINT64(x)) << 24)) & Q_UINT64_C(0x00ff00ff00ff00ff)) * a;
    t += (((Q_UINT64(y)) | ((Q_UINT64(y)) << 24)) & Q_UINT64_C(0x00ff00ff00ff00ff)) * b;
    t >>= 8;
    t &= Q_UINT64_C(0x00ff00ff00ff00ff);
    return (unsigned int)(t) | (unsigned int)(t >> 24);
}

// ⚠ 顺序必须与 Qt6 x86 上实际走的 SSE2 版 interpolate_4_pixels 一致：
//   先按 y(上下) 插值、再按 x(左右) 插值。Qt 文档里的通用 C 版是 x 先 y 后，
//   整数截断不可交换，两者会差 ±1（实测 up_xy 路径 scale_hash 不一致）。
//   这里复刻 SSE2 的 y-first 顺序，保证与 anystik 逐字节一致。
static inline unsigned int interpolate_4_pixels(unsigned int tl, unsigned int tr,
                                                unsigned int bl, unsigned int br,
                                                unsigned int distx, unsigned int disty)
{
    unsigned int idistx = 256 - distx;
    unsigned int idisty = 256 - disty;
    unsigned int left  = INTERPOLATE_PIXEL_256(tl, idisty, bl, disty);
    unsigned int right = INTERPOLATE_PIXEL_256(tr, idisty, br, disty);
    return INTERPOLATE_PIXEL_256(left, idistx, right, distx);
}

void qimageScaleAARGBA_up_xy(QImageScaleInfo *isi, unsigned int *dest,
                             int dw, int dh, int dow, int sow)
{
    const unsigned int **ypoints = isi->ypoints;
    int *xpoints = isi->xpoints;
    int *xapoints = isi->xapoints;
    int *yapoints = isi->yapoints;

    for (int y = 0; y < dh; ++y) {
        const unsigned int *sptr = ypoints[y];
        unsigned int *dptr = dest + (y * dow);
        const int yap = yapoints[y];
        if (yap > 0) {
            for (int x = 0; x < dw; x++) {
                const unsigned int *pix = sptr + xpoints[x];
                const int xap = xapoints[x];
                if (xap > 0)
                    *dptr = interpolate_4_pixels(pix[0], pix[1], pix[sow], pix[sow + 1],
                                                 xap, yap);
                else
                    *dptr = INTERPOLATE_PIXEL_256(pix[0], 256 - yap, pix[sow], yap);
                dptr++;
            }
        } else {
            for (int x = 0; x < dw; x++) {
                const unsigned int *pix = sptr + xpoints[x];
                const int xap = xapoints[x];
                if (xap > 0)
                    *dptr = INTERPOLATE_PIXEL_256(pix[0], 256 - xap, pix[1], xap);
                else
                    *dptr = pix[0];
                dptr++;
            }
        }
    }
}

void qimageScaleAARGBA_up_x_down_y(QImageScaleInfo *isi, unsigned int *dest,
                                   int dw, int dh, int dow, int sow)
{
    const unsigned int **ypoints = isi->ypoints;
    int *xpoints = isi->xpoints;
    int *xapoints = isi->xapoints;
    int *yapoints = isi->yapoints;

    for (int y = 0; y < dh; ++y) {
        int Cy = yapoints[y] >> 16;
        int yap = yapoints[y] & 0xffff;

        unsigned int *dptr = dest + (y * dow);
        for (int x = 0; x < dw; x++) {
            const unsigned int *sptr = ypoints[y] + xpoints[x];
            int r, g, b, a;
            qimageScaleAARGBA_helper(sptr, yap, Cy, sow, r, g, b, a);

            int xap = xapoints[x];
            if (xap > 0) {
                int rr, gg, bb, aa;
                qimageScaleAARGBA_helper(sptr + 1, yap, Cy, sow, rr, gg, bb, aa);

                r = r * (256 - xap);
                g = g * (256 - xap);
                b = b * (256 - xap);
                a = a * (256 - xap);
                r = (r + (rr * xap)) >> 8;
                g = (g + (gg * xap)) >> 8;
                b = (b + (bb * xap)) >> 8;
                a = (a + (aa * xap)) >> 8;
            }
            *dptr++ = qRgba(r >> 14, g >> 14, b >> 14, a >> 14);
        }
    }
}

void qimageScaleAARGBA_down_x_up_y(QImageScaleInfo *isi, unsigned int *dest,
                                   int dw, int dh, int dow, int sow)
{
    const unsigned int **ypoints = isi->ypoints;
    int *xpoints = isi->xpoints;
    int *xapoints = isi->xapoints;
    int *yapoints = isi->yapoints;

    for (int y = 0; y < dh; ++y) {
        unsigned int *dptr = dest + (y * dow);
        for (int x = 0; x < dw; x++) {
            int Cx = xapoints[x] >> 16;
            int xap = xapoints[x] & 0xffff;

            const unsigned int *sptr = ypoints[y] + xpoints[x];
            int r, g, b, a;
            qimageScaleAARGBA_helper(sptr, xap, Cx, 1, r, g, b, a);

            int yap = yapoints[y];
            if (yap > 0) {
                int rr, gg, bb, aa;
                qimageScaleAARGBA_helper(sptr + sow, xap, Cx, 1, rr, gg, bb, aa);

                r = r * (256 - yap);
                g = g * (256 - yap);
                b = b * (256 - yap);
                a = a * (256 - yap);
                r = (r + (rr * yap)) >> 8;
                g = (g + (gg * yap)) >> 8;
                b = (b + (bb * yap)) >> 8;
                a = (a + (aa * yap)) >> 8;
            }
            *dptr = qRgba(r >> 14, g >> 14, b >> 14, a >> 14);
            dptr++;
        }
    }
}

void qimageScaleAARGBA_down_xy(QImageScaleInfo *isi, unsigned int *dest,
                               int dw, int dh, int dow, int sow)
{
    const unsigned int **ypoints = isi->ypoints;
    int *xpoints = isi->xpoints;
    int *xapoints = isi->xapoints;
    int *yapoints = isi->yapoints;

    for (int y = 0; y < dh; ++y) {
        int Cy = (yapoints[y]) >> 16;
        int yap = (yapoints[y]) & 0xffff;

        unsigned int *dptr = dest + (y * dow);
        for (int x = 0; x < dw; x++) {
            int Cx = xapoints[x] >> 16;
            int xap = xapoints[x] & 0xffff;

            const unsigned int *sptr = ypoints[y] + xpoints[x];
            int rx, gx, bx, ax;
            qimageScaleAARGBA_helper(sptr, xap, Cx, 1, rx, gx, bx, ax);

            int r = ((rx >> 4) * yap);
            int g = ((gx >> 4) * yap);
            int b = ((bx >> 4) * yap);
            int a = ((ax >> 4) * yap);

            int j;
            for (j = (1 << 14) - yap; j > Cy; j -= Cy) {
                sptr += sow;
                qimageScaleAARGBA_helper(sptr, xap, Cx, 1, rx, gx, bx, ax);
                r += ((rx >> 4) * Cy);
                g += ((gx >> 4) * Cy);
                b += ((bx >> 4) * Cy);
                a += ((ax >> 4) * Cy);
            }
            sptr += sow;
            qimageScaleAARGBA_helper(sptr, xap, Cx, 1, rx, gx, bx, ax);

            r += ((rx >> 4) * j);
            g += ((gx >> 4) * j);
            b += ((bx >> 4) * j);
            a += ((ax >> 4) * j);

            *dptr = qRgba(r >> 24, g >> 24, b >> 24, a >> 24);
            dptr++;
        }
    }
}

// 与 Qt6 qt_qimageScaleAARGBA 同款的四路分派。
void qimageScaleAARGBA(QImageScaleInfo *isi, unsigned int *dest,
                       int dw, int dh, int dow, int sow)
{
    if (isi->xup_yup == 3)
        qimageScaleAARGBA_up_xy(isi, dest, dw, dh, dow, sow);
    else if (isi->xup_yup == 1)
        qimageScaleAARGBA_up_x_down_y(isi, dest, dw, dh, dow, sow);
    else if (isi->xup_yup == 2)
        qimageScaleAARGBA_down_x_up_y(isi, dest, dw, dh, dow, sow);
    else
        qimageScaleAARGBA_down_xy(isi, dest, dw, dh, dow, sow);
}

} // namespace

// 32 位带 alpha 的平滑缩放（Imlib2 面积采样）。签名与 Qt6
// scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation) 对齐。
QImage qImageSmoothScale(const QImage &srcImage, int dw, int dh)
{
    if (srcImage.isNull() || dw <= 0 || dh <= 0)
        return QImage();

    // 统一成 32 位带 alpha：算法的 qRed/qGreen/qBlue/qAlpha 按 QRgb(ARGB) 取，
    // Qt3 的 32 位 QImage 内存正是 0xAARRGGBB（见 qimage_shim.h 头注实测）。
    QImage src = srcImage;
    if (src.depth() != 32) src = src.convertDepth(32);
    if (!src.hasAlphaBuffer()) {
        src = src.copy();
        src.setAlphaBuffer(true);
    }

    int w = src.width();
    int h = src.height();
    QImageScaleInfo *scaleinfo = qimageCalcScaleInfo(src, w, h, dw, dh, true);
    if (!scaleinfo)
        return QImage();

    QImage buffer(dw, dh, 32);
    if (buffer.isNull()) {
        qimageFreeScaleInfo(scaleinfo);
        return QImage();
    }
    buffer.setAlphaBuffer(true);

    qimageScaleAARGBA(scaleinfo, (unsigned int *)buffer.scanLine(0),
                      dw, dh, dw, src.bytesPerLine() / 4);

    qimageFreeScaleInfo(scaleinfo);
    return buffer;
}

#endif // QT_VERSION < 0x040000