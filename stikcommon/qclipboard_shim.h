#ifndef QLSTIK_QCLIPBOARD_SHIM_H
#define QLSTIK_QCLIPBOARD_SHIM_H

// QMimeData / QGuiApplication / QClipboard 垫片（Qt5 之前）。
//
// 需求：stickerstore.cpp 桌面端剪贴板读写的 6 处调用（1969/2134/2607-2626/
// 2666/2722/2873）用到 Qt5 的：
//     QGuiApplication::clipboard()->mimeData()      → const QMimeData*
//     QGuiApplication::clipboard()->setMimeData(md) → 接管所有权
//     QGuiApplication::clipboard()->image()/setImage()
//     QMimeData::setData(QString,QByteArray)/data/formats/urls/setUrls/setImageData
// Qt3 的对应物是 QClipboard::data()/setData(QMimeSource*) + image()/setImage()：
//   QClipboard 是 QObject 且随 QApplication 生成，**不能加成员**；QMimeData 在
//   Qt3 根本不存在（QMimeSource 是抽象基类，qmime.h:54）。
//
// 故本头：
//   · 定义 Qt3 版 QMimeData（public 继承 QMimeSource），把「MIME→字节」表 +
//     url 列表 + 图像承载起来，实现 format()/encodedData() 供 QClipboard::setData。
//   · 定义 Qt3Clipboard 包装，把真实 QClipboard 包成 mimeData()/setMimeData()/
//     image()/setImage() 四件套。
//   · 定义 QGuiApplication 垫片，clipboard() 返回 Qt3Clipboard*，使调用点
//     `QGuiApplication::clipboard()->...` 零改动。
//
// 已实测的 Qt3 语义（/tmp/vtest/clip/*.cpp，Xvfb + Qt3.5 真机）：
//   · setData(QMimeSource*) **接管指针所有权**：替换时 delete 旧的、clear() 时
//     delete 当前（town3.cpp：setData(s2) 打印 ~S(1)，clear() 打印 ~S(2)）。故
//     本包装 setMimeData 只 new 后交给 Qt3，**自己不再 delete**，否则双删。
//   · setData 与 setImage **互斥**（后调用者清前者）。故动画路径把 PNG 位图回退
//     作为 "image/png" **格式**写进 QMimeSource，而不是另调 setImage。
//   · 当剪贴板是图像（setImage）时，Qt3 的 data() 源已自带 image/png（且
//     image/bmp、image/jpeg…）格式，encodedData("image/png") 返回真 PNG
//     （timg3.cpp：len=73，hdr=89 50）。故读适配器**统一按 format(i)/
//     encodedData** 拉取，无需特判图像。
//   · Qt3 PNG 编解码是内建（非插件）：input/outputFormats 均含 PNG，save/load 通过。
//   · QUrl：Qt3 QUrl::path() 返回**已解码**路径；isLocalFile() 对裸路径也成立
//     （turl3.cpp）。故 qUrlToLocalFile 用 path()。
//
// ⚠ Qt3 的 QClipboard 无 setText(Mode) 之外的富文本；本头只覆盖上述用到的面。
// ⚠ 未实现 MatchMode 式权重或跨进程所有细节；X11 平台上 Qt3 会自行处理靶子/选择。
//
// QGuiApplication 随 Qt5 引入；Qt4.2+ 起 QClipboard 已有 mimeData()/setMimeData()。
// 故 Qt4 分支只需 QGuiApplication 垫片（返回原生 QClipboard*），QMimeData 走原生。
// 本仓库 stickerstore 不在 Qt4 构建，但此处仍让 Qt4 可编。

#include <qglobal.h>

// ── QUrl → 本地文件路径（两版本可用；Qt3 无 toLocalFile）──
#if QT_VERSION < 0x050000
#include <qurl.h>
#include <qstring.h>
inline QString qUrlToLocalFile(const QUrl& u)
{
    return u.isLocalFile() ? u.path() : QString();
}
inline QUrl qUrlFromLocalFile(const QString& path)
{
    // Qt3 QUrl(QString) 对裸路径默认 protocol="file"（实测 turl3.cpp）。
    return QUrl(path);
}
#else
#include <QUrl>
inline QString qUrlToLocalFile(const QUrl& u) { return u.toLocalFile(); }
inline QUrl qUrlFromLocalFile(const QString& path) { return QUrl::fromLocalFile(path); }
#endif

#if QT_VERSION < 0x050000

#include <qapplication.h>
#include <qclipboard.h>
#include <qimage.h>
#include <qbuffer.h>
#include <qstringlist.h>

#if QT_VERSION < 0x040200

#include <qmime.h>
#include <qmap.h>
#include <qcstring.h>
#include <qvaluelist.h>
#include <string.h>
#include "qlist_shim.h"   // Qt3 值语义 QList<T>

// Qt3 的 QByteArray（QMemArray<char>）没有 operator+=(const char*)，按字节追加。
static inline void qBaAppendBytes(QByteArray& a, const char* p, int n)
{
    if (n <= 0) return;
    const int old = (int)a.size();
    a.resize(old + n);
    memcpy(a.data() + old, p, (size_t)n);
}

// ── Qt3 版 QMimeData：以 QMimeSource 形式供 QClipboard::setData ──
class QMimeData : public QMimeSource
{
public:
    QMimeData() : m_hasImage(false), m_keysDirty(true) {}

    void setData(const QString& mimeType, const QByteArray& bytes)
    {
        m_data.insert(mimeType, bytes);
        m_keysDirty = true;
    }
    QByteArray data(const QString& mimeType) const
    {
        QMap<QString, QByteArray>::ConstIterator it = m_data.find(mimeType);
        return it == m_data.end() ? QByteArray() : it.data();
    }
    bool hasFormat(const QString& mimeType) const { return m_data.contains(mimeType); }
    void removeFormat(const QString& mimeType) { m_data.remove(mimeType); m_keysDirty = true; }

