#include "logpage.h"
#include "pagemanager.h"
#include <QskLinearBox.h>
#include <QskTextLabel.h>
#include <QskTextField.h>
#include <QskPushButton.h>
#include <QskComboBox.h>
#include <QskScrollView.h>
#include <QskLabelData.h>
#include <QskSeparator.h>

LogPage::LogPage(QQuickItem* parent)
    : Page(parent)
{
}

void LogPage::onCreate(const QVariantMap&, const QVariantMap&)
{
    setAutoLayoutChildren(true);
    auto* layout = new QskLinearBox(Qt::Vertical, this);
    layout->setPanel(true);

    // ── TopBar ──
    auto* topBar = new QskLinearBox(Qt::Horizontal, layout);
    topBar->setPanel(true);
    topBar->setPreferredHeight(56);

    auto* backBtn = new QskPushButton(QString::fromUtf8("←"), topBar);
    backBtn->setPreferredSize(44, 44);

    auto* title = new QskTextLabel("App Log", topBar);
    title->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    title->setAlignment(Qt::AlignCenter);

    auto* clearBtn = new QskPushButton("Clear", topBar);
    clearBtn->setPreferredHeight(40);

    connect(backBtn, &QskAbstractButton::clicked, this, [this]() {
        finish();
    });

    connect(clearBtn, &QskAbstractButton::clicked, this, [this]() {
        LogModel::instance().clear();
    });

    // ── Filter bar ──
    auto* filterBar = new QskLinearBox(Qt::Horizontal, layout);
    filterBar->setPanel(true);
    filterBar->setPreferredHeight(48);
    filterBar->setSpacing(8);

    m_levelCombo = new QskComboBox(filterBar);
    m_levelCombo->addOption(QskLabelData("All"));
    m_levelCombo->addOption(QskLabelData("Info"));
    m_levelCombo->addOption(QskLabelData("Warn"));
    m_levelCombo->addOption(QskLabelData("Error"));
    m_levelCombo->setPreferredWidth(100);

    {
        auto* field = new QskTextField(filterBar);
        field->setPlaceholderText(QString::fromUtf8("🔍 Search..."));
        m_searchField = field;
    }
    m_searchField->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    m_searchField->setPreferredHeight(40);

    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(150);

    connect(m_debounceTimer, &QTimer::timeout, this, &LogPage::rebuildList);
    connect(m_levelCombo, &QskComboBox::currentIndexChanged,
        this, [this](int) { m_debounceTimer->start(); });
    connect(m_searchField, &QskTextInput::textChanged,
        this, [this]() { m_debounceTimer->start(); });

    // ── Scrollable log list ──
    auto* scrollView = new QskScrollView(layout);
    scrollView->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Expanding);
    scrollView->setFlickableOrientations(Qt::Vertical);

    m_listBox = new QskLinearBox(Qt::Vertical, scrollView);

    // ── Status bar ──
    auto* statusBar = new QskLinearBox(Qt::Horizontal, layout);
    statusBar->setPanel(true);
    statusBar->setPreferredHeight(32);

    m_countLabel = new QskTextLabel("0 entries", statusBar);
    m_countLabel->setAlignment(Qt::AlignCenter);

    // ── Connect to model signals ──
    auto& model = LogModel::instance();
    connect(&model, &LogModel::entryAdded, this, [this](int) {
        auto& m = LogModel::instance();
        int last = m.count() - 1;
        if (!matchFilter(m.at(last))) return;
        const auto& e = m.at(last);
        auto* row = new QskTextLabel(m_listBox);
        QString levelTag;
        switch (e.level) {
            case LogModel::Error: levelTag = "🔴 ERR"; break;
            case LogModel::Warn:  levelTag = "🟡 WRN"; break;
            case LogModel::Info:  levelTag = "ℹ️ INF"; break;
            default:             levelTag = "⚪ DBG";
        }
        row->setText(e.timestamp + "  " + levelTag + "  " + e.tag + "  " + e.message);
        row->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
        m_rows.append(row);
        int visible = m_rows.size();
        m_countLabel->setText(
            QString::number(visible) + " / " + QString::number(m.count()) + " entries");
    });
    connect(&model, &LogModel::cleared, this, [this]() {
        rebuildList();
    });

    // ── Initial population ──
    rebuildList();
}

bool LogPage::matchFilter(const LogModel::Entry& e) const
{
    int levelIdx = m_levelCombo->currentIndex();
    if (levelIdx > 0) {
        static const LogModel::Level levels[] = {
            LogModel::Debug, LogModel::Info, LogModel::Warn, LogModel::Error
        };
        if (levelIdx - 1 < 4 && e.level != levels[levelIdx - 1])
            return false;
    }
    QString text = m_searchField->text();
    if (!text.isEmpty()) {
        if (!e.tag.contains(text, Qt::CaseInsensitive) &&
            !e.message.contains(text, Qt::CaseInsensitive))
            return false;
    }
    return true;
}

void LogPage::rebuildList()
{
    qDeleteAll(m_rows);
    m_rows.clear();

    auto& model = LogModel::instance();
    for (int i = 0; i < model.count(); ++i) {
        const auto& e = model.at(i);
        if (!matchFilter(e)) continue;
        auto* row = new QskTextLabel(m_listBox);
        QString levelTag;
        switch (e.level) {
            case LogModel::Error: levelTag = "🔴 ERR"; break;
            case LogModel::Warn:  levelTag = "🟡 WRN"; break;
            case LogModel::Info:  levelTag = "ℹ️ INF"; break;
            default:             levelTag = "⚪ DBG";
        }
        row->setText(e.timestamp + "  " + levelTag + "  " + e.tag + "  " + e.message);
        row->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
        m_rows.append(row);
    }
    m_countLabel->setText(
        QString::number(m_rows.size()) + " / " + QString::number(model.count()) + " entries");
}
