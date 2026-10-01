#ifndef QLSTIK_QMIMEDATABASE_SHIM_H
#define QLSTIK_QMIMEDATABASE_SHIM_H

// QMimeDatabase / QMimeType 垫片（Qt5 之前无此二类）。
//
// 需求：stickerstore.cpp 只在「probeImageValidity 失败后的宽松回退」（1860）
// 与「probe 成功后精化 mime」（1913）两处用到，接口仅：
//     QMimeDatabase::mimeTypeForFile(const QFileInfo&)
//     QMimeType::isValid() / name() / comment()
// comment() 只在 name() 以 "image/" 开头时才被读取（1862-1864 的 if 内），
// 故非图片返回无效 QMimeType 与 Qt6 返回 application/octet-stream 对该调用点等价。
//
// 行为对齐（Qt6.7.3 + 系统 shared-mime-info 实测，见下方注释）：
//   · 优先级：**已知扩展名 > 文件内容幻数 > 无效**。
//     （实测 `misnamed.gif` 内为 PNG 字节 → 仍 image/gif；`real.apng` 内为
//      普通 PNG → 仍 image/apng；`real.zzz` 内为 PNG → image/png 走内容。）
//   · comment() 串取自 Qt6 对同扩展名的输出，逐字复刻。
//
// ⚠ 局限：不实现 Qt 的 `MatchMode` 细分与完整 shared-mime-info 权重/别名库；
//   只覆盖 anystik 涉及的图片格式。非图片、非图片内容一律无效——对该调用点
//   与 Qt6 的非 image/* 结果等价（只在 else 分支处理）。
//
// QMimeDatabase/QMimeType 随 Qt 5.0 引入，故本垫片由 QT_VERSION < 0x050000 门控。

#include <qglobal.h>

#if QT_VERSION < 0x050000

#include <qstring.h>
#include <qstringlist.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <string.h>

class QMimeType
{
public:
    QMimeType() : m_valid(false) {}
    QMimeType(const QString& name, const QString& comment)
        : m_name(name), m_comment(comment), m_valid(!name.isEmpty()) {}

    bool isValid() const { return m_valid; }
    QString name() const { return m_name; }
    QString comment() const { return m_comment; }
    QString genericIconName() const { return QString(); }
    QString iconName() const { return QString(); }
    QStringList aliases() const { return QStringList(); }

private:
    QString m_name;
    QString m_comment;
    bool    m_valid;
};

// 扩展名 → (mime, comment)。comment 为 Qt6（shared-mime-info）实测值。
static inline QMimeType qMimeTypeFromExtension(const QString& ext)
{
    if (ext == QString("png"))  return QMimeType(QString("image/png"), QString("PNG image"));
    if (ext == QString("apng")) return QMimeType(QString("image/apng"), QString("Animated PNG image"));
    if (ext == QString("jpg")  || ext == QString("jpeg") ||
        ext == QString("jpe")  || ext == QString("jfif"))
        return QMimeType(QString("image/jpeg"), QString("JPEG image"));
    if (ext == QString("gif"))  return QMimeType(QString("image/gif"), QString("GIF image"));
    if (ext == QString("webp")) return QMimeType(QString("image/webp"), QString("WebP image"));
    if (ext == QString("tiff") || ext == QString("tif"))
        return QMimeType(QString("image/tiff"), QString("TIFF image"));
    if (ext == QString("bmp"))  return QMimeType(QString("image/bmp"), QString("Windows BMP image"));
    if (ext == QString("ico"))  return QMimeType(QString("image/vnd.microsoft.icon"), QString("Windows icon"));
    if (ext == QString("svg"))  return QMimeType(QString("image/svg+xml"), QString("SVG image"));
    if (ext == QString("svgz")) return QMimeType(QString("image/svg+xml-compressed"), QString("Compressed SVG image"));
    if (ext == QString("avif")) return QMimeType(QString("image/avif"), QString("AVIF image"));
    if (ext == QString("heic") || ext == QString("heif"))
        return QMimeType(QString("image/heif"), QString("HEIF image"));
    if (ext == QString("ppm"))  return QMimeType(QString("image/x-portable-pixmap"), QString("PPM image"));
    if (ext == QString("pgm"))  return QMimeType(QString("image/x-portable-graymap"), QString("PGM image"));
    if (ext == QString("pbm"))  return QMimeType(QString("image/x-portable-bitmap"), QString("PBM image"));
    if (ext == QString("xbm"))  return QMimeType(QString("image/x-xbitmap"), QString("XBM image"));
    if (ext == QString("xpm"))  return QMimeType(QString("image/x-xpixmap"), QString("XPM image"));
    if (ext == QString("psd"))  return QMimeType(QString("image/vnd.adobe.photoshop"), QString("Photoshop image"));
    if (ext == QString("tga"))  return QMimeType(QString("image/x-tga"), QString("TGA image"));
    if (ext == QString("dds"))  return QMimeType(QString("image/vnd.ms-dds"), QString("DirectDraw Surface"));
    if (ext == QString("exr"))  return QMimeType(QString("image/x-exr"), QString("EXR image"));
    if (ext == QString("hdr"))  return QMimeType(QString("image/vnd.radiance"), QString("Radiance HDR image"));
    if (ext == QString("qoi"))  return QMimeType(QString("image/qoi"), QString("Quite OK Image Format"));
    return QMimeType();
}

