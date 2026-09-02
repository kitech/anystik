#include "stickerpreviewoverlay.h"
#include "androidutils.h"
#include "toastpopup.h"

#include <QGuiApplication>
#include <QClipboard>
#include <QPainter>
#include <QPainterPath>
#include <QMovie>
#include <QEvent>
#include <QTouchEvent>
#include <QMouseEvent>
#include <QCursor>
#include <QImageReader>

static constexpr qreal CLOSE_BTN_SIZE = 44;
static constexpr qreal ACTION_H = 52;
static constexpr qreal META_H = 150;   // 底部元信息文本区高度
static constexpr qreal META_PAD_LEFT = 16;
static constexpr qreal META_PAD_TOP = 10;
static constexpr qreal META_LINE_H = 20;

// ═══════════════════════════════════════════════════════════════════
// StickerPreviewNode
// ═══════════════════════════════════════════════════════════════════

void StickerPreviewNode::setData(const QString& emoji, const QImage& image,
                                 const QSizeF& size, const QString& metaText)
{
    m_emoji = emoji;
    m_image = image;
    m_size = size;
    m_metaText = metaText;
}

void StickerPreviewNode::triggerUpdate(QQuickWindow* window, const QRectF& rect,
                                       const QSizeF& size)
{
    update(window, rect, size, nullptr);
}

void StickerPreviewNode::paint(QPainter* painter, const QSize& size, const void*)
{
    const qreal w = size.width();
    const qreal h = size.height();

    painter->fillRect(0, 0, w, h, QColor(0, 0, 0, 200));

    // ── 元信息文本区背景（图片区域下方，动作栏上方） ──
    const qreal metaY = h - ACTION_H - META_H;
    painter->fillRect(QRectF(0, metaY, w, META_H), QColor(18, 18, 26));
    painter->setPen(QColor(35, 35, 45));
    painter->drawLine(0, metaY, w, metaY);

    // ── 绘制元信息文本（左对齐，小号字体） ──
    if (!m_metaText.isEmpty()) {
        QFont metaFont;
        metaFont.setPixelSize(14);
        painter->setFont(metaFont);
        painter->setPen(QColor(200, 200, 210));
        QStringList lines = m_metaText.split(QLatin1Char('\n'));
        const qreal textW = w - META_PAD_LEFT * 2;
        qreal ty = metaY + META_PAD_TOP + 14;
        for (const QString& line : lines) {
            painter->drawText(QRectF(META_PAD_LEFT, ty - 14, textW, META_LINE_H),
                Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, line);
            ty += META_LINE_H;
        }
    }

    // ── 图片 contain 居中（区域：顶部留 32px，下方扣 meta 区域和动作栏） ──
    if (!m_image.isNull()) {
        const qreal availW = w - 32;
        const qreal availH = h - ACTION_H - META_H - 64;
        const QSizeF imgSize = m_image.size();
        qreal scale = qMin(availW / imgSize.width(), availH / imgSize.height());
        const qreal dw = imgSize.width()  * scale;
        const qreal dh = imgSize.height() * scale;
        const QRectF dst((w - dw) / 2, (h - dh) / 2 - ACTION_H / 2 - META_H / 2, dw, dh);

        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->drawImage(dst, m_image);
    }

    // ── 关闭按钮 ──
    const QRectF closeBtn(w - CLOSE_BTN_SIZE - 12, 12,
        CLOSE_BTN_SIZE, CLOSE_BTN_SIZE);
    QPainterPath closePath;
    closePath.addRoundedRect(closeBtn, 12, 12);
    painter->fillPath(closePath, QColor(40, 40, 50, 220));
    QFont closeFont;
    closeFont.setPixelSize(24);
    painter->setFont(closeFont);
    painter->setPen(QColor(230, 230, 230));
    painter->drawText(closeBtn, Qt::AlignCenter, QStringLiteral("✕"));

    // ── 底部动作栏：复制 | Emoji | 删除 ──
    const qreal barY = h - ACTION_H;
    QRectF barRect(0, barY, w, ACTION_H);
    painter->fillRect(barRect, QColor(20, 20, 28));
    painter->setPen(QColor(40, 40, 50));
    painter->drawLine(0, barY, w, barY);

    QFont actionFont;
    actionFont.setPixelSize(15);
    painter->setFont(actionFont);

    painter->setPen(QColor(110, 190, 255));
    painter->drawText(QRectF(0, barY, w * (1.0 / 3.0), ACTION_H),
        Qt::AlignCenter, QString::fromUtf8("复制"));

    if (!m_emoji.isEmpty()) {
        painter->drawText(QRectF(w * (1.0 / 3.0), barY, w * (1.0 / 3.0), ACTION_H),
            Qt::AlignCenter, QString::fromUtf8("Emoji: ") + m_emoji);
    }

    painter->setPen(QColor(230, 110, 110));
    painter->drawText(QRectF(w * (2.0 / 3.0), barY, w * (1.0 / 3.0), ACTION_H),
        Qt::AlignCenter, QString::fromUtf8("删除"));
}

