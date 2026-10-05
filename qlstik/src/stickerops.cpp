#include "stickerops.h"
#include "storage.h"              // Storage::instance().stickerDb()
#include "sticker_db.h"           // StickerDbSyncInterface
#include "qimagereader_shim.h"    // Qt3 的完整 QImageReader + imageCount 垫片
#include "qmimedatabase_shim.h"   // Qt3/Qt4 的 QMimeDatabase 垫片（Qt5+ 走原生）
#include "qdatetime_shim.h"       // qDateTimeEpochSecs / qFormatDateTime
// ⚠ 刻意**不**引 qimage_shim.h / qfile_shim.h / qstring_shim.h：三者都在 Qt3 分支
//   里无条件 `#include <qcstring.h>`（qimage_shim.h:34、qfile_shim.h:33、
//   qstring_shim.h:24），而 QCString 在 Qt4 就已移除、本机 Qt 4.8.7 无
//   qcstring.h → 会把 Qt4 构建拖挂。需要的三个跨版本小工具就地自写（见下）。

#ifdef QT3_BUILD
#include <qapplication.h>
#include <qclipboard.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qcstring.h>
#else
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#if QT_VERSION >= 0x050000
// QMimeDatabase/QMimeType 随 Qt 5.0 引入，垫片只覆盖 QT_VERSION < 0x050000
// （qmimedatabase_shim.h:27），Qt5/6 必须自己引原生头。
#include <QMimeDatabase>
#endif
#endif

// ── 跨版本 const char* 载体 ──
// 与 stickerlistpage.cpp:239-255 的同名类逐字相同。
// ⚠ 故意重复而没有上提到 compatcore34.h：那个头是 inode 1600434 的硬链接，
//   与 /home/gzleo/aprog/doxhttpd/qlcomp/ 共享同一份内容，往里加东西会波及
//   doxhttpd 的构建。故此处各留一份；改动时两边同步。
#ifdef QT3_BUILD
class TextArg {
public:
    explicit TextArg(const QString& s) : m_s(s.utf8()) {}
    operator const char*() const { return m_s; }
private:
    QCString m_s;
};
#else
class TextArg {
public:
    explicit TextArg(const QString& s) : m_s(s.toUtf8()) {}
    operator const char*() const { return m_s.constData(); }
private:
    QByteArray m_s;
};
#endif

// ── 三个跨版本小工具（就地自写，理由见文件头关于 shim 的注记）──
// QImage 缩放：Qt3 只有 **const 且返回副本** 的 scale()/smoothScale()
// （qimage.h:158/163，ScaleFree=拉伸到恰好 w×h），Qt4+ 是非 const 的
// scaled()（默认 IgnoreAspectRatio，语义与 Qt3 ScaleFree 一致）。
static QImage qScaledTo(const QImage& im, int w, int h)
{
#ifdef QT3_BUILD
    return im.smoothScale(w, h, QImage::ScaleFree);
#else
    return im.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
#endif
}

// 文件扩展名（不含点）：Qt3 只有 extension(bool)，Qt4+ 才有 suffix()。
// 两者语义等价（a.tar.gz→gz、.bashrc→bashrc、a.→空）。
static QString qSuffixOf(const QFileInfo& fi)
{
#ifdef QT3_BUILD
    return fi.extension(false);
#else
    return fi.suffix();
#endif
}

// QString 大小写：Qt3 是 lower()/upper()，Qt4+ 改名 toLower()/toUpper()。
static QString qLowerOf(const QString& s)
{
#ifdef QT3_BUILD
    return s.lower();
#else
    return s.toLower();
#endif
}
static QString qUpperOf(const QString& s)
{
#ifdef QT3_BUILD
    return s.upper();
#else
    return s.toUpper();
#endif
}

bool StickerOps::copyToClipboard(const QString& filePath)
{
    // 复制**原图**而不是 152px 缩略图：瓦片缓存里只有缩略图，
    // 粘出去的图必须是原始分辨率，否则用户拿到的就是糊的。
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        return false;
    }
    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    const QImage full = reader.read();
    if (full.isNull()) {
        return false;
    }
    // qltox photoviewer.cpp:92-105 就是用 QClipboard::setImage() 递图
    QApplication::clipboard()->setImage(full);
    return true;
}

bool StickerOps::copyScaledToClipboard(const QString& filePath, double scale)
{
    if (filePath.isEmpty() || scale <= 0.0 || !QFile::exists(filePath)) {
        return false;
    }
    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) {
        return false;
    }
    // ⚠ 缩放走本地 qScaledTo（见上方注释），**不要直接调 scale()/scaled()**：
    //   Qt3 是 const 返回副本的 scale()，Qt4+ 是非 const 的 scaled()，签名不兼容。
    //   ⚠ 至少 1px：0.1 缩放后宽或高可能算出 0，QImage 拒绝 0 尺寸。
    //   ⚠ 用 ScaleFree/IgnoreAspectRatio 而非 KeepAspectRatio：w/h 由同一个
    //   scale 因子算出、比例已严格一致，两者结果相同，但 free 语义保证拿到的
    //   就是请求尺寸（KeepAspectRatio 允许返回更小的尺寸）。
    int w = int(img.width() * scale + 0.5);
    int h = int(img.height() * scale + 0.5);
    if (w < 1) { w = 1; }
    if (h < 1) { h = 1; }
    img = qScaledTo(img, w, h);
    if (img.isNull()) {
        return false;
    }
    QApplication::clipboard()->setImage(img);
    return true;
}

