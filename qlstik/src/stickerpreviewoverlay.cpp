#include "stickerpreviewoverlay.h"
#include "qimagereader_shim.h"   // 本仓完整 QImageReader：GIF/APNG/WebP 多帧，delay 已是 ms
#include "translator.h"          // _() / qFromUtf8
#include "toastwidget.h"          // ToastWidget::show(parent, text, ms)
#include "stickerops.h"           // 动作栏「复制」复用同一份实现
#include "compatcore34.h"         // qFromUtf8 等跨版本助手

#ifdef QT3_BUILD
#include <qpainter.h>
#include <qfont.h>
#include <qcursor.h>
#include <qapplication.h>
#include <qclipboard.h>
#include <qevent.h>
#else
#include <QPainter>
#include <QFont>
#include <QCursor>
#include <QApplication>
#include <QClipboard>
#include <QEvent>
// ⚠ Qt4+ 必须自己引原生头：qimagereader_shim.h 的 QImageReader 类**只覆盖
//   QT_VERSION < 0x040000**（shim:58 的门控到 :159 才闭合），Qt4+ 段里
//   它一个符号都不给。不引就只剩 <QPixmap> 的前置声明 → incomplete type。
//   写法对齐 stickerops.cpp:23。
#include <QImageReader>
#endif

// anysk 这里是硬编码字面量而非主题色（stickerpreviewoverlay.cpp:19-88），
// 1:1 照搬以保持视觉一致；勿"顺手"改成 g_activeParams 主题色，那会与 anysk 不同。
static const QColor kMetaBg(18, 18, 26);
static const QColor kMetaBorder(35, 35, 45);
static const QColor kMetaText(200, 200, 210);
static const QColor kActionBg(20, 20, 28);
static const QColor kActionBorder(40, 40, 50);
static const QColor kCopyColor(110, 190, 255);
static const QColor kDeleteColor(230, 110, 110);
#ifdef QT3_BUILD
// ⚠⚠ Qt3 的 QColor **根本没有 alpha 这个概念**：qcolor.h:81-84 只有
//   QColor(r,g,b) 与 QColor(x,y,z,Spec)，全文件唯一的 alpha 符号是**只读**的
//   qAlpha(QRgb)（:60）——既无 4 参构造也无 setAlpha。故 Qt3 连*表达*都做不到
//   anysk 的 (40,40,50,220)，只能退化成不透明近似值（叠在不透明遮罩上肉眼几乎无差）。
static const QColor kCloseBg(40, 40, 50);
#else
static const QColor kCloseBg(40, 40, 50, 220);
#endif
static const QColor kCloseText(230, 230, 230);

// 平滑缩放到**恰好** w×h 的 QPixmap（比例已由 scaledImageRect 算好）。
// 两处跨版本差异，都必须分支：
//  1) 缩放 API：Qt3 是 QImage::smoothScale（qimage.h:158，默认 ScaleFree ≡
//     IgnoreAspectRatio）；Qt4+ **没有**可用的 smoothScale —— Qt6 的
//     smoothScaled(int,int) 是 protected（qimage.h:295，public 段无此函数），
//     公开的平滑缩放只有 scaled(w,h,mode,Qt::SmoothTransformation)。
//     写法对齐 shim 自己的注释（qimagereader_shim.cpp:511-513 就这么对齐 Qt6 的）。
//  2) QImage→QPixmap：Qt3 有 QPixmap(const QImage&) 构造（qpixmap.h:68）；
//     **Qt6 删掉了该构造**，只剩 static QPixmap::fromImage（qpixmap.h:89）。
static QPixmap scaleToPixmap(const QImage& src, int w, int h)
{
#ifdef QT3_BUILD
    return QPixmap(src.smoothScale(w, h));
#else
    return QPixmap::fromImage(src.scaled(w, h, Qt::IgnoreAspectRatio,
                                         Qt::SmoothTransformation));
#endif
}

