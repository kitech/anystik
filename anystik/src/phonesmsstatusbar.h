#ifndef PHONE_SMS_STATUS_BAR_H
#define PHONE_SMS_STATUS_BAR_H

#include <QskLinearBox.h>
#include "myi18n.h"

class PhoneSmsListPopup;
class QskPushButton;
class QskTextLabel;
class QTimer;

class PhoneSmsStatusBar : public QskLinearBox
{
    Q_OBJECT
public:
    PhoneSmsStatusBar(QQuickItem* parent = nullptr);
    ~PhoneSmsStatusBar() override;
    Q_INVOKABLE void retranslateUi();

private:
    void updateStatus();
    void updateDeviceStatus();
    void attachPhoneMonitor();
    void openListsPopup();
    void refreshList();
    QString stateLabel(const QString& state) const;
    QString attributionOf(const QString& number) const;

    QskTextLabel* m_rootLabel = nullptr;
    QskTextLabel* m_chargeLabel = nullptr;
    QskTextLabel* m_statusLabel = nullptr;
    QskPushButton* m_listBtn = nullptr;
    PhoneSmsListPopup* m_popup = nullptr;
    QTimer* m_deviceTimer = nullptr;
    bool m_attached = false;
};

#endif