#ifndef MSG_ENLARGE_OVERLAY_H
#define MSG_ENLARGE_OVERLAY_H

#include "messagelist.h"
#include <QskPaintedNode.h>
#include <QQuickItem>
#include <QPointer>
#include <QRectF>

class MsgEnlargeOverlayNode : public QskPaintedNode
{
public:
    void setData(const MessageItem& item, int fontSizeIndex, const QSizeF& size,
                 qreal scrollY, const QRectF& textAreaRect);
    void triggerUpdate(QQuickWindow* window, const QRectF& rect, const QSizeF& size);
    void paint(QPainter*, const QSize&, const void*) override;
    QskHashValue hash(const void*) const override;

    static constexpr int BTN_CLOSE = 0;
    static constexpr int BTN_COPY = 1;
    static constexpr int BTN_FAV = 2;
    static constexpr int BTN_TEXT = 3;
    static constexpr int BTN_SIZE_S = 4;
    static constexpr int BTN_SIZE_M = 5;
    static constexpr int BTN_SIZE_L = 6;
    static constexpr int BTN_SIZE_XL = 7;

private:
    MessageItem m_item;
    int m_fontSizeIndex = 1;
    QSizeF m_size;
    qreal m_scrollY = 0;
    QRectF m_textAreaRect;
};

class MsgEnlargeOverlay : public QQuickItem
{
    Q_OBJECT
public:
    explicit MsgEnlargeOverlay(QQuickItem* parent = nullptr);

    void show(const MessageItem& item);

Q_SIGNALS:
    void closed();

protected:
    void touchEvent(QTouchEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;

private:
    void handlePress(const QPointF& scenePos);
    void triggerRepaint();
    void recalcMaxScroll();

    MessageItem m_item;
    int m_fontSizeIndex = 1;
    qreal m_scrollY = 0;
    qreal m_maxScrollY = 0;
    bool m_touchScrolling = false;
    qreal m_touchStartY = 0;
    qreal m_scrollStartY = 0;
    bool m_dirty = true;
    QRectF m_textAreaRect;
    bool m_ignoreFirstTouch = false;
};

#endif // MSG_ENLARGE_OVERLAY_H
