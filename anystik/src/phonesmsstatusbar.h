#ifndef PHONE_SMS_STATUS_BAR_H
#define PHONE_SMS_STATUS_BAR_H

#include <QskLinearBox.h>
#include "myi18n.h"

class PhoneSmsListPopup;
class QskPushButton;
class QskTextLabel;

class PhoneSmsStatusBar : public QskLinearBox
{
    Q_OBJECT
public:
    PhoneSmsStatusBar(QQuickItem* parent = nullptr);
    ~PhoneSmsStatusBar() override;
    Q_INVOKABLE void retranslateUi();

private:
    void updateStatus();
    void openListsPopup();
    void refreshList();
    QString stateLabel(const QString& state) const;

    QskTextLabel* m_statusLabel = nullptr;
    QskPushButton* m_listBtn = nullptr;
    PhoneSmsListPopup* m_popup = nullptr;
};

#endif