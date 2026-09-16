#ifndef SYNC_PROGRESS_POPUP_H
#define SYNC_PROGRESS_POPUP_H

#include <QskPopup.h>
#include <QString>
#include <QPointer>
#include <QTimer>
#include <QVector>

class QskProgressBar;
class QskTextLabel;
class QskLinearBox;
class QskPushButton;
class QskBox;
class LogListView;
class LogModel;
class SyncEngine;

/*
 * 同步进度浮动窗口（参照 showRenameDialog 的 QskPopup 弹层）：
 *   - 实时结构化日志：时间戳 / 级别(配色) / [tag] / 原文，自动滚底
 *   - 进度条（progressUpdated）与完成/失败/已取消摘要（finished）
 *   - 过滤：级别下拉 + 关键词(150ms 防抖，匹配 tag+line，大小写不敏感)
 *   - 复制当前过滤结果 / 清空日志
 *   - 运行中禁用「关闭」；「取消」调用 SyncEngine::abort()
 * 数据存于私有 LogModel 实例（不依赖全局日志，避免相互污染）。
 */
class SyncProgressPopup : public QskPopup
{
    Q_OBJECT
public:
    explicit SyncProgressPopup(SyncEngine* engine, QQuickItem* parent = nullptr);
    ~SyncProgressPopup() override;

    void clearLog();
    void resetForRun();

protected:
    void updateLayout() override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    void registerEngine(SyncEngine* engine);
    void applyFinished(int exitCode, const QString& summary);
    void updateGeometry();

    QPointer<SyncEngine> m_engine;
    LogModel* m_model = nullptr;
    QskBox* m_panel = nullptr;
    QskLinearBox* m_layout = nullptr;
    LogListView* m_logList = nullptr;

    QskProgressBar* m_progressBar = nullptr;
    QskTextLabel* m_pctLabel = nullptr;
    QskTextLabel* m_detailLabel = nullptr;
    QskTextLabel* m_featureLabel = nullptr;
    QskTextLabel* m_statusLabel = nullptr;
    QskPushButton* m_cancelBtn = nullptr;
    QskPushButton* m_closeBtn = nullptr;
    QskPushButton* m_cornerCloseBtn = nullptr;
    bool m_finished = false;
    bool m_escFilterInstalled = false;
};

#endif // SYNC_PROGRESS_POPUP_H