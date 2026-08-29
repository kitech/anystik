#ifndef LOG_PAGE_H
#define LOG_PAGE_H

#include "page.h"
#include "logmodel.h"
#include <QTimer>
#include <QVector>

class QskTextLabel;
class QskComboBox;
class QskTextInput;
class QskLinearBox;
class QskScrollView;

class LogPage : public Page
{
    Q_OBJECT
public:
    LogPage(QQuickItem* parent = nullptr);

protected:
    void onCreate(const QVariantMap& launchArgs,
                  const QVariantMap& savedState) override;

private:
    void rebuildList();
    bool matchFilter(const LogModel::Entry& e) const;

    QskComboBox* m_levelCombo = nullptr;
    QskTextInput* m_searchField = nullptr;
    QskTextLabel* m_countLabel = nullptr;
    QskLinearBox* m_listBox = nullptr;
    QTimer* m_debounceTimer = nullptr;
    QVector<QskTextLabel*> m_rows;
};

#endif
