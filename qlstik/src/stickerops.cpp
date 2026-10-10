#include "stickerops.h"
#include "stickerclipboard.h"     // 复制管线真身（§18）：保动画、保原格式
#include "storage.h"              // Storage::instance().stickerDb()
#include "sticker_db.h"           // StickerDbSyncInterface
#include "pack_meta_store.h"      // 描述载体（_stikmeta.svg）读写
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
#include "qclipboard_shim.h"   // Qt3 的 QMimeData + QGuiApplication::clipboard()
#include <qfile.h>
#include <qfileinfo.h>
#include <qcstring.h>
#else
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
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
    // ⚠ 这里**不再**自己读图（§18.1）。原实现是 QImageReader::read() 读首帧 +
    // QApplication::clipboard()->setImage()，动图因此被拍成静图。
    // StickerClipboard::copyOriginal() 走原始字节直通，实测两端逐字节无损
    // （§18.10 第 2 项）。
    //
    // 「复制原图而不是 152px 缩略图」这条不变：瓦片缓存里只有缩略图，
    // 读的就是磁盘上的原始文件。
    return StickerClipboard::copyOriginal(filePath);
}

bool StickerOps::copyScaledToClipboard(const QString& filePath, double scale,
                                       bool* fellBackToPng)
{
    // 缩放逐帧、解码/重编码全在 StickerClipboard 里（§18.5）。本函数只负责
    // 把「是否回退 PNG」透给调用方，让它选 copied_scale / copied_scale_fallback
    // 文案（§18.8）—— 文案是调用方的责任，静态类不该管 UI。
    return StickerClipboard::copyScaled(filePath, scale, fellBackToPng);
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
#ifdef QT34_READER_NO_AUTOTRANSFORM
    /* Qt4：EXIF 方向不矫正（无此 API，见 qimagereader_shim.h） */
#else
    reader.setAutoTransform(true);
#endif
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

// 按系统文件管理器的复制机制发布「路径 + 元信息」：
//   text/uri-list（RFC 2483，CRLF 尾）全环境通用；
//   x-special/gnome-copied-files（"copy\nURI"、verb 后一个 LF、URI 后**无尾换行**）
//   供 Nautilus/Thunar/Nemo/MATE 识别复制动作（Nautilus 44+ 遇尾换行/CRLF 整包拒绝）；
//   text/plain（绝对路径 + 换行 + 5 行元信息）供当纯文本粘贴。
//   不放图片像素。URI 与 qclipboard_shim.h setUrls 同款（file:// + 原始 UTF-8 路径，
//   未做百分号编码——与仓库既有 text/uri-list 行为保持一致）。
bool StickerOps::copyPathAndMetaToClipboard(const QString& filePath)
{
    StickerMetaLite meta;
    if (!collectMeta(filePath, meta)) {
        return false;
    }
    const QFileInfo fi(filePath);
#ifdef QT3_BUILD
    const QString absPath = fi.absFilePath();   // Qt3 API 名
#else
    const QString absPath = fi.absoluteFilePath();
#endif
    const QString uri = QString("file://") + absPath;
    QByteArray uriList;      // text/uri-list：CRLF 尾缀（含最后一行）
    QByteArray copiedFiles;  // x-special/gnome-copied-files：无尾换行
    QByteArray plain;        // text/plain：路径 + 换行 + 5 行元信息
#ifdef QT3_BUILD
    const QCString uriU8 = uri.utf8();
    const QCString plainU8 = (absPath + QString("\n") + formatMeta(meta)).utf8();
    qBaAppendBytes(uriList, uriU8, (int)uriU8.length());
    qBaAppendBytes(uriList, "\r\n", 2);
    qBaAppendBytes(copiedFiles, "copy\n", 5);
    qBaAppendBytes(copiedFiles, uriU8, (int)uriU8.length());
    qBaAppendBytes(plain, plainU8, (int)plainU8.length());
#else
    uriList.append(uri.toUtf8());
    uriList.append("\r\n");
    copiedFiles.append("copy\n");
    copiedFiles.append(uri.toUtf8());
    plain.append((absPath + QString("\n") + formatMeta(meta)).toUtf8());
#endif
    QMimeData* md = new QMimeData;
    md->setData(QString("text/uri-list"), uriList);
    md->setData(QString("x-special/gnome-copied-files"), copiedFiles);
    md->setData(QString("text/plain"), plain);
#ifdef QT3_BUILD
    QGuiApplication::clipboard()->setMimeData(md);   // Qt3 接管所有权（已实测）
#else
    QApplication::clipboard()->setMimeData(md);      // Qt4.2+/5/6 接管所有权
#endif
    return true;
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
    const bool ok = db->update_sticker_description(TextArg(id), TextArg(desc));
    if (ok) {
        // 同步落一份云端描述载体（同包同行）；失败不影响本地库写入
        const std::unique_ptr<StickerRow> row = db->get_sticker(TextArg(id));
        if (row) {
            packmeta::setDescQ(*db, Storage::instance().dataDir(),
                               row->pack_id,
                               packmeta::basenameOf(row->file_path),
                               desc, packmeta::nowMsec());
        }
    }
    return ok;
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
    // 删前取行：定位该包描述载体并移除对应行。
    const std::unique_ptr<StickerRow> row = db->get_sticker(TextArg(id));
    const bool ok = db->delete_sticker(TextArg(id));
    if (ok && row) {
        packmeta::removeDesc(*db, Storage::instance().dataDir(),
                             row->pack_id,
                             packmeta::basenameOf(row->file_path));
    }
    return ok;
}