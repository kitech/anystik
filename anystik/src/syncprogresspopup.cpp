#include "syncprogresspopup.h"
#include "logmodel.h"
#include "davbisync.h"
#include <QskLinearBox.h>
#include <QskTextLabel.h>
#include <QskProgressBar.h>
#include <QskComboBox.h>
#include <QskTextField.h>
#include <QskPushButton.h>
#include <QskScrollView.h>
#include <QskLabelData.h>
#include <QskFontRole.h>
#include <QskBox.h>
#include <QskBoxShapeMetrics.h>
#include <QClipboard>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>

namespace {

// davbisync::LogLevel{Info,Warn,Error} → LogModel::Level{Debug,Info,Warn,Error}（偏移 +1）
const LogModel::Level kLevelMapInt[] = {
    LogModel::Info, LogModel::Warn, LogModel::Error
};

QColor levelColor(const LogModel::Entry& e)
{
    switch (e.level) {
        case LogModel::Error: return QColor(255, 91, 91);
        case LogModel::Warn:  return QColor(255, 180, 84);
        case LogModel::Info:  return QColor(150, 205, 255);
        default:              return QColor();
    }
}

QString levelTag(const LogModel::Entry& e)
{
    switch (e.level) {
        case LogModel::Error: return QStringLiteral("ERR");
        case LogModel::Warn:  return QStringLiteral("WRN");
        case LogModel::Info:  return QStringLiteral("INF");
        default:              return QStringLiteral("DBG");
    }
}

QRectF popupParentRect(QQuickItem* parent)
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

SyncProgressPopup::SyncProgressPopup(SyncEngine* engine, QQuickItem* parent)
    : QskPopup(parent)
    , m_model(new LogModel(this))
{
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(150);

    setModal(true);
    setOverlay(true);
    setPopupFlag(QskPopup::DeleteOnClose, false);
    setPolishOnResize(true);
    setPolishOnParentResize(true);

    m_panel = new QskBox(this);
    m_panel->setBoxShapeHint(QskBox::Panel,
        QskBoxShapeMetrics(14, Qt::AbsoluteSize));

    m_layout = new QskLinearBox(Qt::Vertical, m_panel);
    m_layout->setSpacing(8);
    m_layout->setMargins(18);

    // ── 标题 ──
    auto* title = new QskTextLabel(QStringLiteral("同步进度"), m_layout);
    title->setFontRole(QskFontRole::Title);
    title->setAlignment(Qt::AlignCenter);

    // ── 进度条 ──
    auto* progressRow = new QskLinearBox(Qt::Horizontal, m_layout);
    progressRow->setSpacing(8);
    m_progressBar = new QskProgressBar(0, 100, progressRow);
    m_progressBar->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    m_progressBar->setValue(0);
    m_pctLabel = new QskTextLabel(QStringLiteral("0%"), progressRow);
    m_pctLabel->setPreferredWidth(40);
    m_pctLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);

