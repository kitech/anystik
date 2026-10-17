#include "syncprogressdialog.h"
#include "davbisync.h"

#ifdef QT3_BUILD
#include <qprogressbar.h>
#include <qpushbutton.h>
#include <qtextedit.h>
#include <qlabel.h>
#include <qcolor.h>
#include <qevent.h>
#include <qdatetime.h>
#else
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QLabel>
#include <QColor>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QDateTime>
#endif

namespace {

// Qt3/Qt4+ 控件 API 差异收敛（各一处）：
//   QWidget 标题：Qt3 setCaption / Qt4+ setWindowTitle —— 用 compat34 的 qSetWindowTitle
//   QProgressBar：Qt3 setTotalSteps+setProgress / Qt4+ setRange+setValue
//   QTextEdit 当前色：Qt3 setColor / Qt4+ setTextColor
inline void progressSetRange(QProgressBar* b, int total)
{
#ifdef QT3_BUILD
    b->setTotalSteps(total);
#else
    b->setRange(0, total);
#endif
}
inline void progressSetValue(QProgressBar* b, int v)
{
#ifdef QT3_BUILD
    b->setProgress(v);
#else
    b->setValue(v);
#endif
}
inline void textEditSetColor(QTextEdit* t, const QColor& c)
{
#ifdef QT3_BUILD
    t->setColor(c);
#else
    t->setTextColor(c);
#endif
}

// davbisync::LogLevel{Info,Warn,Error} → 级别标签/配色（anysk syncprogresspopup.cpp:26-44）
QColor levelColor(int lvl)
{
    switch (lvl) {
        case davbisync::Error: return QColor(255, 91, 91);
        case davbisync::Warn:  return QColor(255, 180, 84);
        default:               return QColor(150, 205, 255);
    }
}
QString levelTag(int lvl)
{
    switch (lvl) {
        case davbisync::Error: return QString::fromUtf8("ERR");
        case davbisync::Warn:  return QString::fromUtf8("WRN");
        default:               return QString::fromUtf8("INF");
    }
}

} // namespace

SyncProgressDialog::SyncProgressDialog(SyncEngine* engine, QWidget* parent)
    : QDialog(parent)
    , m_engine(engine)
    , m_bar(0)
    , m_pctLabel(0)
    , m_detailLabel(0)
    , m_featureLabel(0)
    , m_statusLabel(0)
    , m_log(0)
    , m_cancelBtn(0)
    , m_closeBtn(0)
{
    setModal(true);                       // anysk QskPopup setModal(true)
    qSetWindowTitle(this, QString::fromUtf8("同步进度"));

    QBoxLayout* v = qNewBoxLayout(this, QBoxLayout::TopToBottom, 10);
    v->setSpacing(8);

    QLabel* title = new QLabel(QString::fromUtf8("同步进度"), this);

    // ── 进度条 + 百分比 ──
    QWidget* pRow = new QWidget(this);
    QBoxLayout* ph = qNewBoxLayout(pRow, QBoxLayout::LeftToRight, 0);
    ph->setSpacing(8);
    m_bar = new QProgressBar(pRow);
    progressSetRange(m_bar, 100);
#ifdef QT3_BUILD
    m_bar->setPercentageVisible(false);   // 百分比另置 m_pctLabel
#else
    m_bar->setTextVisible(false);
#endif
    ph->addWidget(m_bar, 1);
    m_pctLabel = new QLabel(QString::fromUtf8("0%"), pRow);
    m_pctLabel->setMinimumWidth(40);
    m_pctLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    ph->addWidget(m_pctLabel, 0);

    m_detailLabel  = new QLabel(QString::fromUtf8("第 0/0 个文件"), this);
    m_featureLabel = new QLabel(QString::fromUtf8("远程特征: 检测中..."), this);
    m_statusLabel  = new QLabel(QString::fromUtf8("同步中..."), this);

    // ── 只读彩色日志（anysk LogListView 的「中档」替代）──
    m_log = new QTextEdit(this);
    m_log->setReadOnly(true);
    qTextEditWrapNoHScroll(m_log);
    m_log->setMinimumHeight(200);

    // ── 按钮行：取消 / 关闭 ──
    QWidget* bRow = new QWidget(this);
    QBoxLayout* bh = qNewBoxLayout(bRow, QBoxLayout::LeftToRight, 0);
    bh->addStretch(1);
    m_cancelBtn = new QPushButton(bRow);
    m_cancelBtn->setText(QString::fromUtf8("取消"));
    connect(m_cancelBtn, SIGNAL(clicked()), this, SLOT(onCancelClicked()));
    bh->addWidget(m_cancelBtn, 0);
    m_closeBtn = new QPushButton(bRow);
    m_closeBtn->setText(QString::fromUtf8("关闭"));
    m_closeBtn->setEnabled(false);
    connect(m_closeBtn, SIGNAL(clicked()), this, SLOT(close()));
    bh->addWidget(m_closeBtn, 0);

    v->addWidget(title, 0);
    v->addWidget(pRow, 0);
    v->addWidget(m_detailLabel, 0);
    v->addWidget(m_featureLabel, 0);
    v->addWidget(m_statusLabel, 0);
    v->addWidget(m_log, 1);
    v->addWidget(bRow, 0);

    resize(440, 420);

    // ── 连接引擎信号（anysk syncprogresspopup.cpp:215-247）──
    if (m_engine) {
        connect(m_engine, SIGNAL(syncLog(int,const QString&,const QString&)),
                this, SLOT(onSyncLog(int,const QString&,const QString&)));
        connect(m_engine, SIGNAL(progressUpdated(int,const QString&,const QString&)),
                this, SLOT(onProgressUpdated(int,const QString&,const QString&)));
        connect(m_engine, SIGNAL(finished(int,const QString&)),
                this, SLOT(onFinished(int,const QString&)));
        connect(m_engine, SIGNAL(remoteFeature(const QString&)),
                this, SLOT(onRemoteFeature(const QString&)));
    }
}

