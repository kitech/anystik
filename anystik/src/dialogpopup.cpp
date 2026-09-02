#include "dialogpopup.h"

#include <QskBox.h>
#include <QskBoxShapeMetrics.h>
#include <QskLinearBox.h>
#include <QskTextLabel.h>
#include <QskPushButton.h>
#include <QskTextOptions.h>
#include <QskFunctions.h>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>

namespace
{
    QRectF dialogParentRect(QQuickItem* parent)
    {
        if (!parent)
            return {};

        if (auto* w = parent->window())
            return QRectF(QPointF(), w->size());

        // fallback: parent's own bounding rect
        return QRectF(-parent->x(), -parent->y(),
                      parent->width(), parent->height());
    }
}

/* ── ConfirmPopup ─────────────────────────────────────────────── */

ConfirmPopup::ConfirmPopup(const QString& title, const QString& text,
                           const QString& yesText, const QString& noText,
                           QQuickItem* parent)
    : QskPopup(parent)
{
    setModal(true);
    setOverlay(true);                       // 全屏压暗遮罩,增强"对话框"感
    setPopupFlag(QskPopup::DeleteOnClose, false);
    setPolishOnResize(true);
    setPolishOnParentResize(true);

    // 不透明面板(QskBox 自带 Panel 背景,消除弹出层透明感)
    m_panel = new QskBox(this);
    m_panel->setBoxShapeHint(QskBox::Panel,
        QskBoxShapeMetrics(14, Qt::AbsoluteSize));

    m_layout = new QskLinearBox(Qt::Vertical, m_panel);
    m_layout->setMargins(18);
    m_layout->setSpacing(12);
    m_layout->setSizePolicy(
        QskSizePolicy::MinimumExpanding, QskSizePolicy::Constrained);

    m_titleLabel = new QskTextLabel(title, m_layout);
    m_titleLabel->setWrapMode(QskTextOptions::WordWrap);
    m_titleLabel->setAlignment(Qt::AlignCenter);

    m_textLabel = new QskTextLabel(text, m_layout);
    m_textLabel->setWrapMode(QskTextOptions::WordWrap);
    m_textLabel->setAlignment(Qt::AlignCenter);

    auto* buttons = new QskLinearBox(Qt::Horizontal, m_layout);
    buttons->setSpacing(14);

    auto* yesButton = new QskPushButton(yesText, buttons);
    auto* noButton = new QskPushButton(noText, buttons);
    yesButton->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    noButton->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));

    connect(yesButton, &QskPushButton::clicked, this,
        [this]() { finish(true); });
    connect(noButton, &QskPushButton::clicked, this,
        [this]() { finish(false); });
}

ConfirmPopup* ConfirmPopup::show(
    QQuickItem* parent, const QString& title, const QString& text,
    const QString& yesText, const QString& noText,
    const std::function<void(bool)>& onResult)
{
    auto* popup = new ConfirmPopup(title, text, yesText, noText, parent);
    popup->m_onResult =
        std::make_shared<std::function<void(bool)>>(onResult);

    connect(popup, &QskPopup::closed, popup, &QObject::deleteLater);

    QTimer::singleShot(0, popup, [popup]() {
        popup->open();
        popup->updateGeometry();
    });

    return popup;
}

void ConfirmPopup::finish(bool accepted)
{
    if (m_finished)
        return;
    m_finished = true;

    // Not DeleteOnClose: the popup stays alive through this dispatch, so
    // invoking the callback synchronously is safe (no nested exec loop).
    if (m_onResult)
        (*m_onResult)(accepted);

    close();
}

void ConfirmPopup::updateLayout()
{
    updateGeometry();
    m_layout->setGeometry(layoutRect());
}

