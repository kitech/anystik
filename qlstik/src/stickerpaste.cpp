#include "stickerpaste.h"

// ⚠ include 顺序有讲究：qclipboard_shim.h 在 Qt3 分支会 `#undef`/替换 QList 宏
//   （它引 qlist_shim.h 拿到值语义 QList<T>），必须排在 Qt 原生 QList 头之后。
//   本文件统一把 qlist_shim.h 放在最后一批 include（stickerclipboard.cpp:3-46 同款）。
#include "qformatsniff_shim.h"    // qSniffImageFormat / qSniffFormatFamily（header-only）
#include "qimagereader_shim.h"    // Qt3 的完整 QImageReader（Qt6 走原生 <QImageReader>）
#include "qclipboard_shim.h"      // Qt3 的 QMimeData / QGuiApplication 垫片（Qt6 整体不定义）
#include "qglobaltype_shim.h"     // qOpenReadOnly / qOpenWriteOnly / qBufferSetData
#include "qfile_shim.h"           // qIODeviceWrite（Qt3 的 QFile/QBuffer 没有 write()）
#include "qba_shim.h"             // qbaConstData / qbaLit
#include "qimage_shim.h"          // qImageConvertToFormat / qFmtArgb32（位图兜底编码）

// DB 层：与 anystik 同一个 StickerDbSyncInterface、同一个 message.db
#include "storage.h"              // Storage::instance().stickerDb() / dataDir()
#include "sticker_db.h"           // StickerRow / StickerPackRow / StickerDbSyncInterface

// 粘贴幂等 id 的 SHA-1：qlcomp/sha1.c（steve reid 公有领域；qlstik.pro 挂载）。
// ⚠ 全局符号 SHA1_Init/Update/Final 与 libcrypto 同名；静态 TU 强符号遮蔽动态库，
//   验证见移植计划.md §19.7（nm 抽查）。本 TU 不引 <openssl/sha.h>。
#include "sha1.h"

#ifdef QT3_BUILD
#include <qapplication.h>
#include <qfile.h>
#include <qbuffer.h>
#include <qimage.h>
#include <qdir.h>
#include <qurlinfo.h>
#include <qurl.h>
#include <qcstring.h>
#else
#include <QApplication>
#include <QFile>
#include <QBuffer>
#include <QImage>
#include <QDir>
#include <QImageReader>
#include <QMimeData>
// QGuiApplication：Qt5+ 原生；Qt4 无此类，由 qclipboard_shim.h 的垫片提供
// （其 <0x050000 段 include <qapplication.h> 并 define class QGuiApplication）。
// QUrl：Qt4 由 qclipboard_shim.h 引入，Qt5+ 原生头自行补（QList<QUrl> 值语义）。
#if QT_VERSION >= 0x050000
#include <QGuiApplication>
#include <QUrl>
#endif
#include <QClipboard>   // QGuiApplication::clipboard() 返回 QClipboard*，不引是 incomplete type
#endif

#include "qlist_shim.h"           // Qt3 值语义 QList<T>（必须在上面所有 Qt 头之后）

#include <string.h>

// ═══════════════════════════════════════════════════════════════════════
// 跨版本小工具
// ═══════════════════════════════════════════════════════════════════════

// QString → UTF-8 std::string。约束见移植计划.md §19 与 stickerops.cpp:8-11：
// 不引 qstring_shim.h（其 Qt3 分支无条件 include <qcstring.h>，会拖挂 Qt4 构建），
// 一律走 utf8()（AGENTS.md 编码纪律：禁止 latin1 族做文本转换）。
static std::string qPasteToStd(const QString& s)
{
#ifdef QT3_BUILD
    const QCString cs = s.utf8();
    if (s.isEmpty() || !cs.data()) return std::string();
    return std::string(cs.data(), (size_t)cs.length());
#else
    const QByteArray ba = s.toUtf8();
    return std::string(ba.constData(), (size_t)ba.size());
#endif
}

// SHA-1(任意字节) → 40 位小写 hex。qlcomp/sha1 输出字节序大端（FIPS 180-1）。
// 自写 hex 而非 QString::arg：避免 Qt3/4 arg(int,base,fill) 的跨版本差异。
static QString qPasteSha1Hex(const uint8_t* data, size_t len)
{
    SHA1_CTX ctx;
    uint8_t digest[SHA1_DIGEST_SIZE];
    SHA1_Init(&ctx);
    SHA1_Update(&ctx, data, len);
    SHA1_Final(&ctx, digest);
    static const char kHex[] = "0123456789abcdef";
    QString hex;
    hex.reserve(40);
    for (int i = 0; i < SHA1_DIGEST_SIZE; ++i) {
        hex += QChar(kHex[(digest[i] >> 4) & 0xf]);
        hex += QChar(kHex[digest[i] & 0xf]);
    }
    return hex;
}

