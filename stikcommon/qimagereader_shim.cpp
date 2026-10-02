// Qt3 完整 QImageReader 实现，配套 qimagereader_shim.h。
//
// 只在 QT3_BUILD 下编译；Qt6 走 Qt 原生 <QImageReader>，本文件不参与。
//
// 本文件引入了三个后端的实现：
//   · libnsgif          —— GIF 动画解码（anystik/vendor/libnsgif/gif.c,lzw.c）
//   · uc_apng_loader    —— APNG 动画解码（自带 stb_image 实现）
//   · libwebp           —— WebP 动画解码（系统库 -lwebp -lwebpdemux）
// 其余静态格式走 Qt3 原生 QImage::loadFromData。

#include "qimagereader_shim.h"

#if QT_VERSION < 0x040000

#include <qbuffer.h>
#include <qfile.h>
#include <stddef.h>
#include <stdint.h>
#include <cstdlib>
#include <cstring>

#include <webp/decode.h>
#include <webp/demux.h>

// uc_apng_loader 自己 include stb_image.h，但要求实现体在同 TU 里只出现一次，
// 因此这里先展开实现体（uc_apng_loader.h 见到 STBI_INCLUDE_STB_IMAGE_H 就跳过）。
// 注意：不能定义 UC_APNG_LOADER_NO_EXCEPTION，否则吞掉损坏文件错误。
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "uc_apng_loader.h"

// libnsgif 是 C99 + C 链接。C++ 侧要先备好 size_t/stdint 再 extern "C" 包含。
extern "C" {
#include "nsgif.h"
}

