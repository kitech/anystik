#include "toastpopup.h"
#include "androidutils.h"

#include <QskBox.h>
#include <QskBoxShapeMetrics.h>
#include <QskLinearBox.h>
#include <QskTextLabel.h>
#include <QskTextOptions.h>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>

namespace
{
    QRectF toastParentRect(QQuickItem* parent)
    {
        if (!parent)
            return {};

        if (auto* w = parent->window())
            return QRectF(QPointF(), w->size());

        return QRectF(-parent->x(), -parent->y(),
                      parent->width(), parent->height());
    }
}

void ToastPopup::show(QQuickItem* parent, const QString& text)
{
    if (text.isEmpty())
        return;

#ifdef Q_OS_ANDROID
    showAndroidToast(text);            // 保原生系统 toast
#else
    auto* toast = new ToastPopup(text, parent);
    connect(toast, &QskPopup::closed, toast, &QObject::deleteLater);

    QTimer::singleShot(0, toast, [toast]() {
        toast->open();
        toast->updateGeometry();
        QTimer::singleShot(1300, toast, [toast]() {
            if (toast->isOpen())
                toast->close();
        });
    });
#endif
}

ToastPopup::ToastPopup(const QString& text, QQuickItem* parent)
    : QskPopup(parent)
{
    setModal(false);                   // 非模态浮层
    setOverlay(false);                 // 不压暗全屏
    setPopupFlag(QskPopup::DeleteOnClose, false);
    setPolishOnResize(true);
    setPolishOnParentResize(true);

    m_panel = new QskBox(this);
    m_panel->setBoxShapeHint(QskBox::Panel,
        QskBoxShapeMetrics(12, Qt::AbsoluteSize));

    m_layout = new QskLinearBox(Qt::Vertical, m_panel);
    m_layout->setMargins(14);
    m_layout->setSpacing(0);

    m_label = new QskTextLabel(text, m_layout);
    m_label->setWrapMode(QskTextOptions::WordWrap);
    m_label->setAlignment(Qt::AlignCenter);
}

void ToastPopup::updateLayout()
{
    updateGeometry();
    m_layout->setGeometry(layoutRect());
}

void ToastPopup::updateGeometry()
{
    const auto parentRect = toastParentRect(parentItem());
    if (parentRect.isEmpty())
        return;

    const auto hint = m_layout->effectiveSizeHint(Qt::PreferredSize, QSizeF());
    const qreal maxW = qMin(0.86 * parentRect.width(), 420.0);
    const qreal w = qBound(120.0, hint.width() + 28, maxW);
    const qreal h = qMin(hint.height() + 24.0, 0.5 * parentRect.height());

    QRectF r(0, 0, w, h);
    r.moveCenter(parentRect.center());   // 窗口水平+垂直居中
    setGeometry(r);
    m_panel->setGeometry(r.translated(-r.topLeft()));
}

#include "moc_toastpopup.cpp"
