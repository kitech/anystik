#ifndef QLSTIK_QFORMATSNIFF_SHIM_H
#define QLSTIK_QFORMATSNIFF_SHIM_H

// 文件头魔数嗅探：按**内容**判格式，不看扩展名。Qt3/Qt6 共用，header-only。
//
// 为什么不直接复用 qimagereader_shim.cpp 里的 sniffFormat()：
//  1. 那是 .cpp 匿名 namespace 里的私有函数（连同私有助手 qba()/be32()/
//     startsWith()），外部拿不到。
//  2. 它的返回值语义是**格式族**：APNG 命中后返回 "png"（qimagereader_shim.cpp:87），
//     因为 reader 内部 m_fmt == "png" 同时表示「PNG 族」，被 :258（IHDR 解析画布
//     尺寸）和 :382（APNG 解码分派）两处依赖。把 :87 改成 "apng" 会让这两个分支
//     同时落空 —— size() 恒 -1×-1，且 APNG 掉进通用 QImage::loadFromData 只出首帧、
//     动画静默丢失。故 reader 那行**不改**，本头另立两个入口。
//  3. 原函数整体只编在 Qt3（.cpp 挂在 stikcommon.pri 的 isEmpty(QT_VERSION) 块），
//     Qt6 侧根本用不了，而复制管线在 Qt6 上同样要嗅探（移植计划.md §18.3/§18.4）。
//
// 为什么不 include qba_shim.h 复用它的 qbaLit/qbaConstData：
//  qba_shim.h 会拉进 qset_shim.h，而 qset_shim.h 的 Qt3/Qt4 分支 include <QSet>
//  （Qt3 无 QSet、Qt4 有但大小写敏感），在「本头要跨 Qt3/Qt6」的语境下不可控。
//  本头只需要 strlen+memcpy 和 data()/constData() 两件事，各自 3~4 行，自足更稳。
//
// 整数类型用 <stdint.h> 的 uint32_t 而**不是** quint32：后者 Qt 3.5 没有
// （quint32 是 Qt4 引入），既有 qimagereader_shim.cpp:18/55 也是这么做的。

#ifndef QT_VERSION
#include <qglobal.h>
#endif

// QByteArray 的落点跨版本不同：Qt3 是 qcstring.h 里的 QByteArray（:98，
// public 继承 QMemArray<char>），Qt5+ 移到了 QByteArray 头。必须按版本分别显式包含
// —— 不能依赖「包含者恰好先引了 QByteArray」，否则换个包含顺序就报 incomplete type。
#if defined(QT_VERSION) && QT_VERSION >= 0x050000
#include <QByteArray>
#else
#include <qcstring.h>     // Qt3：QByteArray 定义处
#endif
#include <stdint.h>
#include <string.h>

// Qt3 的 QMemArray 没有 const char* 构造（qmemarray.h 只有 QByteArray(int size)，
// 见 qba_shim.h:366-371 的实测记录），字面量必须 strlen + resize + memcpy 手搓。
// Qt4+ 有原生 const char* 构造。
inline QByteArray qFmtLit(const char* s)
{
#if QT_VERSION < 0x040000
    const int n = (int)::strlen(s);
    QByteArray r;
    r.resize((uint)n);
    if (n > 0) ::memcpy(r.data(), s, (size_t)n);
    return r;
#else
    return QByteArray(s);
#endif
}

// Qt3 的 QMemArray<char>::data() 是 `type *data() const`（qmemarray.h:67）——
// **const 成员函数却返回可写 char***，没有 constData()，故取地址用它。
// Qt4+ 有真正的 constData()。
inline const char* qFmtData(const QByteArray& b)
{
#if QT_VERSION < 0x040000
    return b.data();
#else
    return b.constData();
#endif
}

// Qt3 size() 返回 uint、Qt6 返回 qsizetype，统一转 int（嗅探只用前若干字节，
// 32 位足够；超大文件会溢出但只影响 12~24 字节头部的存在性判断，可接受）。
inline int qFmtSize(const QByteArray& b)
{
    return (int)b.size();
}

inline bool qFmtStartsWith(const QByteArray& b, const char* sig, int n)
{
    return qFmtSize(b) >= n && ::memcmp(qFmtData(b), sig, (size_t)n) == 0;
}

inline uint32_t qFmtBe32(const char* p)
{
    const unsigned char* u = (const unsigned char*)p;
    return ((uint32_t)u[0] << 24) | ((uint32_t)u[1] << 16)
         | ((uint32_t)u[2] << 8)  | (uint32_t)u[3];
}

// PNG 是否含 acTL 块（= 真 APNG）。逐字搬自 qimagereader_shim.cpp:69-83。
// ⚠ 这不是可选优化：uc_apng_loader 遇到**静态** PNG 会抛异常
//   （image_t : d == BPP failed.，见移植计划.md §18.11），必须先分流，
//   不能把静态 PNG 喂给它。
inline bool qFmtPngIsApng(const QByteArray& b)
{
    if (!qFmtStartsWith(b, "\x89PNG\r\n\x1a\n", 8)) return false;
    int pos = 8;
    const int n = qFmtSize(b);
    while (pos + 8 <= n) {
        const uint32_t len = qFmtBe32(qFmtData(b) + pos);
        if (::memcmp(qFmtData(b) + pos + 4, "acTL", 4) == 0) return true;
        if (::memcmp(qFmtData(b) + pos + 4, "IEND", 4) == 0) return false;
        if (len > 0x7fffffffu) return false;
        pos += 12 + (int)len;   // len + type + data + crc
        if (pos <= 0) return false;
    }
    return false;
}

