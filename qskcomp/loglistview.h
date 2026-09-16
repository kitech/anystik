#ifndef QSK_LOG_LIST_VIEW_H
#define QSK_LOG_LIST_VIEW_H

#include <QskControl.h>
#include <QColor>
#include <QString>
#include <QVector>
#include <deque>

class QskComboBox;
class QskTextField;
class QskTextLabel;
class QskLinearBox;
class QskScrollArea;
class QskPushButton;
class QTimer;

class LogListView : public QskControl
{
    Q_OBJECT
public:
    struct RowItem {
        int level = 0;          // 0=Debug 1=Info 2=Warn 3=Error（仅用于级别过滤）
        QString tag;            // 过滤
        QString message;        // 过滤
        QString text;           // 显示文本（宿主预格式化）
        QColor color;           // 行前景色（无效则不染色）
    };

    explicit LogListView(QQuickItem* parent = nullptr);

    void appendItem(const RowItem&);
    void clearItems();

    // ── 过滤（内部 150ms 防抖）──
    void setLevelIndex(int index);            // 0=All 1=Info 2=Warn 3=Error
    int levelIndex() const;
    void setSearchText(const QString& text);
    void setLevelComboOptions(const QStringList&);   // 默认 All/Info/Warn/Error

    // ── 行为 ──
    void setAutoScroll(bool on = true);               // 追加/重建后滚到底
    void setListPreferredHeight(qreal h);             // -1 = 填满
    void setCountLabelFormat(const QString& fmt);     // %1=可见 %2=总数
    void setCountLabelVisible(bool on);
    void setCopyButtonVisible(bool on);
    void setClearButtonVisible(bool on);

    int visibleCount() const { return m_rows.size(); }
    int totalCount() const { return int(m_items.size()); }
    QString filteredText(const QString& separator = QStringLiteral("\n")) const;

    QskComboBox* levelCombo() const;                 // 宿主微调宽度/文案
    QskTextField* searchField() const;               // 宿主微调占位/高度

signals:
    void copyClicked(const QString& text);
    void clearClicked();
    void countChanged(int visible, int total);

private:
    void addRow(const RowItem& item);
    void rebuild();
    bool match(const RowItem& item) const;
    void scrollToBottom();
    void updateCount();

    QskLinearBox* m_filterBar = nullptr;
    QskComboBox* m_levelCombo = nullptr;
    QskTextField* m_searchField = nullptr;
    QskScrollArea* m_scrollArea = nullptr;
    QskLinearBox* m_listBox = nullptr;
    QskLinearBox* m_toolsBar = nullptr;
    QskTextLabel* m_countLabel = nullptr;
    QskPushButton* m_copyBtn = nullptr;
    QskPushButton* m_clearBtn = nullptr;
    QTimer* m_debounceTimer = nullptr;

    std::deque<RowItem> m_items;
    QVector<QskTextLabel*> m_rows;
    QString m_countFormat = QStringLiteral("%1 / %2");
    bool m_autoScroll = true;
};

#endif