StickerPreviewOverlay::StickerPreviewOverlay(QWidget* parent)
    : QWidget(parent)
    , m_reader(0)
    , m_frameCount(1)
    , m_animated(false)
{
    // ⚠ 焦点策略枚举的**位置**跨版本不同：Qt3 放在 QWidget 内
    //   （qwidget.h:305-306，StrongFocus = TabFocus|ClickFocus|0x8），
    //   Qt4 起才挪进 Qt（Qt::FocusPolicy）——故要版本分支，不是抄一个名字就完。
#ifdef QT3_BUILD
    setFocusPolicy(QWidget::StrongFocus);
#else
    setFocusPolicy(Qt::StrongFocus);
#endif
    setMouseTracking(true);            // qwidget.h:119 实测有
    // ⚠ 不调 setAutoFillBackground：Qt3 的 QWidget 无此成员（-fsyntax-only 实测报错）
#ifndef QT3_BUILD
    // ⚠ Qt3 不走这里：Qt3 无 WA_TranslucentBackground（Qt4.1+ 才有），
    //   且本机 Qt3 画 QPixmap 不做 alpha 混合（stickerlistpage.h:25 已实测
    //   记录），半透明会渲染成实色。故 Qt3 由 backdropColor() 返回不透明色。
    setAttribute(Qt::WA_TranslucentBackground, true);
#endif
    // Qt3 moc 不认 lambda，槽一律用旧式 SIGNAL/SLOT 语法（三版本通吃）
    connect(&m_animTimer, SIGNAL(timeout()), this, SLOT(onAnimTick()));
    // ⚠⚠ 关键：带 parent 构造的 QWidget **默认只有 100x30**（不是 640x480，
    //   那是无 parent 的窗口默认值），且本控件不进任何 layout —— 所以不装这个
    //   过滤器的话遮罩会缩成左上角一小块。Resize/Show 时把自己铺满父控件。
    if (parentWidget()) {
        parentWidget()->installEventFilter(this);
    }
}

StickerPreviewOverlay::~StickerPreviewOverlay()
{
    // m_reader 不是 QObject（见 openReader 注释），Qt 不会自动回收
    delete m_reader;
    m_reader = 0;
    // 必须摘掉，否则父控件析构时还会回调到已死对象
    if (parentWidget()) {
        parentWidget()->removeEventFilter(this);
    }
}

bool StickerPreviewOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget()
        && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        // setGeometry 会触发本控件 resizeEvent → rebuildCache() 重算 contain 比例
        setGeometry(parentWidget()->rect());
    }
    return QWidget::eventFilter(watched, event);   // 不吞事件，父控件照常处理
}

QColor StickerPreviewOverlay::backdropColor() const
{
    // anysk :43 原值 QColor(0,0,0,200)。
#ifndef QT3_BUILD
    return QColor(0, 0, 0, 200);
#else
    // ⚠ Qt3 降级为不透明：见构造函数里 WA_TranslucentBackground 的说明。
    //   取 kMetaBg(18,18,26) 而非纯黑，与 anysk 的 200-alpha 黑叠在深色
    //   主题上的观感接近。**代价**：Qt3 下看不到被遮的网格（anysk 能透出）。
    return kMetaBg;
#endif
}

QRect StickerPreviewOverlay::closeButtonRect() const
{
    return QRect(width() - kCloseBtnSize - 12, 12, kCloseBtnSize, kCloseBtnSize);
}

QRect StickerPreviewOverlay::metaRect() const
{
    return QRect(0, height() - kActionH - kMetaH, width(), kMetaH);
}

QRect StickerPreviewOverlay::actionBarRect() const
{
    return QRect(0, height() - kActionH, width(), kActionH);
}