namespace {

// Qt3 的 QByteArray 没有 const char* 构造（qmemarray.h:62 只接受 int 长度），
// 字符串字面量必须走这个辅助函数。
QByteArray qba(const char* s)
{
    const int n = (int)::strlen(s);
    QByteArray r;
    r.resize(n);
    if (n > 0) ::memcpy(r.data(), s, (size_t)n);
    return r;
}

bool startsWith(const QByteArray& b, const char* sig, int n)
{
    return (int)b.size() >= n && ::memcmp(b.data(), sig, (size_t)n) == 0;
}

uint32_t be32(const char* p)
{
    const unsigned char* u = (const unsigned char*)p;
    return ((uint32_t)u[0] << 24) | ((uint32_t)u[1] << 16) | ((uint32_t)u[2] << 8) | (uint32_t)u[3];
}

uint32_t le32(const char* p)
{
    const unsigned char* u = (const unsigned char*)p;
    return (uint32_t)u[0] | ((uint32_t)u[1] << 8) | ((uint32_t)u[2] << 16) | ((uint32_t)u[3] << 24);
}

// PNG 是否含 acTL 块（= 真 APNG）。uc_apng_loader 遇到静态 PNG 会抛异常，
// 所以必须先分流，不能把静态 PNG 喂给它。
bool pngIsApng(const QByteArray& b)
{
    if (!startsWith(b, "\x89PNG\r\n\x1a\n", 8)) return false;
    int pos = 8;
    const int n = (int)b.size();
    while (pos + 8 <= n) {
        const uint32_t len = be32(b.data() + pos);
        if (::memcmp(b.data() + pos + 4, "acTL", 4) == 0) return true;
        if (::memcmp(b.data() + pos + 4, "IEND", 4) == 0) return false;
        if (len > 0x7fffffffu) return false;
        pos += 12 + (int)len;   // len + type + data + crc
        if (pos <= 0) return false;
    }
    return false;
}

QByteArray sniffFormat(const QByteArray& b)
{
    if (pngIsApng(b))               return qba("png");
    if (startsWith(b, "\x89PNG\r\n\x1a\n", 8)) return qba("png");
    if (startsWith(b, "\xff\xd8\xff", 3))      return qba("jpeg");
    if (startsWith(b, "GIF87a", 6))            return qba("gif");
    if (startsWith(b, "GIF89a", 6))            return qba("gif");
    if (b.size() >= 12 && ::memcmp(b.data(), "RIFF", 4) == 0
                      && ::memcmp(b.data() + 8, "WEBP", 4) == 0)
        return qba("webp");
    if (startsWith(b, "BM", 2))                return qba("bmp");
    if (startsWith(b, "II\x2a\x00", 4))        return qba("tiff");
    if (startsWith(b, "MM\x00\x2a", 4))        return qba("tiff");
    if (startsWith(b, "II\x2b\x00", 4))        return qba("tiff");
    if (startsWith(b, "MM\x00\x2b", 4))        return qba("tiff");
    if (startsWith(b, "\x00\x00\x01\x00", 4))  return qba("ico");
    if (startsWith(b, "/* XPM */", 9))         return qba("xpm");
    if (startsWith(b, "#define", 7))            return qba("xbm");
    if (startsWith(b, "P1", 2))                return qba("pgm");
    if (startsWith(b, "P2", 2))                return qba("pgm");
    if (startsWith(b, "P3", 2))                return qba("ppm");
    if (startsWith(b, "P4", 2))                return qba("pbm");
    if (startsWith(b, "P5", 2))                return qba("pgm");
    if (startsWith(b, "P6", 2))                return qba("ppm");
    if (startsWith(b, "<?xml", 5) || startsWith(b, "<svg", 4))
        return qba("svg");
    return QByteArray();
}

// RGBA8888 字节序 → Qt3 32 位 QImage。
// Qt3 32 位内存是 BGRA（实测见 qimage_shim.h 文件头），必须逐像素换通道。
// JPEG 尺寸：扫 marker 找 SOF0..SOF15（跳过 DHT=C4/DAC=CC/RSTn=D0..D7）。
QSize sniffJpegSize(const QByteArray& b)
{
    const unsigned char* p = (const unsigned char*)b.data();
    const int n = (int)b.size();
    if (n < 4) return QSize();
    int i = 2;
    while (i + 3 < n) {
        if (p[i] != 0xFF) { ++i; continue; }
        const unsigned char m = p[i + 1];
        if (m == 0xFF) { ++i; continue; }
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { i += 2; continue; }
        if (i + 3 >= n) break;
        const int seg = ((int)p[i + 2] << 8) | (int)p[i + 3];
        const bool isSof = (m >= 0xC0 && m <= 0xCF) && m != 0xC4 && m != 0xC8 && m != 0xCC;
        if (isSof) {
            if (i + 9 < n)
                return QSize(((int)p[i + 7] << 8) | (int)p[i + 8],
                             ((int)p[i + 5] << 8) | (int)p[i + 6]);
            break;
        }
        if (seg < 2) break;
        i += 2 + seg;
    }
    return QSize();
}

QImage imageFromRgba(const uint8_t* rgba, int w, int h)
{
    QImage im(w, h, 32);
    if (im.isNull() || w <= 0 || h <= 0) return QImage();
    im.setAlphaBuffer(true);
    const int stride = im.bytesPerLine();
    uchar* base = im.bits();
    for (int y = 0; y < h; ++y) {
        const uint8_t* p = rgba + (size_t)y * (size_t)w * 4;
        uchar* q = base + (size_t)y * (size_t)stride;
        for (int x = 0; x < w; ++x) {
            q[0] = p[2];   // B
            q[1] = p[1];   // G
            q[2] = p[0];   // R
            q[3] = p[3];   // A
            p += 4;
            q += 4;
        }
    }
    return im;
}

// ── libnsgif 回调 ─────────────────────────────────────────────────────────
nsgif_bitmap_t* gifCbCreate(int w, int h)
{
    return (nsgif_bitmap_t*)::malloc((size_t)w * (size_t)h * 4);
}
void gifCbDestroy(nsgif_bitmap_t* b) { ::free(b); }
uint8_t* gifCbBuffer(nsgif_bitmap_t* b) { return (uint8_t*)b; }
void gifCbSetOpaque(nsgif_bitmap_t*, bool) {}
bool gifCbTestOpaque(nsgif_bitmap_t*) { return false; }
void gifCbModified(nsgif_bitmap_t*) {}

} // namespace

