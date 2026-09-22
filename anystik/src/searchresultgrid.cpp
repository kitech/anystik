#include "searchresultgrid.h"
#include "httpua.h"

#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QImageReader>
#include <QBuffer>
#include <QTimer>
#include <QtMath>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QKeyEvent>
#include <QGuiApplication>
#include <utility>

#include <QskBox.h>
#include <QskBoxShapeMetrics.h>
#include <QskEvent.h>
#include <QskFontRole.h>
#include <QskGraphic.h>
#include <QskGraphicLabel.h>
#include <QskLinearBox.h>
#include <QskPushButton.h>
#include <QskTextLabel.h>

#include <private/qquicktaphandler_p.h>

static constexpr qreal SEARCH_TILE_SIZE = 96;
static constexpr qreal SEARCH_TILE_GAP  = 10;
static constexpr qreal SEARCH_STEP      = SEARCH_TILE_SIZE + SEARCH_TILE_GAP;

// ═══════════════════════════════════════════════════════════════════
// SearchResultTileNode — 单个结果瓦片绘制
// ═══════════════════════════════════════════════════════════════════

void SearchResultTileNode::setData(const QImage& image, bool failed, bool loading)
{
    m_image = image;
    m_failed = failed;
    m_loading = loading;
}

void SearchResultTileNode::triggerUpdate(QQuickWindow* window, const QRectF& rect,
                                         const QSizeF& size)
{
    update(window, rect, size, nullptr);
}

void SearchResultTileNode::paint(QPainter* painter, const QSize& size, const void*)
{
    const qreal w = size.width();
    const qreal h = size.height();

    painter->setRenderHint(QPainter::Antialiasing, true);

    const qreal radius = 10;
    QPainterPath clip;
    clip.addRoundedRect(0, 0, w, h, radius, radius);
    painter->setClipPath(clip);

    painter->fillPath(clip, QColor("#1e1e34"));

    if (m_image.isNull()) {
        painter->setPen(QColor("#777"));
        QFont f;
        f.setPixelSize(14);
        painter->setFont(f);
        painter->drawText(QRectF(0, 0, w, h), Qt::AlignCenter,
                          m_loading ? QStringLiteral("…") : QStringLiteral("×"));
    } else {
        const qreal inner = SEARCH_TILE_SIZE - 12;
        const QSizeF imgSize = m_image.size();
        qreal scale = qMin(inner / imgSize.width(), inner / imgSize.height());
        const qreal dw = imgSize.width()  * scale;
        const qreal dh = imgSize.height() * scale;
        const QRectF dst((w - dw) / 2, (h - dh) / 2, dw, dh);

        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawImage(dst, m_image);
    }

    QPen pen(QColor("#2a2a42"));
    pen.setWidth(1);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(QRectF(0.5, 0.5, w - 1, h - 1), radius, radius);
}

QskHashValue SearchResultTileNode::hash(const void*) const
{
    return qHash(m_image.cacheKey()) ^ (m_failed ? 1 : 0)
        ^ (m_loading ? 2 : 0);
}

// ═══════════════════════════════════════════════════════════════════
// SearchResultTileItem — QQuickItem 承载一张远程结果
// ═══════════════════════════════════════════════════════════════════

SearchResultTileItem::SearchResultTileItem(SearchResultGrid* grid, QQuickItem* parent)
    : QQuickItem(parent)
    , m_grid(grid)
{
    setFlag(ItemHasContents, true);
    setSize(QSizeF(SEARCH_TILE_SIZE, SEARCH_TILE_SIZE));

    if (m_grid) {
        connect(m_grid, &SearchResultGrid::imageReady, this,
            [this](const QString& url) {
                if (url == m_url) {
                    m_dirty = true;
                    update();
                }
            });
    }
}

void SearchResultTileItem::setUrl(const QString& url)
{
    m_url = url;
    m_dirty = true;
    if (m_grid) {
        m_grid->ensureLoaded(url);
    }
    update();
}

