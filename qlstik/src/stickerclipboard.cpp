#include "stickerclipboard.h"

// ⚠ include 顺序有讲究：qclipboard_shim.h 在 Qt3 分支会 `#undef`/替换 QList 宏
//   （它引 qlist_shim.h 拿到值语义 QList<T>），必须排在 Qt 原生 QList 头之后。
//   本文件统一把 qlist_shim.h 放在最后一批 include。
#include "qformatsniff_shim.h"    // qSniffImageFormat / qMimeForFormat（header-only）
#include "qimagereader_shim.h"    // Qt3 的完整 QImageReader（Qt6 走原生 <QImageReader>）
#include "qclipboard_shim.h"      // Qt3 的 QMimeData / QGuiApplication 垫片（Qt6 整体不定义）
#include "qglobaltype_shim.h"     // qOpenReadOnly / qOpenWriteOnly（Qt3 枚举是 IO_ReadOnly）
#include "qfile_shim.h"           // qIODeviceWrite（Qt3 的 QFile/QBuffer 没有 write()）
#include "qba_shim.h"             // qbaConstData / qbaSize

// 批次 4/5/6 的编码器（GIF/APNG/WebP）共享：
#include "qimage_shim.h"          // qImageRgbaBytes / qImageScanlineRgba（RGBA8888 字节序）
#include "qtemporaryfile_shim.h"  // Qt3 的 QTemporaryFile（Qt6 走原生 <QTemporaryFile>）
#include "tangora_gif_O3.h"       // gif-h（公开域单头）Qt3 专用 O3 副本：GifBegin/GifWriteFrame/GifEnd
#include "qzlib_shim.h"           // Qt3 的 qCompress（Qt6 走 QtCore 原生，头内门控）

// WebP 动图编码（批次 6）：系统 libwebp 1.6.0，mux 提供 WebPAnimEncoder。
// 两个头自身带 extern "C"，直接 include 即可（stikcommon.pri 两端都挂 webpmux）。
#include <webp/encode.h>
#include <webp/mux.h>

// APNG/WebP 编码器用 <stdint.h> 的 uint32_t/uint16_t/uint8_t，不用 quint*：
// 后者 Qt 3.5 没有（quint32 是 Qt4 引入，qformatsniff_shim.h:22-23 已记）。
#include <stdint.h>

#ifdef QT3_BUILD
#include <qapplication.h>
#include <qfile.h>
#include <qbuffer.h>
#include <qimage.h>
#include <qcstring.h>
#else
#include <QApplication>
#include <QFile>
#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QMimeData>
#include <QGuiApplication>
#include <QClipboard>   // QGuiApplication::clipboard() 返回 QClipboard*，不引是 incomplete type
#include <QTemporaryFile>
#endif

#include "qlist_shim.h"           // Qt3 值语义 QList<T>（必须在上面所有 Qt 头之后）

#include <stdio.h>                // 仅探针/调试用；生产路径不用

// ═══════════════════════════════════════════════════════════════════════
// 常量
// ═══════════════════════════════════════════════════════════════════════

// 帧数上限。防止 600 帧 4096px 的图把内存吃穿（anysk 同样是硬上限）。
static const int kMaxFrames = 600;

// 缩放后单边最大像素。2.0 档遇到超大原图时等比缩回，防止爆内存。
static const int kMaxDim = 4096;

// ═══════════════════════════════════════════════════════════════════════
// 跨版本小工具
// ═══════════════════════════════════════════════════════════════════════

// QImage 缩放。Qt3 只有 **const 且返回副本** 的 scale()/smoothScale()
// （qimage.h:158/163，ScaleFree=拉伸到恰好 w×h），Qt4+ 是非 const 的 scaled()
// （默认 IgnoreAspectRatio，与 ScaleFree 一致）。
// ⚠ 与 stickerops.cpp:58 的 qScaledTo 逐字相同：StickerOps 降为薄包装后自己那份
//   就没有调用方了（唯一用它在 copyScaledToClipboard，而那函数改调本类），
//   故不留重复副本。若日后 stickerops 又要缩放，从这里取。
static QImage qClipScaled(const QImage& im, int w, int h)
{
    // Qt3 走本仓移植的 Imlib2 面积采样（qImageSmoothScale），Qt6 走原生
    // scaled(IgnoreAspectRatio, Smooth)。原 Qt3 的 im.smoothScale() 是
    // pnmscale 系实现，实测慢约 38 倍（见 qimage_shim.h 注）。
    return qImageSmoothScale(im, w, h);
}

