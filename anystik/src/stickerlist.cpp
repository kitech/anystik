#include "stickerlist.h"
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QImageReader>
#include <QTimer>
#include <QtMath>
#include <QskEvent.h>
#include <private/qquicktaphandler_p.h>

static constexpr qreal TILE_SIZE = 76;
static constexpr qreal TILE_GAP  = 10;
static constexpr qreal STEP      = TILE_SIZE + TILE_GAP;

// ═══════════════════════════════════════════════════════════════════
// StickerTileNode — 单个贴纸瓦片绘制
// ═══════════════════════════════════════════════════════════════════

void StickerTileNode::setData(const StickerBrief& brief, const QImage& image,
                              bool highlighted)
{
    m_brief = brief;
    m_image = image;
    m_highlighted = highlighted;
}

void StickerTileNode::triggerUpdate(QQuickWindow* window, const QRectF& rect,
                                    const QSizeF& size)
{
    update(window, rect, size, nullptr);
}

void StickerTileNode::paint(QPainter* painter, const QSize& size, const void*)
{
    const qreal w = size.width();
    const qreal h = size.height();

    painter->setRenderHint(QPainter::Antialiasing, true);

    const qreal radius = 12;
    QPainterPath clip;
    clip.addRoundedRect(0, 0, w, h, radius, radius);
    painter->setClipPath(clip);

    // ── 背景 ──
    QColor bg(m_highlighted ? "#2c2c44" : "#1e1e34");
    painter->fillPath(clip, bg);

    if (m_image.isNull()) {
        painter->setPen(QColor("#666"));
        QFont f;
        f.setPixelSize(11);
        painter->setFont(f);
        painter->drawText(QRectF(0, 0, w, h), Qt::AlignCenter, QStringLiteral("?"));
        return;
    }

    // ── 图片 contain 缩放 ──
    const qreal inner = TILE_SIZE - 14;
    const QSizeF imgSize = m_image.size();
    qreal scale = qMin(inner / imgSize.width(), inner / imgSize.height());
    const qreal dw = imgSize.width()  * scale;
    const qreal dh = imgSize.height() * scale;
    const QRectF dst((w - dw) / 2, (h - dh) / 2, dw, dh);

    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->drawImage(dst, m_image);

    // ── 边框 ──
    QPen pen(m_highlighted ? QColor("#8ab4f8") : QColor("#2a2a42"));
    pen.setWidth(1);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(QRectF(0.5, 0.5, w - 1, h - 1), radius, radius);
}

QskHashValue StickerTileNode::hash(const void*) const
{
    return qHash(m_brief.id) ^ qHash(m_brief.emoji)
        ^ (m_highlighted ? 1 : 0) ^ qHash(m_image.cacheKey());
}

// ═══════════════════════════════════════════════════════════════════
// StickerTileItem — QQuickItem 承载一个贴纸
// ═══════════════════════════════════════════════════════════════════

StickerTileItem::StickerTileItem(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setSize(QSizeF(TILE_SIZE, TILE_SIZE));
}

void StickerTileItem::setStickerData(const StickerBrief& brief)
{
    m_brief = brief;

    // 首次展示时加载首帧（GIF/WebP 动图取首帧，预览层负责动图播放）
    QImageReader reader(brief.filePath);
    reader.setAutoTransform(true);
    reader.setScaledSize(QSize(TILE_SIZE * 2, TILE_SIZE * 2));
    m_image = reader.read();

    m_dirty = true;
    update();
}

QSGNode* StickerTileItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    auto* node = static_cast<StickerTileNode*>(oldNode);
    if (!node) {
        node = new StickerTileNode();
    }
    if (m_dirty) {
        node->setData(m_brief, m_image, m_highlighted);
        m_dirty = false;
    }
    node->triggerUpdate(window(),
        QRectF(QPointF(0, 0), QSizeF(width(), height())),
        QSizeF(width(), height()));
    return node;
}

// ═══════════════════════════════════════════════════════════════════
// StickerGridWidget — 虚拟网格
// ═══════════════════════════════════════════════════════════════════

