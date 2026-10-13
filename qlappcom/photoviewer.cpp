#include "photoviewer.h"
#include "compat34.h"
#include <qlayout.h>
#include <qpushbutton.h>
#include <qlabel.h>
#include <qpainter.h>
#include <qcursor.h>
#include <qapplication.h>
#include <qfile.h>
#include <qmessagebox.h>
#include <algorithm>
#include <math.h>
#include <string.h>
#include <time.h>
#ifdef QT3_BUILD
#include <qclipboard.h>
#include <qmime.h>
#include <qimage.h>
#include <qbuffer.h>
#include <qfiledialog.h>
#include <qdesktopwidget.h>
#else
#include <QClipboard>
#include <QMimeData>
#include <QImage>
#include <QBuffer>
#include <QFileDialog>
#endif
using std::min;
using std::max;

// 放大上限：50 倍（m_scale == 50.0 即 5000%）
static const double kMaxZoomScale = 50.0;

static QString humanBytes(int n) {
    if (n < 1024) {
        return QString::number(n) + " B";
    }
    double kb = n / 1024.0;
    if (kb < 1024.0) {
        return QString("%1 KB").arg(kb, 0, 'f', 1);
    }
    double mb = kb / 1024.0;
    return QString("%1 MB").arg(mb, 0, 'f', 2);
}

// 魔数探测图片 mime；无法识别返回空串
static QString sniffImageMime(const QByteArray& data) {
    if (data.size() >= 12) {
        const uchar* p = (const uchar*)data.data();   // Qt3 QMemArray 无 constData()
        if (p[0] == 0xFF && p[1] == 0xD8) { return "image/jpeg"; }
        if (p[0] == 0x89 && p[1] == 'P' && p[2] == 'N' && p[3] == 'G') { return "image/png"; }
        if (memcmp(p, "RIFF", 4) == 0 && memcmp(p + 8, "WEBP", 4) == 0) { return "image/webp"; }
    }
    return QString();
}

#ifdef QT3_BUILD
// 剪贴板数据源（修复 CPU 100% 的核心）：
// PNG/JPEG 原图时只暴露原始格式一个目标，粘贴体积与原文一致；
// 其余格式懒编码 PNG 一次并缓存。无 Q_OBJECT，不涉 moc。
class PhotoClipSource : public QMimeSource {
public:
    PhotoClipSource(const QByteArray& origData, const QString& origMime,
                    const QImage& img)
        : m_orig(origData), m_mime(origMime), m_img(img),
          m_havePng(false) {}

    virtual const char* format(int n) const {
        if (n != 0) { return 0; }
        if (m_mime == "image/png")  { return "image/png"; }
        if (m_mime == "image/jpeg") { return "image/jpeg"; }
        return "image/png";
    }

    virtual QByteArray encodedData(const char* fmt) const {
        if (!fmt) { return QByteArray(); }
        // 原始字节直通（300KB 原图粘贴后仍是 300KB）
        if ((m_mime == "image/png" || m_mime == "image/jpeg")
            && qstrcmp(fmt, m_mime.latin1()) == 0) {
            return m_orig;
        }
        // 其余格式（webp/未知）兜底一次编码 PNG
        if (m_mime != "image/png" && m_mime != "image/jpeg"
            && qstrcmp(fmt, "image/png") == 0) {
            if (!m_havePng) { encodeTo(m_png, "PNG"); m_havePng = true; }
            return m_png;
        }
        return QByteArray();
    }

private:
    // Qt3 QBuffer 按值持有 QByteArray，必须写完再取回 buffer()
    void encodeTo(QByteArray& out, const char* f) const {
        QBuffer buf;
        buf.open(IO_WriteOnly);
        QImageIO io(&buf, f);
        io.setImage(m_img);
        io.write();
        buf.close();
        out = buf.buffer();
    }

    QByteArray m_orig;
    QString m_mime;
    QImage m_img;
    mutable QByteArray m_png;
    mutable bool m_havePng;
};
#endif

// ============================================================
// PhotoCanvas
// ============================================================

PhotoCanvas::PhotoCanvas(QWidget* parent, const QPixmap& pixmap)
    : QWidget(parent
#ifdef QT3_BUILD
      , nullptr, WNoAutoErase
#endif
      )
    , m_pixmap(pixmap)
    , m_scale(1.0)
    , m_rotation(0.0)
    , m_offX(0.0)
    , m_offY(0.0)
    , m_fitMode(true)
    , m_showHelp(false)
    , m_dragging(false)
{
#ifndef QT3_BUILD
    setAttribute(Qt::WA_OpaquePaintEvent, true);
#endif
    fitToWindow();
    rebuildCache();
}

