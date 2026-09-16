#ifndef IMAGE_SEARCH_POPUP_H
#define IMAGE_SEARCH_POPUP_H

#include <QskPopup.h>
#include <QString>

class QskBox;
class QskLinearBox;
class QskProgressBar;
class QskTextLabel;
class QskPushButton;

/*
 * 搜索相似 · 上传浮动层（1/3 高度，水平垂直居中，面板 0.9 透明圆角）：
 *   - 进度条 + 状态行（显示图床 host 与失败切换文案）
 *   - 成功：显示上传直链 + 「复制链接」按钮（打开系统浏览器由宿主触发）
 *   - 失败：红色状态；右上角 ✕ 与 ESC 皆可随时关闭
 */
class ImageSearchPopup : public QskPopup
{
    Q_OBJECT
public:
    explicit ImageSearchPopup(QQuickItem* parent = nullptr);
    ~ImageSearchPopup() override;

    void resetForRun(const QString& fileName, const QString& engineName);
    void setProgress(int percent, qint64 bytesSent, qint64 bytesTotal,
                     const QString& status);
    void setUploadedUrl(const QString& url);
    void setFailed(const QString& reason);

protected:
    void updateLayout() override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    void updateGeometry();

    QskBox* m_panel = nullptr;
    QskLinearBox* m_layout = nullptr;
    QskProgressBar* m_progressBar = nullptr;
    QskTextLabel* m_pctLabel = nullptr;
    QskTextLabel* m_bytesLabel = nullptr;
    QskTextLabel* m_fileLabel = nullptr;
    QskTextLabel* m_statusLabel = nullptr;
    QskTextLabel* m_urlLabel = nullptr;
    QskPushButton* m_copyBtn = nullptr;
    QskPushButton* m_closeBtn = nullptr;
    QskPushButton* m_cornerCloseBtn = nullptr;
    bool m_escFilterInstalled = false;
};

#endif // IMAGE_SEARCH_POPUP_H