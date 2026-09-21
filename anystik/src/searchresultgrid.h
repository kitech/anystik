#ifndef SEARCH_RESULT_GRID_H
#define SEARCH_RESULT_GRID_H

#include "myscrollarea.h"
#include <QskPaintedNode.h>
#include <QskPopup.h>
#include <QQuickItem>
#include <QImage>
#include <QMap>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QPointer>
#include <QNetworkAccessManager>

class QNetworkAccessManager;
class QNetworkReply;
class QQuickWindow;

// 远程图片结果网格 + 点击预览弹窗（「在线表情」页 Bing/Yandex 图片搜索展示用）：
// 结果原图 URL 列表 → 虚拟化瓦片网格，瓦片异步拉取并缩略解码（GIF 取首帧），
// 内存缓存 URL→QImage；点击发射 resultClicked，由宿主打开 RemoteImagePreview 预览（不导入）。

class SearchResultTileNode : public QskPaintedNode
{
public:
    void setData(const QImage& image, bool failed, bool loading);
    void triggerUpdate(QQuickWindow* window, const QRectF& rect, const QSizeF& size);
    void paint(QPainter*, const QSize&, const void*) override;
    QskHashValue hash(const void*) const override;

private:
    QImage m_image;
    bool m_failed = false;
    bool m_loading = true;
};

class SearchResultTileItem : public QQuickItem
{
public:
    SearchResultTileItem(class SearchResultGrid* grid, QQuickItem* parent = nullptr);

    void setUrl(const QString& url);
    const QString& url() const { return m_url; }

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;

private:
    class SearchResultGrid* m_grid = nullptr;
    QString m_url;
    bool m_dirty = true;
};

class SearchResultGrid : public MyScrollArea
{
    Q_OBJECT
public:
    friend class SearchResultTileItem;
    explicit SearchResultGrid(QQuickItem* parent = nullptr);
    ~SearchResultGrid() override;

    void setResults(const QStringList& urls);
    void clear();

    QImage imageFor(const QString& url) const;
    bool isFailed(const QString& url) const;

Q_SIGNALS:
    void resultClicked(const QString& url);
    // 某 URL 的下载/解码已结束（成功或失败），瓦片据此重绘
    void imageReady(const QString& url);

protected:
    void geometryChangeEvent(QskGeometryChangeEvent*) override;

private:
    void ensureLoaded(const QString& url);
    void handleReplyFinished(const QString& url);
    void updateVisibleRows();
    void clearTiles();
    int indexAt(const QPointF& contentPos) const;
    void relayoutContent();
    void initNam();

    QQuickItem* m_contentView = nullptr;
    QStringList m_items;
    QMap<int, SearchResultTileItem*> m_visibleTiles;
    int m_cols = 3;
    int m_rows = 0;

    QNetworkAccessManager* m_nam = nullptr;
    QHash<QString, QImage> m_cache;
    QSet<QString> m_inFlight;
    QSet<QString> m_failed;
    QHash<QString, QNetworkReply*> m_replies;
};

// 结果预览弹窗：拉取原始 URL 字节，QskGraphicLabel 等比预览（不导入），仅静态首帧。
class RemoteImagePreview : public QskPopup
{
    Q_OBJECT
public:
    explicit RemoteImagePreview(QQuickItem* parent = nullptr);
    ~RemoteImagePreview() override;

    void showImage(const QString& url);

protected:
    void updateLayout() override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    void updateGeometry();
    void startFetch(const QString& url);

    class QskBox* m_panel = nullptr;
    class QskLinearBox* m_layout = nullptr;
    class QskGraphicLabel* m_imageLabel = nullptr;
    class QskTextLabel* m_statusLabel = nullptr;
    class QskPushButton* m_closeBtn = nullptr;
    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;
    bool m_escFilterInstalled = false;
};

#endif // SEARCH_RESULT_GRID_H