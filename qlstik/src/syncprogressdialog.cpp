#include "syncprogressdialog.h"
#include "davbisync.h"

#ifdef QT3_BUILD
#include <qcombobox.h>
#include <qlineedit.h>
#include <qtimer.h>
#include <qclipboard.h>
#include <qprogressbar.h>
#include <qpushbutton.h>
#include <qtextedit.h>
#include <qlabel.h>
#include <qcolor.h>
#include <qevent.h>
#include <qdatetime.h>
#else
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QTimer>
#include <QClipboard>
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

inline void comboAdd(QComboBox* c, const QString& text)
{
#ifdef QT3_BUILD
    c->insertItem(text);
#else
    c->addItem(text);
#endif
}
inline int comboIndex(const QComboBox* c)
{
#ifdef QT3_BUILD
    return c->currentItem();
#else
    return c->currentIndex();
#endif
}
// Qt3 QTimer 无 setSingleShot/setInterval：start(ms, sshot)
inline void timerStartSingleShot(QTimer* t, int ms)
{
#ifdef QT3_BUILD
    t->start(ms, TRUE);
#else
    t->setSingleShot(true);
    t->setInterval(ms);
    t->start();
#endif
}
// 大小写不敏感包含：Qt3 用 bool cs（contains 返回次数），Qt4+ 用 Qt::CaseInsensitive
inline bool containsCI(const QString& hay, const QString& needle)
{
#ifdef QT3_BUILD
    return hay.contains(needle, FALSE) > 0;
#else
    return hay.contains(needle, Qt::CaseInsensitive);
#endif
}
// 富文本转义（Qt3 无 toHtmlEscaped）
inline QString escapeHtml(const QString& s)
{
    QString r = s;
    r.replace(QString::fromUtf8("&"), QString::fromUtf8("&amp;"));
    r.replace(QString::fromUtf8("<"), QString::fromUtf8("&lt;"));
    r.replace(QString::fromUtf8(">"), QString::fromUtf8("&gt;"));
    return r;
}
// 状态行结果配色（富文本；palette 在 Qt6 主题 QSS 下会被压制）
inline QString statusHtml(const QColor& c, const QString& text)
{
    return QString::fromUtf8("<font color=\"") + c.name()
         + QString::fromUtf8("\">") + escapeHtml(text)
         + QString::fromUtf8("</font>");
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
    , m_levelCombo(0)
    , m_searchEdit(0)
    , m_debounce(0)
    , m_countLabel(0)
    , m_copyBtn(0)
    , m_clearBtn(0)
    , m_cancelBtn(0)
    , m_closeBtn(0)
    , m_cornerCloseBtn(0)
    , m_autoScroll(true)
{
    setModal(true);                       // anysk QskPopup setModal(true)
    qSetWindowTitle(this, QString::fromUtf8("同步进度"));

    QBoxLayout* v = qNewBoxLayout(this, QBoxLayout::TopToBottom, 10);
    v->setSpacing(8);

    // 标题行：标题 + 右侧 ✕（照 anysk cornerCloseBtn）
    QWidget* titleRow = new QWidget(this);
    QBoxLayout* titleLay = qNewBoxLayout(titleRow, QBoxLayout::LeftToRight, 0);
    QLabel* title = new QLabel(QString::fromUtf8("同步进度"), titleRow);
    titleLay->addWidget(title, 0);
    titleLay->addStretch(1);
    m_cornerCloseBtn = new QPushButton(QString::fromUtf8("✕"), titleRow);
    connect(m_cornerCloseBtn, SIGNAL(clicked()), this, SLOT(close()));
    m_cornerCloseBtn->setEnabled(false);
    titleLay->addWidget(m_cornerCloseBtn, 0);

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
    m_statusLabel->setTextFormat(Qt::RichText);

    // ── 只读彩色日志（anysk LogListView 的「中档」替代）──
    m_log = new QTextEdit(this);
    m_log->setReadOnly(true);
    qTextEditWrapNoHScroll(m_log);
    m_log->setMinimumHeight(200);

    // ── 过滤行：级别下拉 + 搜索（Qt3 无 placeholderText，前置标签）──
    QWidget* filterRow = new QWidget(this);
    QBoxLayout* filterLay = qNewBoxLayout(filterRow, QBoxLayout::LeftToRight, 0);
    filterLay->setSpacing(8);
    m_levelCombo = new QComboBox(filterRow);
    comboAdd(m_levelCombo, QString::fromUtf8("全部"));
    comboAdd(m_levelCombo, QString::fromUtf8("Info"));
    comboAdd(m_levelCombo, QString::fromUtf8("Warn"));
    comboAdd(m_levelCombo, QString::fromUtf8("Error"));
    filterLay->addWidget(m_levelCombo, 0);
    filterLay->addWidget(new QLabel(QString::fromUtf8("搜索"), filterRow), 0);
    m_searchEdit = new QLineEdit(filterRow);
    filterLay->addWidget(m_searchEdit, 1);

    // ── 工具行：计数 + 复制 + 清空 ──
    QWidget* toolsRow = new QWidget(this);
    QBoxLayout* toolsLay = qNewBoxLayout(toolsRow, QBoxLayout::LeftToRight, 0);
    toolsLay->setSpacing(6);
    m_countLabel = new QLabel(QString::fromUtf8("0 / 0 条"), toolsRow);
    toolsLay->addWidget(m_countLabel, 0);
    toolsLay->addStretch(1);
    m_copyBtn = new QPushButton(QString::fromUtf8("复制"), toolsRow);
    m_clearBtn = new QPushButton(QString::fromUtf8("清空"), toolsRow);
    toolsLay->addWidget(m_copyBtn, 0);
    toolsLay->addWidget(m_clearBtn, 0);

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

    v->addWidget(titleRow, 0);
    v->addWidget(pRow, 0);
    v->addWidget(m_detailLabel, 0);
    v->addWidget(m_featureLabel, 0);
    v->addWidget(m_statusLabel, 0);
    v->addWidget(filterRow, 0);
    v->addWidget(m_log, 1);
    v->addWidget(toolsRow, 0);
    v->addWidget(bRow, 0);

    setMinimumSize(360, 420);
    {
        const QRect scr = qScreenGeometryFor(this);
        const int w = qMin(sizeHint().width(),  int(scr.width()  * 0.92));
        const int h = qMin(sizeHint().height(), int(scr.height() * 0.90));
        resize(qMax(360, w), qMax(420, h));
    }

    // ── 过滤/搜索 150ms 防抖 + 复制/清空 ──
    m_debounce = new QTimer(this);
    connect(m_debounce, SIGNAL(timeout()), this, SLOT(onDebounceTimeout()));
    connect(m_levelCombo, SIGNAL(activated(int)), this, SLOT(onFilterChanged()));
    connect(m_searchEdit, SIGNAL(textChanged(const QString&)), this, SLOT(onFilterChanged()));
    connect(m_copyBtn, SIGNAL(clicked()), this, SLOT(onCopyClicked()));
    connect(m_clearBtn, SIGNAL(clicked()), this, SLOT(onClearClicked()));

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
    m_entries.clear();
    if (m_log) {
        m_log->clear();
    }
    progressSetValue(m_bar, 0);
    m_pctLabel->setText(QString::fromUtf8("0%"));
    m_detailLabel->setText(QString::fromUtf8("第 0/0 个文件"));
    m_featureLabel->setText(QString::fromUtf8("远程特征: 检测中..."));
    m_statusLabel->setText(statusHtml(QColor(150, 205, 255),
                                      QString::fromUtf8("同步中...")));
    m_cancelBtn->setEnabled(true);
    m_closeBtn->setEnabled(false);
    m_cornerCloseBtn->setEnabled(false);
    updateCount();
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
    LogEntry e;
    e.level   = level;
    e.tag     = tag;
    e.message = line;
    const QString ts = QDateTime::currentDateTime().toString(QString::fromUtf8("hh:mm:ss.zzz"));
    e.line = ts + QString::fromUtf8("  [") + tag + QString::fromUtf8("] ")
             + levelTag(level) + QString::fromUtf8("  ") + line;

    m_entries.push_back(e);
    if (m_entries.size() > 500) {
        m_entries.erase(m_entries.begin());   // 上限 500（对齐 anysk LogModel）
    }
    if (match(e)) {
        appendRowToView(e);
    }
    updateCount();
}

void SyncProgressDialog::appendRowToView(const LogEntry& e)
{
    textEditSetColor(m_log, levelColor(e.level));
    m_log->append(e.line);
    if (m_autoScroll) {
        m_log->ensureCursorVisible();   // Qt3/Qt6 均有；scrollToBottom 仅 Qt3 有
    }
}

bool SyncProgressDialog::match(const LogEntry& e) const
{
    const int idx = levelIndex();          // 0=全部 1=Info 2=Warn 3=Error
    if (idx > 0 && (e.level + 1) != idx) {
        return false;
    }
    const QString t = m_searchEdit ? m_searchEdit->text() : QString();
    if (!t.isEmpty()) {
        if (!containsCI(e.tag, t) && !containsCI(e.message, t)) {
            return false;
        }
    }
    return true;
}

QString SyncProgressDialog::filteredText() const
{
    QString out;
    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (!match(m_entries[i])) {
            continue;
        }
        if (!out.isEmpty()) {
            out += QString::fromUtf8("\n");
        }
        out += m_entries[i].line;
    }
    return out;
}