    QStringList formats() const
    {
        QStringList out;
        for (QMap<QString, QByteArray>::ConstIterator it = m_data.begin();
             it != m_data.end(); ++it)
            out.append(it.key());
        return out;
    }

    void setUrls(const QList<QUrl>& urls)
    {
        m_urls = urls;
        // 同时也发布为 text/uri-list（标准之一行一个 URI；本地文件写成 file:// 绝对路径）。
        QByteArray list;
        for (int i = 0; i < urls.count(); ++i) {
            const QUrl& u = urls.at((uint)i);
            QString s = u.isLocalFile()
                    ? (QString("file://") + qUrlToLocalFile(u))
                    : u.toString();
            qBaAppendBytes(list, s.latin1(), s.length());
            qBaAppendBytes(list, "\r\n", 2);
        }
        setData(QString("text/uri-list"), list);
    }
    QList<QUrl> urls() const
    {
        if (!m_urls.isEmpty()) return m_urls;
        QList<QUrl> out;
        const QByteArray raw = data(QString("text/uri-list"));
        if (raw.isEmpty()) return out;
        const char* p = raw.data();
        const int n = (int)raw.size();
        int start = 0;
        for (int i = 0; i <= n; ++i) {
            const bool eol = (i == n) || p[i] == '\n' || p[i] == '\r';
            if (!eol) continue;
            if (i > start) {
                QString line = QString::fromLatin1(p + start, i - start);
                line = line.stripWhiteSpace();
                if (!line.isEmpty() && !line.startsWith(QString("#")))
                    out.append(QUrl(line));
            }
            // 跳过 CRLF 的第二半 / 连续空行
            if (i < n && p[i] == '\r' && i + 1 < n && p[i + 1] == '\n') ++i;
            start = i + 1;
        }
        return out;
    }

    void setImageData(const QImage& im)
    {
        if (im.isNull()) return;
        m_image = im;
        m_hasImage = true;
        // 把位图编成 PNG 字节作为 "image/png" 格式（Qt3 PNG 内建，实测可 save）。
        QByteArray png;
        QBuffer b(png);
        if (b.open(IO_WriteOnly)) {
            if (im.save(&b, "PNG")) { b.close(); setData(QString("image/png"), png); }
            else b.close();
        }
    }
    QImage imageData() const { return m_image; }
    bool hasImage() const { return m_hasImage; }
    QImage image() const { return m_image; }

    void clear() { m_data.clear(); m_urls.clear(); m_image = QImage(); m_hasImage = false; m_keysDirty = true; }

    // ── QMimeSource 接口 ──
    const char* format(int n = 0) const
    {
        rebuildKeys();
        if (n < 0 || n >= (int)m_keys.count()) return 0;
        return m_keys[(uint)n].data();
    }
    bool provides(const char* mimeType) const
    {
        return m_data.contains(QString::fromLatin1(mimeType));
    }
    QByteArray encodedData(const char* mimeType) const
    {
        return data(QString::fromLatin1(mimeType));
    }

private:
    void rebuildKeys() const
    {
        if (!m_keysDirty) return;
        m_keys.clear();
        for (QMap<QString, QByteArray>::ConstIterator it = m_data.begin();
             it != m_data.end(); ++it)
            m_keys.append(QCString(it.key().latin1()));
        m_keysDirty = false;
    }

    QMap<QString, QByteArray> m_data;
    QList<QUrl>               m_urls;
    QImage                    m_image;
    bool                      m_hasImage;
    mutable QValueList<QCString> m_keys;
    mutable bool                 m_keysDirty;
};

// ── Qt3 版 QClipboard 包装 ──
class Qt3Clipboard
{
public:
    explicit Qt3Clipboard(QClipboard* cb) : m_cb(cb) {}

    // 读：把真实剪贴板的 QMimeSource 逐格式读入缓存 QMimeData 并返回其指针。
    // 指针由本包装持有；调用点只在其作用域内使用，且不会在多次调用间长期保存。
    QMimeData* mimeData()
    {
        m_read.clear();
        QMimeSource* src = m_cb ? m_cb->data() : 0;
        if (src) {
            for (int i = 0; src->format(i); ++i) {
                const char* f = src->format(i);
                m_read.setData(QString::fromLatin1(f), src->encodedData(f));
            }
        }
        // 若源未直接给出 text/uri-list 但存在 URL 语义，交由调用方的 image() 兜底即可。
        return &m_read;
    }

    void setMimeData(QMimeData* md)
    {
        if (!m_cb || !md) return;
        // 所有权交给 Qt3 QClipboard::setData（实测其会在替换/clear 时 delete）。
        m_cb->setData(md);
    }

    QImage image() const { return m_cb ? m_cb->image() : QImage(); }
    void setImage(const QImage& im) { if (m_cb) m_cb->setImage(im); }

private:
    QClipboard* m_cb;
    QMimeData   m_read;
};

class QGuiApplication
{
public:
    static Qt3Clipboard* clipboard()
    {
        static Qt3Clipboard* s = 0;
        if (!s) {
            QClipboard* cb = QApplication::clipboard();
            if (cb) s = new Qt3Clipboard(cb);
        }
        return s;
    }
};

#else // Qt4.2 .. Qt4.x：QMimeData 原生，仅需 QGuiApplication 垫片

class QGuiApplication
{
public:
    static QClipboard* clipboard() { return QApplication::clipboard(); }
};

#endif // QT_VERSION < 0x040200
#endif // QT_VERSION < 0x050000
#endif // QLSTIK_QCLIPBOARD_SHIM_H