QSGNode* SearchResultTileItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    auto* node = static_cast<SearchResultTileNode*>(oldNode);
    if (!node) {
        node = new SearchResultTileNode();
    }
    if (m_dirty) {
        const QImage img = m_grid ? m_grid->imageFor(m_url) : QImage();
        const bool failed = m_grid && m_grid->isFailed(m_url);
        node->setData(img, failed, img.isNull() && !failed);
        m_dirty = false;
    }
    node->triggerUpdate(window(),
        QRectF(QPointF(0, 0), QSizeF(width(), height())),
        QSizeF(width(), height()));
    return node;
}

// ═══════════════════════════════════════════════════════════════════
// SearchResultGrid — 虚拟网格（异步拉缩略图）
// ═══════════════════════════════════════════════════════════════════

SearchResultGrid::SearchResultGrid(QQuickItem* parent)
    : MyScrollArea(parent)
{
    setFlickableOrientations(Qt::Vertical);
    setItemResizable(false);

    m_contentView = new QQuickItem(this);
    setScrolledItem(m_contentView);

    m_nam = new QNetworkAccessManager(this);
    m_nam->setTransferTimeout(20000);

    auto* tapHandler = new QQuickTapHandler(m_contentView);
    tapHandler->setGesturePolicy(QQuickTapHandler::DragThreshold);
    connect(tapHandler, &QQuickTapHandler::singleTapped, this,
        [this](QEventPoint point, Qt::MouseButton) {
            const int idx = indexAt(point.position());
            if (idx >= 0 && idx < m_items.size()) {
                Q_EMIT resultClicked(m_items.at(idx));
            }
        });

    connect(this, &QskScrollBox::scrollPosChanged,
        this, &SearchResultGrid::updateVisibleRows);
}

SearchResultGrid::~SearchResultGrid()
{
    clearTiles();
    for (auto* reply : std::as_const(m_replies)) {
        reply->abort();
        reply->disconnect();
        reply->deleteLater();
    }
    m_replies.clear();
}

void SearchResultGrid::setResults(const QStringList& urls)
{
    for (auto* reply : std::as_const(m_replies)) {
        reply->abort();
        reply->disconnect();
        reply->deleteLater();
    }
    m_replies.clear();
    m_inFlight.clear();
    m_cache.clear();
    m_failed.clear();

    m_items = urls;
    if (m_items.size() > 100) {
        m_items = m_items.mid(0, 100);
    }
    m_cols = qMax(1, int(viewContentsRect().width()) / int(SEARCH_STEP + 0.5));
    relayoutContent();
}

void SearchResultGrid::clear()
{
    for (auto* reply : std::as_const(m_replies)) {
        reply->abort();
        reply->disconnect();
        reply->deleteLater();
    }
    m_replies.clear();
    m_inFlight.clear();
    m_cache.clear();
    m_failed.clear();
    m_items.clear();
    clearTiles();
    m_contentView->setSize(QSizeF(0, 0));
    update();
}

QImage SearchResultGrid::imageFor(const QString& url) const
{
    return m_cache.value(url);
}

bool SearchResultGrid::isFailed(const QString& url) const
{
    return m_failed.contains(url);
}

void SearchResultGrid::ensureLoaded(const QString& url)
{
    if (url.isEmpty()
        || m_cache.contains(url)
        || m_failed.contains(url)
        || m_inFlight.contains(url)) {
        return;
    }
    m_inFlight.insert(url);

    QNetworkRequest req{ QUrl(url) };
    req.setRawHeader("User-Agent", kHttpUserAgent());
    QNetworkReply* reply = m_nam->get(req);
    m_replies.insert(url, reply);
    connect(reply, &QNetworkReply::finished, this,
        [this, url]() { handleReplyFinished(url); });
}

void SearchResultGrid::handleReplyFinished(const QString& url)
{
    QNetworkReply* reply = m_replies.take(url);
    m_inFlight.remove(url);

    if (reply) {
        const QByteArray data = reply->readAll();
        if (reply->error() == QNetworkReply::NoError && !data.isEmpty()) {
            QBuffer buf(const_cast<QByteArray*>(&data));
            buf.open(QIODevice::ReadOnly);
            QImageReader reader(&buf);
            reader.setAutoTransform(true);
            const QSize srcSize = reader.size();
            const int maxDim = int(SEARCH_TILE_SIZE * 2);
            if (srcSize.isValid()
                && (srcSize.width() > maxDim || srcSize.height() > maxDim)) {
                reader.setScaledSize(
                    srcSize.scaled(QSize(maxDim, maxDim), Qt::KeepAspectRatio));
            }
            QImage img = reader.read();
            if (!img.isNull()) {
                m_cache.insert(url, img);
                if (m_cache.size() > 300) {
                    m_cache.clear();
                }
            } else {
                m_failed.insert(url);
            }
        } else {
            m_failed.insert(url);
        }
        reply->disconnect();
        reply->deleteLater();
    }

    Q_EMIT imageReady(url);
}