// ── 构造 / 生命周期 ───────────────────────────────────────────────────────

QImageReader::QImageReader(QIODevice* device)
    : m_sizeParsed(false), m_autoTransform(false), m_decoded(false),
      m_animated(false), m_index(0),
      m_lastDelayMs(0), m_error(UnknownError)
{
    if (!device) {
        m_error = DeviceError;
        m_errorStr = "no device";
        return;
    }
    // Qt3 的 QBuffer 二次 open() 会失败，因此不 open，直接在当前位置 readAll。
    // Qt3 QIODevice 没有 seek()/isSeekable()（Qt4 才有），故无法回绕复位，
    // 只能在当前偏移读一次；这与 Qt6 插件同样遵守「device 位置由调用方负责」。
    m_bytes = device->readAll();
    detectFormat();
}

QImageReader::QImageReader(const QString& fileName)
    : m_fileName(fileName), m_sizeParsed(false), m_autoTransform(false),
      m_decoded(false),
      m_animated(false), m_index(0), m_lastDelayMs(0), m_error(UnknownError)
{
    QFile f(fileName);
    if (!f.open(IO_ReadOnly)) {
        m_error = FileNotFoundError;
        m_errorStr = QString("cannot open %1").arg(fileName);
        return;
    }
    m_bytes = f.readAll();
    f.close();
    detectFormat();
}

bool QImageReader::detectFormat()
{
    if (m_fmt.isEmpty()) m_fmt = sniffFormat(m_bytes);
    if (m_fmt.isEmpty() && !m_forcedFmt.isEmpty()) m_fmt = m_forcedFmt;
    // ⚠ 不能用 m_canvasSize.isNull() 当「未解析」标志：Qt3 的 QSize 默认值是
    // (-1,-1)，而 isNull() 判的是 w==0&&h==0，故 (-1,-1).isNull() == false，
    // 头部解析会被整个跳过（实测 size() 恒为 -1x-1）。故显式用 m_sizeParsed。
    if (!m_fmt.isEmpty() && !m_sizeParsed) {
        m_sizeParsed = true;
        // 便宜的头部尺寸解析：stickerstore 在 read() 之前先问 size()
        if (m_fmt == qba("png") && m_bytes.size() >= 24) {
            m_canvasSize = QSize((int)be32(m_bytes.data() + 16),
                                 (int)be32(m_bytes.data() + 20));
        } else if (m_fmt == qba("gif") && m_bytes.size() >= 10) {
            const int w = (int)(m_bytes.at(6) & 0xff) | ((int)(m_bytes.at(7) & 0xff) << 8);
            const int h = (int)(m_bytes.at(8) & 0xff) | ((int)(m_bytes.at(9) & 0xff) << 8);
            m_canvasSize = QSize(w, h);
        } else if (m_fmt == qba("bmp") && m_bytes.size() >= 26) {
            m_canvasSize = QSize((int)le32(m_bytes.data() + 18),
                                 (int)le32(m_bytes.data() + 22));
        } else if (m_fmt == qba("jpeg")) {
            m_canvasSize = sniffJpegSize(m_bytes);
        } else if (m_fmt == qba("webp") && m_bytes.size() > 0) {
            // 用 WebPGetInfo 取画布尺寸：静态与动画 WebP 都适用（读 VP8/VP8L/VP8X 头）
            int w = 0, h = 0;
            WebPData wd;
            wd.bytes = (const uint8_t*)m_bytes.data();
            wd.size = (size_t)m_bytes.size();
            if (WebPGetInfo(wd.bytes, wd.size, &w, &h)) m_canvasSize = QSize(w, h);
        }
    }
    return !m_fmt.isEmpty();
}