// Qt3 无 qMax/qBound（/opt/qt338sh/include 下全树 grep 无定义，qglobal.h 只有
// qRound），故不能像 Qt4+ 那样直接写 qMax(...)，就地自写。
static int qClipMax(int a, int b) { return (a > b) ? a : b; }

// round(src*scale)，任一边超过 kMaxDim 则等比缩回（anysk stickerstore.cpp:1589
// cappedScaledSize 的等价物：qlstik 侧原先没有这个函数，本类自建）。
static QSize qClipTargetSize(const QSize& src, double scale)
{
    if (!src.isValid() || src.width() <= 0 || src.height() <= 0) return QSize();
    int w = int(src.width() * scale + 0.5);
    int h = int(src.height() * scale + 0.5);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    const int longer = qClipMax(w, h);
    if (longer > kMaxDim) {
        // 整数运算避免浮点误差累积：w/h 同比缩。至少留 1px。
        w = int((double)w * kMaxDim / longer + 0.5);
        h = int((double)h * kMaxDim / longer + 0.5);
        if (w < 1) w = 1;
        if (h < 1) h = 1;
    }
    return QSize(w, h);
}

// 读全文件字节。
static QByteArray qClipReadFile(const QString& path)
{
    QFile f(path);
    if (!qOpenReadOnly(f)) return QByteArray();
    const QByteArray bytes = f.readAll();
    f.close();
    return bytes;
}

// QImage → 指定格式的字节。Qt3 的 QImage::save 只有 (QIODevice*, const char*)
// 重载（qimage.h:198），两端签名一致；但 **QBuffer 拿到字节的方式不同**：
//   Qt3  QBuffer(QByteArray) 按值构造（qbuffer.h:54），写进去的是内部副本，
//        必须用 buffer() 取回（qbuffer.h:57）—— 依赖调用方那个变量会是空的。
//   Qt6  QBuffer(QByteArray*) 按引用构造，写完原变量即结果。
static QByteArray qClipEncodeOne(const QImage& im, const char* format)
{
    if (im.isNull()) return QByteArray();
#ifdef QT3_BUILD
    QBuffer buf;
    if (!qOpenWriteOnly(buf)) return QByteArray();
    if (!im.save(&buf, format)) { buf.close(); return QByteArray(); }
    buf.close();
    return buf.buffer();
#else
    QByteArray out;
    QBuffer buf(&out);
    if (!qOpenWriteOnly(buf)) return QByteArray();
    if (!im.save(&buf, format)) { buf.close(); return QByteArray(); }
    buf.close();
    return out;
#endif
}

// ═══════════════════════════════════════════════════════════════════════
// 批次 4：动图 GIF 编码器（tangora gif-h）
// ═══════════════════════════════════════════════════════════════════════

// 毫秒 → 百分秒（GIF GCE Delay Time 单位是 1/100s，tangora_gif.h:765 注释）。
// Qt 的 nextImageDelay() 是毫秒，不换算动画会慢 10 倍；GIF 合法域 1..65535，
// 0ms 会被某些查看器当 0 吞掉，clamp 到至少 1。
static uint32_t qClipGifDelayCs(int ms)
{
    if (ms < 1) ms = 1;
    int cs = (ms + 5) / 10;                 // 就近取整
    if (cs < 1) cs = 1;
    if (cs > 65535) cs = 65535;
    return uint32_t(cs);
}