StickerGridWidget::StickerGridWidget(QQuickItem* parent)
    : MyScrollArea(parent)
{
    setFlickableOrientations(Qt::Vertical);
    setItemResizable(false);

    m_contentView = new QQuickItem(this);
    setScrolledItem(m_contentView);

    // ── 单击：复制 ──
    auto* tapHandler = new QQuickTapHandler(m_contentView);
    tapHandler->setGesturePolicy(QQuickTapHandler::DragThreshold);
    connect(tapHandler, &QQuickTapHandler::singleTapped, this,
        [this](QEventPoint point, Qt::MouseButton) {
            int idx = indexAt(point.position());
            if (idx >= 0 && idx < m_items.size()) {
                Q_EMIT stickerClicked(m_items[idx]);
            }
        });

    // ── 长按：菜单 ──
    connect(tapHandler, &QQuickTapHandler::longPressed, this,
        [this]() {
            QPointF scenePos = static_cast<MyScrollArea*>(this)->lastTouchScenePos();
            QPointF localPos = m_contentView->mapFromScene(scenePos);
            int idx = indexAt(localPos);
            if (idx >= 0 && idx < m_items.size()) {
                Q_EMIT stickerLongPressed(m_items[idx], scenePos);
            }
        });

    // ── 双击：预览 ──
    connect(this, &MyScrollArea::doubleTapped, this,
        [this](QPointF scenePos) {
            QPointF localPos = m_contentView->mapFromScene(scenePos);
            int idx = indexAt(localPos);
            if (idx >= 0 && idx < m_items.size()) {
                Q_EMIT stickerDoubleClicked(m_items[idx]);
            }
        });

    connect(this, &QskScrollBox::scrollPosChanged,
        this, &StickerGridWidget::updateVisibleRows);
}

StickerGridWidget::~StickerGridWidget()
{
    for (auto* tile : std::as_const(m_visibleTiles)) {
        delete tile;
    }
    m_visibleTiles.clear();
}

void StickerGridWidget::setStickers(const QVector<StickerBrief>& stickers)
{
    m_items = stickers;
    m_cols = qMax(1, int(viewContentsRect().width()) / int(STEP + 0.5));
    relayoutContent();
}

StickerBrief StickerGridWidget::stickerAt(int index) const
{
    if (index >= 0 && index < m_items.size()) {
        return m_items[index];
    }
    return StickerBrief();
}

void StickerGridWidget::relayoutContent()
{
    if (m_cols <= 0) m_cols = 4;

    m_rows = (m_items.size() + m_cols - 1) / m_cols;
    qreal viewW = viewContentsRect().width();
    if (viewW <= 0) viewW = width();
    m_contentView->setSize(QSizeF(viewW, m_rows * STEP));
    update();
    updateVisibleRows();
}

void StickerGridWidget::updateVisibleRows()
{
    if (!m_contentView || m_items.isEmpty()) {
        return;
    }

    for (auto* tile : std::as_const(m_visibleTiles)) {
        delete tile;
    }
    m_visibleTiles.clear();

    const qreal scrollY = scrollPos().y();
    const qreal viewH = viewContentsRect().height();

    const int rowMin = qMax(0, int(qFloor(scrollY / STEP)));
    const int rowMax = qMin(m_rows - 1, int(qCeil((scrollY + viewH) / STEP)));

    for (int row = rowMin; row <= rowMax; ++row) {
        for (int col = 0; col < m_cols; ++col) {
            const int idx = row * m_cols + col;
            if (idx >= m_items.size()) break;

            auto* tile = new StickerTileItem(m_contentView);
            tile->setX(col * STEP);
            tile->setY(row * STEP);
            tile->setStickerData(m_items[idx]);
            m_visibleTiles[idx] = tile;
        }
    }
}

int StickerGridWidget::indexAt(const QPointF& contentPos) const
{
    if (m_cols <= 0) return -1;
    const int col = int(qFloor(contentPos.x() / STEP));
    const int row = int(qFloor(contentPos.y() / STEP));
    const int idx = row * m_cols + col;
    return idx;
}

void StickerGridWidget::geometryChangeEvent(QskGeometryChangeEvent* event)
{
    QskScrollArea::geometryChangeEvent(event);
    if (event->isResized() && m_contentView) {
        m_cols = qMax(1, int(viewContentsRect().width()) / int(STEP + 0.5));
        relayoutContent();
    }
}

#include "moc_stickerlist.cpp"