// QImage → 指定格式字节。与 stickerclipboard.cpp:117 的 qClipEncodeOne 逐字相同：
// Qt3 的 QBuffer(QByteArray) 按值构造，写完必须用 buffer() 取回（拷贝副本）；
// Qt6 按引用构造，原变量即结果。
static QByteArray qPasteEncodeOne(const QImage& im, const char* format)
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
// 探测
// ═══════════════════════════════════════════════════════════════════════

// 统一探测：魔数嗅探（qSniffImageFormat，区分 apng）+ 运行时插件白名单
// （supportedImageFormats()；Qt3 是硬编码白名单，qimagereader_shim.cpp:572）+
// 首帧完整解码。等价 anystik 的 probeImageValidity（stickerstore.cpp:1034）。
// ⚠ apng 归族检查：运行时白名单只有 png（无 apng），故按族判，输出格式保留 "apng"。
static bool qPasteProbe(const QByteArray& bytes,
                        QByteArray* outFmt, QSize* outSize, int* outFrames)
{
    if (bytes.isEmpty()) return false;
    const QByteArray rawFmt = qSniffImageFormat(bytes);
    if (rawFmt.isEmpty()) return false;

    // ⚠ 参数必须是**原始字节流**：qSniffFormatFamily 内部会再调一次
    //   qSniffImageFormat（qformatsniff_shim.h:142-146「族级嗅探」=apng 归 png）。
    //   若误传已嗅出的格式名（如 "png" 文本），魔数全不匹配 → 返回空 → runtimeOk=0
    //   整链误判失败（断言探针 paste_assert 实测定位）。
    const QByteArray fam = qSniffFormatFamily(bytes);
    const auto fmts = QImageReader::supportedImageFormats();
    bool runtimeOk = false;
    for (int i = 0; i < (int)fmts.size(); ++i) {
        if (fmts[i] == fam) { runtimeOk = true; break; }
    }
    if (!runtimeOk) return false;

    QBuffer probe;
    qBufferSetData(probe, bytes);
    if (!qOpenReadOnly(probe)) return false;
    QImageReader reader(&probe);
#ifdef QT34_READER_NO_AUTOTRANSFORM
    /* Qt4：EXIF 方向不矫正（无此 API，见 qimagereader_shim.h） */
#else
    reader.setAutoTransform(true);
#endif
    if (!reader.canRead()) return false;
    const QSize size = reader.size();
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0) return false;
    if (outFrames) {
        const int n = reader.imageCount();
        *outFrames = n > 0 ? n : 0;
    }
    // 终验：完整解码首帧（read() 会推进游标，探测后不再复用 reader）。
    const QImage first = reader.read();
    if (first.isNull() || !first.size().isValid()) return false;

    if (outFmt) *outFmt = rawFmt;
    if (outSize) *outSize = size;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
// 读剪贴板：MIME 原始字节 → uri-list 本地文件 → 位图兜底
// ═══════════════════════════════════════════════════════════════════════

// 多帧动画优先、静态兜底置后。既定 MIME 顺序对齐 anystik stickerstore.cpp:2181-2186
// （去掉 macOS 专用分支与 QQ 病理，见移植计划.md §19.1 裁剪）。
// 同一剪贴板可挂多 representation，命中首个可解码者即取原始字节（保动画/格式）。
static QByteArray qPasteReadMime(const QMimeData* mime)
{
    static const char* const kFmtOrder[] = {
        "image/apng", "image/webp", "image/gif",
        "com.compuserve.gif", "public.gif", "image/x-gif",
        "image/png", "image/jpeg", "image/tiff", "image/bmp",
        "image/x-png"
    };
    for (size_t i = 0; i < sizeof(kFmtOrder) / sizeof(kFmtOrder[0]); ++i) {
        const QByteArray raw = mime ? mime->data(QString::fromUtf8(kFmtOrder[i]))
                                    : QByteArray();
        if (raw.isEmpty()) continue;
        QByteArray fmt;
        QSize size;
        if (qPasteProbe(raw, &fmt, &size, 0)) return raw;
    }
    return QByteArray();
}

// text/uri-list 回退：只取本地文件，直读字节 + 探测通过即取（保各格式/动画）。
static QByteArray qPasteReadUri(const QMimeData* mime)
{
    if (!mime) return QByteArray();
    const QList<QUrl> urls = mime->urls();
    for (int i = 0; i < (int)urls.size(); ++i) {
        const QUrl& url = urls.at((uint)i);
        if (!url.isLocalFile()) continue;          // 只取本地文件
        const QString p = qUrlToLocalFile(url);
        if (p.isEmpty()) continue;
        QFile f(p);
        if (!qOpenReadOnly(f)) continue;
        const QByteArray bytes = f.readAll();
        QByteArray fmt;
        QSize size;
        if (qPasteProbe(bytes, &fmt, &size, 0)) return bytes;
    }
    return QByteArray();
}