// 帧列表 → GIF 字节。
//
// ⚠ tangora 的 GifBegin 第二参是**文件名**（tangora_gif.h:766）不是 FILE*，
//   且内部 fopen("wb") 独占创建，故必须走临时文件：写完 GifEnd 关句柄、再读回。
// ⚠ 临时文件模板沿用 test_qmimedatabase_shim.cpp:42-55 的范式：扩展名写在
//   XXXXXX **之后**，Qt3 的 fillTemplate() 用 mid(xPos+6) 保留尾部；
//   Qt3 QFile 没有 rename()，所以不能「先建再改名」。
// ⚠ 入参必须是 RGBA8888 字节序：qImageRgbaBytes()（qimage_shim.h）在 Qt3 侧
//   把 BGRA 内存逐像素换序、Qt6 侧等价于 convertToFormat，两端产出相同。
static QByteArray qClipEncodeGif(const QList<QImage>& frames,
                                 const QList<int>& delays)
{
    if (frames.size() < 2) return QByteArray();

#ifdef QT3_BUILD
    QTemporaryFile tmp(QString::fromUtf8("/tmp/stikclip_XXXXXX.gif"));
#else
    QTemporaryFile tmp(QStringLiteral("/tmp/stikclip_XXXXXX.gif"));
#endif
    if (!tmp.open()) return QByteArray();
    const QString path = tmp.fileName();
    tmp.close();                            // GifBegin 要独占创建文件

    const int w = frames.first().width();
    const int h = frames.first().height();
    const int defDelayCs = delays.size() == frames.size()
                         ? int(qClipGifDelayCs(delays.first())) : 10;

    GifWriter writer;
    if (!GifBegin(&writer, qUtf8Printable(path), uint32_t(w), uint32_t(h),
                  uint32_t(defDelayCs), 8, true)) {
        return QByteArray();
    }

    bool ok = true;
    for (int i = 0; i < frames.size(); ++i) {
        int delayCs = defDelayCs;
        if (delays.size() == frames.size())
            delayCs = int(qClipGifDelayCs(delays.at(i)));
        const QByteArray rgba = qImageRgbaBytes(frames.at(i));
        // ✓ 与 anystik stickerstore.cpp:1651 同款：qbaConstData() 返回
        //   const char*，gif-h 收 const uint8_t*，reinterpret_cast 逐位透传。
        ok = GifWriteFrame(&writer,
                reinterpret_cast<const uint8_t*>(qbaConstData(rgba)),
                uint32_t(w), uint32_t(h), uint32_t(delayCs), 8, true);
        if (!ok) break;
    }
    GifEnd(&writer);
    if (!ok) return QByteArray();

    QFile f(path);
    if (!qOpenReadOnly(f)) return QByteArray();
    const QByteArray bytes = f.readAll();
    f.close();
    return bytes;
}

// ═══════════════════════════════════════════════════════════════════════
// 批次 5：动图 APNG 编码器（手拼 PNG chunk）
// ═══════════════════════════════════════════════════════════════════════
//
// Qt 两端都没有 APNG 写出插件（§18.10 第 1 项），故自己拼 chunk。结构照
// anystik stickerstore.cpp 的 buildApngFromFrames：IHDR/acTL/fcTL/IDAT/fdAT/IEND；
// 每帧 filter=none 逐行、再 qCompress（zlib）成 IDAT/fdAT 的压缩流。
// ⚠ chunk 类型字面量一律走 qbaLit()，**不能**用 QByteArrayLiteral()：后者在
//   Qt3 会落成含尾 NUL 的 QCString（qba_shim.h:16），chunk type 变 5 字节、CRC 全错。

static QByteArray qClipBe32(uint32_t v)
{
    QByteArray b = qbaUninit(4);
    b[0] = char(v >> 24); b[1] = char(v >> 16); b[2] = char(v >> 8); b[3] = char(v);
    return b;
}

static QByteArray qClipBe16(uint16_t v)
{
    QByteArray b = qbaUninit(2);
    b[0] = char(v >> 8); b[1] = char(v);
    return b;
}