void PhotoCanvas::fitToWindow() {
    m_fitMode = true;
    if (m_pixmap.isNull()) { return; }
    double w = (m_rotation == 90.0 || m_rotation == 270.0)
               ? (double)m_pixmap.height() : (double)m_pixmap.width();
    double h = (m_rotation == 90.0 || m_rotation == 270.0)
               ? (double)m_pixmap.width() : (double)m_pixmap.height();
    if (w <= 0.0 || h <= 0.0) { return; }
    double sx = (double)width() / w;
    double sy = (double)height() / h;
    m_scale = min(sx, sy);
    centerImage();
    rebuildCache();
    update();
    emit viewChanged();
}

void PhotoCanvas::actualSize() {
    m_fitMode = false;
    m_scale = 1.0;
    centerImage();
    rebuildCache();
    update();
    emit viewChanged();
}

void PhotoCanvas::toggleFitMode() {
    if (m_fitMode) {
        actualSize();
    } else {
        fitToWindow();
    }
}

void PhotoCanvas::zoomIn() {
    m_fitMode = false;
    m_scale = m_scale * 1.25;
    if (m_scale < 0.05) { m_scale = 0.05; }
    if (m_scale > kMaxZoomScale) { m_scale = kMaxZoomScale; }
    rebuildCache();
    update();
    emit viewChanged();
}

void PhotoCanvas::zoomOut() {
    m_fitMode = false;
    m_scale = m_scale / 1.25;
    if (m_scale < 0.05) { m_scale = 0.05; }
    rebuildCache();
    update();
    emit viewChanged();
}

void PhotoCanvas::rotateCW() {
    m_rotation += 90.0;
    if (m_rotation >= 360.0) { m_rotation -= 360.0; }
    if (m_fitMode) {
        fitToWindow();
    } else {
        centerImage();
        rebuildCache();
        update();
        emit viewChanged();
    }
}

void PhotoCanvas::rotateCCW() {
    m_rotation -= 90.0;
    if (m_rotation < 0.0) { m_rotation += 360.0; }
    if (m_fitMode) {
        fitToWindow();
    } else {
        centerImage();
        rebuildCache();
        update();
        emit viewChanged();
    }
}

void PhotoCanvas::setShowHelp(bool show) {
    m_showHelp = show;
}

void PhotoCanvas::panBy(int dx, int dy) {
    m_offX += dx;
    m_offY += dy;
    update();
}

int PhotoCanvas::zoomPercent() const {
    return qRound(m_scale * 100.0);
}

QSize PhotoCanvas::displayedSize() const {
    double w = (m_rotation == 90.0 || m_rotation == 270.0)
               ? (double)m_pixmap.height() : (double)m_pixmap.width();
    double h = (m_rotation == 90.0 || m_rotation == 270.0)
               ? (double)m_pixmap.width() : (double)m_pixmap.height();
    return QSize(qRound(w * m_scale), qRound(h * m_scale));
}

void PhotoCanvas::centerImage() {
    double iw = (m_rotation == 90.0 || m_rotation == 270.0)
                ? (double)m_pixmap.height() : (double)m_pixmap.width();
    double ih = (m_rotation == 90.0 || m_rotation == 270.0)
                ? (double)m_pixmap.width() : (double)m_pixmap.height();
    m_offX = ((double)width() - iw * m_scale) / 2.0;
    m_offY = ((double)height() - ih * m_scale) / 2.0;
}

void PhotoCanvas::updateCursor() {
    if (m_dragging) {
        setCursor(QCursor(Qt::SizeAllCursor));
    } else {
        setCursor(QCursor(Qt::ArrowCursor));
    }
}

void PhotoCanvas::rebuildCache() {
    if (m_pixmap.isNull()) { m_cachedPixmap = QPixmap(); return; }
    double dw = (m_rotation == 90.0 || m_rotation == 270.0)
                ? (double)m_pixmap.height() : (double)m_pixmap.width();
    double dh = (m_rotation == 90.0 || m_rotation == 270.0)
                ? (double)m_pixmap.width() : (double)m_pixmap.height();
    m_cachedPixmap = QPixmap(qRound(dw * m_scale), qRound(dh * m_scale));
    QPainter cp(&m_cachedPixmap);
    cp.translate(dw * m_scale / 2.0, dh * m_scale / 2.0);
    cp.rotate(m_rotation);
    cp.translate(-m_pixmap.width() * m_scale / 2.0, -m_pixmap.height() * m_scale / 2.0);
    cp.scale(m_scale, m_scale);
    cp.drawPixmap(0, 0, m_pixmap);
}

