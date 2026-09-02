#include "msgenlargeoverlay.h"
#include "androidutils.h"
#include "toastpopup.h"

#include <QGuiApplication>
#include <QClipboard>
#include <QFont>
#include <QFontMetrics>
#include <QTextLayout>
#include <QPainter>
#include <QPainterPath>
#include <QEvent>
#include <QTouchEvent>
#include <QMouseEvent>
#include <QCursor>
#include <QSGSimpleRectNode>

static constexpr int FONT_SIZES[] = {18, 24, 32, 40};
static const char* SIZE_LABELS[] = {"S", "M", "L", "XL"};

static constexpr qreal CLOSE_BTN_SIZE = 40;
static constexpr qreal BAR_H = 44;
static constexpr qreal PAD = 20;
static constexpr qreal BTN_H = 36;

struct LayoutRects {
    QRectF closeBtn;
    QRectF senderLabel;
    QRectF timeLabel;
    QRectF textArea;
    QRectF copyBtn;
    QRectF favBtn;
    QRectF sizeBtns[4];
    qreal contentX;
    qreal contentW;
};

static LayoutRects computeLayout(const QSizeF& size)
{
    LayoutRects r{};
    const qreal w = size.width();
    const qreal h = size.height();
    r.contentW = qMin(w - PAD * 2, 500.0);
    r.contentX = (w - r.contentW) / 2;

    r.closeBtn = QRectF(w - CLOSE_BTN_SIZE - 8, 8, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE);
    r.senderLabel = QRectF(r.contentX + 10, PAD + 8, r.contentW - 20, 20);
    r.timeLabel = QRectF(r.contentX + r.contentW - 60, PAD + 8, 50, 20);
    r.textArea = QRectF(r.contentX + 10, PAD + 32, r.contentW - 20, h - BAR_H - PAD * 2 - 60);

    const qreal barY = h - BAR_H - PAD;
    const qreal btnW = 60;
    r.copyBtn = QRectF(r.contentX + 10, barY, btnW, BTN_H);
    r.favBtn = QRectF(r.contentX + 10 + btnW + 10, barY, btnW, BTN_H);

    const qreal sizeBtnW = 30;
    qreal sizeX = r.contentX + r.contentW - (sizeBtnW + 5) * 4;
    for (int i = 0; i < 4; i++) {
        r.sizeBtns[i] = QRectF(sizeX + (sizeBtnW + 5) * i, barY, sizeBtnW, BTN_H);
    }

    return r;
}

static int hitTestPos(const QPointF& pos, const QSizeF& size, const QRectF& textArea)
{
    auto r = computeLayout(size);
    if (r.closeBtn.contains(pos)) {
        return MsgEnlargeOverlayNode::BTN_CLOSE;
    }
    if (r.copyBtn.contains(pos)) {
        return MsgEnlargeOverlayNode::BTN_COPY;
    }
    if (r.favBtn.contains(pos)) {
        return MsgEnlargeOverlayNode::BTN_FAV;
    }
    if (textArea.contains(pos)) {
        return MsgEnlargeOverlayNode::BTN_TEXT;
    }
    for (int i = 0; i < 4; i++) {
        if (r.sizeBtns[i].contains(pos)) {
            return MsgEnlargeOverlayNode::BTN_SIZE_S + i;
        }
    }
    return -1;
}

// ── MsgEnlargeOverlayNode ──

void MsgEnlargeOverlayNode::setData(const MessageItem& item, int fontSizeIndex,
                                     const QSizeF& size, qreal scrollY,
                                     const QRectF& textAreaRect)
{
    m_item = item;
    m_fontSizeIndex = fontSizeIndex;
    m_size = size;
    m_scrollY = scrollY;
    m_textAreaRect = textAreaRect;
}

void MsgEnlargeOverlayNode::triggerUpdate(QQuickWindow* window, const QRectF& rect,
                                           const QSizeF& size)
{
    update(window, rect, size, nullptr);
}

