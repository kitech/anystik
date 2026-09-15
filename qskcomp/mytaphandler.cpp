#include "mytaphandler.h"
#include <QEvent>
#include <QMouseEvent>
#include <QQuickItem>
#include <QStyleHints>
#include <QGuiApplication>
#include <QtMath>

MyTapHandler::MyTapHandler(QObject* parent)
    : QObject(parent)
{
    m_clickTimer.setSingleShot(true);
    m_longPressTimer.setSingleShot(true);

    connect(&m_clickTimer, &QTimer::timeout, this, [this]() {
        Q_EMIT singleClicked(m_clickScenePos);
    });

    connect(&m_longPressTimer, &QTimer::timeout, this, [this]() {
        m_longPressFired = true;
        m_clickTimer.stop();
        Q_EMIT longPressed(m_pressScenePos);
    });
}

bool MyTapHandler::filterChildEvent(QQuickItem* child, QEvent* event)
{
    Q_UNUSED(child)

    if (event->type() == QEvent::MouseButtonDblClick) {
        m_clickTimer.stop();
        m_longPressTimer.stop();
        auto* me = static_cast<QMouseEvent*>(event);
        Q_EMIT doubleClicked(me->scenePosition());
        return true;
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        m_pressScenePos = me->scenePosition();
        m_longPressFired = false;
        m_longPressTimer.start(500);
    }

    return false;
}

bool MyTapHandler::filterEvent(QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        m_pressScenePos = me->scenePosition();
        m_longPressFired = false;
        m_longPressTimer.start(500);
    } else if (event->type() == QEvent::MouseMove) {
        auto* me = static_cast<QMouseEvent*>(event);
        qreal dist = (me->scenePosition() - m_pressScenePos).manhattanLength();
        if (dist > qApp->styleHints()->startDragDistance()) {
            m_longPressTimer.stop();
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        m_longPressTimer.stop();
        if (!m_longPressFired) {
            auto* me = static_cast<QMouseEvent*>(event);
            qreal dist = (me->scenePosition() - m_pressScenePos).manhattanLength();
            if (dist < qApp->styleHints()->startDragDistance()) {
                m_clickScenePos = me->scenePosition();
                m_clickTimer.start(qApp->styleHints()->mouseDoubleClickInterval());
            }
        }
        m_longPressFired = false;
    }

    return false;
}
