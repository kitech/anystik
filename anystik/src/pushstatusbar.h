#ifndef PUSH_STATUS_BAR_H
#define PUSH_STATUS_BAR_H

#include <QskLinearBox.h>
#include "myi18n.h"

class QskTextLabel;

class PushStatusBar : public QskLinearBox
{
    Q_OBJECT
public:
    PushStatusBar(QQuickItem* parent = nullptr);
    ~PushStatusBar() override;
    Q_INVOKABLE void retranslateUi();

private:
    void updateStatus();
    QskTextLabel* m_statusLabel = nullptr;
};

#endif