bool StickerOps::collectMeta(const QString& filePath, StickerMetaLite& out)
{
    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile()) {
        return false;
    }
    out.sizeBytes = (long long)fi.size();
    // Qt3 与 Qt4+ 格式化 API 不同：Qt3 走 qdatetime_shim 的 qFormatDateTime，
    // Qt4+ 分支是它的原生 inline（qdatetime_shim.h 的 #else 段），调用侧零分支。
    out.modified = qFormatDateTime(fi.lastModified(), "yyyy-MM-dd hh:mm:ss");

    // ⚠ QMimeDatabase 垫片按 QT_VERSION < 0x050000 门控（qmimedatabase_shim.h:27），
    //   Qt5+ 是原生类；两边 mimeTypeForFile(const QFileInfo&) 签名一致。
    QMimeType mt = QMimeDatabase().mimeTypeForFile(fi);
    if (mt.isValid() && mt.name().startsWith(QString("image/"))) {
        out.mime = mt.name();
        out.typeLabel = mt.comment() + QString(" (") + mt.name() + QString(")");
    } else {
        // 认不出扩展名：走本地 qSuffixOf（Qt3 extension(false) / Qt4+ suffix()）
        const QString ext = qLowerOf(qSuffixOf(fi));
        if (ext.isEmpty()) {
            out.mime = QString("image/unknown");
            out.typeLabel = QString::fromUtf8("未知 (unknown)");
        } else {
            out.mime = QString("image/") + ext;
            out.typeLabel = qUpperOf(ext) + QString(" (") + out.mime + QString(")");
        }
    }

    // 尺寸与帧数。qimagereader_shim.h 补齐了 imageCount / jumpToNextImage /
    // nextImageDelay（Qt3 原生没有），Qt5+ 是原生接口。
    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    if (reader.canRead()) {
        const QSize sz = reader.size();
        if (sz.isValid()) {
            out.width = sz.width();
            out.height = sz.height();
        }
        const int cnt = reader.imageCount();
        if (cnt > 0) {
            out.frames = cnt;
        }
        out.animated = out.frames > 1;
    }
    return true;
}

QString StickerOps::formatMeta(const StickerMetaLite& m)
{
    // 逐行照搬 anysk stickerstore.cpp 的 formatStickerMeta
    QString sz;
    if (m.sizeBytes < 1024) {
        sz = QString::number(m.sizeBytes) + QString::fromUtf8(" B");
    } else if (m.sizeBytes < 1024 * 1024) {
        sz = QString::number(double(m.sizeBytes) / 1024.0, 'f', 1)
             + QString::fromUtf8(" KB");
    } else {
        sz = QString::number(double(m.sizeBytes) / (1024.0 * 1024.0), 'f', 2)
             + QString::fromUtf8(" MB");
    }
    QStringList lines;
    lines << QString::fromUtf8("类型: ") + m.typeLabel;
    lines << QString::fromUtf8("大小: ") + sz;
    lines << QString::fromUtf8("尺寸: %1 × %2").arg(m.width).arg(m.height);
    lines << QString::fromUtf8("帧数: %1").arg(m.frames);
    lines << QString::fromUtf8("更新时间: ") + m.modified;
    return lines.join(QString("\n"));
}

bool StickerOps::setDescription(const QString& id, const QString& desc)
{
    if (id.isEmpty()) {
        return false;
    }
    StickerDbSyncInterface* db = Storage::instance().stickerDb();
    if (!db) {
        return false;
    }
    return db->update_sticker_description(TextArg(id), TextArg(desc));
}

bool StickerOps::touch(const QString& id)
{
    if (id.isEmpty()) {
        return false;
    }
    StickerDbSyncInterface* db = Storage::instance().stickerDb();
    if (!db) {
        return false;
    }
    // ⚠ qdatetime_shim 末尾的跨版本 inline：Qt3 的 QDateTime 只有 toTime_t()
    //   （正好是 Unix 纪元秒），Qt5.8 才有 currentSecsSinceEpoch()。
    return db->touch_sticker(TextArg(id), (long long)qDateTimeEpochSecs());
}

bool StickerOps::remove(const QString& id)
{
    if (id.isEmpty()) {
        return false;
    }
    StickerDbSyncInterface* db = Storage::instance().stickerDb();
    if (!db) {
        return false;
    }
    // 软删（sticker_db.cpp:174 是 UPDATE stickers SET deleted=1），不删文件 ——
    // 与 anysk stickerstore.cpp 的 deleteSticker 逐字一致。
    return db->delete_sticker(TextArg(id));
}