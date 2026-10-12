#ifndef PHOTOVIEWER_H
#define PHOTOVIEWER_H

#include "compat34.h"
#include <qdialog.h>

class QPixmap;
class QLabel;

class PhotoCanvas : public QWidget {
    Q_OBJECT
public:
    PhotoCanvas(QWidget* parent, const QPixmap& pixmap);

    void zoomIn();
    void zoomOut();
    void panBy(int dx, int dy);
    void fitToWindow();
    void actualSize();
    void toggleFitMode();
    void rotateCW();
    void rotateCCW();
    void setShowHelp(bool show);
    bool showHelp() const { return m_showHelp; }
    const QPixmap& pixmap() const { return m_pixmap; }
    int zoomPercent() const;
    QSize displayedSize() const;

signals:
    void viewChanged();

protected:
    void paintEvent(QPaintEvent* event);
    void wheelEvent(QWheelEvent* event);
    void mousePressEvent(QMouseEvent* event);
    void mouseMoveEvent(QMouseEvent* event);
    void mouseReleaseEvent(QMouseEvent* event);
    void mouseDoubleClickEvent(QMouseEvent* event);
    void resizeEvent(QResizeEvent* event);

private:
    void centerImage();
    void updateCursor();
    void rebuildCache();

    QPixmap m_pixmap;
    double m_scale;
    double m_rotation;
    double m_offX;
    double m_offY;
    bool m_fitMode;
    bool m_showHelp;
    bool m_dragging;
    QPoint m_dragStart;
    double m_dragOffX;
    double m_dragOffY;
    QPixmap m_cachedPixmap; // 按当前 scale/rotation 渲染好的离线缓冲图，拖动只改 offset 不复算变换
    QPixmap m_doubleBuffer; // Qt3 双缓冲：全尺寸背景+缓存图，避免 fill+draw 闪烁
};

class PhotoViewer : public QDialog {
    Q_OBJECT
public:
    // origData: 原始压缩文件字节（JPEG/PNG），供剪贴板直传原文件；空则回退像素模式
    // suggestUrl: 原媒体 URL，用于另存对话框预填文件名
    PhotoViewer(QWidget* parent, const QPixmap& pixmap,
                const QByteArray& origData = QByteArray(),
                const QString& suggestUrl = QString());
    ~PhotoViewer();

protected:
    void keyPressEvent(QKeyEvent* event);
    void closeEvent(QCloseEvent* event);

private slots:
    void onSave();
    void onCopy();
    void onZoomIn();
    void onZoomOut();
    void onFitWindow();
    void onActualSize();
    void onRotateCW();
    void onRotateCCW();
    void onFullscreen();
    void onToggleHelp();
    void onClose();
    void onViewChanged();

private:
    void setupToolbar(QVBoxLayout* lay);
    void updateTitle();
    void updateStatus();
    QString defaultPhotoName() const;

    PhotoCanvas* m_canvas;
    QWidget* m_toolbar;
    QWidget* m_statusBar;
    QLabel* m_statusLabel;
    bool m_fullscreen;
    int m_savedX;
#ifndef QT3_BUILD
    Qt::WindowFlags m_savedFlags;
    QByteArray m_savedGeo;
#endif
    int m_savedY;
    int m_savedW;
    int m_savedH;
    QPixmap m_origPixmap;
    QByteArray m_origData;   // 原始文件字节（可为空）
    QString m_origMime;      // 魔数探测结果："image/png"/"image/jpeg"/"image/webp"/""
    QString m_suggestUrl;    // 建议文件名来源（原媒体 URL），另存对话框预填用
};

#endif