SyncProgressDialog::~SyncProgressDialog()
{
}

void SyncProgressDialog::resetForRun()
{
    if (m_log) {
        m_log->clear();
    }
    progressSetValue(m_bar, 0);
    m_pctLabel->setText(QString::fromUtf8("0%"));
    m_detailLabel->setText(QString::fromUtf8("第 0/0 个文件"));
    m_featureLabel->setText(QString::fromUtf8("远程特征: 检测中..."));
    m_statusLabel->setText(QString::fromUtf8("同步中..."));
    m_cancelBtn->setEnabled(true);
    m_closeBtn->setEnabled(false);
}

void SyncProgressDialog::onSyncLog(int level, const QString& tag, const QString& line)
{
    appendLog(level, tag, line);
}

void SyncProgressDialog::appendLog(int level, const QString& tag, const QString& line)
{
    if (!m_log) {
        return;
    }
    const QString ts = QDateTime::currentDateTime().toString(QString::fromUtf8("hh:mm:ss"));
    textEditSetColor(m_log, levelColor(level));
    m_log->append(ts + QString::fromUtf8("  [") + tag + QString::fromUtf8("] ")
                  + levelTag(level) + QString::fromUtf8("  ") + line);
    m_log->ensureCursorVisible();   // Qt3/Qt6 均有；scrollToBottom 仅 Qt3 有
}

void SyncProgressDialog::onProgressUpdated(int percent, const QString& step, const QString& detail)
{
    Q_UNUSED(step)
    progressSetValue(m_bar, percent);
    m_pctLabel->setText(QString::number(percent) + QString::fromUtf8("%"));
    if (!detail.isEmpty()) {
        m_detailLabel->setText(detail);
    }
}

void SyncProgressDialog::onFinished(int exitCode, const QString& summary)
{
    m_cancelBtn->setEnabled(false);
    m_closeBtn->setEnabled(true);
    if (exitCode == davbisync::FinishOk) {
        m_statusLabel->setText(QString::fromUtf8("同步完成 · ") + summary);
    } else if (exitCode == davbisync::FinishCancelled) {
        m_statusLabel->setText(QString::fromUtf8("已取消 · ") + summary);
    } else {
        m_statusLabel->setText(QString::fromUtf8("同步失败 · ") + summary);
    }
}

void SyncProgressDialog::onRemoteFeature(const QString& text)
{
    m_featureLabel->setText(text);
}

void SyncProgressDialog::onCancelClicked()
{
    if (m_engine) {
        m_engine->abort();
    }
}

void SyncProgressDialog::closeEvent(QCloseEvent* e)
{
    emit closed();
    QDialog::closeEvent(e);
}

void SyncProgressDialog::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape && m_engine && m_engine->isRunning()) {
        e->ignore();     // 运行中禁止 Esc 关闭（照 anysk eventFilter:203-213）
        return;
    }
    QDialog::keyPressEvent(e);
}

void SyncProgressDialog::reject()
{
    close();             // 统一走 closeEvent → closed()；否则只 hide 不发 closed()
}
