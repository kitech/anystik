#ifndef STICKER_PREVIEW_OVERLAY_H
#define STICKER_PREVIEW_OVERLAY_H

#include "stickerstore.h"
#include <QskPaintedNode.h>
#include <QQuickItem>
#include <QTimer>
#include <QImage>

class QMovie;
class QImageReader;

class StickerPreviewNode : public QskPaintedNode
{
public:
    void setData(const QString& emoji, const QImage& image,
                 const QSizeF& size, const QString& metaText = {});
    void triggerUpdate(QQuickWindow* window, const QRectF& rect, const QSizeF& size);
    void paint(QPainter*, const QSize&, const void*) override;
    QskHashValue hash(const void*) const override;

private:
    QString m_emoji;
    QImage m_image;
    QSizeF m_size;
    QString m_metaText;
};

class StickerPreviewOverlay : public QQuickItem
{
    Q_OBJECT
public:
    explicit StickerPreviewOverlay(QQuickItem* parent = nullptr);
    ~StickerPreviewOverlay() override;

    void show(const StickerBrief& brief);

Q_SIGNALS:
    void closed();
    void deleteRequested(const StickerBrief& brief);

protected:
    void touchEvent(QTouchEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;

private:
    void handlePress(const QPointF& scenePos);
    void triggerRepaint();
    bool metaRegionContains(const QPointF& localPos) const;

    StickerBrief m_brief;
    QImage m_image;
    QString m_emoji;
    QMovie* m_movie = nullptr;
    QImageReader* m_reader = nullptr;
    QTimer m_redrawTimer;
    QTimer m_animTimer;
    bool m_dirty = true;
    QString m_metaText;
};

#endif // STICKER_PREVIEW_OVERLAY_H