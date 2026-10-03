#ifndef QLSTIK_QIMAGEREADER_SHIM_H
#define QLSTIK_QIMAGEREADER_SHIM_H

// Qt3 完整 QImageReader（Qt3 连 qimagereader.h / qimageio.h 都没有）。
//
// ── 需求来源（anystik/src/stickerstore.cpp）────────────────────────────────
//   构造：QImageReader(QIODevice*) ×7 / QImageReader(QString) ×2
//   接口：setAutoTransform / setFormat / setScaledSize / scaledSize / canRead /
//         format / size / read / imageCount / jumpToNextImage / nextImageDelay /
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
//   1) read() **无条件推进游标**，读尽返回 null（Qt6 实测静态 JPEG 第 2 次
//      read() 即 null；gif/webp 动画每次吐下一帧）。早期误写成「仅动画自推进」
//      会让静态图 while(!isNull()) 死循环，已由代码与测试钉死。
//   2) jumpToNextImage() 永远推进一帧并返回是否还有下一帧；失败时游标推到末尾
//      之外，使随后 read() 返回 null。
//   插帧读取顺序必须与 Qt6 一致，否则动画帧会重复/漏读。
//
// ── 惰性解码模型（B-1.2，2026-10-04）：对齐 Qt6 的「结构扫描 / 按需解码」分离 ──
//   prepareDecoder() 只做结构扫描（GIF nsgif_data_scan、WebP GetInfo、
//   APNG num_frames），拿到尺寸/帧数/动画标志而**不解任何像素**；read() 才按需
//   解出目标帧。imageCount()/supportsOption() 因此同 Qt6 一样基于扫描结果，
//   而非解码全帧。列表页缩略图只读第 0 帧，120 帧 GIF 不再被整段解出。
//   ★ 本类跨调用持有后端句柄（nsgif/WebPAnimDecoder/apng loader），故**不可
//     拷贝**（裸指针独占），与 Qt6 QImageReader 的 Q_DISABLE_COPY 一致。
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

// 后端解码器状态：完整定义在 .cpp（pimpl 惯例，避免头文件引入 nsgif/webp/apng 头）。
// 必须先于 class QImageReader 前置声明，因为类内以指针成员引用它。
struct QImageReaderDecoderState;

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
    ~QImageReader();

    QImageReader(const QImageReader&) = delete;              // 对齐 Qt6 Q_DISABLE_COPY
    QImageReader& operator=(const QImageReader&) = delete;

    void setFormat(const QByteArray& format) { m_forcedFmt = format; }
    void setAutoTransform(bool on)           { m_autoTransform = on; }
    void setScaledSize(const QSize& s)       { m_scaledSize = s; }
    QSize scaledSize() const                 { return m_scaledSize; }

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
    void     prepareDecoder();          // 结构扫描：建后端句柄 + 尺寸/帧数/动画标志
    bool     detectFormat();
    int      decodeMoreFrames(int want);   // 按需把帧缓存补到 want 帧，返回已缓存数

    QByteArray            m_bytes;
    QByteArray            m_fmt;
    QByteArray            m_forcedFmt;
    QString               m_fileName;
    QSize                 m_canvasSize;
    QSize                 m_scaledSize;    // setScaledSize()；无效则原尺寸
    bool                  m_sizeParsed;    // 头部是否已解析（不能靠 isNull 判断）
    bool                  m_autoTransform;
    bool                  m_prepared;      // prepareDecoder() 是否已跑
    bool                  m_animated;      // 格式是否多帧动画（对齐 Qt6 语义）
    int                   m_imageCount;    // 结构扫描得到的帧数（非解出的）
    int                   m_index;         // 下一帧下标
    int                   m_lastDelayMs;   // 上一 read() 帧的延迟(ms)
    QImageReaderDecoderState* m_dec;       // 后端句柄（裸指针，析构里释放）
    std::vector<QImage>   m_frames;        // 已解帧**缓存**（按需增长）
    std::vector<int>      m_delaysMs;
    ImageReaderError      m_error;
    QString               m_errorStr;
};

#endif // QT_VERSION < 0x040000

#endif // QLSTIK_QIMAGEREADER_SHIM_H