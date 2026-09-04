#ifndef MIGRATION_DIALOG_H
#define MIGRATION_DIALOG_H

#include <QskPopup.h>
#include <QElapsedTimer>
#include <QString>

class QskBox;
class QskLinearBox;
class QskTextLabel;
class QskProgressBar;
class QskPushButton;

/*
 * 迁移存储位置的进度对话框（桌面/Android 通用，非阻塞）。
 *
 * 结构与 ConfirmPopup 一致（同 QQuickWindow 内 QskPopup + QskPushButton，
 * 避免 QskDialog::select() 的 Android 触摸/悬垂指针问题）：
 *   - 标题「迁移存储位置」
 *   - 来源/目标目录两行（只读展示）
 *   - QskProgressBar（进度按「文件数」计算，见 StickerStore::migrationProgress）
 *   - 「已拷贝」字节数（仅展示，不参与进度计算）
 *   - 暂停 / 取消 按钮（本次仅占位为空实现：点击只打日志，不做实际暂停/取消）
 *
 * 监听 StickerStore::migrationProgress / migrationFinished：
 *   - progress：更新进度条与字节数
 *   - finished：关闭自身
 */
class MigrationDialog : public QskPopup
{
    Q_OBJECT
public:
    // 在 target 目录迁移过程中展示；完成后自动关闭
    static MigrationDialog* show(QQuickItem* parent,
                                 const QString& fromRoot,
                                 const QString& toRoot);

protected:
    void updateLayout() override;

private:
    MigrationDialog(const QString& fromRoot, const QString& toRoot,
                    QQuickItem* parent);
    void updateGeometry();
    void onProgress(int done, int total, qint64 copiedBytes,
                    int copiedFiles, int skippedFiles, const QString& current);
    void onFinished(bool ok, const QString& detail);

    QskBox* m_panel = nullptr;
    QskLinearBox* m_layout = nullptr;
    QskTextLabel* m_titleLabel = nullptr;
    QskTextLabel* m_fromLabel = nullptr;
    QskTextLabel* m_toLabel = nullptr;
    QskProgressBar* m_progress = nullptr;
    QskTextLabel* m_bytesLabel = nullptr;
    QskPushButton* m_pauseButton = nullptr;
    QskPushButton* m_cancelButton = nullptr;

    // 迁移统计（完成 toast / 日志用）
    QElapsedTimer m_timer;            // 从对话框弹出开始计时
    int m_lastDone = 0;
    int m_lastTotal = 0;
    int m_lastCopied = 0;
    int m_lastSkipped = 0;
    qint64 m_lastBytes = 0;
};

#endif // MIGRATION_DIALOG_H