// 内容嗅探，**区分** APNG 与静态 PNG。复制管线用这个（移植计划.md §18.4）。
// 与 qimagereader_shim.cpp:85-112 的唯一差异：第 1 行返回 "apng" 而非 "png"。
// 认不出返回空 QByteArray。
inline QByteArray qSniffImageFormat(const QByteArray& b)
{
    if (qFmtPngIsApng(b))                             return qFmtLit("apng");
    if (qFmtStartsWith(b, "\x89PNG\r\n\x1a\n", 8))      return qFmtLit("png");
    if (qFmtStartsWith(b, "\xff\xd8\xff", 3))           return qFmtLit("jpeg");
    if (qFmtStartsWith(b, "GIF87a", 6))                 return qFmtLit("gif");
    if (qFmtStartsWith(b, "GIF89a", 6))                 return qFmtLit("gif");
    if (qFmtSize(b) >= 12 && ::memcmp(qFmtData(b), "RIFF", 4) == 0
                        && ::memcmp(qFmtData(b) + 8, "WEBP", 4) == 0)
        return qFmtLit("webp");
    if (qFmtStartsWith(b, "BM", 2))                     return qFmtLit("bmp");
    if (qFmtStartsWith(b, "II\x2a\x00", 4))             return qFmtLit("tiff");
    if (qFmtStartsWith(b, "MM\x00\x2a", 4))             return qFmtLit("tiff");
    if (qFmtStartsWith(b, "II\x2b\x00", 4))             return qFmtLit("tiff");
    if (qFmtStartsWith(b, "MM\x00\x2b", 4))             return qFmtLit("tiff");
    if (qFmtStartsWith(b, "\x00\x00\x01\x00", 4))       return qFmtLit("ico");
    if (qFmtStartsWith(b, "/* XPM */", 9))              return qFmtLit("xpm");
    if (qFmtStartsWith(b, "#define", 7))                return qFmtLit("xbm");
    if (qFmtStartsWith(b, "P1", 2))                     return qFmtLit("pgm");
    if (qFmtStartsWith(b, "P2", 2))                     return qFmtLit("pgm");
    if (qFmtStartsWith(b, "P3", 2))                     return qFmtLit("ppm");
    if (qFmtStartsWith(b, "P4", 2))                     return qFmtLit("pbm");
    if (qFmtStartsWith(b, "P5", 2))                     return qFmtLit("pgm");
    if (qFmtStartsWith(b, "P6", 2))                     return qFmtLit("ppm");
    if (qFmtStartsWith(b, "<?xml", 5) || qFmtStartsWith(b, "<svg", 4))
        return qFmtLit("svg");
    return QByteArray();
}

// 族级嗅探：APNG 归 "png"。需要「PNG 族」语义的调用方用这个，
// 以对齐 qimagereader_shim.cpp 内部 m_fmt 的既有语义（见文件头说明 2）。
inline QByteArray qSniffFormatFamily(const QByteArray& b)
{
    const QByteArray f = qSniffImageFormat(b);
    return (f == qFmtLit("apng")) ? qFmtLit("png") : f;
}

// 格式 → 剪贴板 MIME。返回 ASCII 字面量（静态存储期，调用方直接
// QString::fromUtf8() 包，不要缓存、不要 free）。认不出的返回 0。
//
// ⚠ "apng" 的 MIME 有争议：RFC 2483 只定义 image/png，image/apng 是社区约定，
//   部分接收端只认 image/png。故 APNG 在 §18.4 的字节直通里由调用方**两个都挂**
//   （image/apng 优先 + image/png 兜底），本函数只给首选值。
//
// 本头刻意不引入 QString（跨版本头最易在这里翻车），MIME 全是 ASCII，
// QString::fromUtf8 与 fromLatin1 等价，但仓库规范禁用 latin1 族，一律 fromUtf8。
inline const char* qMimeForFormat(const QByteArray& fmt)
{
    if (fmt == qFmtLit("png"))  return "image/png";
    if (fmt == qFmtLit("apng")) return "image/apng";
    if (fmt == qFmtLit("gif"))  return "image/gif";
    if (fmt == qFmtLit("webp")) return "image/webp";
    if (fmt == qFmtLit("jpeg")) return "image/jpeg";
    if (fmt == qFmtLit("bmp"))  return "image/bmp";
    if (fmt == qFmtLit("tiff")) return "image/tiff";
    if (fmt == qFmtLit("ico"))  return "image/x-icon";
    if (fmt == qFmtLit("svg"))  return "image/svg+xml";
    if (fmt == qFmtLit("xpm"))  return "image/x-xpixmap";
    if (fmt == qFmtLit("xbm"))  return "image/x-xbitmap";
    if (fmt == qFmtLit("pgm"))  return "image/x-portable-graymap";
    if (fmt == qFmtLit("ppm"))  return "image/x-portable-pixmap";
    if (fmt == qFmtLit("pbm"))  return "image/x-portable-bitmap";
    return 0;
}

#endif // QLSTIK_QFORMATSNIFF_SHIM_H