void MsgEnlargeOverlayNode::paint(QPainter* painter, const QSize& size, const void*)
{
    painter->setRenderHint(QPainter::Antialiasing, true);
    const qreal w = size.width();
    const qreal h = size.height();
    auto layout = computeLayout(size);

    // Semi-transparent background
    painter->fillRect(0, 0, w, h, QColor(0, 0, 0, 180));

    // Content card background
    QRectF cardRect(layout.contentX, PAD - 5, layout.contentW, h - BAR_H - PAD + 5);
    QPainterPath cardPath;
    cardPath.addRoundedRect(cardRect, 12, 12);
    painter->fillPath(cardPath, QColor(40, 40, 50));

    // Close button
    QFont closeFont;
    closeFont.setPixelSize(22);
    painter->setFont(closeFont);
    painter->setPen(QColor(200, 200, 200));
    painter->drawText(layout.closeBtn, Qt::AlignCenter, "✕");

    // Sender
    QFont senderFont;
    senderFont.setPixelSize(14);
    senderFont.setBold(true);
    painter->setFont(senderFont);
    painter->setPen(QColor(255, 255, 255));
    painter->drawText(layout.senderLabel, Qt::AlignLeft | Qt::AlignVCenter, m_item.sender);

    // Time
    QFont timeFont;
    timeFont.setPixelSize(12);
    painter->setFont(timeFont);
    painter->setPen(QColor(160, 160, 160));
    painter->drawText(layout.timeLabel, Qt::AlignRight | Qt::AlignVCenter, m_item.time);

    // Message text (scrollable)
    QFont msgFont;
    msgFont.setPixelSize(FONT_SIZES[m_fontSizeIndex]);
    painter->setFont(msgFont);
    painter->setPen(QColor(240, 240, 240));
    QTextOption textOption;
    textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

    painter->save();
    painter->setClipRect(m_textAreaRect);
    QRectF scrolledTextArea = m_textAreaRect;
    scrolledTextArea.moveTop(scrolledTextArea.top() - m_scrollY);
    painter->drawText(scrolledTextArea, m_item.content, textOption);
    painter->restore();

    // Action bar background
    QRectF barRect(layout.contentX, h - BAR_H - PAD - 5, layout.contentW, BAR_H + 10);
    QPainterPath barPath;
    barPath.addRoundedRect(barRect, 8, 8);
    painter->fillPath(barPath, QColor(50, 50, 60));

    // Action buttons
    QFont actionFont;
    actionFont.setPixelSize(13);
    painter->setFont(actionFont);

    auto paintBtn = [&](const QRectF& rect, const QString& text) {
        painter->setPen(QColor(100, 180, 255));
        painter->drawText(rect, Qt::AlignCenter, text);
    };

    paintBtn(layout.copyBtn, QString::fromUtf8("复制"));
    paintBtn(layout.favBtn, QString::fromUtf8("收藏"));

    // Size buttons
    for (int i = 0; i < 4; i++) {
        if (i == m_fontSizeIndex) {
            painter->setPen(QColor(255, 255, 255));
            painter->fillRect(layout.sizeBtns[i].adjusted(-2, -2, 2, 2), QColor(80, 80, 100));
        } else {
            painter->setPen(QColor(100, 180, 255));
        }
        painter->drawText(layout.sizeBtns[i], Qt::AlignCenter, SIZE_LABELS[i]);
    }
}

QskHashValue MsgEnlargeOverlayNode::hash(const void*) const
{
    return qHash(m_item.content) ^ qHash(m_fontSizeIndex) ^ qHash(int(m_size.width())) ^ qHash(int(m_size.height()));
}

// ── MsgEnlargeOverlay ──

MsgEnlargeOverlay::MsgEnlargeOverlay(QQuickItem* parent)
    : QQuickItem(parent)
{
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptTouchEvents(true);
    setAcceptHoverEvents(true);
    setZ(1000);
    setFlag(ItemHasContents, true);

    if (auto* p = parentItem()) {
        connect(p, &QQuickItem::widthChanged, this, [this]() {
            if (auto* p = parentItem()) {
                setWidth(p->width());
                recalcMaxScroll();
                m_dirty = true;
                update();
            }
        });
        connect(p, &QQuickItem::heightChanged, this, [this]() {
            if (auto* p = parentItem()) {
                setHeight(p->height());
                recalcMaxScroll();
                m_dirty = true;
                update();
            }
        });
    }
}

void MsgEnlargeOverlay::show(const MessageItem& item)
{
    m_item = item;
    m_dirty = true;
    m_scrollY = 0;
    m_ignoreFirstTouch = true;

    if (auto* p = parentItem()) {
        setSize(p->size());
        setPosition(QPointF(0, 0));

        // 计算文本实际高度，垂直居中
        const qreal h = p->height();
        const qreal w = p->width();
        const qreal contentW = qMin(w - PAD * 2, 500.0);
        const qreal contentX = (w - contentW) / 2;
        const qreal senderBottom = PAD + 32 + 20;
        const qreal actionBarTop = h - BAR_H - PAD;
        const qreal availableH = actionBarTop - senderBottom;

        QFont msgFont;
        msgFont.setPixelSize(FONT_SIZES[m_fontSizeIndex]);
        QTextOption textOption;
        textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        QTextLayout textLayout(m_item.content, msgFont);
        textLayout.setTextOption(textOption);
        textLayout.beginLayout();
        while (textLayout.createLine().isValid()) {}
        textLayout.endLayout();
        qreal textHeight = textLayout.boundingRect().height();
        qreal textAreaH = qMin(textHeight, availableH);
        qreal textAreaY = senderBottom + (availableH - textAreaH) / 2.0;

        m_textAreaRect = QRectF(contentX + 10, textAreaY, contentW - 20, textAreaH);
    }

    setVisible(true);
    recalcMaxScroll();
    update();
}