// ── 格式解码 ───────────────────────────────────────────────────────────────

bool QImageReader::decodeGif()
{
    nsgif_bitmap_cb_vt vt;
    ::memset(&vt, 0, sizeof(vt));
    vt.create = gifCbCreate;
    vt.destroy = gifCbDestroy;
    vt.get_buffer = gifCbBuffer;
    vt.set_opaque = gifCbSetOpaque;
    vt.test_opaque = gifCbTestOpaque;
    vt.modified = gifCbModified;
    // get_rowspan 故意留 NULL：库内部用 info.width×4 作为行跨度，这是 32bpp
    // 下的正确值。若填一个返回 0 的回调，所有行会重叠成错误画面（已实测）。

    nsgif_t* gif = 0;
    if (nsgif_create(&vt, NSGIF_BITMAP_FMT_R8G8B8A8, &gif) != NSGIF_OK || !gif) {
        m_error = UnsupportedFormatError;
        m_errorStr = "nsgif_create failed";
        return false;
    }
    if (nsgif_data_scan(gif, (size_t)m_bytes.size(),
                        (const uint8_t*)m_bytes.data()) != NSGIF_OK) {
        nsgif_destroy(gif);
        m_error = InvalidDataError;
        m_errorStr = "nsgif_data_scan failed";
        return false;
    }
    nsgif_data_complete(gif);
    const nsgif_info_t* info = nsgif_get_info(gif);
    if (!info || info->width == 0 || info->height == 0 || info->frame_count == 0) {
        nsgif_destroy(gif);
        m_error = InvalidDataError;
        m_errorStr = "no gif info";
        return false;
    }
    m_canvasSize = QSize((int)info->width, (int)info->height);
    // loop_max==0 表示无限循环，必须以 frame_count 截断
    for (uint32_t i = 0; i < info->frame_count; ++i) {
        nsgif_rect_t area;
        uint32_t delay = 0, frameNum = 0;
        if (nsgif_frame_prepare(gif, &area, &delay, &frameNum) != NSGIF_OK) break;
        nsgif_bitmap_t* bmp = 0;
        if (nsgif_frame_decode(gif, frameNum, &bmp) != NSGIF_OK || !bmp) break;
        m_frames.push_back(imageFromRgba((const uint8_t*)bmp,
                                         (int)info->width, (int)info->height));
        m_delaysMs.push_back((int)delay * 10);   // 厘秒 → 毫秒
    }
    nsgif_destroy(gif);
    if (m_frames.empty()) {
        m_error = DecodeError;
        m_errorStr = "gif decode produced no frame";
        return false;
    }
    // 与 Qt6 gif 插件一致：多帧即动画，read() 自推进
    m_animated = m_frames.size() > 1;
    return true;
}

bool QImageReader::decodeApng()
{
try {
        // create_memory_loader 收 const char* 并按值返回 loader<std::istringstream>
        const char* ptr = m_bytes.isEmpty() ? "" : m_bytes.data();
        uc::apng::loader<std::istringstream> l =
            uc::apng::create_memory_loader(ptr, (size_t)m_bytes.size());
        m_canvasSize = QSize((int)l.width(), (int)l.height());
        while (l.has_frame()) {
            auto fr = l.next_frame();
            const uint8_t* d = fr.image.data();
            m_frames.push_back(imageFromRgba(d, (int)fr.image.width(), (int)fr.image.height()));
            const uint32_t den = fr.delay_den ? fr.delay_den : 100;
            m_delaysMs.push_back((int)(fr.delay_num * 1000 / den));
        }
    } catch (std::exception& e) {
        m_error = InvalidDataError;
        m_errorStr = QString("apng: %1").arg(e.what());
        return false;
    }
    if (m_frames.empty()) {
        m_error = DecodeError;
        m_errorStr = "apng decode produced no frame";
        return false;
    }
    // 刻意与 Qt6 对齐：APNG 报 supportsOption(Animation)==false，
    // 靠 jumpToNextImage() 推进（详见 qimagereader_shim.h 头部说明）
    m_animated = false;
    return true;
}