void PhotoCanvas::paintEvent(QPaintEvent*) {
    {
        QPainter bp(&m_doubleBuffer);
#ifdef QT3_BUILD
        bp.fillRect(rect(), paletteBackgroundColor());
#else
        bp.fillRect(rect(), palette().window().color());
#endif
        if (!m_cachedPixmap.isNull()) {
            bp.drawPixmap(qRound(m_offX), qRound(m_offY), m_cachedPixmap);
        }
        if (m_showHelp) {
#ifdef QT3_BUILD
            bp.fillRect(rect(), QColor(0, 0, 0));
#else
            bp.fillRect(rect(), QColor(0, 0, 0, 180));
#endif
            bp.setPen(Qt::white);
            QFont f = font();
            f.setPointSize(13);
            bp.setFont(f);
            QFontMetrics fm(f);

            static const char* helpLines[] = {
                "快捷键帮助",
                "",
                "q / Esc    关闭",
                "f / F11    全屏切换",
                "s          保存",
                "c / Ctrl+C 复制到剪贴板",
                ">          顺时针旋转 90度",
                "<          逆时针旋转 90度",
                "上 / +     放大",
                "下 / -     缩小",
                "*          实际大小 100%",
                "/          适应窗口",
                "Z          切换自动适应",
                "h          显示/隐藏帮助",
            };
            int numLines = sizeof(helpLines) / sizeof(helpLines[0]);
            int lineH = fm.lineSpacing() + 4;
            int textH = numLines * lineH;
            int y0 = (height() - textH) / 2 + fm.ascent();
            for (int i = 0; i < numLines; i++) {
                QString s = qFromUtf8(helpLines[i]);
                int tw = qFontWidth(fm, s);
                bp.drawText((width() - tw) / 2, y0 + i * lineH, s);
            }
        }
    }
    QPainter p(this);
    p.drawPixmap(0, 0, m_doubleBuffer);
}

void PhotoCanvas::wheelEvent(QWheelEvent* event) {
    if (m_pixmap.isNull()) { return; }
    int delta = qWheelDeltaY(event);
    if (delta == 0) { return; }

    // 缩放幅度与滚动角度成比例（120 = 一个标准齿格），Qt 官方推荐的
    // "partial response" 模式：标准滚轮一格 ±120 → ×1.25/÷0.8 手感不变；
    // 触控板/高分辨率滚轮的小步长事件流细粒度缩放，
    // 修复 Qt4 平滑滚动下"一事件一整格"导致的过快
    double steps = delta / 120.0;
    double factor = pow(1.25, steps);
    double oldScale = m_scale;
    double newScale = max(0.05, m_scale * factor);
    if (newScale > kMaxZoomScale) { newScale = kMaxZoomScale; }

    QPoint pos = qWheelPos(event);
    double cx = (double)pos.x() - m_offX;
    double cy = (double)pos.y() - m_offY;
    m_offX = (double)pos.x() - cx * (newScale / oldScale);
    m_offY = (double)pos.y() - cy * (newScale / oldScale);

    m_scale = newScale;
    m_fitMode = false;
    rebuildCache();
    update();
    emit viewChanged();
}

void PhotoCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragStart = event->pos();
        m_dragOffX = m_offX;
        m_dragOffY = m_offY;
        updateCursor();
    }
}

void PhotoCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragging) {
        m_offX = m_dragOffX + (double)(event->x() - m_dragStart.x());
        m_offY = m_dragOffY + (double)(event->y() - m_dragStart.y());
        update();
    }
}

void PhotoCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        updateCursor();
    }
}

void PhotoCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        toggleFitMode();
    }
}

void PhotoCanvas::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    m_doubleBuffer = QPixmap(size());
    if (m_fitMode) {
        fitToWindow();
    } else {
        rebuildCache();
        update();
    }
}

// ============================================================
// PhotoViewer
// ============================================================

