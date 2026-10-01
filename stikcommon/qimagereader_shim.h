#ifndef QLSTIK_QIMAGEREADER_SHIM_H
#define QLSTIK_QIMAGEREADER_SHIM_H

// Qt3 完整 QImageReader（Qt3 连 qimagereader.h / qimageio.h 都没有）。
//
// ── 需求来源（anystik/src/stickerstore.cpp）────────────────────────────────
//   构造：QImageReader(QIODevice*) ×7 / QImageReader(QString) ×2
//   接口：setAutoTransform / setFormat / canRead / format / size / read /
//         imageCount / jumpToNextImage / nextImageDelay /
//         supportsOption(QImageIOHandler::Animation) /
//         error / errorString / ImageReaderError
//   静态：imageFormat(QIODevice*) / supportedImageFormats()
//   另有 imageaiutil.cpp:463 用 format()。
//
// ── 格式后端（Qt3 图像插件只有 jpeg/mng，无 gif/webp/apng）────────────────
//   png/jpeg/bmp/xpm/xbm/ppm/pbm/pgm  → Qt3 原生 QImage::loadFromData
//   gif                             → libnsgif（vendor/libnsgif）
//   apng                            → uc_apng_loader + stb_image（vendor/）
//   webp                            → 系统 libwebp WebPAnimDecoder
//
// ── 控制流对齐 Qt6 的两条铁律（实测 Qt6 6.7.3，见文件末实测记录）──────────
//   1) read() **只在 supportsOption(Animation) 为真时自推进**。Qt6 的 gif/webp
//      插件 read() 自动吐下一帧；png/apng（Qt6 PNG 插件把 APNG 拍平）/tiff 不
//      推进，必须靠 jumpToNextImage()。stickerstore 的 decodeAllFrames /
//      tiffFrameCount / fullDecodeScaledFrames 全靠这个区分来写循环
//      （stickerstore.cpp:1396-1408 有明确注释）。
//   2) jumpToNextImage() 永远推进一帧并返回是否还有下一帧。
//   插帧读取顺序必须与 Qt6 一致，否则动画帧会重复/漏读。
//
// ── ⚠ APNG 的平台差异（有意为之，须知悉）─────────────────────────────────
//   Qt6 的 PNG 插件不识别 APNG 动画：format()="png"、imageCount()=1、
//   read() 只出 1 帧（实测：t3.apng canRead=1 fmt=png anim=0 count=1 frames read=1，
//   且 libpng 报 "IDAT: Too much image data"）。故 **Qt6 上 APNG 缩放复制会
//   因 decodeAllFrames 只拿到 1 帧而回退静态首帧**。
//   本垫片让 format() 同样报 "png"、supportsOption(Animation) 同样报 false
//   （保持 Qt6 的控制流），但 read()+jumpToNextImage() 能吐出全部帧，因此
//   **Qt3 上 APNG 缩放复制是真动画**。这是用户明确要求的方向（"不靠转换格式
//   绕过，GIF/APNG/WebP 解码后缩放与重编码须保持原格式"），但确实是一处
//   Qt3/Qt6 行为差异，非 bug。
//
// 静态像素一致性实测（/opt/qt338sh + 系统 libwebp 1.6.0，样本 ~/ztprobe/img）：
//   · gif  t3.gif/d2.gif/big.gif(120帧)  libnsgif vs Qt6 RGBA8888 逐字节 **0 差异**
//   · apng t3.apng                       uc_apng vs PIL 逐字节 **0 差异**
//   · webp t.webp                        libwebp vs Qt6 RGBA8888 逐字节 **0 差异**
//   延迟也一致：gif 12/8/20cs → Qt6 nextImageDelay() 120/80/200ms（×10）；
//   apng 3/25、2/25、1/5 分数 → 120/80/200ms（与 PIL duration 相同）；
//   webp 时间戳 100/200 → 差值 100/100ms。

#include <qglobal.h>

#if QT_VERSION < 0x040000

#include <qcstring.h>
#include <qimage.h>
#include <qiodevice.h>
#include <qsize.h>
#include <qstring.h>
#include <qvaluelist.h>
#include <vector>

// ── QImageIOHandler 极简替身 ───────────────────────────────────────────────
// Qt3 整个 qimageio.h 都不存在，但调用点只用到 Animation 这一个枚举值。
class QImageIOHandler
{
public:
    enum ImageOption {
        Gamma,
        Description,
        ImageTransformation,
        Quality,
        Size,
        ScaledSize,
        Animation,
        Format,
        Depth,
        ProgressiveScan
    };
};

class QImageReader
{
public:
    enum ImageReaderError {
        UnknownError,
        FileNotFoundError,
        DeviceError,
        DecodeError,
        UnsupportedFormatError,
        InvalidDataError,
        RawImageFormatError
    };

    explicit QImageReader(QIODevice* device);
    explicit QImageReader(const QString& fileName);

    void setFormat(const QByteArray& format) { m_forcedFmt = format; }
    void setAutoTransform(bool on)           { m_autoTransform = on; }

    bool     canRead();
    QByteArray format() const                { return m_fmt; }
    QSize    size() const                    { return m_canvasSize; }
    QImage   read();
    int      imageCount();
    bool     jumpToNextImage();
    int      nextImageDelay() const          { return m_lastDelayMs; }
    bool     supportsOption(QImageIOHandler::ImageOption option);

    ImageReaderError error() const           { return m_error; }
    QString errorString() const              { return m_errorStr; }

    // 与 Qt6 同名静态。注意 Qt3 的 <QList> 其实是 qptrlist.h（QList=QPtrList，
    // append 收 const T*，迭代器解引用得到指针），故这里必须显式用 QValueList，
    // 才能让上层 `for (const QByteArray& f : fmts)` 这种值语义 range-for 成立。
    static QByteArray imageFormat(QIODevice* device);
    static QValueList<QByteArray> supportedImageFormats();

private:
    void     ensureDecoded();
    bool     detectFormat();
    bool     decodeGif();
    bool     decodeApng();
    bool     decodeWebp();
    bool     decodeStatic();

    QByteArray            m_bytes;
    QByteArray            m_fmt;
    QByteArray            m_forcedFmt;
    QString               m_fileName;
    QSize                 m_canvasSize;
    bool                  m_sizeParsed;    // 头部是否已解析（不能靠 isNull 判断）
    bool                  m_autoTransform;
    bool                  m_decoded;
    bool                  m_animated;      // read() 是否自推进（对齐 Qt6 插件行为）
    int                   m_index;         // 下一帧下标
    int                   m_lastDelayMs;   // 上一 read() 帧的延迟(ms)
    std::vector<QImage>   m_frames;
    std::vector<int>      m_delaysMs;
    ImageReaderError      m_error;
    QString               m_errorStr;
};

#endif // QT_VERSION < 0x040000

#endif // QLSTIK_QIMAGEREADER_SHIM_H