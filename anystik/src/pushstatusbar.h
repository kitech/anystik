#ifndef PUSH_STATUS_BAR_H
#define PUSH_STATUS_BAR_H

#include <QskLinearBox.h>

class QskTextLabel;

class PushStatusBar : public QskLinearBox
{
    Q_OBJECT
public:
    PushStatusBar(QQuickItem* parent = nullptr);

private:
    void updateStatus();
    QskTextLabel* m_statusLabel = nullptr;
};

#endif