bool QImageReader::decodeWebp()
{
    WebPData wd;
    wd.bytes = (const uint8_t*)m_bytes.data();
    wd.size = (size_t)m_bytes.size();
    WebPAnimDecoderOptions opts;
    if (!WebPAnimDecoderOptionsInit(&opts)) {
        m_error = UnsupportedFormatError;
        m_errorStr = "webp options failed";
        return false;
    }
    opts.color_mode = MODE_RGBA;
    opts.use_threads = 0;
    WebPAnimDecoder* dec = WebPAnimDecoderNew(&wd, &opts);
    if (!dec) {
        m_error = InvalidDataError;
        m_errorStr = "webp decode new failed";
        return false;
    }
    WebPAnimInfo info;
    if (!WebPAnimDecoderGetInfo(dec, &info)) {
        WebPAnimDecoderDelete(dec);
        m_error = InvalidDataError;
        m_errorStr = "webp get info failed";
        return false;
    }
    m_canvasSize = QSize((int)info.canvas_width, (int)info.canvas_height);
    int prevTs = 0;
    uint8_t* buf = 0;
    int ts = 0;
    while (WebPAnimDecoderHasMoreFrames(dec)) {
        if (!WebPAnimDecoderGetNext(dec, &buf, &ts)) break;
        m_frames.push_back(imageFromRgba(buf, (int)info.canvas_width, (int)info.canvas_height));
        int d = ts - prevTs;
        m_delaysMs.push_back(d > 0 ? d : 0);
        prevTs = ts;
    }
    WebPAnimDecoderDelete(dec);
    if (m_frames.empty()) {
        m_error = DecodeError;
        m_errorStr = "webp decode produced no frame";
        return false;
    }
    m_animated = m_frames.size() > 1;
    return true;
}

bool QImageReader::decodeStatic()
{
    // ⚠ 必须传 0（自动嗅探），不能传 m_fmt.data()。两个实测坑：
    //  (1) Qt3 QByteArray 底层 QMemArray<char>，data() 不以 '\0' 结尾，直接当
    //      const char* 会读到越界垃圾；
    //  (2) 就算补了终止符，Qt3 各 handler 注册的 format 名大小写不统一且敏感：
    //      bmp 只认 "BMP"、jpeg 只认 "JPEG"、png 两者都认、其余全不认
    //      （/tmp/qim/st4.cpp 对 t.bmp/t.jpg/t.png 各 7 种参数实测）。
    // 传 0 时三种样本均 ok=1 且尺寸正确，故一律自动嗅探。
    QImage im;
    im.loadFromData(m_bytes);
    if (im.isNull()) {
        m_error = UnsupportedFormatError;
        m_errorStr = QString("loadFromData failed for %1").arg(m_fmt.data());
        return false;
    }
    m_frames.push_back(im);
    m_delaysMs.push_back(0);
    if (m_canvasSize.isNull()) m_canvasSize = im.size();
    m_animated = false;
    return true;
}

void QImageReader::ensureDecoded()
{
    if (m_decoded) return;
    m_decoded = true;
    if (m_bytes.isEmpty()) {
        m_error = InvalidDataError;
        m_errorStr = "empty data";
        return;
    }
    if (!detectFormat()) {
        m_error = UnsupportedFormatError;
        m_errorStr = "unknown image format";
        return;
    }
    if (m_fmt == qba("gif"))       decodeGif();
    else if (m_fmt == qba("webp")) decodeWebp();
    else if (m_fmt == qba("png") && pngIsApng(m_bytes)) decodeApng();
    else                           decodeStatic();
}

// ── 查询接口 ───────────────────────────────────────────────────────────────

bool QImageReader::canRead()
{
    if (m_fmt.isEmpty()) detectFormat();
    return !m_fmt.isEmpty();
}