void MsgEnlargeOverlay::recalcMaxScroll()
{
    QFont msgFont;
    msgFont.setPixelSize(FONT_SIZES[m_fontSizeIndex]);
    QTextOption textOption;
    textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

    QTextLayout layout(m_item.content, msgFont);
    layout.setTextOption(textOption);
    layout.beginLayout();
    while (layout.createLine().isValid()) {}
    layout.endLayout();

    auto rect = m_textAreaRect;
    m_maxScrollY = qMax(0.0, layout.boundingRect().height() - rect.height());
    m_scrollY = qBound(0.0, m_scrollY, m_maxScrollY);
}

void MsgEnlargeOverlay::triggerRepaint()
{
    m_dirty = true;
    update();
}

QSGNode* MsgEnlargeOverlay::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    auto* node = static_cast<MsgEnlargeOverlayNode*>(oldNode);
    if (!node) {
        node = new MsgEnlargeOverlayNode();
    }
    if (m_dirty) {
        node->setData(m_item, m_fontSizeIndex, size(), m_scrollY, m_textAreaRect);
        node->triggerUpdate(window(),
            QRectF(QPointF(0, 0), size()), size());
        m_dirty = false;
    }
    return node;
}

void MsgEnlargeOverlay::touchEvent(QTouchEvent* event)
{
    if (event->type() == QEvent::TouchBegin) {
        if (m_ignoreFirstTouch) {
            m_ignoreFirstTouch = false;
            event->accept();
            return;
        }
        if (!event->points().isEmpty()) {
            auto& pt = event->points().first();
            QPointF localPos = mapFromScene(pt.scenePosition());
            int hit = hitTestPos(localPos, size(), m_textAreaRect);
            if (hit == MsgEnlargeOverlayNode::BTN_TEXT) {
                m_touchScrolling = true;
                m_touchStartY = pt.scenePosition().y();
                m_scrollStartY = m_scrollY;
            }
            handlePress(pt.scenePosition());
        }
        event->accept();
    } else if (event->type() == QEvent::TouchUpdate) {
        if (m_touchScrolling && !event->points().isEmpty()) {
            auto& pt = event->points().first();
            qreal dy = pt.scenePosition().y() - m_touchStartY;
            m_scrollY = qBound(0.0, m_scrollStartY - dy, m_maxScrollY);
            m_dirty = true;
            update();
        }
        event->accept();
    } else if (event->type() == QEvent::TouchEnd) {
        m_touchScrolling = false;
        event->accept();
    } else {
        QQuickItem::touchEvent(event);
    }
}

void MsgEnlargeOverlay::mousePressEvent(QMouseEvent* event)
{
    if (m_ignoreFirstTouch) {
        m_ignoreFirstTouch = false;
        event->accept();
        return;
    }
    handlePress(event->scenePosition());
    event->accept();
}

void MsgEnlargeOverlay::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        Q_EMIT closed();
        deleteLater();
        return;
    }
    QQuickItem::keyPressEvent(event);
}

void MsgEnlargeOverlay::hoverMoveEvent(QHoverEvent* event)
{
    int hit = hitTestPos(event->position(), size(), m_textAreaRect);
    if (hit == MsgEnlargeOverlayNode::BTN_CLOSE
        || hit == MsgEnlargeOverlayNode::BTN_COPY
        || hit == MsgEnlargeOverlayNode::BTN_FAV
        || (hit >= MsgEnlargeOverlayNode::BTN_SIZE_S
            && hit <= MsgEnlargeOverlayNode::BTN_SIZE_XL)) {
        setCursor(QCursor(Qt::PointingHandCursor));
    } else {
        unsetCursor();
    }
    QQuickItem::hoverMoveEvent(event);
}

void MsgEnlargeOverlay::handlePress(const QPointF& scenePos)
{
    QPointF localPos = mapFromScene(scenePos);
    int hit = hitTestPos(localPos, size(), m_textAreaRect);

    switch (hit) {
    case MsgEnlargeOverlayNode::BTN_CLOSE:
        Q_EMIT closed();
        deleteLater();
        return;
    case MsgEnlargeOverlayNode::BTN_COPY:
        QGuiApplication::clipboard()->setText(m_item.content);
        ToastPopup::show(this, QString::fromUtf8("已复制"));
        return;
    case MsgEnlargeOverlayNode::BTN_FAV:
        ToastPopup::show(this, QString::fromUtf8("收藏功能暂未实现"));
        return;
    case MsgEnlargeOverlayNode::BTN_TEXT:
        return;
    case MsgEnlargeOverlayNode::BTN_SIZE_S:
    case MsgEnlargeOverlayNode::BTN_SIZE_M:
    case MsgEnlargeOverlayNode::BTN_SIZE_L:
    case MsgEnlargeOverlayNode::BTN_SIZE_XL:
        m_fontSizeIndex = hit - MsgEnlargeOverlayNode::BTN_SIZE_S;
        recalcMaxScroll();
        triggerRepaint();
        return;
    default:
        break;
    }

    // Click on background -> close
    Q_EMIT closed();
    deleteLater();
}
