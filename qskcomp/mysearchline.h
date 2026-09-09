#ifndef MYSEARCHLINE_H
#define MYSEARCHLINE_H

#include <QskControl.h>
#include <QskSkin.h>

class QskBox;
class QskTextLabel;
class QskTextField;
class QskPushButton;

class MySearchLine : public QskControl
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

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void updateLayout() override;

private:
    void updateCapsuleColor();
    void updateFieldPadding();
    void updateClearButton();

    QskBox* m_capsule = nullptr;
    QskTextLabel* m_iconLabel = nullptr;
    QskTextField* m_field = nullptr;
    QskPushButton* m_clearBtn = nullptr;
};

#endif // MYSEARCHLINE_H