void SearchResultGrid::relayoutContent()
{
    clearTiles();

    if (m_cols <= 0) {
        m_cols = 3;
    }
    m_rows = (m_items.size() + m_cols - 1) / m_cols;

    qreal viewW = viewContentsRect().width();
    if (viewW <= 0) {
        viewW = width();
    }
    m_contentView->setSize(QSizeF(viewW, m_rows * SEARCH_STEP));
    update();
    updateVisibleRows();
}

void SearchResultGrid::updateVisibleRows()
{
    if (!m_contentView || m_items.isEmpty()) {
        return;
    }

    const qreal scrollY = scrollPos().y();
    const qreal viewH = viewContentsRect().height();

    const int rowMin = qMax(0, int(qFloor(scrollY / SEARCH_STEP)));
    const int rowMax = qMin(m_rows - 1, int(qCeil((scrollY + viewH) / SEARCH_STEP)));
    const int idxMin = rowMin * m_cols;
    const int idxMax = qMin(m_items.size() - 1,
                            rowMax * m_cols + (m_cols - 1));

    auto it = m_visibleTiles.begin();
    while (it != m_visibleTiles.end()) {
        if (it.key() < idxMin || it.key() > idxMax) {
            delete it.value();
            it = m_visibleTiles.erase(it);
        } else {
            ++it;
        }
    }

    for (int idx = idxMin; idx <= idxMax; ++idx) {
        if (m_visibleTiles.contains(idx)) {
            continue;
        }
        const int row = idx / m_cols;
        const int col = idx % m_cols;
        auto* tile = new SearchResultTileItem(this, m_contentView);
        tile->setX(col * SEARCH_STEP);
        tile->setY(row * SEARCH_STEP);
        tile->setUrl(m_items.at(idx));
        m_visibleTiles.insert(idx, tile);
    }
}

void SearchResultGrid::clearTiles()
{
    for (auto* tile : std::as_const(m_visibleTiles)) {
        delete tile;
    }
    m_visibleTiles.clear();
}

int SearchResultGrid::indexAt(const QPointF& contentPos) const
{
    if (m_cols <= 0) {
        return -1;
    }
    const int col = int(qFloor(contentPos.x() / SEARCH_STEP));
    const int row = int(qFloor(contentPos.y() / SEARCH_STEP));
    const qreal inCellX = contentPos.x() - col * SEARCH_STEP;
    const qreal inCellY = contentPos.y() - row * SEARCH_STEP;
    if (inCellX >= SEARCH_TILE_SIZE || inCellY >= SEARCH_TILE_SIZE) {
        return -1;
    }
    const int idx = row * m_cols + col;
    if (idx < 0 || idx >= m_items.size()) {
        return -1;
    }
    return idx;
}

void SearchResultGrid::geometryChangeEvent(QskGeometryChangeEvent* event)
{
    QskScrollArea::geometryChangeEvent(event);
    if (event->isResized() && m_contentView) {
        m_cols = qMax(1, int(viewContentsRect().width()) / int(SEARCH_STEP + 0.5));
        relayoutContent();
    }
}

// ═══════════════════════════════════════════════════════════════════
// RemoteImagePreview — 结果页内预览弹窗（不导入）
// ═══════════════════════════════════════════════════════════════════

namespace {

QRectF previewParentRect(QQuickItem* parent)
{
    if (!parent) {
        return {};
    }
    if (auto* w = parent->window()) {
        return QRectF(QPointF(), w->size());
    }
    return QRectF(-parent->x(), -parent->y(),
                  parent->width(), parent->height());
}

} // namespace