QRect StickerPreviewOverlay::scaledImageRect() const
{
    // anysk :41-54。availW = w-32，availH = h-ACTION_H-META_H-64；
    // scale 无上限钳制 → 小图会被放大（与 anysk 一致，**刻意不改成只缩小**）。
    if (m_image.isNull()) {
        return QRect();
    }
    const int availW = width() - 32;
    const int availH = height() - kActionH - kMetaH - 64;
    if (availW <= 0 || availH <= 0) {
        return QRect();
    }
    const double iw = m_image.width(), ih = m_image.height();
    if (iw <= 0.0 || ih <= 0.0) {
        return QRect();
    }
    const double sw = availW / iw, sh2 = availH / ih;
    // ⚠ 不用 qMin：本机 Qt3 未把 qMin 引进本 TU（-fsyntax-only 实测 not declared）
    const double sc = sw < sh2 ? sw : sh2;
    const int dw = int(iw * sc), dh = int(ih * sc);
    // 居中后再上移半个动作栏+元信息区（anysk :49 的 y 偏移）
    return QRect((width() - dw) / 2,
                 (height() - dh) / 2 - kActionH / 2 - kMetaH / 2,
                 dw, dh);
}

bool StickerPreviewOverlay::metaRegionContains(const QPoint& p) const
{
    return metaRect().contains(p);          // anysk :271-274 同义
}

bool StickerPreviewOverlay::actionBarContains(const QPoint& p) const
{
    return actionBarRect().contains(p);     // anysk :109
}

bool StickerPreviewOverlay::isInteractive(const QPoint& p) const
{
    // anysk hoverMoveEvent :344-357：✕ / 动作栏 / 元信息区 → PointingHand
    return closeButtonRect().contains(p)
        || actionBarContains(p)
        || metaRegionContains(p);
}

void StickerPreviewOverlay::rebuildCache()
{
    // ⚠ 手法照 qltox/photoviewer.cpp 的 PhotoCanvas：rebuildCache()（:256-269）
    //   把缩放结果预渲染进缓存，paintEvent（:271-281）只做 1:1 drawPixmap blit。
    //   不这么做的代价：Qt3 的 drawPixmap(QRect,pm) **每次调用**都重跑一遍
    //   QImage::smoothScale（stickerlistpage.cpp:168-169 已实测记录），
    //   动图每帧一次全图缩放 → 卡。
    m_cachedPixmap = QPixmap();
    const QRect r = scaledImageRect();
    if (r.isEmpty()) {
        return;
    }
    // Qt3 无 SmoothPixmapTransform：在此**一次性**缩放到目标尺寸（见 scaleSmooth）。
    // QPixmap(const QImage&) 见 qpixmap.h:68。
    m_cachedPixmap = scaleToPixmap(m_image, r.width(), r.height());
}

bool StickerPreviewOverlay::openReader()
{
    stopAnimation();
    delete m_reader;
    m_reader = 0;
    m_image = QImage();
    m_cachedPixmap = QPixmap();
    m_frameCount = 1;
    if (m_filePath.isEmpty()) {
        return false;
    }
    // ⚠ shim 的 QImageReader **不是 QObject**（qimagereader_shim.h:104-105 只接
    //   文件名/QIODevice，无 parent 参数），故不能挂 parent 让 Qt 回收 ——
    //   必须自己 delete，见 openReader() 与析构函数。
    QImageReader* r = new QImageReader(m_filePath);
    r->setAutoTransform(true);
    if (!r->canRead()) {
        delete r;
        return false;
    }
    m_reader = r;
    m_frameCount = r->imageCount();      // 结构扫描，不解全部帧
    return true;
}