int SyncProgressDialog::visibleCount() const
{
    int n = 0;
    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (match(m_entries[i])) {
            ++n;
        }
    }
    return n;
}

void SyncProgressDialog::updateCount()
{
    if (!m_countLabel) {
        return;
    }
    m_countLabel->setText(QString::fromUtf8("%1 / %2 条")
        .arg(QString::number(visibleCount()))
        .arg(QString::number(int(m_entries.size()))));
}

int SyncProgressDialog::levelIndex() const
{
    return m_levelCombo ? comboIndex(m_levelCombo) : 0;
}

void SyncProgressDialog::rebuildLog()
{
    if (!m_log) {
        return;
    }
    m_log->clear();
    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (match(m_entries[i])) {
            appendRowToView(m_entries[i]);
        }
    }
    updateCount();
}

void SyncProgressDialog::onFilterChanged()
{
    timerStartSingleShot(m_debounce, 150);
}

void SyncProgressDialog::onDebounceTimeout()
{
    rebuildLog();
}

void SyncProgressDialog::onCopyClicked()
{
    QApplication::clipboard()->setText(filteredText());
    const int n = visibleCount();
    m_statusLabel->setText(statusHtml(QColor(150, 205, 255),
        QString::fromUtf8("已复制 ") + QString::number(n)
        + QString::fromUtf8(" 条到剪贴板")));
}

void SyncProgressDialog::onClearClicked()
{
    m_entries.clear();
    if (m_log) {
        m_log->clear();
    }
    updateCount();
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
    m_cornerCloseBtn->setEnabled(true);
    if (exitCode == davbisync::FinishOk) {
        m_statusLabel->setText(statusHtml(QColor(120, 210, 140),
            QString::fromUtf8("同步完成 · ") + summary));
    } else if (exitCode == davbisync::FinishCancelled) {
        m_statusLabel->setText(statusHtml(QColor(255, 180, 84),
            QString::fromUtf8("已取消 · ") + summary));
    } else {
        m_statusLabel->setText(statusHtml(QColor(255, 91, 91),
            QString::fromUtf8("同步失败 · ") + summary));
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
    if (m_engine && m_engine->isRunning()) {
        e->ignore();       // 运行中禁止关闭（与 Esc 门控一致），须先「取消」
        return;
    }
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
