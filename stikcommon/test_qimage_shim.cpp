// stikcommon/test_qimage_shim.cpp —— QImage/QColor/QPainter 垫片契约（Qt3 单端）
//
// 被测对象：qimage_shim.h
//   qImageRgbaBytes / qImageRgba / qImageScaledKeepAspectSmooth / qImageNew32
//   qImageFillTransparent / qImageSameRgba / qImageScanlineRgba
//   qImageFormatTag / qImageConvertToFormat / qColorRgba
//
// 纯头文件，**无需链接产品 .cpp**，也**无需 QApplication**（实测 Qt3 的
// QImage/QPainter 都能在无 QApplication 下用；唯一例外是 QPainter 画 QImage
// ——Qt3 的 QImage 根本不继承 QPaintDevice，见下方「死代码」说明）。
//
// ── 本文件锁定的事实（全部由 /opt/qt338sh 隔离探针实测，非推测）──────────
//
// ⚠【BGRA 而非 RGBA】Qt3 的 32 位 QImage 内存是**本机字节序下的 BGRA**：
//     im.fill(qRgba(0x11,0x22,0x33,0xFF)); im.bits() → `33 22 11 FF`
//   故 qImageRgbaBytes() 必须逐像素换序（q[0]=p[2]…）才能给出真正的 RGBA。
//   这不是 Qt6 语义，Qt6 侧靠 convertToFormat(RGBA8888) 直接拿。
//
// ⚠【fill() 丢 alpha，setPixel() 不丢】Qt3 的 QImage::fill(QRgb) 会把
//   alpha 字节清 0：fill(qRgba(...,0xFF)) 后 bits() 得 `33 22 11 00`、
//   qAlpha(pixel()) 为 0；同一颜色用 setPixel 写则是 `33 22 11 FF`。
//   qRgba 本身没问题（qRgba(...,0xFF) == 0xFF112233）。convertDepth(32)
//   与 copy() 则都正确保留 alpha。qImageFillTransparent 已因此改为逐像素
//   setPixel（不再依赖「分量全 0 所以丢 alpha 恰好无害」的巧合）。
//
// ⚠【Qt3 下 QPainter 画 QImage 不可行】Qt3 的 QImage **不继承 QPaintDevice**
//   （qimage.h:68 `class Q_EXPORT QImage` 无基类；Qt4 起才继承），故
//   `QPainter p(&img)` 编译失败："no matching function for call to
//   'QPainter::QPainter(QImage*)'"。QPixmap 中转也不行：Qt3 只有
//   QPixmap(const QImage&) 单向构造，没有 QPixmap→QImage 反向转换。
//   ⇒ qPainterSetAntialiasing / qPainterDrawRoundedRect 在 **Qt3 下是死代码**
//     （产品侧 makeTgsPlaceholder 的 Qt3 分支改用 setPixel 手绘，
//     见 anystik/src/stickerstore.cpp:782-790 的说明）。
//   故本文件**不对这两个函数写 Qt3 断言**，只在此记录该边界；
//   它们的真实覆盖要靠 Qt6 侧运行（如 anystik 的 x64 构建）。
//
// ⚠【Qt3 的 bytesPerLine() 有对齐填充】qImageScanlineRgba 因此不能直接从
//   constScanLine(y) 切 w*4 —— 宽度非 4 倍数时会把下一行的填充字节当像素。
//   实现的做法是「qImageRgbaBytes() 整块取出后自己 memcpy 切片」，本文件
//   用 w=3..7 逐个宽度验证这一点。

#include <qstring.h>
#include <qimage.h>
#include <qcolor.h>
#include <qcstring.h>
#include <stdio.h>

#include "doctest/doctest.h"
#include "qglobaltype_shim.h"   // qint64/qreal（qimage_shim 用了 qreal）
#include "qimage_shim.h"

// 造一张 w×h 的 32 位带 alpha 图，并逐像素写入可预测的颜色。
// 用 setPixel 而非 fill —— fill 在 Qt3 下丢 alpha（见文件头说明）。
static QImage makeImage(int w, int h)
{
    QImage im(w, h, 32);
    im.setAlphaBuffer(true);
    return im;
}