void StickerPreviewOverlay::showSticker(const QString& filePath,
                                        const QString& id,
                                        const QString& emoji,
                                        const QString& metaText)
{
    m_filePath = filePath;
    m_id = id;
    m_emoji = emoji;
    m_metaText = metaText;

    if (openReader() && m_reader) {
        m_image = m_reader->read();
        m_animated = !m_image.isNull() && m_frameCount > 1;
    } else {
        m_animated = false;
    }

    // ⚠ 解码失败就不弹空壳预览。实测会走到这里的真实案例：Qt3 下**无 alpha
    //   通道的 RGB APNG**（PNG color type 2）——vendored uc_apng_loader 在
    //   uc_apng_loader.h:244 用 STBI_rgb_alpha 解却拿到 d!=4，抛
    //   "image_t : d == BPP failed"，shim 转成 m_errorStr 后 read() 给 null。
    //   带 alpha 的 APNG（color type 6）则正常。
    if (m_image.isNull()) {
        stopAnimation();
        m_cachedPixmap = QPixmap();
        // ⚠⚠ toast 绝不能挂到 **this**：ToastWidget::show 是
        //   `new ToastWidget(parent,...)` 再 tw->show()（toastwidget.cpp:24-28），
        //   而 show 子控件会让祖先一并可见 —— 那就等于把空壳预览显示出来了。
        //   故只挂父控件（生产环境恒为 StickerListPage）；无父则只记日志不弹。
        QWidget* host = parentWidget();
        if (host) {
            ToastWidget::show(host, _(qFromUtf8("sticker_msg.preview_failed")), 2000);
        } else {
            // ⚠ 用仓内 qToUtf8（qlcomp/compatcore34.h:48）而不是 QString::utf8()：
            //   后者是 Qt3 专有，Qt4 起改名 toUtf8()，Qt6 直接没有该成员。
            qWarning("StickerPreviewOverlay: decode failed and no parent to toast on: %s",
                     qToUtf8(m_filePath).data());
        }
        return;
    }

    rebuildCache();
    show();
    raise();
    setFocus();                  // Esc 靠这个（qwidget.h:298）
    if (m_animated) {
        const int d = m_reader->nextImageDelay();
        m_animTimer.start(d > 0 ? d : 33);
    }
}

void StickerPreviewOverlay::stopAnimation()
{
    m_animTimer.stop();
    m_animated = false;
}

void StickerPreviewOverlay::onAnimTick()
{
    if (!m_reader) {
        return;
    }
    // ⚠⚠ **只能反复 read()，不能调 jumpToNextImage()**：本仓 shim 的 read()
    //   已**无条件**自增游标（qimagereader_shim.cpp:525 的 `++m_index`，
    //   注释里写了 Qt6 对照实测），再 jump 会**跳过一帧**。
    //   且 read() 耗尽后不回头（:506-507 返回 QImage()），故末尾靠
    //   openReader() 重建句柄回到首帧循环。
    QImage f = m_reader->read();
    if (f.isNull()) {
        if (!openReader() || !m_reader) {
            return;
        }
        f = m_reader->read();
        if (f.isNull()) {
            return;
        }
    }
    m_image = f;
    rebuildCache();
    update();
    const int d = m_reader->nextImageDelay();
    m_animTimer.start(d > 0 ? d : 33);
}

void StickerPreviewOverlay::copyMetaToClipboard()
{
    // anysk :318/:325：剪贴板取 m_metaText + toast「已复制元信息」
    QApplication::clipboard()->setText(m_metaText);
    ToastWidget::show(this, _(qFromUtf8("sticker_msg.meta_copied")), 2000);
}

void StickerPreviewOverlay::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const int w = width(), h = height();

    // 遮罩
    p.fillRect(0, 0, w, h, backdropColor());

    // ── 图片：contain 居中（尺寸在 scaledImageRect，缩放在 rebuildCache）──
    if (!m_cachedPixmap.isNull()) {
        // ⚠ 必须 1:1 blit 缓存，**不能**写 drawPixmap(scaledImageRect(), m_image)
        //   —— Qt3 那会每次重跑全图 smoothScale。
        p.drawPixmap(scaledImageRect().topLeft(), m_cachedPixmap);
    }

    // ── 元信息区（anysk :19-39）──
    const QRect mr = metaRect();
    p.fillRect(mr, kMetaBg);
    p.setPen(kMetaBorder);
    p.drawLine(mr.left(), mr.top(), mr.right(), mr.top());
    if (!m_metaText.isEmpty()) {
        QFont f;
        f.setPixelSize(14);
        p.setFont(f);
        p.setPen(kMetaText);
        const QRect tr(kMetaPadLeft, mr.top() + kMetaPadTop,
                       mr.width() - kMetaPadLeft * 2, mr.height() - kMetaPadTop);
#ifdef QT3_BUILD
        // Qt3 的换行标志叫 WordBreak（qnamespace.h:128，=0x0800），
        // Qt4 起改名 TextWordWrap（**同一位值**）——行为与 anysk :36 一致。
        p.drawText(tr, Qt::AlignLeft | Qt::AlignTop | Qt::WordBreak, m_metaText);
#else
        p.drawText(tr, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_metaText);
#endif
    }

    // ── 动作栏（anysk :67-88）──
    const QRect ar = actionBarRect();
    p.fillRect(ar, kActionBg);
    p.setPen(kActionBorder);
    p.drawLine(ar.left(), ar.top(), ar.right(), ar.top());
    QFont af;
    af.setPixelSize(15);
    p.setFont(af);
    const int third = w / 3;
    p.setPen(kCopyColor);
    p.drawText(QRect(0, ar.top(), third, ar.height()), Qt::AlignCenter,
               _(qFromUtf8("sticker_menu.copy")));
    if (!m_emoji.isEmpty()) {
        p.setPen(kMetaText);
        p.drawText(QRect(third, ar.top(), third, ar.height()), Qt::AlignCenter,
                   QString::fromUtf8("Emoji: ") + m_emoji);
    }
    p.setPen(kDeleteColor);
    p.drawText(QRect(2 * third, ar.top(), w - 2 * third, ar.height()),
               Qt::AlignCenter, _(qFromUtf8("sticker_menu.delete")));

    // ── ✕ 钮（anysk :56-65）──
    const QRect cr = closeButtonRect();
    p.setPen(kCloseBg);
    p.setBrush(kCloseBg);
