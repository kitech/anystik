#ifndef STICKER_LIST_H
#define STICKER_LIST_H

#include "stickerstore.h"
#include "myscrollarea.h"
#include <QskPaintedNode.h>
#include <QQuickItem>
#include <QColor>
#include <QVector>
#include <QMap>
#include <QPointF>
#include <QImage>

class StickerTileItem;
class QTimer;

class StickerTileNode : public QskPaintedNode
{
public:
    void setData(const StickerBrief& brief, const QImage& image,
                 bool highlighted);
    void triggerUpdate(QQuickWindow* window, const QRectF& rect, const QSizeF& size);
    void paint(QPainter*, const QSize&, const void*) override;
    QskHashValue hash(const void*) const override;

private:
    StickerBrief m_brief;
    QImage m_image;
    bool m_highlighted = false;
};

class StickerTileItem : public QQuickItem
{
public:
    StickerTileItem(QQuickItem* parent = nullptr);

    void setStickerData(const StickerBrief& brief);
    const StickerBrief& brief() const { return m_brief; }

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;

private:
    StickerBrief m_brief;
    QImage m_image;
    bool m_dirty = true;
    bool m_highlighted = false;
};

class StickerGridWidget : public MyScrollArea
{
    Q_OBJECT
public:
    StickerGridWidget(QQuickItem* parent = nullptr);
    ~StickerGridWidget() override;

    void setStickers(const QVector<StickerBrief>& stickers);
    const QVector<StickerBrief>& stickers() const { return m_items; }

    StickerBrief stickerAt(int index) const;

Q_SIGNALS:
    void stickerClicked(const StickerBrief& brief);
    void stickerDoubleClicked(const StickerBrief& brief);
    void stickerLongPressed(const StickerBrief& brief, const QPointF& scenePos);

protected:
    void geometryChangeEvent(QskGeometryChangeEvent*) override;

private:
    void updateVisibleRows();
    int indexAt(const QPointF& contentPos) const;
    void relayoutContent();

    QQuickItem* m_contentView = nullptr;
    QVector<StickerBrief> m_items;
    QMap<int, StickerTileItem*> m_visibleTiles;
    int m_cols = 4;
    int m_rows = 0;
};

#endif // STICKER_LIST_H