#include "menuoverlay.h"
#include <QskMenu.h>
#include <QskPopup.h>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QEvent>

MenuOverlay::MenuOverlay(QskMenu* menu, QskMenu* parentMenu)
    : QQuickItem(menu ? menu->parentItem() : nullptr)
    , m_menu(menu)
    , m_parentMenu(parentMenu)
{
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptTouchEvents(true);

    if (auto* p = parentItem()) {
        setSize(p->size());
        setPosition(QPointF(0, 0));

        connect(p, &QQuickItem::widthChanged, this, [this]() {
            if (auto* p = parentItem()) setWidth(p->width());
        });
        connect(p, &QQuickItem::heightChanged, this, [this]() {
            if (auto* p = parentItem()) setHeight(p->height());
        });
    }

    if (menu)
        stackBefore(menu);
}

bool MenuOverlay::event(QEvent* event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress:
        handlePress(static_cast<QMouseEvent*>(event)->scenePosition());
        event->ignore();
        return event->isAccepted();

    case QEvent::TouchBegin: {
        auto& touch = *static_cast<QTouchEvent*>(event);
        if (!touch.points().isEmpty())
            handlePress(touch.points().first().scenePosition());
        event->ignore();
        return event->isAccepted();
    }

    default:
        return QQuickItem::event(event);
    }
}

bool MenuOverlay::handlePress(const QPointF& scenePos)
{
    if (!m_menu)
        return false;

    if (!m_menu->isOpen())
        return false;

    if (m_parentMenu && m_parentMenu->isOpen())
    {
        const QPointF localPos = m_parentMenu->mapFromScene(scenePos);
        if (m_parentMenu->contains(localPos))
        {
            // 按下父菜单区域：仅关自身（子菜单），事件放行给父菜单项
            m_menu->close();
            return true;
        }
    }

    QPointF localPos = m_menu->mapFromScene(scenePos);

    if (m_menu->contains(localPos))
        return false;

    m_menu->close();
    if (m_parentMenu)
        m_parentMenu->close();
    return true;
}

#include "moc_menuoverlay.cpp"