RemoteImagePreview::RemoteImagePreview(QQuickItem* parent)
    : QskPopup(parent)
{
    setModal(true);
    setOverlay(true);
    setPopupFlag(QskPopup::DeleteOnClose, false);
    setPolishOnResize(true);
    setPolishOnParentResize(true);

    m_nam.setTransferTimeout(30000);

    m_panel = new QskBox(this);
    m_panel->setBoxShapeHint(QskBox::Panel,
        QskBoxShapeMetrics(16, Qt::AbsoluteSize));

    m_layout = new QskLinearBox(Qt::Vertical, m_panel);
    m_layout->setMargins(16);
    m_layout->setSpacing(10);

    auto* titleRow = new QskLinearBox(Qt::Horizontal, m_layout);
    auto* title = new QskTextLabel(tr("预览"), titleRow);
    title->setFontRole(QskFontRole::Title);
    title->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    title->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    m_closeBtn = new QskPushButton(QStringLiteral("✕"), titleRow);
    m_closeBtn->setPreferredSize(44, 44);
    connect(m_closeBtn, &QskAbstractButton::clicked, this, &QskPopup::close);

    m_statusLabel = new QskTextLabel(tr("加载中…"), m_layout);
    m_statusLabel->setFontRole(QskFontRole::Caption);

    m_imageLabel = new QskGraphicLabel(m_layout);
    m_imageLabel->setFillMode(QskGraphicLabel::PreserveAspectFit);
    m_imageLabel->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Expanding);
    m_imageLabel->setPanel(true);

    connect(this, &QskPopup::opened, this, [this]() {
        if (m_escFilterInstalled) {
            return;
        }
        if (auto* w = window()) {
            w->installEventFilter(this);
            m_escFilterInstalled = true;
        }
    });

    QTimer::singleShot(0, this, [this]() { updateGeometry(); });
}

RemoteImagePreview::~RemoteImagePreview()
{
    if (m_reply) {
        m_reply->abort();
    }
    if (m_escFilterInstalled) {
        if (auto* w = window()) {
            w->removeEventFilter(this);
        }
        m_escFilterInstalled = false;
    }
}

bool RemoteImagePreview::eventFilter(QObject*, QEvent* ev)
{
    if (ev->type() == QEvent::KeyPress
        && static_cast<QKeyEvent*>(ev)->key() == Qt::Key_Escape) {
        close();
        return true;
    }
    return false;
}

void RemoteImagePreview::showImage(const QString& url)
{
    if (m_reply) {
        m_reply->abort();
        m_reply = nullptr;
    }
    m_imageLabel->setGraphic(QskGraphic());
    m_statusLabel->setTextColor(QColor(100, 180, 255));
    m_statusLabel->setText(tr("加载中…"));
    startFetch(url);
    open();
}

void RemoteImagePreview::startFetch(const QString& url)
{
    QNetworkRequest req{ QUrl(url) };
    req.setRawHeader("User-Agent", kHttpUserAgent());
    QNetworkReply* reply = m_nam.get(req);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        if (m_reply != reply) {
            reply->deleteLater();
            return;
        }
        const QByteArray data = reply->readAll();
        QImage img;
        if (reply->error() == QNetworkReply::NoError) {
            img = QImage::fromData(data);
        }
        if (img.isNull()) {
            m_statusLabel->setTextColor(QColor(255, 91, 91));
            m_statusLabel->setText(tr("图片加载失败"));
        } else {
            m_imageLabel->setGraphic(QskGraphic::fromImage(img));
            m_statusLabel->setText(QString());
        }
        reply->deleteLater();
        m_reply = nullptr;
        Q_UNUSED(url)
    });
}

void RemoteImagePreview::updateLayout()
{
    updateGeometry();
    m_layout->setGeometry(layoutRect());
}

void RemoteImagePreview::updateGeometry()
{
    const auto parentRect = previewParentRect(parentItem());
    if (parentRect.isEmpty()) {
        return;
    }

    const qreal maxW = 0.92 * parentRect.width();
    const qreal maxH = 0.8 * parentRect.height();

    const qreal panelW = qMax(260.0, maxW);
    const qreal panelH = qMax(260.0, maxH);

    QRectF r(0, 0, panelW, panelH);
    r.moveCenter(parentRect.center());
    setGeometry(r);
    m_panel->setGeometry(r.translated(-r.topLeft()));
}

#include "moc_searchresultgrid.cpp"