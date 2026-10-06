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
#ifdef QT3_BUILD
    return im.smoothScale(w, h, QImage::ScaleFree);
#else
    return im.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
#endif
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

    // 动图三兄弟（GIF/APNG/WebP）需要专用编码器，分别在批次 4/5/6。
    // 在那之前一律走下面的 PNG 回退分支 —— 出参如实报 true，toast 会显示
    //「回退PNG」，不会静默给用户一张静图。
    const bool animatedFormat = (fmt == qFmtLit("gif") || fmt == qFmtLit("apng")
                                 || fmt == qFmtLit("webp"));

    if (animated && animatedFormat) {
        // 待批次 4/5/6 填入真实编码器；此处保持空实现。
        sameFormat = false;
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