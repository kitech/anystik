#ifndef MYSEARCHLINE_H
#define MYSEARCHLINE_H

#include <QskLinearBox.h>

class QskTextLabel;
class QskTextField;
class QskPushButton;

class MySearchLine : public QskLinearBox
{
    Q_OBJECT
public:
    explicit MySearchLine(QQuickItem* parent = nullptr);

    QString text() const;
    void setPlaceholderText(const QString&);
    QString placeholderText() const;
    void clear();

Q_SIGNALS:
    void textChanged(const QString& text);

private:
    QskTextLabel* m_iconLabel = nullptr;
    QskTextField* m_field = nullptr;
    QskPushButton* m_clearBtn = nullptr;
};

#endif // MYSEARCHLINE_H