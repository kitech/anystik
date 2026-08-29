#include "stickerpreviewoverlay.h"
#include "androidutils.h"

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

// ═══════════════════════════════════════════════════════════════════
// StickerPreviewNode
// ═══════════════════════════════════════════════════════════════════

void StickerPreviewNode::setData(const QString& emoji, const QImage& image,
                                 const QSizeF& size)
{
    m_emoji = emoji;
    m_image = image;
    m_size = size;
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

    if (m_image.isNull()) {
        return;
    }

    // ── 图片 contain 居中 ──
    const qreal availW = w - 32;
    const qreal availH = h - ACTION_H - 64;
    const QSizeF imgSize = m_image.size();
    qreal scale = qMin(availW / imgSize.width(), availH / imgSize.height());
    const qreal dw = imgSize.width()  * scale;
    const qreal dh = imgSize.height() * scale;
    const QRectF dst((w - dw) / 2, (h - dh) / 2 - ACTION_H / 2, dw, dh);

    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->drawImage(dst, m_image);

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

    // ── 底部动作栏 ──
    const qreal barY = h - ACTION_H;
    QRectF barRect(0, barY, w, ACTION_H);
    painter->fillRect(barRect, QColor(20, 20, 28));
    painter->setPen(QColor(40, 40, 50));
    painter->drawLine(0, barY, w, barY);

    QFont actionFont;
    actionFont.setPixelSize(15);
    painter->setFont(actionFont);
    painter->setPen(QColor(110, 190, 255));
    painter->drawText(QRectF(0, barY, w * 0.5, ACTION_H),
        Qt::AlignCenter, QString::fromUtf8("复制"));

    if (!m_emoji.isEmpty()) {
        painter->drawText(QRectF(w * 0.5, barY, w * 0.5, ACTION_H),
            Qt::AlignCenter, QString::fromUtf8("Emoji: ") + m_emoji);
    }
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

void StickerPreviewOverlay::show(const StickerBrief& brief)
{
    m_brief = brief;
    m_emoji = brief.emoji;

    // 动图：QMovie 播放；静态图：QImage 一次加载
    delete m_movie;
    m_movie = nullptr;
    m_redrawTimer.stop();

    const QString filePath = brief.filePath;
    if (QImageReader::imageFormat(filePath).toLower() == "gif") {
        m_movie = new QMovie(filePath);
        connect(m_movie, &QMovie::frameChanged, this, [this]() {
            m_dirty = true;
            update();
        });
        if (m_movie->isValid()) {
            m_movie->start();
            m_image = m_movie->currentImage();
            m_redrawTimer.start();
        } else {
            delete m_movie;
            m_movie = nullptr;
        }
    }

    if (!m_movie) {
        QImageReader reader(filePath);
        reader.setAutoTransform(true);
        m_image = reader.read();
    }

    if (auto* p = parentItem()) {
        setSize(p->size());
        setPosition(QPointF(0, 0));
    }

    setVisible(true);
    m_dirty = true;
    update();
}

void StickerPreviewOverlay::triggerRepaint()
{
    m_dirty = true;
    update();
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
        node->setData(m_emoji, m_image, size());
        node->triggerUpdate(window(),
            QRectF(QPointF(0, 0), size()), size());
        m_dirty = false;
    }
    return node;
}

void StickerPreviewOverlay::touchEvent(QTouchEvent* event)
{
    if (event->type() == QEvent::TouchBegin && !event->points().isEmpty()) {
        handlePress(event->points().first().scenePosition());
        event->accept();
    } else {
        QQuickItem::touchEvent(event);
    }
}

void StickerPreviewOverlay::mousePressEvent(QMouseEvent* event)
{
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
    if (closeBtn.contains(event->position()) || event->position().y() >= height() - ACTION_H) {
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

    // 底部动作栏：复制
    if (localPos.y() >= height() - ACTION_H) {
        StickerStore::instance()->touchSticker(m_brief.id);
        bool ok = StickerStore::instance()->copyStickerToClipboard(m_brief.filePath);
        showAndroidToast(ok ? QString::fromUtf8("已复制到剪贴板")
                            : QString::fromUtf8("复制失败"));
        return;
    }

    // 背景点击 → 关闭
    Q_EMIT closed();
    deleteLater();
}

#include "moc_stickerpreviewoverlay.cpp"