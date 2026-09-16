#include "loglistview.h"
#include "scrollfader.h"
#include <QskLinearBox.h>
#include <QskTextLabel.h>
#include <QskTextField.h>
#include <QskTextInput.h>
#include <QskPushButton.h>
#include <QskComboBox.h>
#include <QskScrollArea.h>
#include <QskLabelData.h>
#include <QTimer>
#include <QQuickItem>

LogListView::LogListView(QQuickItem* parent)
    : QskControl(parent)
{
    setAutoLayoutChildren(true);
    setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Expanding);

    auto* layout = new QskLinearBox(Qt::Vertical, this);
    layout->setSpacing(4);

    // ── 过滤条 ──
    m_filterBar = new QskLinearBox(Qt::Horizontal, layout);
    m_filterBar->setSpacing(8);

    m_levelCombo = new QskComboBox(m_filterBar);
    m_levelCombo->addOption(QskLabelData(QStringLiteral("All")));
    m_levelCombo->addOption(QskLabelData(QStringLiteral("Info")));
    m_levelCombo->addOption(QskLabelData(QStringLiteral("Warn")));
    m_levelCombo->addOption(QskLabelData(QStringLiteral("Error")));
    m_levelCombo->setPreferredWidth(80);

    m_searchField = new QskTextField(m_filterBar);
    m_searchField->setPlaceholderText(QStringLiteral("🔍 Search..."));
    m_searchField->setPreferredHeight(38);
    m_searchField->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    // ── 滚动列表（两个坑已固化：setScrolledItem + 内容盒垂直 Minimum）──
    m_scrollArea = new QskScrollArea(layout);
    m_scrollArea->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Expanding);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFlickableOrientations(Qt::Vertical);
    m_scrollArea->setPreferredHeight(340);

    // 桌面 Fusion：滚动条“滚动时短暂显现→空闲淡出”（其他皮肤自动忽略）
    ScrollFader::attach(m_scrollArea);

    m_listBox = new QskLinearBox(Qt::Vertical, m_scrollArea);
    m_listBox->setSpacing(1);
    m_listBox->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Minimum);
    m_scrollArea->setScrolledItem(m_listBox);

    // ── 工具行：计数 | 复制 | 清空（取消/关闭由宿主自摆，不属于本组件）──
    m_toolsBar = new QskLinearBox(Qt::Horizontal, layout);
    m_toolsBar->setSpacing(6);

    m_countLabel = new QskTextLabel(QStringLiteral("0 / 0"), m_toolsBar);
    m_countLabel->setAlignment(Qt::AlignVCenter);

    m_copyBtn = new QskPushButton(QStringLiteral("复制"), m_toolsBar);
    m_clearBtn = new QskPushButton(QStringLiteral("清空"), m_toolsBar);
    m_toolsBar->addSpacer(0, 0);

    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(150);
    connect(m_debounceTimer, &QTimer::timeout, this, &LogListView::rebuild);

    connect(m_levelCombo, &QskComboBox::currentIndexChanged,
        this, [this](int) { m_debounceTimer->start(); });
    connect(m_searchField, &QskTextInput::textChanged,
        this, [this]() { m_debounceTimer->start(); });
    connect(m_copyBtn, &QskAbstractButton::clicked, this, [this]() {
        Q_EMIT copyClicked(filteredText());
    });
    connect(m_clearBtn, &QskAbstractButton::clicked, this, [this]() {
        Q_EMIT clearClicked();
    });
}

void LogListView::appendItem(const RowItem& item)
{
    m_items.push_back(item);
    if (match(item)) {
        addRow(item);
        if (m_autoScroll)
            scrollToBottom();
    }
    updateCount();
}

void LogListView::clearItems()
{
    m_items.clear();
    qDeleteAll(m_rows);
    m_rows.clear();
    updateCount();
}

void LogListView::setLevelIndex(int index)
{
    if (m_levelCombo && m_levelCombo->currentIndex() != index)
        m_levelCombo->setCurrentIndex(index);
}

int LogListView::levelIndex() const
{
    return m_levelCombo ? m_levelCombo->currentIndex() : 0;
}

void LogListView::setSearchText(const QString& text)
{
    if (m_searchField)
        m_searchField->setText(text);
}

void LogListView::setLevelComboOptions(const QStringList& labels)
{
    if (m_levelCombo)
        m_levelCombo->setOptions(labels);
}

void LogListView::setAutoScroll(bool on)
{
    m_autoScroll = on;
}

void LogListView::setListPreferredHeight(qreal h)
{
    if (!m_scrollArea)
        return;
    if (h < 0)
        m_scrollArea->resetExplicitSizeHint(Qt::PreferredSize);
    else
        m_scrollArea->setPreferredHeight(h);
}

void LogListView::setCountLabelFormat(const QString& fmt)
{
    m_countFormat = fmt.isEmpty() ? QStringLiteral("%1 / %2")
                                  : fmt;
    updateCount();
}

void LogListView::setCountLabelVisible(bool on)
{
    if (m_countLabel)
        m_countLabel->setVisible(on);
}

void LogListView::setCopyButtonVisible(bool on)
{
    if (m_copyBtn)
        m_copyBtn->setVisible(on);
}

void LogListView::setClearButtonVisible(bool on)
{
    if (m_clearBtn)
        m_clearBtn->setVisible(on);
}

QString LogListView::filteredText(const QString& separator) const
{
    QString out;
    for (auto* row : m_rows) {
        if (!out.isEmpty())
            out += separator;
        out += row->text();
    }
    return out;
}

QskComboBox* LogListView::levelCombo() const { return m_levelCombo; }
QskTextField* LogListView::searchField() const { return m_searchField; }

bool LogListView::match(const RowItem& item) const
{
    const int idx = levelIndex();
    if (idx > 0 && item.level != idx)
        return false;
    const QString text = m_searchField ? m_searchField->text() : QString();
    if (!text.isEmpty()) {
        if (!item.tag.contains(text, Qt::CaseInsensitive) &&
            !item.message.contains(text, Qt::CaseInsensitive))
            return false;
    }
    return true;
}

void LogListView::addRow(const RowItem& item)
{
    auto* row = new QskTextLabel(m_listBox);
    row->setText(item.text);
    if (item.color.isValid())
        row->setTextColor(item.color);
    row->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    m_rows.append(row);
}

void LogListView::rebuild()
{
    qDeleteAll(m_rows);
    m_rows.clear();
    for (const auto& item : m_items) {
        if (match(item))
            addRow(item);
    }
    if (m_autoScroll)
        scrollToBottom();
    updateCount();
}

void LogListView::scrollToBottom()
{
    if (!m_scrollArea)
        return;
    m_scrollArea->setScrollPos(QPointF(0, 0));
    const QSizeF size = m_scrollArea->scrollableSize();
    m_scrollArea->scrollTo(QPointF(0, size.height()));
}

void LogListView::updateCount()
{
    const int visible = m_rows.size();
    const int total = totalCount();
    if (m_countLabel)
        m_countLabel->setText(m_countFormat
            .arg(QString::number(visible))
            .arg(QString::number(total)));
    Q_EMIT countChanged(visible, total);
}