QskHashValue StickerPreviewNode::hash(const void*) const
{
    return qHash(m_emoji) ^ qHash(int(m_size.width())) ^ qHash(int(m_size.height()));
}

// ═══════════════════════════════════════════════════════════════════
// StickerPreviewOverlay
// ═══════════════════════════════════════════════════════════════════

StickerPreviewOverlay::StickerPreviewOverlay(QQuickItem* parent)
    : QQuickItem(parent)
{
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptTouchEvents(true);
    setAcceptHoverEvents(true);
    setZ(2000);
    setFlag(ItemHasContents, true);

    // 动图重绘驱动：QMovie 帧切换时刷新
    m_redrawTimer.setInterval(33);
    connect(&m_redrawTimer, &QTimer::timeout, this, [this]() {
        if (m_movie) {
            m_dirty = true;
            update();
        }
    });

    // 通用动图播放（非 QMovie 能力内的多帧格式，如 Qt 插件支持的动画 WebP）：
    // 每帧按 nextImageDelay 推进；到底后跳回首帧循环。
    connect(&m_animTimer, &QTimer::timeout, this, [this]() {
        if (!m_reader) return;
        if (!m_reader->jumpToNextImage() && !m_reader->jumpToImage(0)) return;
        const QImage f = m_reader->read();
        if (f.isNull()) return;
        m_image = f;
        m_dirty = true;
        update();
        const int delay = m_reader->nextImageDelay();
        m_animTimer.start(delay > 0 ? delay : 33);
    });

    if (auto* p = parentItem()) {
        connect(p, &QQuickItem::widthChanged, this, [this]() {
            if (auto* p = parentItem()) {
                setWidth(p->width());
                m_dirty = true;
                update();
            }
        });
        connect(p, &QQuickItem::heightChanged, this, [this]() {
            if (auto* p = parentItem()) {
                setHeight(p->height());
                m_dirty = true;
                update();
            }
        });
    }
}

StickerPreviewOverlay::~StickerPreviewOverlay()
{
    delete m_movie;
    delete m_reader;
}

void StickerPreviewOverlay::show(const StickerBrief& brief)
{
    m_brief = brief;
    m_emoji = brief.emoji;

    delete m_movie;
    m_movie = nullptr;
    delete m_reader;
    m_reader = nullptr;
    m_redrawTimer.stop();
    m_animTimer.stop();

    const QString filePath = brief.filePath;

    // 动图优先走 QMovie（GIF 最稳；Qt 插件若支持也可播动画 WebP）
    m_movie = new QMovie(filePath);
    connect(m_movie, &QMovie::frameChanged, this, [this]() {
        m_dirty = true;
        update();
    });
    if (m_movie->isValid() && m_movie->frameCount() > 1) {
        m_movie->start();
        m_image = m_movie->currentImage();
        m_redrawTimer.start();
    } else {
        delete m_movie;
        m_movie = nullptr;
    }

    // QMovie 不支持的动画格式：QImageReader 多帧逐帧播放
    // APNG：Qt PNG 插件不识别动画帧，imageCount()==1 → 落入末分支静帧首帧
    // （动画预览需引入 APNG 解码库，已知限制，见 detailed.md）
    if (!m_movie) {
        m_reader = new QImageReader(filePath);
        m_reader->setAutoTransform(true);
        if (m_reader->canRead() && m_reader->imageCount() > 1) {
            m_image = m_reader->read();
            if (!m_image.isNull())
                m_animTimer.start(33);
            else {
                delete m_reader;
                m_reader = nullptr;
            }
        } else {
            delete m_reader;
            m_reader = nullptr;
        }
    }

    if (!m_movie && !m_reader) {
        QImageReader reader(filePath);
        reader.setAutoTransform(true);
        m_image = reader.read();
    }

    if (auto* p = parentItem()) {
        setSize(p->size());
        setPosition(QPointF(0, 0));
    }

    // ── 元信息文本（自绘，不用 qskinny 控件） ──
    const StickerMeta meta = StickerStore::instance()->stickerMeta(filePath);
    m_metaText = formatStickerMeta(meta);

    setVisible(true);
    m_dirty = true;
    update();
}

