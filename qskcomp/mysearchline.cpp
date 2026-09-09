#include "mysearchline.h"
#include <QskAspect.h>
#include <QskSkinManager.h>
#include <QskBox.h>
#include <QskTextInput.h>
#include <QskBoxShapeMetrics.h>
#include <QskBoxBorderMetrics.h>
#include <QskTextLabel.h>
#include <QskTextField.h>
#include <QskPushButton.h>
#include <QskGradient.h>

#include <QPropertyAnimation>

namespace
{
    constexpr qreal ICON_SIZE = 22.0;
    constexpr qreal EDGE_GAP = 6.0;          // 悬浮图标距胶囊左右内边
    constexpr qreal ICON_GAP = 8.0;         // 悬浮图标与文本区的间距（左侧）
    constexpr int CLEAR_FADE_MS = 140;
}

MySearchLine::MySearchLine(QQuickItem* parent)
    : QskControl(parent)
{
    setAutoLayoutChildren(false);
    setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    setFixedHeight(32);

    m_field = new QskTextField(this);
    m_field->setBoxBorderMetricsHint(QskTextInput::TextPanel,
        QskBoxBorderMetrics(0));
    m_field->setBoxShapeHint(QskTextInput::TextPanel,
        QskBoxShapeMetrics(0, Qt::AbsoluteSize));
    m_field->setGradientHint(QskTextInput::TextPanel,
        QskGradient(Qt::transparent));
    m_field->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Expanding);

    m_capsule = new QskBox(this);
    m_capsule->setPanel(true);
    m_capsule->setBoxShapeHint(QskBox::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    updateCapsuleColor();
    m_capsule->setBoxBorderMetricsHint(QskBox::Panel,
        QskBoxBorderMetrics(0));
    m_capsule->setZ(-1);

    connect(qskSkinManager, &QskSkinManager::colorSchemeChanged,
        this, [this](QskSkin::ColorScheme) { updateCapsuleColor(); });
    connect(qskSkinManager, &QskSkinManager::skinChanged,
        this, [this](QskSkin*) { updateCapsuleColor(); });

    m_iconLabel = new QskTextLabel(QString::fromUtf8("🔍"), this);
    m_iconLabel->setSizePolicy(QskSizePolicy::Fixed, QskSizePolicy::Fixed);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setAcceptedMouseButtons(Qt::NoButton);
    m_iconLabel->setZ(1);

    m_clearBtn = new QskPushButton(QString::fromUtf8("✕"), this);
    m_clearBtn->setSizePolicy(QskSizePolicy::Fixed, QskSizePolicy::Fixed);
    m_clearBtn->setGradientHint(QskPushButton::Panel,
        QskGradient(Qt::transparent));
    m_clearBtn->setZ(1);
    m_clearBtn->setOpacity(0.0);
    m_clearBtn->setVisible(false);

    connect(m_clearBtn, &QskPushButton::clicked, this, [this]() {
        m_field->setText(QString());
        m_field->setEditing(true);
    });

    connect(m_field, &QskTextField::textChanged, this,
        [this](const QString& text) {
            updateClearButton();
            Q_EMIT textChanged(text);
        });
}

void MySearchLine::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QskControl::geometryChange(newGeometry, oldGeometry);
    updateLayout();
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

void MySearchLine::updateCapsuleColor()
{
    m_capsule->resetSkinHint(QskBox::Panel | QskAspect::Color);
    QskSkinHintStatus status;
    auto color = m_capsule->color(QskBox::Panel, &status);
    if (status.isValid())
        color = color.lighter(108);      // 仅比皮肤背景亮 8%
    m_capsule->setGradientHint(QskBox::Panel, QskGradient(color));
}

void MySearchLine::updateLayout()
{
    const QRectF r = contentsRect();
    if (r.isEmpty())
        return;

    // 胶囊背景铺满
    m_capsule->setGeometry(r);

    // 输入框铺满（透明，悬浮于胶囊之上）
    m_field->setGeometry(r);

    const QRectF fieldRect = m_field->geometry();
    m_iconLabel->setGeometry(
        QRectF(fieldRect.left() + EDGE_GAP,
               fieldRect.center().y() - ICON_SIZE / 2,
               ICON_SIZE, ICON_SIZE));

    if (m_clearBtn->isVisible()) {
        m_clearBtn->setGeometry(
            QRectF(fieldRect.right() - EDGE_GAP - ICON_SIZE,
                   fieldRect.center().y() - ICON_SIZE / 2,
                   ICON_SIZE, ICON_SIZE));
    }

    // 动态留空：左 = 🔍区(28) + 间距；右 = ✕ 可见 ? ✕区+间距 : 收窄
    const qreal rightPad = m_clearBtn->isVisible()
        ? EDGE_GAP + ICON_SIZE + ICON_GAP
        : EDGE_GAP;
    m_field->setPaddingHint(QskTextInput::TextPanel,
        QMarginsF(EDGE_GAP + ICON_SIZE + ICON_GAP, 0, rightPad, 0));
}

void MySearchLine::updateClearButton()
{
    const bool hasText = !m_field->text().isEmpty();

    if (hasText && !m_clearBtn->isVisible()) {
        m_clearBtn->setVisible(true);
        updateLayout();                       // 先占位定位
        auto* anim = new QPropertyAnimation(m_clearBtn, "opacity", m_clearBtn);
        anim->setDuration(CLEAR_FADE_MS);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    } else if (!hasText && m_clearBtn->isVisible()) {
        auto* anim = new QPropertyAnimation(m_clearBtn, "opacity", m_clearBtn);
        anim->setDuration(CLEAR_FADE_MS);
        anim->setStartValue(1.0);
        anim->setEndValue(0.0);
        connect(anim, &QPropertyAnimation::finished, this,
            [this]() {
                m_clearBtn->setVisible(false);
                updateLayout();
            });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    } else {
        updateLayout();
    }
}