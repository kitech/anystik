#ifndef QLSTIK_SYNCPROGRESSDIALOG_H
#define QLSTIK_SYNCPROGRESSDIALOG_H

// 同步进度弹窗（批次 P4）——anystik/src/syncprogresspopup.{h,cpp} 的 Qt3/Qt6
// 对等物：进度条 + 百分比 + 详情行 + 远程特征行 + 状态行(结果配色) + 彩色日志
// （过滤下拉 / 关键字 150ms 防抖 / 复制 / 清空 / 计数）+ 取消/关闭 + 右上 ✕。
//
// 仅 Qt3/Qt≥5 编入（qlstik.pro 与 davsource 同条件；Qt4 不编——davbisync 未编入）。
// 与宿主 StickerListPage 的对接照搬 anystik：构造时连 engine 的
// syncLog/progressUpdated/finished/remoteFeature；关闭时 emit closed() 供宿主回收。

#include "compat34.h"          // QWidget / QString / QLabel / QTextEdit / qNewBoxLayout …

#include <vector>

#ifdef QT3_BUILD
#include <qdialog.h>
#else
#include <QDialog>
#include <QString>
#endif

class SyncEngine;              // stikcommon/davbisync.h
class QProgressBar;
class QLabel;
class EmojiLabel;              // qlcomp/emojiwidgets.h
class QTextEdit;
class QLineEdit;
class QComboBox;
class QTimer;
class QPushButton;
class QCloseEvent;
class QKeyEvent;

class SyncProgressDialog : public QDialog {
    Q_OBJECT
public:
    explicit SyncProgressDialog(SyncEngine* engine, QWidget* parent = 0);
    virtual ~SyncProgressDialog();

    void resetForRun();

signals:
    // 关闭（closeEvent）→ 宿主 deleteLater + 回收引擎（anysk QskPopup::closed）
    void closed();

protected:
    virtual void closeEvent(QCloseEvent* e);
    virtual void keyPressEvent(QKeyEvent* e);
    virtual void reject();     // Esc → 统一走 close()（否则只 hide，closed() 不发）

private slots:
    void onSyncLog(int level, const QString& tag, const QString& line);
    void onProgressUpdated(int percent, const QString& step, const QString& detail);
    void onFinished(int exitCode, const QString& summary);
    void onRemoteFeature(const QString& text);
    void onCancelClicked();
    void onFilterChanged();      // 过滤/搜索变化 → 重启 150ms 防抖
    void onDebounceTimeout();    // 防抖到期 → 重建日志视图
    void onCopyClicked();
    void onClearClicked();

private:
    // 一条日志（level 用 davbisync 值：0=Info 1=Warn 2=Error）
    struct LogEntry {
        int     level;
        QString tag;
        QString message;
        QString line;       // 预格式化显示文本
    };

    void appendLog(int level, const QString& tag, const QString& line);
    void    rebuildLog();
    void    appendRowToView(const LogEntry& e);
    bool    match(const LogEntry& e) const;
    QString filteredText() const;
    int     visibleCount() const;
    void    updateCount();
    int     levelIndex() const;

    SyncEngine*   m_engine;
    QProgressBar* m_bar;
    QLabel*       m_pctLabel;
    // 长文本三行（详情/远程特征/状态）：qlcomp EmojiLabel（QLabel）实现换行。
    // Qt3 用 Qt::WordBreak 对齐标志换行，Qt4+ 用 setWordWrap(true)；
    // 经 qSetLabelSelectable 保留可选中（Qt3 双击选、Qt4+ 拖选）。
    EmojiLabel*   m_detailLabel;
    EmojiLabel*   m_featureLabel;
    EmojiLabel*   m_statusLabel;
    QTextEdit*    m_log;
    QComboBox*    m_levelCombo;
    QLineEdit*    m_searchEdit;
    QTimer*       m_debounce;
    QLabel*       m_countLabel;
    QPushButton*  m_copyBtn;
    QPushButton*  m_clearBtn;
    QPushButton*  m_cancelBtn;
    QPushButton*  m_closeBtn;
    QPushButton*  m_cornerCloseBtn;

    std::vector<LogEntry> m_entries;
    // 当前匹配条数（增量维护；避免每次追加日志都全量扫描 → O(n²)）。
    int m_visibleCount;
    bool m_autoScroll;
};

#endif // QLSTIK_SYNCPROGRESSDIALOG_H
