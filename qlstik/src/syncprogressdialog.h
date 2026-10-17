#ifndef QLSTIK_SYNCPROGRESSDIALOG_H
#define QLSTIK_SYNCPROGRESSDIALOG_H

// 同步进度弹窗（批次 P4）——anystik/src/syncprogresspopup.{h,cpp} 的 Qt3「中档」
// 等价物：进度条 + 百分比 + 详情行 + 远程特征行 + 状态行 + 只读彩色日志 + 取消/关闭。
// 按用户裁定裁去 anystik 的日志过滤下拉 / 关键字防抖 / 复制-清空（那是「高档」）。
//
// 仅 Qt3/Qt≥5 编入（qlstik.pro 与 davsource 同条件；Qt4 不编——davbisync 未编入）。
// 与宿主 StickerListPage 的对接照搬 anystik：构造时连 engine 的
// syncLog/progressUpdated/finished/remoteFeature；关闭时 emit closed() 供宿主回收。

#include "compat34.h"          // QWidget / QString / QLabel / QTextEdit / qNewBoxLayout …

#ifdef QT3_BUILD
#include <qdialog.h>
#else
#include <QDialog>
#include <QString>
#endif

class SyncEngine;              // stikcommon/davbisync.h
class QProgressBar;
class QLabel;
class QTextEdit;
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

private:
    void appendLog(int level, const QString& tag, const QString& line);

    SyncEngine*   m_engine;
    QProgressBar* m_bar;
    QLabel*       m_pctLabel;
    QLabel*       m_detailLabel;
    QLabel*       m_featureLabel;
    QLabel*       m_statusLabel;
    QTextEdit*    m_log;
    QPushButton*  m_cancelBtn;
    QPushButton*  m_closeBtn;
};

#endif // QLSTIK_SYNCPROGRESSDIALOG_H
