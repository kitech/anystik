// stikcommon/test_qimagereader_shim.cpp —— QImageReader 垫片契约（Qt3 单端）
//
// 被测对象：qimagereader_shim.h/.cpp —— Qt3 无 QImageReader / QImageIOHandler，
//   本仓自建。产品用它读贴纸图（含 GIF/APNG/WebP 动画与静态图）。
//
// 覆盖策略：
//   · 静态解码走**真实 JPEG 往返**：Qt3 环境已确认 libqjpeg 插件可用
//     （探针：QImage.save("JPEG") + loadFromData 均成功，8x6 保真），
//     故本测试现场生成 JPEG 再读回，真正走通静态解码路径。
//   · 错误路径（文件不存在 / 非图像数据 / 空数据）逐个覆盖 —— 产品对下载
//     失败的贴纸图就靠这些分支来拒绝并打日志。
//   · GIF/APNG/WebP 动画路径需要二进制样本，本仓不放样本，故不在此覆盖；
//     其像素一致性在垫片开发期已用外部样本对拍（见 qimagereader_shim.h:44-51）。
//
// ⚠ Qt3 陷阱：QImage 构造是 (w, h, depth)，不是 Qt6 的 (w, h, format)…

#include <qstring.h>
#include <qimage.h>
#include <qfile.h>
#include <qcstring.h>
#include <qiodevice.h>
#include <qsize.h>
#include <qvaluelist.h>
#include <unistd.h>

#include "doctest/doctest.h"

#include "qglobaltype_shim.h"
#include "qdir_shim.h"
#include "qba_shim.h"
#include "qimagereader_shim.h"
#include "../qlcomp/compatcore34.h"   // qMkdir