// 把 (x,y) 像素设为纯色 rgba，alpha 固定 255（便于断言字节序）。
static QImage makeRamp(int w, int h)
{
    QImage im = makeImage(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            im.setPixel(x, y, qRgba(x * 10, y * 20, 0x33, 0xFF));
    return im;
}

TEST_CASE("qImageNew32: 建图得到 32 位带 alpha 的 QImage")
{
    const QImage im = qImageNew32(4, 2);
    CHECK_FALSE(im.isNull());
    CHECK_EQ(im.width(), 4);
    CHECK_EQ(im.height(), 2);
    // ⚠ Qt3 无 Format 概念，depth() 是唯一可查的格式线索
    CHECK_EQ(im.depth(), 32);
    CHECK(im.hasAlphaBuffer());
    // 0×0 会得到 null 图，不能崩
    const QImage bad = qImageNew32(0, 0);
    CHECK(bad.isNull());
}

TEST_CASE("qImageRgbaBytes: 输出真正的 RGBA 字节序（Qt3 内存本是 BGRA）")
{
    const QImage im = makeRamp(2, 1);
    const QByteArray b = qImageRgbaBytes(im);
    REQUIRE_EQ(b.size(), 2 * 1 * 4);
    // 第 0 像素 = (0, 0, 0x33, 0xFF) → 必须是 00 00 33 FF
    // 若忘记换序，Qt3 原始内存是 33 00 00 FF（BGRA），断言会挂
    CHECK_EQ(int((unsigned char)b[0]), 0x00);
    CHECK_EQ(int((unsigned char)b[1]), 0x00);
    CHECK_EQ(int((unsigned char)b[2]), 0x33);
    CHECK_EQ(int((unsigned char)b[3]), 0xFF);
    // 第 1 像素 = (10, 0, 0x33, 0xFF)
    CHECK_EQ(int((unsigned char)b[4]), 0x0A);
    CHECK_EQ(int((unsigned char)b[5]), 0x00);
    CHECK_EQ(int((unsigned char)b[6]), 0x33);
    CHECK_EQ(int((unsigned char)b[7]), 0xFF);
}

TEST_CASE("qImageRgbaBytes: null / 零尺寸图返回空，不越界")
{
    CHECK(qImageRgbaBytes(QImage()).isEmpty());
    CHECK(qImageRgbaBytes(qImageNew32(0, 0)).isEmpty());
}

TEST_CASE("qImageRgbaBytes: 8 位调色板图也先转 32 位（不逐像素分支）")
{
    // Qt3 的 8 位图是索引图，bits() 直接读会拿到索引值而非颜色，
    // 故实现里先 convertDepth(32)。这里只断言「能出正确长度的字节」。
    QImage im8(3, 2, 8);
    im8.fill(0);
    const QByteArray b = qImageRgbaBytes(im8);
    CHECK_EQ(b.size(), 3 * 2 * 4);
}

TEST_CASE("qImageRgba: 已是 32 位带 alpha 时保留尺寸")
{
    const QImage im = makeRamp(3, 2);
    const QImage r = qImageRgba(im);
    CHECK_EQ(r.width(), 3);
    CHECK_EQ(r.height(), 2);
    CHECK_EQ(r.depth(), 32);
    CHECK(r.hasAlphaBuffer());
    // null 图透传
    CHECK(qImageRgba(QImage()).isNull());
}

TEST_CASE("qImageFillTransparent: 全部像素归零（含 alpha）")
{
    QImage im = qImageNew32(3, 2);
    // 先填成不透明，确认函数真的改写了内容
    im.setPixel(0, 0, qRgba(0x11, 0x22, 0x33, 0xFF));
    qImageFillTransparent(im);
    const QByteArray b = qImageRgbaBytes(im);
    REQUIRE_EQ(b.size(), 3 * 2 * 4);
    for (int i = 0; i < b.size(); ++i) {
        CHECK_EQ(int((unsigned char)b[i]), 0);
    }
    // null 图不能崩
    QImage nul;
    qImageFillTransparent(nul);
    CHECK(nul.isNull());
}

TEST_CASE("qImageScanlineRgba: 宽度非 4 倍数时逐行切分正确")
{
    // ⚠ 这是本组最容易出错的地方：Qt3 的 bytesPerLine() 含 4 字节对齐
    //   填充。3 像素 × 4 字节 = 12，虽恰好 4 的倍数，但若换成 5/6/7 像素
    //   就会踩到填充字节。两种宽度都测，钉住「整块取出再切片」的做法。
    for (int w = 3; w <= 7; ++w) {
        const QImage im = makeRamp(w, 2);
        const QByteArray row1 = qImageScanlineRgba(im, 1);
        REQUIRE_EQ(row1.size(), w * 4);
        // y=1 行的像素 x：r = x*10, g = 20, b = 0x33, a = 0xFF
        for (int x = 0; x < w; ++x) {
            CHECK_EQ(int((unsigned char)row1[x * 4 + 0]), x * 10);
            CHECK_EQ(int((unsigned char)row1[x * 4 + 1]), 20);
            CHECK_EQ(int((unsigned char)row1[x * 4 + 2]), 0x33);
            CHECK_EQ(int((unsigned char)row1[x * 4 + 3]), 0xFF);
        }
        // y=0 行独立验证一次，确认没把两行搞混
        const QByteArray row0 = qImageScanlineRgba(im, 0);
        REQUIRE_EQ(row0.size(), w * 4);
        CHECK_EQ(int((unsigned char)row0[1]), 0);   // y=0 → g=0
        CHECK_EQ(int((unsigned char)row1[1]), 20);  // y=1 → g=20
    }
}

TEST_CASE("qImageScanlineRgba: 越界行号返回空（不读越界内存）")
{
    const QImage im = makeRamp(2, 2);
    CHECK(qImageScanlineRgba(im, -1).isEmpty());
    CHECK(qImageScanlineRgba(im, 2).isEmpty());
    CHECK(qImageScanlineRgba(im, 999).isEmpty());
    CHECK(qImageScanlineRgba(QImage(), 0).isEmpty());
}

TEST_CASE("qImageSameRgba: 逐像素比较，覆盖 null / 尺寸 / 内容三类差异")
{
    const QImage a = makeRamp(2, 2);
    CHECK(qImageSameRgba(a, a));
    // 内容不同
    QImage b = makeRamp(2, 2);
    b.setPixel(0, 0, qRgba(0x99, 0x99, 0x99, 0xFF));
    CHECK_FALSE(qImageSameRgba(a, b));
    // 尺寸不同
    CHECK_FALSE(qImageSameRgba(a, makeRamp(2, 3)));
    // 两个 null 视为相同（对称性：都 null → true）
    CHECK(qImageSameRgba(QImage(), QImage()));
    // null vs 非 null → false
    CHECK_FALSE(qImageSameRgba(QImage(), a));
    CHECK_FALSE(qImageSameRgba(a, QImage()));
}

TEST_CASE("qImageFormatTag: 编码 depth 与 alpha 标志位")
{
    // ⚠ 该值**只用于日志**，Qt3 与 Qt6 语义不同，不可用于逻辑判断
    //   （见 qimage_shim.h 的说明）。这里锁定 Qt3 侧 (depth<<1)|alpha。
    CHECK_EQ(qImageFormatTag(makeRamp(2, 2)), (32 << 1) | 1);
    QImage noAlpha = makeRamp(2, 2);
    noAlpha.setAlphaBuffer(false);
    CHECK_EQ(qImageFormatTag(noAlpha), (32 << 1) | 0);
    CHECK_EQ(qImageFormatTag(QImage()), -1);
}

TEST_CASE("qImageConvertToFormat: qFmtRgb32 去 alpha，其余保留")
{
    const QImage im = makeRamp(2, 2);
    // ⚠ Qt3 无 Format 概念，三种枚举只能近似成「带/不带 alpha」两种
    CHECK(qImageConvertToFormat(im, qFmtArgb32).hasAlphaBuffer());
    CHECK(qImageConvertToFormat(im, qFmtRgba8888).hasAlphaBuffer());
    CHECK_FALSE(qImageConvertToFormat(im, qFmtRgb32).hasAlphaBuffer());
    // 像素值本身不应因「转格式」而改变
    CHECK(qImageSameRgba(qImageConvertToFormat(im, qFmtRgb32), im));
    // null 图透传
    CHECK(qImageConvertToFormat(QImage(), qFmtRgb32).isNull());
}

TEST_CASE("qImageScaledKeepAspectSmooth: 等比缩放不超出请求框")
{
    // ⚠ Qt3 的 ScaleMin == Qt6 的 KeepAspectRatio（装得下，允许小于请求），
    //   ScaleMax 才是 KeepAspectRatioByExpanding。别用错枚举。
    //   实测 4x2 → 请求 3x3 得 3x1（宽受限于 3，高等比得 1.5→1）。
    const QImage s = qImageScaledKeepAspectSmooth(makeRamp(4, 2), QSize(3, 3));
    CHECK_EQ(s.width(), 3);
    CHECK_EQ(s.height(), 1);
    // 放大方向同样等比
    const QImage up = qImageScaledKeepAspectSmooth(makeRamp(2, 2), QSize(8, 8));
    CHECK_EQ(up.width(), 8);
    CHECK_EQ(up.height(), 8);
    // 已是目标尺寸时尺寸不变
    const QImage same = qImageScaledKeepAspectSmooth(makeRamp(4, 4), QSize(4, 4));
    CHECK_EQ(same.width(), 4);
    CHECK_EQ(same.height(), 4);
}

TEST_CASE("qColorRgba: Qt3 只保 RGB，alpha 被丢弃（有意降级）")
{
    // ⚠ Qt3 的 QColor 只有三参构造（qcolor.h:82），无四参、无 setAlpha，
    //   故 a 参数只能忽略。调用点（makeTgsPlaceholder）已确认不依赖 alpha。
    const QColor c = qColorRgba(10, 20, 30, 40);
    CHECK_EQ(c.red(), 10);
    CHECK_EQ(c.green(), 20);
    CHECK_EQ(c.blue(), 30);
    // 边界值不崩
    const QColor z = qColorRgba(0, 0, 0, 0);
    CHECK_EQ(z.red(), 0);
    const QColor m = qColorRgba(255, 255, 255, 255);
    CHECK_EQ(m.red(), 255);
}