PhotoViewer::PhotoViewer(QWidget* parent, const QPixmap& pixmap,
                         const QByteArray& origData,
                         const QString& suggestUrl)
    : QDialog(parent
#ifdef QT3_BUILD
      , nullptr, false, WDestructiveClose
#endif
      )
    , m_canvas(0)
    , m_toolbar(0)
    , m_statusBar(0)
    , m_statusLabel(0)
    , m_fullscreen(false)
    , m_savedX(0)
    , m_savedY(0)
    , m_savedW(800)
    , m_savedH(600)
    , m_origPixmap(pixmap)
    , m_origData(origData)
    , m_origMime(sniffImageMime(origData))
    , m_suggestUrl(suggestUrl)
{
#ifndef QT3_BUILD
    setAttribute(Qt::WA_DeleteOnClose);
#endif
    m_canvas = new PhotoCanvas(this, pixmap);
    connect(m_canvas, SIGNAL(viewChanged()), this, SLOT(onViewChanged()));

    QVBoxLayout* lay = new QVBoxLayout(this);
    qSetLayoutMargin(lay, 0);
    lay->setSpacing(0);

    lay->addWidget(m_canvas, 1);

    setupToolbar(lay);

    m_statusBar = new QWidget(this);
    QHBoxLayout* sbar = new QHBoxLayout(m_statusBar);
    qSetLayoutMargin(sbar, 0);
    sbar->setSpacing(0);
    m_statusBar->setFixedHeight(24);
    m_statusLabel = new QLabel(m_statusBar);
    sbar->addWidget(m_statusLabel);
    sbar->addStretch();
    lay->addWidget(m_statusBar);

    resize(800, 600);
    updateTitle();
    updateStatus();
    m_canvas->setFocus();
}

void PhotoViewer::setupToolbar(QVBoxLayout* lay) {
    m_toolbar = new QWidget(this);
    QHBoxLayout* h = new QHBoxLayout(m_toolbar);
    qSetLayoutMargin(h, 0);
    h->setSpacing(2);

    struct BtnDef {
        const char* text;
        const char* slot;
    };
    BtnDef btns[] = {
        { "放大 ↑", SLOT(onZoomIn()) },
        { "缩小 ↓", SLOT(onZoomOut()) },
        { "适应 /", SLOT(onFitWindow()) },
        { "实际 *", SLOT(onActualSize()) },
        { "↻ >",   SLOT(onRotateCW()) },
        { "↺ <",   SLOT(onRotateCCW()) },
        { "复制 c", SLOT(onCopy()) },
        { "保存 s", SLOT(onSave()) },
        { "全屏 f", SLOT(onFullscreen()) },
        { "帮助 h", SLOT(onToggleHelp()) },
        { "关闭 q", SLOT(onClose()) },
    };
    int numBtns = sizeof(btns) / sizeof(btns[0]);
    for (int i = 0; i < numBtns; i++) {
        QPushButton* btn = new QPushButton(qFromUtf8(btns[i].text), m_toolbar);
        connect(btn, SIGNAL(clicked()), this, btns[i].slot);
#ifdef QT3_BUILD
        btn->setFocusPolicy(QWidget::NoFocus);
#else
        btn->setFocusPolicy(Qt::NoFocus);
#endif
        h->addWidget(btn);
    }
    h->addStretch();
    lay->addWidget(m_toolbar);
}

void PhotoViewer::updateTitle() {
    QString title = qFromUtf8("PhotoViewer  —  ")
        + QString::number(m_origPixmap.width()) + " × "
        + QString::number(m_origPixmap.height())
        + qFromUtf8("  —  缩放: ") + QString::number(m_canvas->zoomPercent()) + "%";
    if (!m_origData.isEmpty()) {
        title += qFromUtf8("  —  原图: ") + humanBytes(m_origData.size());
    }
    if (m_fullscreen) {
        title += qFromUtf8("  [全屏]");
    }
    qSetWindowTitle(this, title);
}

void PhotoViewer::updateStatus() {
    if (!m_statusLabel) { return; }
    QString s = QString::number(m_origPixmap.width())
        + " × " + QString::number(m_origPixmap.height())
        + qFromUtf8("  |  显示: ")
        + QString::number(m_canvas->displayedSize().width())
        + " × " + QString::number(m_canvas->displayedSize().height())
        + qFromUtf8("  |  倍率: ") + QString::number(m_canvas->zoomPercent()) + "%";
    if (!m_origData.isEmpty()) {
        s += qFromUtf8("  |  原图: ") + humanBytes(m_origData.size());
    }
    if (!m_origMime.isEmpty()) {
        s += qFromUtf8("  |  MIME: ") + m_origMime;
    } else {
        s += qFromUtf8("  |  MIME: ?");   // 魔数不可识别（如无 origData）
    }
    if (m_canvas->showHelp()) {
        s += qFromUtf8("  |  [?]");
    }
    m_statusLabel->setText(s);
}