// PNG CRC32（IEEE 多项式 0xedb88320，反射式），覆盖 type + data。
static uint32_t qClipPngCrc(const QByteArray& type, const QByteArray& data)
{
    static uint32_t table[256];
    static bool tableReady = false;
    if (!tableReady) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        tableReady = true;
    }
    uint32_t c = 0xffffffffu;
    for (int i = 0; i < type.size(); ++i)
        c = table[(c ^ uint8_t(type.at(i))) & 0xff] ^ (c >> 8);
    for (int i = 0; i < data.size(); ++i)
        c = table[(c ^ uint8_t(data.at(i))) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

// 拼一个 PNG chunk：len(4) + type(4) + data + crc(4)。
static QByteArray qClipPngChunk(const QByteArray& type, const QByteArray& data)
{
    QByteArray out;
    qbaReserve(out, 12 + data.size());
    const uint32_t len = uint32_t(data.size());
    qbaAppend(out, char(len >> 24));
    qbaAppend(out, char(len >> 16));
    qbaAppend(out, char(len >> 8));
    qbaAppend(out, char(len));
    qbaAppend(out, type);
    qbaAppend(out, data);
    const uint32_t crc = qClipPngCrc(type, data);
    qbaAppend(out, char(crc >> 24));
    qbaAppend(out, char(crc >> 16));
    qbaAppend(out, char(crc >> 8));
    qbaAppend(out, char(crc));
    return out;
}

// 一帧 RGBA → filter=none 逐行扫描线，再 zlib 压缩（IDAT/fdAT 的载荷）。
static QByteArray qClipApngScanlinesZ(const QImage& im)
{
    QByteArray raw;
    const int bpl = im.width() * 4;
    qbaReserve(raw, im.height() * (bpl + 1));
    for (int y = 0; y < im.height(); ++y) {
        qbaAppend(raw, char(0));                        // filter type: none
        qbaAppend(raw, qImageScanlineRgba(im, y));
    }
    return qCompress(raw, 6);
}

// APNG 结构自检（不依赖解码器，纯 chunk 走查）：签名对、acTL 帧数对、
// fcTL 数==帧数、fdAT 数==帧数-1。Qt3 shim 的 APNG imageCount() 恒报 1，
// 不能靠读回帧数校验，故只能查 chunk。
static bool qClipApngSelfCheck(const QByteArray& png, int frames)
{
    if (png.size() < 8) return false;
    static const char sig[8] = {char(0x89), 'P', 'N', 'G', '\r', '\n', char(0x1a), '\n'};
    const char* base = qbaConstData(png);
    for (int i = 0; i < 8; ++i)
        if (base[i] != sig[i]) return false;

    int off = 8;
    int fcTl = 0, fdAt = 0;
    uint32_t actlFrames = 0;
    while (off + 12 <= png.size()) {
        const char* p = base + off;
        const uint32_t len = (uint32_t(uint8_t(p[0])) << 24) | (uint32_t(uint8_t(p[1])) << 16)
                          | (uint32_t(uint8_t(p[2])) << 8)  | uint32_t(uint8_t(p[3]));
        if (off + 12 + int(len) > png.size()) return false;
        const QByteArray type = qbaFromRaw(p + 4, 4);
        if (type == qbaLit("acTL") && len >= 8) {
            actlFrames = (uint32_t(uint8_t(p[8])) << 24) | (uint32_t(uint8_t(p[9])) << 16)
                       | (uint32_t(uint8_t(p[10])) << 8)  | uint32_t(uint8_t(p[11]));
        } else if (type == qbaLit("fcTL")) {
            ++fcTl;
        } else if (type == qbaLit("fdAT")) {
            ++fdAt;
        } else if (type == qbaLit("IEND")) {
            break;
        }
        off += 12 + int(len);
    }
    return actlFrames == uint32_t(frames) && fcTl == frames && fdAt == frames - 1;
}

// 帧列表（RGBA）→ APNG 字节。失败返回空，调用方走 PNG 回退。
static QByteArray qClipEncodeApng(const QList<QImage>& frames,
                                  const QList<int>& delays)
{
    if (frames.size() < 2) return QByteArray();
    const int w = frames.first().width();
    const int h = frames.first().height();
    if (w <= 0 || h <= 0) return QByteArray();

    QByteArray png;
    {
        const char sig[8] = {char(0x89), 'P', 'N', 'G', '\r', '\n', char(0x1a), '\n'};
        qbaAppend(png, sig, 8);
    }

    QByteArray ihdr;
    qbaAppend(ihdr, qClipBe32(uint32_t(w)));
    qbaAppend(ihdr, qClipBe32(uint32_t(h)));
    qbaAppend(ihdr, char(8));                 // bit depth
    qbaAppend(ihdr, char(6));                 // color type: RGBA
    qbaAppend(ihdr, char(0));                 // compression
    qbaAppend(ihdr, char(0));                 // filter
    qbaAppend(ihdr, char(0));                 // interlace
    qbaAppend(png, qClipPngChunk(qbaLit("IHDR"), ihdr));

    QByteArray actl;
    qbaAppend(actl, qClipBe32(uint32_t(frames.size())));
    qbaAppend(actl, qClipBe32(0));            // 无限循环
    qbaAppend(png, qClipPngChunk(qbaLit("acTL"), actl));

    uint32_t seq = 0;
    const bool hasDelay = (delays.size() == frames.size());
    for (int i = 0; i < frames.size(); ++i) {
        const QImage& fr = frames.at(i);
        uint16_t dnum = 1, dden = 10;          // 无 delay 信息时默认 100ms
        if (hasDelay) {
            int ms = delays.at(i);
            if (ms < 1) ms = 10;              // 0/非法延迟回落
            if (ms > 6553) ms = 6553;         // 百分秒域能表达的倒数上限
            dnum = uint16_t(qBound(1, (ms * 10 + 9) / 10, 6553));  // 四舍五入
            dden = 100;
        }
        QByteArray fctl;
        qbaAppend(fctl, qClipBe32(seq++));
        qbaAppend(fctl, qClipBe32(uint32_t(fr.width())));
        qbaAppend(fctl, qClipBe32(uint32_t(fr.height())));
        qbaAppend(fctl, qClipBe32(0));        // x 偏移
        qbaAppend(fctl, qClipBe32(0));        // y 偏移
        qbaAppend(fctl, qClipBe16(dnum));
        qbaAppend(fctl, qClipBe16(dden));
        qbaAppend(fctl, char(0));             // dispose_op: none（全画布帧）
        qbaAppend(fctl, char(0));             // blend_op: source（整帧替换）
        qbaAppend(png, qClipPngChunk(qbaLit("fcTL"), fctl));

        if (i == 0) {
            qbaAppend(png, qClipPngChunk(qbaLit("IDAT"), qClipApngScanlinesZ(fr)));
        } else {
            QByteArray fdat;
            qbaAppend(fdat, qClipBe32(seq++));
            qbaAppend(fdat, qClipApngScanlinesZ(fr));
            qbaAppend(png, qClipPngChunk(qbaLit("fdAT"), fdat));
        }
    }
    qbaAppend(png, qClipPngChunk(qbaLit("IEND"), QByteArray()));

    if (!qClipApngSelfCheck(png, frames.size())) return QByteArray();
    return png;
}

// ═══════════════════════════════════════════════════════════════════════
// 批次 6：动图 WebP 编码器（libwebp WebPAnimEncoder）
// ═══════════════════════════════════════════════════════════════════════
//
// §18.1 第 5 条硬约束：WebP 必须输出 WebP，不转 APNG。约束见 §18.10 第 4 项：
// 时间戳累计非递减；每帧必须全画布（本机 1.6.0 的 WebPPicture 无 x/y offset）；
// 末帧以 NULL 帧在总时长处收尾；WebPPictureFree 只释像素、结构每帧 Init。
static QByteArray qClipEncodeWebp(const QList<QImage>& frames,
                                  const QList<int>& delays)
{
    if (frames.size() < 2) return QByteArray();
    const int w = frames.first().width();
    const int h = frames.first().height();
    if (w <= 0 || h <= 0) return QByteArray();

    WebPAnimEncoderOptions opts;
    if (!WebPAnimEncoderOptionsInit(&opts)) return QByteArray();
    opts.anim_params.loop_count = 0;          // 无限循环

    WebPAnimEncoder* enc = WebPAnimEncoderNew(w, h, &opts);
    if (!enc) return QByteArray();

    bool ok = true;
    int ts = 0;
    for (int i = 0; i < frames.size(); ++i) {
        const QByteArray rgba = qImageRgbaBytes(frames.at(i));
        if (rgba.size() < w * h * 4) { ok = false; break; }

        int ms = (delays.size() == frames.size()) ? delays.at(i) : 100;
        if (ms < 1) ms = 1;
        ts += ms;                             // 累计时间戳，非递减

        WebPPicture pic;
        if (!WebPPictureInit(&pic)) { ok = false; break; }
        pic.use_argb = 1;
        pic.width = w;
        pic.height = h;
        if (!WebPPictureImportRGBA(
                &pic, reinterpret_cast<const uint8_t*>(qbaConstData(rgba)),
                w * 4)) {                     // rgba_stride 单位是**字节**
            WebPPictureFree(&pic);
            ok = false;
            break;
        }
        if (!WebPAnimEncoderAdd(enc, &pic, ts, NULL)) {
            WebPPictureFree(&pic);
            ok = false;
            break;
        }
        WebPPictureFree(&pic);                // 只释像素，pic 结构下帧复用
    }

    WebPData data;
    WebPDataInit(&data);
    if (ok) {
        if (!WebPAnimEncoderAdd(enc, NULL, ts, NULL)) ok = false;   // 末帧收尾
        else if (!WebPAnimEncoderAssemble(enc, &data)) ok = false;
    }

    QByteArray out;
    if (ok && data.bytes && data.size > 0)
        // ⚠ Qt3 的 QByteArray(QMemArray<char>) 没有 (const char*, int) 构造，
        //   用 qbaFromRaw 显式拷贝（Qt4+ 同样可用，两端一致）。
        out = qbaFromRaw(reinterpret_cast<const char*>(data.bytes), int(data.size));

    WebPDataClear(&data);
    WebPAnimEncoderDelete(enc);
    return out;
}

// ═══════════════════════════════════════════════════════════════════════
// 剪贴板写入
// ═══════════════════════════════════════════════════════════════════════

// 把「若干 (MIME, 字节)」挂进剪贴板。
//
// ⚠ §18.7 的硬约束：所有格式必须挂进**同一个** QMimeData。
//   Qt3 的 setData(QMimeSource*) 与 setImage() **互斥**（qclipboard_shim.h:26
//   实测记录：后者清前者），Qt6 的 setMimeData 也会替换整个剪贴板。所以绝不能
//   「先 setData 原始格式、再 setImageData(PNG) 回退」——那样前一份会被清掉。
// ⚠ Qt3 的 setData 接管指针所有权，new 之后绝不能自己 delete，否则双删。
static void qClipPut(const QList<QPair<QString, QByteArray> >& parts)
{
    if (parts.isEmpty()) return;
    QMimeData* md = new QMimeData;
    for (int i = 0; i < parts.size(); ++i) {
        md->setData(parts[i].first, parts[i].second);
    }
    QGuiApplication::clipboard()->setMimeData(md);
}

// ═══════════════════════════════════════════════════════════════════════
// 非缩放：原始字节直通
// ═══════════════════════════════════════════════════════════════════════

bool StickerClipboard::copyOriginal(const QString& filePath)
{
    if (filePath.isEmpty() || !QFile::exists(filePath)) return false;

    const QByteArray bytes = qClipReadFile(filePath);
    if (bytes.isEmpty()) return false;

    const QByteArray fmt = qSniffImageFormat(bytes);
    const char* mime = fmt.isEmpty() ? 0 : qMimeForFormat(fmt);

    // 嗅探不出的格式（SVG 变体、未知新格式…）：退回首帧位图，让 Qt 自己去认。
    // 这比直接失败好 —— 旧实现就是这条行为，不能让「保动画」改动把这类贴纸
    // 变成复制不了。
    if (!mime) {
#ifdef QT3_BUILD
        QBuffer buf;
        buf.setBuffer(bytes);
        if (!qOpenReadOnly(buf)) return false;
        QImageReader rd(&buf);
#else
        QBuffer buf(const_cast<QByteArray*>(&bytes));
        if (!qOpenReadOnly(buf)) return false;
        QImageReader rd(&buf);
#endif
        rd.setAutoTransform(true);
        const QImage first = rd.read();
        if (first.isNull()) return false;
        QMimeData* md = new QMimeData;
        md->setImageData(first);
        QGuiApplication::clipboard()->setMimeData(md);
        return true;
    }

    QList<QPair<QString, QByteArray> > parts;
    parts.append(qMakePair(QString::fromUtf8(mime), bytes));

    // APNG 双挂：image/apng 是社区约定，部分接收端只认 image/png
    // （qformatsniff_shim.h:150-152）。两个 MIME 指向**同一份字节**，
    // 不额外解码，动画与格式都无损。
    if (fmt == qFmtLit("apng")) {
        parts.append(qMakePair(QString::fromUtf8("image/png"), bytes));
    }

    qClipPut(parts);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
// 缩放：解帧 → 逐帧缩放 → 同格式重编码
// ═══════════════════════════════════════════════════════════════════════

bool StickerClipboard::copyScaled(const QString& filePath, double scale,
                                  bool* fellBackToPng)
{
    if (fellBackToPng) *fellBackToPng = false;
    if (filePath.isEmpty() || scale <= 0.0 || !QFile::exists(filePath)) return false;

    const QByteArray bytes = qClipReadFile(filePath);
    if (bytes.isEmpty()) return false;

    const QByteArray fmt = qSniffImageFormat(bytes);
    if (fmt.isEmpty()) return false;

    // ── 解帧 ──────────────────────────────────────────────────────────
#ifdef QT3_BUILD
    QBuffer buf;
    buf.setBuffer(bytes);
    if (!qOpenReadOnly(buf)) return false;
    QImageReader rd(&buf);
#else
    QBuffer buf(const_cast<QByteArray*>(&bytes));
    if (!qOpenReadOnly(buf)) return false;
    QImageReader rd(&buf);
#endif
    rd.setAutoTransform(true);

    // 多帧判定用 imageCount()，不用 supportsOption(Animation)：Qt6 的 PNG 插件
    // 对 APNG 报 imageCount()==1、Animation=false（stikcommon.pri:487-491 已实测），
    // 而 Qt3 侧想吐 APNG 全部帧靠的正是 read() 循环，不是 Animation 标志。
    const bool animated = (rd.imageCount() > 1);

    const QSize src = rd.size();
    const QSize target = qClipTargetSize(src, scale);
    if (!target.isValid() || target.width() <= 0 || target.height() <= 0) return false;

    QList<QImage> frames;
    QList<int>    delays;
    qListReserve(frames, kMaxFrames);      // Qt3 是 no-op（qlist_shim.h:135），无害
    qListReserve(delays, kMaxFrames);

    while (frames.size() < kMaxFrames) {
        const QImage im = rd.read();
        if (im.isNull()) break;
        frames.append(im);
        // ⚠ nextImageDelay() 必须在 read() **之后** 取：Qt 的契约是「当前已读帧
        //   还要显示多久」。Qt3 垫片同理（qimagereader_shim.cpp:526 在 read()
        //   内赋值）。read() 之前调用恒为 0 —— 探针首版就踩过这个坑。
        int d = rd.nextImageDelay();
        if (d < 1) d = 1;                  // 0 会被某些查看器当 0ms 吞掉
        delays.append(d);
        // 动画格式只能靠 read() 推进；Qt3 实测 read() 穷尽后 jumpToNextImage()
        // 返回 0 且再 read() 为 null，不能拿它判「还有下一帧」。
        // 非动画多页格式（TIFF）才需要显式跳。
        if (!animated && !rd.jumpToNextImage()) break;
    }
    buf.close();

    if (frames.isEmpty()) return false;

    // ── 逐帧缩放 ──────────────────────────────────────────────────────
    for (int i = 0; i < frames.size(); ++i) {
        frames[i] = qClipScaled(frames[i], target.width(), target.height());
        if (frames[i].isNull()) return false;
    }

    // ── 编码 ──────────────────────────────────────────────────────────
    QList<QPair<QString, QByteArray> > parts;
    bool sameFormat = false;

    // 动图三兄弟走专用编码器（批次 4/5/6）。判据用 **frames.size()>1**，不用
    // 上面的 `animated`：Qt3 的 APNG 垫片 imageCount() 恒报 1，但 read() +
    // jumpToNextImage() 能吐全部帧（stikcommon.pri:490 已记录），故必须按实解出
    // 的帧数判，否则 Qt3 的 APNG 会掉进静态分支。
    const bool animatedFormat = (fmt == qFmtLit("gif") || fmt == qFmtLit("apng")
                                 || fmt == qFmtLit("webp"));

    if (frames.size() > 1 && fmt == qFmtLit("gif")) {
        // 批次 4：tangora gif-h。RGBA 字节与帧延迟都来自上面解出的 frames/delays，
        // 帧数与 delay 逐帧保真（§18.5 帧上限 600 已在外层约束）。
        const QByteArray gif = qClipEncodeGif(frames, delays);
        if (!gif.isEmpty()) {
            parts.append(qMakePair(QString::fromUtf8("image/gif"), gif));
            sameFormat = true;
        }
    } else if (frames.size() > 1 && fmt == qFmtLit("apng")) {
        // 批次 5：手拼 APNG chunk。Qt6 上 APNG 被原生 PNG 插件拍平
        // （imageCount==1、read() 只出首帧），走不到这里；能到的只有 Qt3 垫片，
        // 与 §18.1 第 6 条「Qt6 缩放 APNG 回退 PNG」一致。
        const QByteArray apng = qClipEncodeApng(frames, delays);
        if (!apng.isEmpty()) {
            parts.append(qMakePair(QString::fromUtf8("image/apng"), apng));
            // APNG 双挂：社区约定 image/apng，部分接收端只认 image/png（同非缩放
            // 路径，qformatsniff_shim.h:150-152）。同一份字节，不再额外解码。
            parts.append(qMakePair(QString::fromUtf8("image/png"), apng));
            sameFormat = true;
        }
    } else if (frames.size() > 1 && fmt == qFmtLit("webp")) {
        // 批次 6：libwebp WebPAnimEncoder（§18.1 第 5 条：保 WebP，不转 APNG）。
        const QByteArray webp = qClipEncodeWebp(frames, delays);
        if (!webp.isEmpty()) {
            parts.append(qMakePair(QString::fromUtf8("image/webp"), webp));
            sameFormat = true;
        }
    } else if (frames.size() > 1 && !animatedFormat) {
        // 非动图格式却解出多帧（如多页 TIFF）：Qt 的 QImage::save 只能编一张，
        // 同格式做不到保帧数。回退 PNG 只出**首页**，后续页会丢 —— 这是回退的
        // 本义而非静默取舍（回退时出参已置 true，toast 会用"回退PNG"文案）。
        sameFormat = false;
    } else {
        // 单帧：交给 QImage::save。格式名按 sniff 结果映射到 Qt 的写出名。
        const char* outFmt = 0;
        if (fmt == qFmtLit("png"))       outFmt = "PNG";
        else if (fmt == qFmtLit("jpeg")) outFmt = "JPEG";   // ⚠ Qt3 不认 "JPG"
        else if (fmt == qFmtLit("bmp"))  outFmt = "BMP";
        else if (fmt == qFmtLit("ppm"))  outFmt = "PPM";
        else if (fmt == qFmtLit("pgm"))  outFmt = "PGM";
        else if (fmt == qFmtLit("pbm"))  outFmt = "PBM";
        else if (fmt == qFmtLit("xbm"))  outFmt = "XBM";
        else if (fmt == qFmtLit("xpm"))  outFmt = "XPM";
        else if (fmt == qFmtLit("tiff")) outFmt = "TIFF";
        else if (fmt == qFmtLit("webp")) outFmt = "WEBP";   // ⚠ Qt3 无此写出插件
        else if (fmt == qFmtLit("ico"))  outFmt = "ICO";

        if (outFmt) {
            const QByteArray enc = qClipEncodeOne(frames.first(), outFmt);
            // Qt3 的 outputFormats() 里没有 webp（§18.10 实测），save 会返回
            // false；这里靠返回值判定，不靠格式表，格式表会随构建环境漂。
            if (!enc.isEmpty()) {
                const char* mime = qMimeForFormat(fmt);
                if (mime) {
                    parts.append(qMakePair(QString::fromUtf8(mime), enc));
                    sameFormat = true;
                }
            }
        }
    }

    // ── PNG 回退（§18.1 第 4 条 / §18.8）──────────────────────────────
    if (!sameFormat) {
        const QByteArray png = qClipEncodeOne(frames.first(), "PNG");
        if (png.isEmpty()) return false;
        parts.append(qMakePair(QString::fromUtf8("image/png"), png));
        if (fellBackToPng) *fellBackToPng = true;
    }

    qClipPut(parts);
    return true;
}