void ConfirmPopup::updateGeometry()
{
    const auto parentRect = dialogParentRect(parentItem());
    if (parentRect.isEmpty())
        return;

    const auto panelHint = m_layout->effectiveSizeHint(Qt::PreferredSize, QSizeF());
    const qreal spacing = 0;

    const qreal maxW = qMin(0.92 * parentRect.width(), 440.0);
    const qreal maxH = 0.9 * parentRect.height();

    const qreal panelW = qBound(280.0, panelHint.width() + 36, maxW);
    const qreal panelH = qMin(panelHint.height() + 36.0, maxH);

    // panel 尺寸
    const qreal pw = panelW;
    const qreal ph = panelH;

    // 整个 popup 尺寸比 panel 略大,panel 居中
    const qreal w = pw;
    const qreal h = ph;
    QRectF popupRect(0, 0, w, h);
    popupRect.moveCenter(parentRect.center());
    setGeometry(popupRect);

    // panel 填满 popup
    m_panel->setGeometry(popupRect.translated(-popupRect.topLeft()));
}

/* ── SelectPopup ──────────────────────────────────────────────── */

SelectPopup::SelectPopup(const QString& title, const QStringList& items,
                         bool canCancel, QQuickItem* parent)
    : QskPopup(parent)
{
    setModal(true);
    setOverlay(true);
    setPopupFlag(QskPopup::DeleteOnClose, false);
    setPolishOnResize(true);
    setPolishOnParentResize(true);

    m_panel = new QskBox(this);
    m_panel->setBoxShapeHint(QskBox::Panel,
        QskBoxShapeMetrics(14, Qt::AbsoluteSize));

    m_layout = new QskLinearBox(Qt::Vertical, m_panel);
    m_layout->setMargins(18);
    m_layout->setSpacing(10);
    m_layout->setSizePolicy(
        QskSizePolicy::MinimumExpanding, QskSizePolicy::Constrained);

    m_titleLabel = new QskTextLabel(title, m_layout);
    m_titleLabel->setWrapMode(QskTextOptions::WordWrap);
    m_titleLabel->setAlignment(Qt::AlignCenter);

    for (const auto& item : items)
    {
        auto* button = new QskPushButton(item, m_layout);
        button->setBoxShapeHint(QskPushButton::Panel,
            QskBoxShapeMetrics(8, Qt::AbsoluteSize));
        connect(button, &QskPushButton::clicked, this,
            [this, item]() { finish(item); });
    }

    if (canCancel)
    {
        auto* cancel = new QskPushButton(QString::fromUtf8("取消"), m_layout);
        cancel->setBoxShapeHint(QskPushButton::Panel,
            QskBoxShapeMetrics(8, Qt::AbsoluteSize));
        connect(cancel, &QskPushButton::clicked, this,
            [this]() { finish(QString()); });
    }
}

SelectPopup* SelectPopup::show(
    QQuickItem* parent, const QString& title, const QStringList& items,
    const std::function<void(const QString&)>& onPick, bool canCancel)
{
    auto* popup = new SelectPopup(title, items, canCancel, parent);
    popup->m_onPick =
        std::make_shared<std::function<void(const QString&)>>(onPick);

    connect(popup, &QskPopup::closed, popup, &QObject::deleteLater);

    QTimer::singleShot(0, popup, [popup]() {
        popup->open();
        popup->updateGeometry();
    });

    return popup;
}

void SelectPopup::finish(const QString& chosen)
{
    if (m_finished)
        return;
    m_finished = true;

    if (m_onPick)
        (*m_onPick)(chosen);

    close();
}

void SelectPopup::updateLayout()
{
    updateGeometry();
    m_layout->setGeometry(layoutRect());
}

void SelectPopup::updateGeometry()
{
    const auto parentRect = dialogParentRect(parentItem());
    if (parentRect.isEmpty())
        return;

    const auto panelHint = m_layout->effectiveSizeHint(Qt::PreferredSize, QSizeF());

    const qreal maxW = qMin(0.92 * parentRect.width(), 440.0);
    const qreal maxH = 0.9 * parentRect.height();

    const qreal w = qBound(220.0, panelHint.width() + 36, maxW);
    const qreal h = qMin(panelHint.height() + 36.0, maxH);

    QRectF popupRect(0, 0, w, h);
    popupRect.moveCenter(parentRect.center());
    setGeometry(popupRect);

    m_panel->setGeometry(popupRect.translated(-popupRect.topLeft()));
}

#include "moc_dialogpopup.cpp"