void PhotoViewer::onViewChanged() {
    updateTitle();
    updateStatus();
}

void PhotoViewer::keyPressEvent(QKeyEvent* e) {
#ifdef QT3_BUILD
    uint mod = e->state();
    uint shift = Qt::ShiftButton;
#else
    Qt::KeyboardModifiers mod = e->modifiers();
    Qt::KeyboardModifiers shift = Qt::ShiftModifier;
#endif

    bool shiftPressed = (mod & shift) ? true : false;

    switch (e->key()) {
    case Qt::Key_Q:
    case Qt::Key_Escape:
        close();
        return;
    case Qt::Key_F:
    case Qt::Key_F11:
        onFullscreen();
        return;
    case Qt::Key_S:
        onSave();
        return;
    case Qt::Key_C:
        if (!e->isAutoRepeat()) { onCopy(); }   // 长按 c 不再疯狂重复拷贝
        return;
    case Qt::Key_Up:
    case Qt::Key_Equal:
        onZoomIn();
        return;
    case Qt::Key_Down:
    case Qt::Key_Minus:
        onZoomOut();
        return;
    case Qt::Key_Left:
        m_canvas->panBy(-20, 0);
        return;
    case Qt::Key_Right:
        m_canvas->panBy(20, 0);
        return;
    case Qt::Key_8:
        if (shiftPressed) {
            onActualSize();
            return;
        }
        break;
    case Qt::Key_Slash:
        onFitWindow();
        return;
    case Qt::Key_Period:
        if (shiftPressed) {
            onRotateCW();
            return;
        }
        break;
    case Qt::Key_Comma:
        if (shiftPressed) {
            onRotateCCW();
            return;
        }
        break;
    case Qt::Key_Z:
        if (shiftPressed) {
            m_canvas->toggleFitMode();
            updateTitle();
            updateStatus();
            return;
        }
        break;
    case Qt::Key_H:
        onToggleHelp();
        return;
    default:
        break;
    }
    QDialog::keyPressEvent(e);
}

PhotoViewer::~PhotoViewer() {
    qDebug("PhotoViewer destroyed, origPixmap=%dx%d",
           m_origPixmap.width(), m_origPixmap.height());
}

void PhotoViewer::closeEvent(QCloseEvent* event) {
    QDialog::closeEvent(event);
}

// 另存对话框默认文件名：主名优先取原媒体 URL 尾段文件名（仅主名，去掉其扩展名），
// 扩展名一律由原图实际格式 m_origMime 决定。若不这么做，CDN 尾名扩展名与内容不符
// 的场景（如 .png 链接实为 JPEG 字节）会默认 .png，保存时重编码 JPEG→PNG 体积放大。
QString PhotoViewer::defaultPhotoName() const {
    QString base;
    {
        QString u = m_suggestUrl;
#ifdef QT3_BUILD
        int slash = u.findRev('/');
        int q = u.find('?', slash + 1);
#else
        int slash = u.lastIndexOf('/');
        int q = u.indexOf('?', slash + 1);
#endif
        if (slash >= 0) { u = u.mid(slash + 1); }
        if (q >= 0) { u = u.left(q); }
#ifdef QT3_BUILD
        QString low = u.lower();
#else
        QString low = u.toLower();
#endif
        if (low.endsWith(".png"))       { base = u.left(u.length() - 4); }
        else if (low.endsWith(".jpg"))  { base = u.left(u.length() - 4); }
        else if (low.endsWith(".jpeg")) { base = u.left(u.length() - 5); }
    }
    if (base.isEmpty()) {
        char tbuf[32] = {0};
        time_t now = time(NULL);
        struct tm tmv;
        localtime_r(&now, &tmv);
        strftime(tbuf, sizeof(tbuf), "photo_%Y%m%d_%H%M%S", &tmv);
        base = qFromUtf8(tbuf);
    }
    QString ext = ".png";
    if (m_origMime == "image/jpeg") { ext = ".jpg"; }
    base += ext;
    return base;
}

