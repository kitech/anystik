#include "logpage.h"
#include "pagemanager.h"
#include "loglistview.h"
#include <QskLinearBox.h>
#include <QskTextLabel.h>
#include <QskTextField.h>
#include <QskTextInput.h>
#include <QskPushButton.h>
#include <QskComboBox.h>
#include <QskSeparator.h>

namespace {

LogListView::RowItem buildRow(const LogModel::Entry& e)
{
    LogListView::RowItem item;
    item.level = int(e.level);
    item.tag = e.tag;
    item.message = e.message;
    QString levelTag;
    switch (e.level) {
        case LogModel::Error: levelTag = "🔴 ERR"; break;
        case LogModel::Warn:  levelTag = "🟡 WRN"; break;
        case LogModel::Info:  levelTag = "ℹ️ INF"; break;
        default:             levelTag = "⚪ DBG";
    }
    item.text = e.timestamp + "  " + levelTag + "  " + e.tag + "  " + e.message;
    return item;
}

} // namespace

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

    // ── 日志组件：过滤条 + 滚动列表 + 计数（复制/清空按钮隐藏，顶栏 Clear 承担清空）──
    m_logList = new LogListView(layout);
    m_logList->setCountLabelFormat(QStringLiteral("%1 / %2 entries"));
    m_logList->setListPreferredHeight(-1);      // 填满页面
    m_logList->setCopyButtonVisible(false);
    m_logList->setClearButtonVisible(false);
    m_logList->setAutoScroll(false);            // 保持原行为：不自动滚底
    m_logList->setLevelComboOptions(
        { QStringLiteral("All"), QStringLiteral("Info"),
          QStringLiteral("Warn"), QStringLiteral("Error") });
    m_logList->levelCombo()->setPreferredWidth(100);
    m_logList->searchField()->setPreferredHeight(40);

    // ── Connect to model signals ──
    auto& model = LogModel::instance();
    connect(&model, &LogModel::entryAdded, this, [this]() {
        auto& m = LogModel::instance();
        m_logList->appendItem(buildRow(m.at(m.count() - 1)));
    });
    connect(&model, &LogModel::cleared, this, [this]() {
        m_logList->clearItems();
    });

    // ── Initial population ──
    for (int i = 0; i < model.count(); ++i) {
        m_logList->appendItem(buildRow(model.at(i)));
    }
}