    // ── 实时传输统计行（文件名/大小/单文件用时/总用时/ETA）──
    m_detailLabel = new QskTextLabel(QStringLiteral("第 0/0 个文件"), m_layout);
    m_detailLabel->setFontRole(QskFontRole::Caption);
    m_detailLabel->setWrapMode(QskTextOptions::WrapAnywhere);
    m_detailLabel->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Constrained);

    // ── 远程能力检测结果行 ──
    m_featureLabel = new QskTextLabel(QStringLiteral("远程特征: 检测中..."), m_layout);
    m_featureLabel->setFontRole(QskFontRole::Caption);
    m_featureLabel->setWrapMode(QskTextOptions::WrapAnywhere);
    m_featureLabel->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Constrained);

    // ── 状态行 ──
    m_statusLabel = new QskTextLabel(QStringLiteral("同步中..."), m_layout);
    m_statusLabel->setFontRole(QskFontRole::Caption);
    m_statusLabel->setWrapMode(QskTextOptions::WrapAnywhere);
    m_statusLabel->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Constrained);

    // ── 过滤条 ──
    auto* filterBar = new QskLinearBox(Qt::Horizontal, m_layout);
    filterBar->setSpacing(8);

    m_levelCombo = new QskComboBox(filterBar);
    m_levelCombo->addOption(QskLabelData(QStringLiteral("全部")));
    m_levelCombo->addOption(QskLabelData(QStringLiteral("Info")));
    m_levelCombo->addOption(QskLabelData(QStringLiteral("Warn")));
    m_levelCombo->addOption(QskLabelData(QStringLiteral("Error")));
    m_levelCombo->setPreferredWidth(80);

    m_searchField = new QskTextField(filterBar);
    m_searchField->setPlaceholderText(QStringLiteral("过滤标签 / 内容..."));
    m_searchField->setPreferredHeight(38);
    m_searchField->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    connect(m_debounceTimer, &QTimer::timeout, this, &SyncProgressPopup::rebuildList);
    connect(m_levelCombo, &QskComboBox::currentIndexChanged,
        this, [this](int) { m_debounceTimer->start(); });
    connect(m_searchField, &QskTextInput::textChanged,
        this, [this]() { m_debounceTimer->start(); });

    // ── 滚动日志区 ──
    m_scrollView = new QskScrollView(m_layout);
    m_scrollView->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Expanding);
    m_scrollView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollView->setPreferredHeight(340);

    m_listBox = new QskLinearBox(Qt::Vertical, m_scrollView);
    m_listBox->setSpacing(1);

    // ── 工具条 ──
    auto* tools = new QskLinearBox(Qt::Horizontal, m_layout);
    tools->setSpacing(6);

    m_countLabel = new QskTextLabel(QStringLiteral("0 条"), tools);
    m_countLabel->setAlignment(Qt::AlignVCenter);

    auto* copyBtn = new QskPushButton(QStringLiteral("复制"), tools);
    connect(copyBtn, &QskAbstractButton::clicked, this, &SyncProgressPopup::copyFiltered);

    auto* clearBtn = new QskPushButton(QStringLiteral("清空"), tools);
    connect(clearBtn, &QskAbstractButton::clicked, this, &SyncProgressPopup::clearLog);

    tools->addSpacer(0, 0);

    m_cancelBtn = new QskPushButton(QStringLiteral("取消"), tools);
    connect(m_cancelBtn, &QskAbstractButton::clicked, this, [this]() {
        if (m_engine)
            m_engine->abort();
    });

    m_closeBtn = new QskPushButton(QStringLiteral("关闭"), tools);
    m_closeBtn->setEnabled(false);
    connect(m_closeBtn, &QskAbstractButton::clicked, this, &QskPopup::close);

    registerEngine(engine);

    QTimer::singleShot(0, this, [this]() { updateGeometry(); });
}

void SyncProgressPopup::registerEngine(SyncEngine* engine)
{
    m_engine = engine;
    if (!engine) {
        return;
    }

    connect(engine, &SyncEngine::syncLog, this,
            [this](int lvl, const QString& tag, const QString& line) {
                const int idx = qBound(0, lvl, int(davbisync::Error));
                m_model->append(kLevelMapInt[idx], tag, line);
                const int last = m_model->count() - 1;
                if (matchFilter(*m_model, last)) {
                    addEntryRow(*m_model, last);
                    scrollToBottom();
                }
                m_countLabel->setText(QString::number(m_rows.size())
                    + QStringLiteral(" / ") + QString::number(m_model->count())
                    + QStringLiteral(" 条"));
            });

    connect(engine, &SyncEngine::progressUpdated, this,
            [this](int percent, const QString& step, const QString& detail) {
                Q_UNUSED(step)
                m_progressBar->setValue(percent);
                m_pctLabel->setText(QString::number(percent) + QStringLiteral("%"));
                if (!detail.isEmpty() && m_detailLabel) {
                    m_detailLabel->setText(detail);
                }
            });

    connect(engine, &SyncEngine::finished, this,
            [this](int exitCode, const QString& summary) {
                applyFinished(exitCode, summary);
            });

    connect(engine, &SyncEngine::remoteFeature, this,
            [this](const QString& text) { m_featureLabel->setText(text); });
}

void SyncProgressPopup::resetForRun()
{
    m_finished = false;
    m_progressBar->setValue(0);
    m_pctLabel->setText(QStringLiteral("0%"));
    if (m_detailLabel) {
        m_detailLabel->setText(QStringLiteral("第 0/0 个文件"));
    }
    if (m_statusLabel) {
        m_statusLabel->setTextColor(QColor());
        m_statusLabel->setText(QStringLiteral("同步中..."));
    }
    if (m_featureLabel) {
        m_featureLabel->setText(QStringLiteral("远程特征: 检测中..."));
    }
    m_cancelBtn->setEnabled(true);
    m_closeBtn->setEnabled(false);
}

