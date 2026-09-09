#include "mysearchline.h"
#include <QskBoxShapeMetrics.h>
#include <QskTextLabel.h>
#include <QskTextField.h>
#include <QskPushButton.h>
#include <QskGradient.h>

MySearchLine::MySearchLine(QQuickItem* parent)
    : QskLinearBox(Qt::Horizontal, parent)
{
    setPanel(true);
    setSpacing(4);
    setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    setFixedHeight(44);
    setBoxShapeHint(QskBox::Panel,
        QskBoxShapeMetrics(10, Qt::AbsoluteSize));

    m_iconLabel = new QskTextLabel(QString::fromUtf8("🔍"), this);
    m_iconLabel->setSizePolicy(QskSizePolicy::Fixed, QskSizePolicy::Expanding);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setFixedWidth(28);

    m_field = new QskTextField(this);
    m_field->setGradientHint(QskTextField::Panel, QskGradient(Qt::transparent));
    m_field->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Expanding);

    m_clearBtn = new QskPushButton(QString::fromUtf8("✕"), this);
    m_clearBtn->setSizePolicy(QskSizePolicy::Fixed, QskSizePolicy::Expanding);
    m_clearBtn->setFixedWidth(28);
    m_clearBtn->setVisible(false);

    connect(m_field, &QskTextField::textChanged, this,
        [this](const QString& text) {
            m_clearBtn->setVisible(!text.isEmpty());
            Q_EMIT textChanged(text);
        });

    connect(m_clearBtn, &QskPushButton::clicked, this, [this]() {
        m_field->setText(QString());
        m_field->setEditing(true);
    });
}

QString MySearchLine::text() const
{
    return m_field->text();
}

void MySearchLine::setPlaceholderText(const QString& text)
{
    m_field->setPlaceholderText(text);
}

QString MySearchLine::placeholderText() const
{
    return m_field->placeholderText();
}

void MySearchLine::clear()
{
    m_field->setText(QString());
    m_field->setEditing(true);
}