namespace {

class TempDir {
public:
    TempDir()
    {
        m_path = QDir(qDirTempPath()).absFilePath(
            QString::fromLatin1("qlstik_qimg_")
            + QString::number(int(getpid())) + QChar('_')
            + QString::number(++s_counter()));
        qMkdir(m_path, true);
    }
    ~TempDir() { qDirRemoveRecursively(QDir(m_path)); }
    QString path() const { return m_path; }
    QString file(const char* name) const
    { return QDir(m_path).absFilePath(QString::fromLatin1(name)); }
    void writeGarbage(const char* name) const
    {
        QFile f(file(name));
        f.open(IO_WriteOnly | IO_Truncate);
        f.writeBlock("this is definitely not an image", 30);
        f.close();
    }
private:
    static int& s_counter() { static int c = 0; return c; }
    QString m_path;
};

// 生成一张纯色 JPEG 到 path；返回是否成功。
// ⚠ JPEG 有损，故只断言**尺寸**严格相等，不逐像素比（那是 JPEG 编解码的
//   范畴，不是本垫片要保证的）。尺寸错位才是本垫片静态解码的回归信号。
bool makeJpeg(const QString& path, int w, int h)
{
    QImage im(w, h, 32);          // Qt3 构造：(w, h, depth)
    im.fill(0x11223344);
    return im.save(path, "JPEG");
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 静态接口
// ═══════════════════════════════════════════════════════════════════════════

// ⚠ supportedImageFormats() 是硬编码白名单（Qt3 无插件枚举 API）。产品据此
//   判断「这个后缀要不要尝试解码」。若白名单漏了 gif/webp，动画贴纸会被上层
//   提前拒掉；若混进了 svg/tiff（Qt3 无对应插件），则会在运行时静默解码失败。
TEST_CASE("QImageReader::supportedImageFormats: 含 gif/webp/png/jpeg/bmp，不含 svg/tiff")
{
    const QValueList<QByteArray> fmts = QImageReader::supportedImageFormats();
    bool hasGif = false, hasWebp = false, hasPng = false, hasJpeg = false, hasBmp = false;
    bool hasSvg = false, hasTiff = false;
    for (QValueList<QByteArray>::ConstIterator it = fmts.begin(); it != fmts.end(); ++it) {
        const QByteArray& f = *it;
        if (f == qbaLit("gif"))  hasGif = true;
        if (f == qbaLit("webp")) hasWebp = true;
        if (f == qbaLit("png"))  hasPng = true;
        if (f == qbaLit("jpeg")) hasJpeg = true;
        if (f == qbaLit("bmp"))  hasBmp = true;
        if (f == qbaLit("svg"))  hasSvg = true;
        if (f == qbaLit("tiff")) hasTiff = true;
    }
    CHECK(hasGif);
    CHECK(hasWebp);
    CHECK(hasPng);
    CHECK(hasJpeg);
    CHECK(hasBmp);
    CHECK(!hasSvg);                // 明确不含：Qt3 无 svg 插件
    CHECK(!hasTiff);               // 明确不含：Qt3 无 tiff 插件
}

// ⚠ imageFormat() 静态版：非图像数据必须返回空，而不是乱猜一个格式。
TEST_CASE("QImageReader::imageFormat: 非图像数据返回空")
{
    TempDir td;
    td.writeGarbage("g.bin");
    QFile f(td.file("g.bin"));
    REQUIRE(f.open(IO_ReadOnly));
    CHECK_EQ(QImageReader::imageFormat(&f), QByteArray());
    f.close();
}

TEST_CASE("QImageReader::imageFormat: 对真实 JPEG 返回 jpeg")
{
    TempDir td;
    REQUIRE(makeJpeg(td.file("t.jpg"), 12, 9));
    QFile f(td.file("t.jpg"));
    REQUIRE(f.open(IO_ReadOnly));
    CHECK_EQ(QImageReader::imageFormat(&f), qbaLit("jpeg"));
    f.close();
}

// ⚠ imageFormat(nullptr) 必须安全返回空（不得解引用空指针）。
TEST_CASE("QImageReader::imageFormat: 空设备指针返回空")
{
    CHECK_EQ(QImageReader::imageFormat(0), QByteArray());
}

// ═══════════════════════════════════════════════════════════════════════════
// 静态解码：真实 JPEG 往返
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("QImageReader: JPEG 往返——canRead/format/size/read 全链路")
{
    TempDir td;
    REQUIRE(makeJpeg(td.file("t.jpg"), 12, 9));

    QImageReader zr(td.file("t.jpg"));
    CHECK(zr.canRead());
    CHECK_EQ(zr.format(), qbaLit("jpeg"));
    CHECK_EQ(zr.imageCount(), 1);               // 静态图一帧

    const QSize sz = zr.size();
    CHECK_EQ(sz.width(), 12);                   // 尺寸必须与写入一致
    CHECK_EQ(sz.height(), 9);

    const QImage im = zr.read();
    CHECK(!im.isNull());
    CHECK_EQ(im.width(), 12);
    CHECK_EQ(im.height(), 9);
}

// ⚠ setScaledSize 对齐 Qt6 回退缩放：read() 返回缩放后的帧，但 size()（画布尺寸）
//   仍报原图 12×9。列表缩略图正是靠这个把 1657 张贴纸统一缩到 152px。
TEST_CASE("QImageReader: setScaledSize 后 read 返回缩放帧且 size 不变")
{
    TempDir td;
    REQUIRE(makeJpeg(td.file("t.jpg"), 12, 9));
    QImageReader zr(td.file("t.jpg"));
    zr.setScaledSize(QSize(6, 6));
    CHECK_EQ(zr.scaledSize().width(), 6);
    CHECK_EQ(zr.scaledSize().height(), 6);
    const QImage im = zr.read();
    CHECK(!im.isNull());
    CHECK_EQ(im.width(), 6);
    CHECK_EQ(im.height(), 6);
    const QSize sz = zr.size();
    CHECK_EQ(sz.width(), 12);     // 画布尺寸不受 setScaledSize 影响
    CHECK_EQ(sz.height(), 9);
}

// ⚠ 未设置 scaledSize 时按原尺寸返回（默认行为，不能误缩放）。
TEST_CASE("QImageReader: 未设置 scaledSize 时按原尺寸返回")
{
    TempDir td;
    REQUIRE(makeJpeg(td.file("t.jpg"), 12, 9));
    QImageReader zr(td.file("t.jpg"));
    CHECK(!zr.scaledSize().isValid());
    const QImage im = zr.read();
    CHECK(!im.isNull());
    CHECK_EQ(im.width(), 12);
    CHECK_EQ(im.height(), 9);
}

// ⚠ 静态图不是动画：supportsOption(Animation) 必须为 false，否则上层
//   decodeAllFrames 会按动画循环读、多取一帧空图。
TEST_CASE("QImageReader: 静态 JPEG 的 supportsOption(Animation) 为 false")
{
    TempDir td;
    REQUIRE(makeJpeg(td.file("t.jpg"), 4, 4));
    QImageReader zr(td.file("t.jpg"));
    CHECK(!zr.supportsOption(QImageIOHandler::Animation));
    CHECK(!zr.supportsOption(QImageIOHandler::Size));   // 非 Animation 一律 false
}

// ⚠ 读完一帧后再 read() 必须给 null（不能回退成「重复最后一帧」），否则
//   上层 while(!im.isNull()) 循环永不退出。这是 QImageReader::read() 里
//   明确钉住的 Qt6 对齐行为。
TEST_CASE("QImageReader: 静态图读尽后再 read 返回 null")
{
    TempDir td;
    REQUIRE(makeJpeg(td.file("t.jpg"), 5, 5));
    QImageReader zr(td.file("t.jpg"));
    CHECK(!zr.read().isNull());
    CHECK(zr.read().isNull());                  // 第二次 → null
}

// ⚠ jumpToNextImage 对单帧静态图返回 false，且**推进游标到末尾之外**，使随后
//   read() 返回 null（QImageReader::jumpToNextImage()）。
TEST_CASE("QImageReader: 单帧图 jumpToNextImage 返回 false 且随后 read 为 null")
{
    TempDir td;
    REQUIRE(makeJpeg(td.file("t.jpg"), 3, 3));
    QImageReader zr(td.file("t.jpg"));
    CHECK(!zr.jumpToNextImage());
    CHECK(zr.read().isNull());
}

// ═══════════════════════════════════════════════════════════════════════════
// 错误路径
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("QImageReader: 文件不存在 → FileNotFoundError，各项查询安全降级")
{
    TempDir td;
    QImageReader zr(td.file("absent.jpg"));
    CHECK(!zr.canRead());
    CHECK_EQ(zr.error(), QImageReader::FileNotFoundError);
    CHECK(!zr.errorString().isEmpty());
    CHECK_EQ(zr.imageCount(), 0);
    CHECK(zr.read().isNull());
    CHECK(!zr.supportsOption(QImageIOHandler::Animation));
}

TEST_CASE("QImageReader: 存在但非图像 → canRead 为 false，解不出帧")
{
    TempDir td;
    td.writeGarbage("g.bin");
    QImageReader zr(td.file("g.bin"));
    CHECK(!zr.canRead());
    CHECK_EQ(zr.imageCount(), 0);
    CHECK(zr.read().isNull());
}

// ⚠ 空文件（0 字节）：prepareDecoder 走 InvalidDataError 分支。
//   产品对下载到 0 字节的贴纸必须能拒绝。
TEST_CASE("QImageReader: 空文件 → 解不出帧且 read 为 null")
{
    TempDir td;
    { QFile f(td.file("empty.jpg")); f.open(IO_WriteOnly | IO_Truncate); f.close(); }
    QImageReader zr(td.file("empty.jpg"));
    CHECK(!zr.canRead());
    CHECK_EQ(zr.imageCount(), 0);
    CHECK(zr.read().isNull());
}