#ifdef QT3_BUILD
    // Qt3 无 QPainterPath，drawRoundedRect 是 Qt4.1 才加的 → 用原生 drawRoundRect
    // （qpainter.h:203 的 QRect 重载；Qt4 起改名，故必须版本分支）
    p.drawRoundRect(cr, 12, 12);
#else
    p.drawRoundedRect(cr, 12, 12);
#endif
    p.setBrush(Qt::NoBrush);                        // qnamespace.h:798
    QFont cf;
    cf.setPixelSize(24);
    p.setFont(cf);
    p.setPen(kCloseText);
    p.drawText(cr, Qt::AlignCenter, QString::fromUtf8("✕"));
}

void StickerPreviewOverlay::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    rebuildCache();       // 尺寸变了 contain 比例要重算
}

void StickerPreviewOverlay::hideEvent(QHideEvent* event)
{
    stopAnimation();     // 见头文件说明：隐藏后必须停解码定时器
    QWidget::hideEvent(event);
}

void StickerPreviewOverlay::closeEvent(QCloseEvent* event)
{
    stopAnimation();
    emit closed();       // 页面在槽里把 m_previewOverlay 置空，见头文件说明
    QWidget::closeEvent(event);
}

void StickerPreviewOverlay::mouseMoveEvent(QMouseEvent* event)
{
    // Qt3 无 underMouse()，故用 mouseMoveEvent 主动切光标
    if (isInteractive(event->pos())) {
        setCursor(QCursor(Qt::PointingHandCursor));
    } else {
        unsetCursor();
    }
}

void StickerPreviewOverlay::mousePressEvent(QMouseEvent* event)
{
    const QPoint pt = event->pos();                  // qevent.h:185
    if (metaRegionContains(pt)) {
        copyMetaToClipboard();
        return;
    }
    if (closeButtonRect().contains(pt)) {
        close();          // 统一出口：closeEvent 里发 closed + 停动画
        return;
    }
    if (actionBarContains(pt)) {
        const int third = width() / 3;
        if (pt.x() < third) {                        // 左 1/3：复制
            StickerOps::touch(m_id);
            const bool ok = StickerOps::copyToClipboard(m_filePath);
            ToastWidget::show(this,
                _(qFromUtf8(ok ? "sticker_msg.copied" : "sticker_msg.copy_failed")),
                2000);
        } else if (pt.x() > width() - third) {        // 右 1/3：删除
            emit deleteRequested();
        }
        // 中 1/3（Emoji 位）不响应，同 anysk（只画字不绑动作）
        return;
    }
    // 空白：关（anysk handlePress 尾部 :385-389）
    close();
}

void StickerPreviewOverlay::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {            // anysk :334-342
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}