void PhotoViewer::onSave() {
    QString startDir = qGetHomePath() + "/" + defaultPhotoName();
    // mac NSSavePanel 会按过滤器第一个类型补回显示时被隐藏的扩展名；
    // 原图是 JPEG 时把 *.jpg 放首位，避免面板默认补成 *.png 导致转码成 PNG
    QString imageFilter = (m_origMime == "image/jpeg")
        ? qFromUtf8("Images (*.jpg *.png)")
        : qFromUtf8("Images (*.png *.jpg)");
#ifdef QT3_BUILD
    QString path = QFileDialog::getSaveFileName(
        startDir, imageFilter, this);
#else
    QString path = QFileDialog::getSaveFileName(
        this, qFromUtf8("保存图片"), startDir,
        imageFilter);
#endif
    if (path.isEmpty()) { return; }

    // mac 原生 NSSavePanel 显示时隐藏扩展名，个别返回路径可能缺后缀；
    // 这里确保写盘文件名一定带正确扩展名（jpeg 用 .jpg，其余 .png），
    // 并与下方"扩展名匹配则原文直写"逻辑衔接。
    {
        QString lower;
#ifdef QT3_BUILD
        lower = path.lower();
#else
        lower = path.toLower();
#endif
        bool hasExt = lower.endsWith(".png")
            || lower.endsWith(".jpg") || lower.endsWith(".jpeg");
        if (!hasExt) {
            path += (m_origMime == "image/jpeg")
                ? QString(".jpg") : QString(".png");
        }
    }

    // 与剪贴板同策略：扩展名与原格式一致时直接写原始字节，零重编码
    // （300KB 原图保存后仍是 300KB）
#ifdef QT3_BUILD
    QString lower = path.lower();
#else
    QString lower = path.toLower();
#endif
    bool isPngPath = lower.endsWith(".png");
    bool isJpgPath = lower.endsWith(".jpg") || lower.endsWith(".jpeg");

    if ((isPngPath && m_origMime == "image/png")
            || (isJpgPath && m_origMime == "image/jpeg")) {
        QFile f(path);
#ifdef QT3_BUILD
        if (!f.open(IO_WriteOnly)) {
#else
        if (!f.open(QIODevice::WriteOnly)) {
#endif
            qWarning("PhotoViewer::onSave: open failed: %s", qToUtf8(path).data());
            QMessageBox::warning(this, qFromUtf8("保存失败"), path);
            return;
        }
#ifdef QT3_BUILD
        f.writeBlock(m_origData);          // QByteArray 重载 qfile.h:89
#else
        f.write(m_origData.constData(), m_origData.size());
#endif
        f.close();
        if (m_statusLabel) { m_statusLabel->setText(qFromUtf8("已保存: ") + path); }
        return;
    }

    // 转码路径：按扩展名选格式
    // （顺带修复：Qt3 原实现存 .jpg 时硬编码 "PNG"，实际写入 PNG 数据）
    bool ok;
#ifdef QT3_BUILD
    const char* fmt = isJpgPath ? "JPEG" : "PNG";
    ok = m_origPixmap.save(path, fmt);
#else
    ok = m_origPixmap.save(path);          // Qt4 按扩展名自动识别
#endif
    if (!ok) {
        qWarning("PhotoViewer::onSave: transc save failed: %s", qToUtf8(path).data());
        QMessageBox::warning(this, qFromUtf8("保存失败"), path);
    } else if (m_statusLabel) {
        m_statusLabel->setText(qFromUtf8("已保存: ") + path);
    }
}

// 不再用 setPixmap 让进程持有大像素图作为 selection owner：
// 旧实现下每个 SelectionRequest 都在 GUI 线程同步重编码整幅图，
// 导致 UI 卡死级延迟 + CPU 100%，且关闭查看器也无法解除 owner 身份。
// 新实现：缓存式数据源，请求成本≈memcpy；粘贴方拿到的是原始文件字节。
void PhotoViewer::onCopy() {
#ifdef QT3_BUILD
    QApplication::clipboard()->setData(
        new PhotoClipSource(m_origData, m_origMime,
                            m_origPixmap.convertToImage()));  // 所有权移交剪贴板
#else
    QMimeData* md = new QMimeData();
    if (m_origMime == "image/png") {
        // 原始字节直通，零重编码（主场景，体积与原文一致）
        md->setData("image/png", m_origData);
    } else if (m_origMime == "image/jpeg") {
        // 原始字节直通 + 一次性 PNG 像素兜底：仅 image/jpeg 一个目标时，
        // X11 上不认 jpeg 剪贴板的粘贴端什么都拿不到（复制失效回归的根因）。
        // 拷贝点击时编码一次（等效旧 setImageData 成本），不复活
        // "SelectionRequest 期重编码"的 CPU 100% 老 bug。
        md->setData("image/jpeg", m_origData);
        QBuffer buf;
        buf.open(QIODevice::WriteOnly);
        bool ok = m_origPixmap.save(&buf, "PNG");
        if (!ok) {
            qWarning("PhotoViewer::onCopy: PNG encode failed for JPEG fallback");
            QMessageBox::warning(this, qFromUtf8("复制失败"),
                                 qFromUtf8("生成像素数据失败"));
        } else {
            md->setData("image/png", buf.buffer());
        }
    } else {
        QBuffer buf;                                          // webp/空：转 PNG 一次
        buf.open(QIODevice::WriteOnly);
        bool ok = m_origPixmap.save(&buf, "PNG");
        if (!ok) {
            qWarning("PhotoViewer::onCopy: PNG encode failed, mime=%s",
                     qToUtf8(m_origMime).data());
            QMessageBox::warning(this, qFromUtf8("复制失败"),
                                 qFromUtf8("PNG 编码失败，剪贴板已无数据"));
        } else {
            md->setData("image/png", buf.buffer());
        }
    }
    // 像素兜底：macOS Cocoa 的 NSPasteboard 需要 TIFF/pixel 类型才可被目标应用粘贴；
    // 仅自定义 image/png|image/jpeg 字节时 mac 上粘贴为空。加回 setImageData
    // （副作用：粘贴端在 mac 拿到的可能是 TIFF，体积大于原图，但保证可粘贴）。
    md->setImageData(m_origPixmap.toImage());
    QApplication::clipboard()->setMimeData(md);               // 所有权移交剪贴板
#endif
    if (m_statusLabel) { m_statusLabel->setText(qFromUtf8("已复制到剪贴板")); }
}

void PhotoViewer::onZoomIn() {
    m_canvas->zoomIn();
    updateTitle();
    updateStatus();
}

void PhotoViewer::onZoomOut() {
    m_canvas->zoomOut();
    updateTitle();
    updateStatus();
}

void PhotoViewer::onFitWindow() {
    m_canvas->fitToWindow();
    updateTitle();
    updateStatus();
}

void PhotoViewer::onActualSize() {
    m_canvas->actualSize();
    updateTitle();
    updateStatus();
}

void PhotoViewer::onRotateCW() {
    m_canvas->rotateCW();
    updateTitle();
    updateStatus();
}

void PhotoViewer::onRotateCCW() {
    m_canvas->rotateCCW();
    updateTitle();
    updateStatus();
}

void PhotoViewer::onFullscreen() {
    if (!m_fullscreen) {
        m_savedX = x();
        m_savedY = y();
        m_savedW = width();
        m_savedH = height();
        m_toolbar->hide();
        m_statusBar->hide();
#ifdef QT3_BUILD
        setGeometry(QApplication::desktop()->screenGeometry(
            QApplication::desktop()->screenNumber(this)));
#else
        m_savedFlags = windowFlags();
        m_savedGeo = saveGeometry();
        // QDialog::showFullScreen() ignored by some X11 WMs for Qt::Dialog type.
        // Fix: temporarily change type to Qt::Window, per:
        //   stackoverflow.com/questions/12645880  (2012, 76+ votes)
        //   doc.qt.io/archives/qt-4.8/qwidget.html#saveGeometry
        setWindowFlags(Qt::Window);
        showFullScreen();
#endif
        m_fullscreen = true;
    } else {
        m_toolbar->show();
        m_statusBar->show();
#ifdef QT3_BUILD
        setGeometry(m_savedX, m_savedY, m_savedW, m_savedH);
#else
        // Restore flags + geometry. restoreGeometry() handles frame offset.
        // show() on a hidden window with restored flags re-maps normally.
        setWindowFlags(m_savedFlags);
        restoreGeometry(m_savedGeo);
        show();
#endif
        m_fullscreen = false;
    }
    updateTitle();
}

void PhotoViewer::onToggleHelp() {
    m_canvas->setShowHelp(!m_canvas->showHelp());
    m_canvas->update();
    updateStatus();
}

void PhotoViewer::onClose() {
    close();
}