// 位图兜底：剪贴板只有位图时编回 PNG（保存时原生格式信息已丢）。
// 自检修复：RGB555 等格式编出的 PNG 可能无有效 IDAT（anystik stickerstore.cpp:2323
// 实测记录），先归一 ARGB32 再编并 probe 自验；失败落 BMP（BMP 编码不含 zlib，必成）。
static QByteArray qPasteReadBitmap()
{
    const QImage img = QGuiApplication::clipboard()->image();
    if (img.isNull()) return QByteArray();
    const QImage c = qImageConvertToFormat(img, qFmtArgb32);
    const QByteArray png = qPasteEncodeOne(c, "PNG");
    if (!png.isEmpty()) {
        QByteArray fmt;
        QSize size;
        if (qPasteProbe(png, &fmt, &size, 0)) return png;
    }
    return qPasteEncodeOne(c, "BMP");
}

// ═══════════════════════════════════════════════════════════════════════
// 入库
// ═══════════════════════════════════════════════════════════════════════

// 「粘贴板」包：title 查重 → 无则建（id 规则 = anystik stickerstore.cpp:1800-1802）。
// 已存在时返回既有包 id（不重复建包），★ 兼容契约 1 ★。
static QString qPasteFindOrCreatePack(StickerDbSyncInterface* db, QString* err)
{
    const QString kTitle = QString::fromUtf8("粘贴板");
    const std::vector<StickerPackRow> packs = db->list_packs(1);
    for (size_t i = 0; i < packs.size(); ++i) {
        if (QString::fromUtf8(packs[i].title.c_str()) == kTitle) {
            return QString::fromUtf8(packs[i].id.c_str());
        }
    }
    const std::string titleUtf8 = qPasteToStd(kTitle);
    const QString packId = QString::fromUtf8("pack_") + qPasteSha1Hex(
        (const uint8_t*)titleUtf8.data(), titleUtf8.size()).left(12);
    StickerPackRow pack;
    pack.id = qPasteToStd(packId);
    pack.title = qPasteToStd(kTitle);
    pack.author = "";
    pack.position = (int)packs.size();
    if (!db->add_pack(pack)) {
        if (err) *err = QString::fromUtf8("add pack failed");
        return QString();
    }
    return packId;
}

// QDir::mkpath 是 Qt4+ 才有（Qt3 只有单层 mkdir）。distinctly不 introduce
// compatcore34.h（其 qOpenReadOnly(QFile&) 为文本模式，include 后会把下面
// 读 uri 文件/写图片的路由拉偏，见 stickerpaste.h 文件头 §18.12 警告），
// 故自写递归。dataDir 在 storage 初始化时已建 multi级，这里做保底。
static bool qPasteMkdirs(const QString& path)
{
#ifdef QT3_BUILD
    QDir leaf(path);
    if (leaf.exists()) return true;
    const int slash = path.findRev('/');
    if (slash <= 0) return false;
    const QString name = path.mid(slash + 1);
    if (name.isEmpty()) return false;
    const QString parent = QDir::cleanDirPath(path.left(slash));
    if (!parent.isEmpty()) {
        if (!qPasteMkdirs(parent)) return false;
        QDir p(parent);
        if (!p.exists() || !p.mkdir(name)) return false;
    }
    return QDir(path).exists();
#else
    if (QDir().mkpath(path)) return true;   // Qt4+ 原生递归
    return QDir(path).exists();
#endif
}