void SyncProgressPopup::applyFinished(int exitCode, const QString& summary)
{
    m_finished = true;
    m_cancelBtn->setEnabled(false);
    m_closeBtn->setEnabled(true);

    if (exitCode == 0) {
        m_statusLabel->setTextColor(QColor(120, 210, 140));
        m_statusLabel->setText(QStringLiteral("同步完成 · ") + summary);
    } else if (exitCode == 2) {
        m_statusLabel->setTextColor(QColor(255, 180, 84));
        m_statusLabel->setText(QStringLiteral("已取消 · ") + summary);
    } else {
        m_statusLabel->setTextColor(QColor(255, 91, 91));
        m_statusLabel->setText(QStringLiteral("同步失败 · ") + summary);
    }
}

void SyncProgressPopup::clearLog()
{
    if (m_model) {
        m_model->clear();
    }
    rebuildList();
}

bool SyncProgressPopup::matchFilter(const LogModel& model, int index) const
{
    const auto& e = model.at(index);
    const int li = m_levelCombo ? m_levelCombo->currentIndex() : 0;
    if (li > 0) {
        const LogModel::Level target = (li == 1) ? LogModel::Info
            : (li == 2) ? LogModel::Warn : LogModel::Error;
        if (e.level != target) {
            return false;
        }
    }
    if (m_searchField && !m_searchField->text().isEmpty()) {
        const QString text = m_searchField->text();
        if (!e.tag.contains(text, Qt::CaseInsensitive) &&
            !e.message.contains(text, Qt::CaseInsensitive)) {
            return false;
        }
    }
    return true;
}

void SyncProgressPopup::addEntryRow(const LogModel& model, int index)
{
    auto* row = new QskTextLabel(m_listBox);
    const auto& e = model.at(index);
    row->setText(e.timestamp + QStringLiteral("  [") + e.tag + QStringLiteral("] ")
                     + levelTag(e) + QStringLiteral("  ") + e.message);
    const QColor c = levelColor(e);
    if (c.isValid()) {
        row->setTextColor(c);
    }
    row->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    m_rows.append(row);
}

void SyncProgressPopup::rebuildList()
{
    qDeleteAll(m_rows);
    m_rows.clear();

    if (!m_model) {
        return;
    }
    for (int i = 0; i < m_model->count(); ++i) {
        if (matchFilter(*m_model, i)) {
            addEntryRow(*m_model, i);
        }
    }
    scrollToBottom();
    m_countLabel->setText(QString::number(m_rows.size())
        + QStringLiteral(" / ") + QString::number(m_model->count())
        + QStringLiteral(" 条"));
}

void SyncProgressPopup::scrollToBottom()
{
    if (m_scrollView) {
        const QSizeF size = m_scrollView->scrollableSize();
        m_scrollView->scrollTo(QPointF(0, size.height()));
    }
}

void SyncProgressPopup::copyFiltered()
{
    if (!m_model) {
        return;
    }
    QString out;
    for (int i = 0; i < m_model->count(); ++i) {
        if (!matchFilter(*m_model, i)) {
            continue;
        }
        const auto& e = m_model->at(i);
        if (!out.isEmpty()) {
            out += QLatin1Char('\n');
        }
        out += e.timestamp + QStringLiteral("  [") + e.tag + QStringLiteral("] ")
               + levelTag(e) + QStringLiteral("  ") + e.message;
    }
    QGuiApplication::clipboard()->setText(out);
    m_statusLabel->setText(QStringLiteral("已复制 ")
        + QString::number(out.isEmpty() ? 0 : out.count(QLatin1Char('\n')) + 1)
        + QStringLiteral(" 条到剪贴板"));
}

void SyncProgressPopup::updateLayout()
{
    updateGeometry();
    m_layout->setGeometry(layoutRect());
}

void SyncProgressPopup::updateGeometry()
{
    const auto parentRect = popupParentRect(parentItem());
    if (parentRect.isEmpty()) {
        return;
    }

    const auto hint = m_layout->effectiveSizeHint(Qt::PreferredSize, QSizeF());
    const qreal maxW = qMin(0.92 * parentRect.width(), 440.0);
    const qreal maxH = 0.9 * parentRect.height();

    const qreal panelW = qBound(320.0, hint.width() + 36, maxW);
    const qreal panelH = qBound(360.0, hint.height() + 36, maxH);

    QRectF r(0, 0, panelW, panelH);
    r.moveCenter(parentRect.center());
    setGeometry(r);
    m_panel->setGeometry(r.translated(-r.topLeft()));
}