int QImageReader::imageCount()
{
    ensureDecoded();
    return (int)m_frames.size();
}

bool QImageReader::supportsOption(QImageIOHandler::ImageOption option)
{
    if (option != QImageIOHandler::Animation) return false;
    // m_animated 是解码后才定的（多帧才算动画），故必须先解一次；
    // Qt6 插件在 handler 构造期就填好该标志，但对外表现一致。
    ensureDecoded();
    return m_animated;
}

QImage QImageReader::read()
{
    ensureDecoded();
    // ⚠ 越界时必须直接返回 null。Qt6 对照实测（/tmp/qim/q6jump.cpp）：
    //   非动画格式 jumpToNextImage() 返回 false 后，read() 给的是 null
    //   （t3.apng/t.png exhausted isNull=1）；动画格式跳完则是最后一次 read() 给 null。
    // 若这里回退成「返回最后一帧」，上层 decodeAllFrames 的 while 循环会永远不退出。
    if (m_index < 0 || m_index >= (int)m_frames.size()) {
        m_error = InvalidDataError;
        m_errorStr = "no more frames";
        m_lastDelayMs = 0;   // 耗尽后不残留上一帧的延迟
        return QImage();
    }
    m_lastDelayMs = m_delaysMs[m_index];
    const QImage im = m_frames[m_index];
    // ⚠⚠ 必须**无条件**推进游标，不能只在 m_animated 时推进。
    //   Qt6 对照实测（/tmp/opencode/probe/zk6.cpp，6.7.3，静态 JPEG）：
    //     read#0 isNull=0 → read#1 isNull=1 → read#2 isNull=1
    //   即静态格式**读一次就耗尽**。而旧实现 `if (m_animated) ++m_index;`
    //   让静态图永远停在第 0 帧，第二次 read() 又返回首帧 —— 上层
    //   `while (!im.isNull()) im = reader.read();` 对静态图会**死循环**。
    //   （该 if 是早期按「动画才自推进」的想当然写的，未经 Qt6 对照。）
    ++m_index;
    return im;
}

bool QImageReader::jumpToNextImage()
{
    ensureDecoded();
    if (m_index + 1 < (int)m_frames.size()) {
        ++m_index;
        return true;
    }
    // 失败时必须把游标推到末尾之外：否则非动画格式（如 png/apng/tiff）会一直
    // 返回最后一帧，上层 decodeAllFrames 的 while(!im.isNull()) 循环不退出。
    // Qt6 对照实测：jump 返回 false 后紧接着 read() 给 null。
    m_index = (int)m_frames.size();
    return false;
}

// ── 静态接口 ───────────────────────────────────────────────────────────────

QByteArray QImageReader::imageFormat(QIODevice* device)
{
    if (!device) return QByteArray();
    // Qt3 无 seek()，只能在当前偏移读；调用方需保证设备刚复位。
    return sniffFormat(device->readAll());
}

QValueList<QByteArray> QImageReader::supportedImageFormats()
{
    // Qt3 没有 qimageio.h，也就没有任何枚举图像插件的公开 API，只能硬编码。
    // 运行时真能用 QImage::loadFromData 解的静态格式 + 垫片补的 gif/webp。
    // 刻意不含 svg/svgz/tiff：Qt3 无 svg 插件、无 tiff 插件，让上层据此明确
    // 拒绝并打日志，好过「列进白名单却在运行时静默解码失败」。
    static const char* kFormats[] = {
        "bmp", "jpeg", "mng", "pbm", "pgm", "png", "ppm", "xbm", "xpm",
        "gif", "webp"
    };
    QValueList<QByteArray> f;
    for (size_t i = 0; i < sizeof(kFormats) / sizeof(kFormats[0]); ++i) {
        const QByteArray v = qba(kFormats[i]);
        f.append(v);
    }
    return f;
}

#endif // QT_VERSION < 0x040000