// 内容幻数 → (mime, comment)。只覆盖图片；识别不出返回 false。
static inline bool qMimeSniffImage(const char* p, int n, QMimeType& out)
{
    if (n >= 8 && (unsigned char)p[0] == 0x89 && p[1] == 'P' && p[2] == 'N' &&
        p[3] == 'G' && p[4] == '\r' && p[5] == '\n' && p[6] == 0x1a && p[7] == '\n')
    { out = QMimeType(QString("image/png"), QString("PNG image")); return true; }
    if (n >= 3 && (unsigned char)p[0] == 0xFF && (unsigned char)p[1] == 0xD8 &&
        (unsigned char)p[2] == 0xFF)
    { out = QMimeType(QString("image/jpeg"), QString("JPEG image")); return true; }
    if (n >= 6 && (memcmp(p, "GIF87a", 6) == 0 || memcmp(p, "GIF89a", 6) == 0))
    { out = QMimeType(QString("image/gif"), QString("GIF image")); return true; }
    if (n >= 12 && memcmp(p, "RIFF", 4) == 0 && memcmp(p + 8, "WEBP", 4) == 0)
    { out = QMimeType(QString("image/webp"), QString("WebP image")); return true; }
    if (n >= 2 && p[0] == 'B' && p[1] == 'M')
    { out = QMimeType(QString("image/bmp"), QString("Windows BMP image")); return true; }
    if (n >= 4 && (memcmp(p, "II\x2a\x00", 4) == 0 || memcmp(p, "MM\x00\x2a", 4) == 0))
    { out = QMimeType(QString("image/tiff"), QString("TIFF image")); return true; }
    if (n >= 4 && p[0] == 0 && p[1] == 0 && p[2] == 1 && p[3] == 0)
    { out = QMimeType(QString("image/vnd.microsoft.icon"), QString("Windows icon")); return true; }
    if (n >= 12 && memcmp(p + 4, "ftyp", 4) == 0) {
        const char* brand = p + 8;
        if (memcmp(brand, "avif", 4) == 0 || memcmp(brand, "avis", 4) == 0)
        { out = QMimeType(QString("image/avif"), QString("AVIF image")); return true; }
        if (memcmp(brand, "heic", 4) == 0 || memcmp(brand, "heix", 4) == 0 ||
            memcmp(brand, "heif", 4) == 0 || memcmp(brand, "mif1", 4) == 0 ||
            memcmp(brand, "msf1", 4) == 0)
        { out = QMimeType(QString("image/heif"), QString("HEIF image")); return true; }
    }
    if (n >= 4 && memcmp(p, "qoif", 4) == 0)
    { out = QMimeType(QString("image/qoi"), QString("Quite OK Image Format")); return true; }
    if (n >= 4 && p[0] == 0x76 && p[1] == 0x2f && p[2] == 0x31 && p[3] == 0x01)
    { out = QMimeType(QString("image/x-exr"), QString("EXR image")); return true; }
    if (n >= 4 && memcmp(p, "8BPS", 4) == 0)
    { out = QMimeType(QString("image/vnd.adobe.photoshop"), QString("Photoshop image")); return true; }
    if (n >= 5 && memcmp(p, "<?xml", 5) == 0)
    { out = QMimeType(QString("image/svg+xml"), QString("SVG image")); return true; }
    if (n >= 4 && memcmp(p, "<svg", 4) == 0)
    { out = QMimeType(QString("image/svg+xml"), QString("SVG image")); return true; }
    return false;
}

class QMimeDatabase
{
public:
    enum MatchMode {
        MatchDefault   = 0x00,
        MatchExtension = 0x01,
        MatchContent   = 0x02
    };

    QMimeType mimeTypeForFile(const QFileInfo& fi, MatchMode mode = MatchDefault) const
    {
        // Qt3 QFileInfo 无 suffix()，其 extension(false) 的语义与 Qt6 suffix() 等价
        // （实测：a.tar.gz→gz，.bashrc→bashrc，a.→空）。
        return match(fi.filePath(), fi.extension(false).lower(), mode);
    }
    QMimeType mimeTypeForFile(const QString& fileName, MatchMode mode = MatchDefault) const
    {
        QFileInfo fi(fileName);
        return match(fi.filePath(), fi.extension(false).lower(), mode);
    }
    // mimeTypeForName 未在 stickerstore 用到；给最小实现（按 name 直构，不查别名）。
    QMimeType mimeTypeForName(const QString& nameOrAlias) const
    {
        QMimeType t = qMimeTypeFromExtension(nameOrAlias.lower());
        if (t.isValid()) return t;
        // 允许 "image/png" 这类直接名字
        if (nameOrAlias.contains(QString("/")))
            return QMimeType(nameOrAlias, QString());
        return QMimeType();
    }

private:
    QMimeType match(const QString& path, const QString& extLower, MatchMode mode) const
    {
        // MatchExtension：只按扩展名。
        if (mode == MatchExtension)
            return qMimeTypeFromExtension(extLower);

        char head[64];
        const int n = readHead(path, head);

        // MatchContent：只按内容幻数。
        if (mode == MatchContent) {
            QMimeType t;
            if (n > 0 && qMimeSniffImage(head, n, t))
                return t;
            return QMimeType();
        }

        // MatchDefault：已知扩展名优先，其次内容（与 Qt6 实测一致）。
        QMimeType byExt = qMimeTypeFromExtension(extLower);
        if (byExt.isValid()) return byExt;
        QMimeType byContent;
        if (n > 0 && qMimeSniffImage(head, n, byContent))
            return byContent;
        return QMimeType();
    }

    int readHead(const QString& path, char* buf) const
    {
        QFile f(path);
        if (!f.open(IO_ReadOnly))
            return 0;
        const int n = f.readBlock(buf, 64);
        f.close();
        return n > 0 ? n : 0;
    }
};

#endif // QT_VERSION < 0x050000
#endif // QLSTIK_QMIMEDATABASE_SHIM_H