void StickerPreviewOverlay::triggerRepaint()
{
    m_dirty = true;
    update();
}

bool StickerPreviewOverlay::metaRegionContains(const QPointF& localPos) const
{
    const qreal h = height();
    return localPos.y() >= h - ACTION_H - META_H
        && localPos.y() < h - ACTION_H;
}

QSGNode* StickerPreviewOverlay::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    auto* node = static_cast<StickerPreviewNode*>(oldNode);
    if (!node) {
        node = new StickerPreviewNode();
    }
    if (m_movie && m_movie->state() == QMovie::Running) {
        m_image = m_movie->currentImage();
    }
    if (m_dirty) {
        node->setData(m_emoji, m_image, size(), m_metaText);
        node->triggerUpdate(window(),
            QRectF(QPointF(0, 0), size()), size());
        m_dirty = false;
    }
    return node;
}

void StickerPreviewOverlay::touchEvent(QTouchEvent* event)
{
    if (event->type() == QEvent::TouchBegin && !event->points().isEmpty()) {
        const QPointF lp = mapFromScene(event->points().first().scenePosition());
        if (metaRegionContains(lp)) {
            QGuiApplication::clipboard()->setText(m_metaText);
            ToastPopup::show(this, QString::fromUtf8("已复制元信息"));
            event->accept();
            return;
        }
        handlePress(event->points().first().scenePosition());
        event->accept();
    } else {
        QQuickItem::touchEvent(event);
    }
}

void StickerPreviewOverlay::mousePressEvent(QMouseEvent* event)
{
    const QPointF lp = mapFromScene(event->scenePosition());
    if (metaRegionContains(lp)) {
        QGuiApplication::clipboard()->setText(m_metaText);
        ToastPopup::show(this, QString::fromUtf8("已复制元信息"));
        event->accept();
        return;
    }
    handlePress(event->scenePosition());
    event->accept();
}

void StickerPreviewOverlay::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        Q_EMIT closed();
        deleteLater();
        return;
    }
    QQuickItem::keyPressEvent(event);
}

void StickerPreviewOverlay::hoverMoveEvent(QHoverEvent* event)
{
    const qreal w = width();
    const QRectF closeBtn(w - CLOSE_BTN_SIZE - 12, 12,
        CLOSE_BTN_SIZE, CLOSE_BTN_SIZE);
    if (closeBtn.contains(event->position()) || event->position().y() >= height() - ACTION_H
        || metaRegionContains(event->position())) {
        setCursor(QCursor(Qt::PointingHandCursor));
    } else {
        unsetCursor();
    }
    QQuickItem::hoverMoveEvent(event);
}

void StickerPreviewOverlay::handlePress(const QPointF& scenePos)
{
    QPointF localPos = mapFromScene(scenePos);

    // 关闭按钮
    const qreal w = width();
    const QRectF closeBtn(w - CLOSE_BTN_SIZE - 12, 12,
        CLOSE_BTN_SIZE, CLOSE_BTN_SIZE);
    if (closeBtn.contains(localPos)) {
        Q_EMIT closed();
        deleteLater();
        return;
    }

    // 底部动作栏：左 复制 / 右 删除
    if (localPos.y() >= height() - ACTION_H) {
        const qreal w = width();
        if (localPos.x() < w * (1.0 / 3.0)) {
            StickerStore::instance()->touchSticker(m_brief.id);
            bool ok = StickerStore::instance()->copyStickerToClipboard(m_brief.filePath);
            ToastPopup::show(this, ok ? QString::fromUtf8("已复制到剪贴板")
                                      : QString::fromUtf8("复制失败"));
        } else if (localPos.x() > w * (2.0 / 3.0)) {
            Q_EMIT deleteRequested(m_brief);
        }
        return;
    }

    // 背景点击 → 关闭
    Q_EMIT closed();
    deleteLater();
}

#include "moc_stickerpreviewoverlay.cpp"
