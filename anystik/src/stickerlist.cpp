#include "stickerlist.h"
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QImageReader>
#include <QTimer>
#include <QtMath>
#include <QskEvent.h>
#include <private/qquicktaphandler_p.h>
#include <private/qquicksinglepointhandler_p.h>
#include <functional>
#include <QHash>

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

namespace {
QHash<QString, QImage> s_tileImageCache;

QImage decodedTileImage(const QString& filePath)
{
    auto it = s_tileImageCache.constFind(filePath);
    if (it != s_tileImageCache.constEnd())
        return *it;
    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    reader.setScaledSize(QSize(TILE_SIZE * 2, TILE_SIZE * 2));
    QImage img = reader.read();
    if (!img.isNull())
        s_tileImageCache.insert(filePath, img);
    if (s_tileImageCache.size() > 400)
        s_tileImageCache.clear();
    return img;
}
} // namespace

StickerTileItem::StickerTileItem(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setSize(QSizeF(TILE_SIZE, TILE_SIZE));
}

void StickerTileItem::setStickerData(const StickerBrief& brief)
{
    m_brief = brief;
    m_image = decodedTileImage(brief.filePath);
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
// RightClickHandler — 桌面右键上下文菜单专用（Android 无右键事件，天然不触发）
// ═══════════════════════════════════════════════════════════════════

class RightClickHandler final : public QQuickSinglePointHandler
{
public:
    explicit RightClickHandler(QQuickItem* target)
        : QQuickSinglePointHandler(target)
    {
        setAcceptedButtons(Qt::RightButton);   // 只接收右键，与左键 tap/长按路径互斥
    }

    std::function<void(const QPointF&)> onRightPress;  // 免 Q_OBJECT/moc，回调到网格

protected:
    void handleEventPoint(QPointerEvent* event, QEventPoint& point) override
    {
        const auto* single = dynamic_cast<const QSinglePointEvent*>(event);
        if (event->type() == QEvent::MouseButtonPress
                && single && single->button() == Qt::RightButton
                && onRightPress) {
            onRightPress(point.scenePosition());
        }
    }
};

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
                Q_EMIT stickerClicked(m_items[idx], idx);
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

    // ── 右键：菜单（桌面；复用长按菜单信号，Android 不触发）──
    m_contentView->setAcceptedMouseButtons(m_contentView->acceptedMouseButtons()
                                               | Qt::RightButton);
    auto* rightHandler = new RightClickHandler(m_contentView);
    rightHandler->onRightPress = [this](const QPointF& scenePos) {
        int idx = indexAt(m_contentView->mapFromScene(scenePos));
        if (idx >= 0 && idx < m_items.size()) {
            Q_EMIT stickerLongPressed(m_items[idx], scenePos);
        }
    };

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
    clearTiles();
}

void StickerGridWidget::setStickers(const QVector<StickerBrief>& stickers)
{
    // 包更新/重下载后同路径内容可能已变，先废缓存再解码，杜绝 stale 图
    s_tileImageCache.clear();
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
    // 布局/列数变化会使瓦片索引映射失效，先整体清空再按新映射重建
    clearTiles();

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

    const qreal scrollY = scrollPos().y();
    const qreal viewH = viewContentsRect().height();

    const int rowMin = qMax(0, int(qFloor(scrollY / STEP)));
    const int rowMax = qMin(m_rows - 1, int(qCeil((scrollY + viewH) / STEP)));
    const int idxMin = rowMin * m_cols;
    const int idxMax = qMin(m_items.size() - 1, rowMax * m_cols + (m_cols - 1));

    // 原位复用：只删移出可视区的瓦片
    auto it = m_visibleTiles.begin();
    while (it != m_visibleTiles.end()) {
        if (it.key() < idxMin || it.key() > idxMax) {
            delete it.value();
            it = m_visibleTiles.erase(it);
        } else {
            ++it;
        }
    }

    // 补建新进入可视区的瓦片（图片走缓存，不再逐帧磁盘解码）
    for (int idx = idxMin; idx <= idxMax; ++idx) {
        if (m_visibleTiles.contains(idx))
            continue;
        const int row = idx / m_cols;
        const int col = idx % m_cols;
        auto* tile = new StickerTileItem(m_contentView);
        tile->setX(col * STEP);
        tile->setY(row * STEP);
        tile->setStickerData(m_items[idx]);
        m_visibleTiles.insert(idx, tile);
    }
}

void StickerGridWidget::clearTiles()
{
    for (auto* tile : std::as_const(m_visibleTiles)) {
        delete tile;
    }
    m_visibleTiles.clear();
}

int StickerGridWidget::indexAt(const QPointF& contentPos) const
{
    if (m_cols <= 0) return -1;
    const int col = int(qFloor(contentPos.x() / STEP));
    const int row = int(qFloor(contentPos.y() / STEP));
    // 点击落在瓦片间 gap（右侧/下方 10px 空白）→ 未命中任何贴纸
    const qreal inCellX = contentPos.x() - col * STEP;
    const qreal inCellY = contentPos.y() - row * STEP;
    if (inCellX >= TILE_SIZE || inCellY >= TILE_SIZE)
        return -1;
    const int idx = row * m_cols + col;
    if (idx < 0 || idx >= m_items.size())
        return -1;
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