// 原始图片字节落库：写 pastes/<id>.<ext> → 建/取「粘贴板」包 → add_sticker。
// dup/resurrect 三态照搬 anystik importImageBytes（stickerstore.cpp:2444-2507）：
//   - 文件已存在（同内容）：st==1 软删 → *resurrectId；st!=1 存活 → *dup；
//     不重写、不 REPLACE、不挪位、不隐式复活。
//   - 文件不存在：写文件 + 事务内 add_pack(+count)+add_sticker。
static bool qPasteStore(StickerDbSyncInterface* db,
                        const QByteArray& bytes,
                        const QByteArray& fmt,
                        const QSize& size,
                        QString* err, bool* dup, QString* resurrectId)
{
    // ★ 兼容契约 2 ★：幂等 id = sha1(原始字节)。
    const QString idHex = qPasteSha1Hex(
        (const uint8_t*)qbaConstData(bytes), (size_t)bytes.size());

    // 扩展名映射与 anystik :2427-2442 逐项对齐（apng 刻板 .png）。
    QString ext = QString::fromUtf8(".png");
    if (fmt == qbaLit("jpeg") || fmt == qbaLit("jpg"))  ext = QString::fromUtf8(".jpg");
    else if (fmt == qbaLit("gif"))  ext = QString::fromUtf8(".gif");
    else if (fmt == qbaLit("webp")) ext = QString::fromUtf8(".webp");
    else if (fmt == qbaLit("bmp"))  ext = QString::fromUtf8(".bmp");
    else if (fmt == qbaLit("tif") || fmt == qbaLit("tiff")) ext = QString::fromUtf8(".tif");
    else if (fmt == qbaLit("tga"))  ext = QString::fromUtf8(".tga");
    else if (fmt == qbaLit("xpm"))  ext = QString::fromUtf8(".xpm");
    else if (fmt == qbaLit("xbm"))  ext = QString::fromUtf8(".xbm");
    else if (fmt == qbaLit("ppm"))  ext = QString::fromUtf8(".ppm");
    else if (fmt == qbaLit("pbm"))  ext = QString::fromUtf8(".pbm");
    else if (fmt == qbaLit("pgm"))  ext = QString::fromUtf8(".pgm");
    else if (fmt == qbaLit("wbmp")) ext = QString::fromUtf8(".wbmp");
    else if (fmt == qbaLit("svg") || fmt == qbaLit("svgz")) ext = QString::fromUtf8(".svg");
    else if (fmt == qbaLit("avif")) ext = QString::fromUtf8(".avif");

    const QString base = QString::fromUtf8(Storage::instance().dataDir().c_str());
    const QString pasteDirPath = base + QString::fromUtf8("/pastes");
    if (!qPasteMkdirs(pasteDirPath)) {
        if (err) *err = QString::fromUtf8("无法创建 pastes 目录");
        return false;
    }
    const QString filePath = QDir(pasteDirPath).filePath(idHex + ext);

    if (QFile::exists(filePath)) {
        // 同内容已在（文件幂等命中）：按行态分流，直接视为成功。
        const int st = db->sticker_deleted_state(qPasteToStd(idHex).c_str());
        if (resurrectId && st == 1) *resurrectId = idHex;
        if (dup) *dup = (st != 1);
        return true;
    }

    {
        QFile file(filePath);
        if (!qOpenWriteOnly(file)) {
            if (err) *err = QString::fromUtf8("图片保存失败");
            return false;
        }
        const qint64 n = qIODeviceWrite(file, qbaConstData(bytes), (qint64)bytes.size());
        file.close();
        if (n != (qint64)bytes.size()) {
            if (err) *err = QString::fromUtf8("图片保存失败");
            return false;
        }
    }

    if (!db->begin_write_transaction()) {
        if (err) *err = QString::fromUtf8("贴纸入库失败");
        return false;
    }
    // ★ 兼容契约 1 ★：包 id 必须与 anystik 相同，见 qPasteFindOrCreatePack。
    const QString packId = qPasteFindOrCreatePack(db, err);
    if (packId.isEmpty()) {
        db->commit_transaction();
        return false;                       // err 已在 findOrCreate 里填
    }

    StickerRow row;
    row.id = qPasteToStd(idHex);
    row.pack_id = qPasteToStd(packId);
    row.file_path = qPasteToStd(QString::fromUtf8("pastes/") + idHex + ext);
    row.emoji = "";
    row.width = size.width();
    row.height = size.height();
    row.size = (int)bytes.size();
    row.last_used = 0;
    row.position = db->count_stickers(qPasteToStd(packId).c_str());

    const bool ok = db->add_sticker(row);
    db->commit_transaction();
    if (!ok) {
        if (err) *err = QString::fromUtf8("贴纸入库失败");
        return false;
    }
    if (dup) *dup = false;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
// 对外入口
// ═══════════════════════════════════════════════════════════════════════

bool StickerPaste::pasteFromClipboard(bool* dup, QString* resurrectId, QString* err)
{
    if (dup) *dup = false;
    if (resurrectId) *resurrectId = QString();
    if (err) *err = QString();

    StickerDbSyncInterface* db = Storage::instance().stickerDb();
    if (!db) {
        if (err) *err = QString::fromUtf8("storage init failed");
        return false;
    }

    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();

    // 顺序：多帧动画/原始格式优先（保 GIF/APNG 动画），位图兜底置后。
    QByteArray bytes = qPasteReadMime(mime);
    if (bytes.isEmpty()) bytes = qPasteReadUri(mime);
    if (bytes.isEmpty()) bytes = qPasteReadBitmap();

    QByteArray fmt;
    QSize size;
    int frames = 0;
    if (!qPasteProbe(bytes, &fmt, &size, &frames)) {
        if (err) *err = QString::fromUtf8("剪贴板中没有图片");
        return false;
    }
    return qPasteStore(db, bytes, fmt, size, err, dup, resurrectId);
}