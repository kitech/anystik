#include "myscrollarea.h"
#include <QskEvent.h>
#include <QEvent>
#include <QTouchEvent>
#include <QtMath>
#include <QGuiApplication>
#include <QStyleHints>
#include <QDateTime>
#include <QTimer>

MyScrollArea::MyScrollArea(QQuickItem* parent)
    : QskScrollArea(parent)
{
    auto* hints = QGuiApplication::styleHints();
    m_doubleTapInterval = hints->mouseDoubleClickInterval();
    m_doubleTapDistance = hints->touchDoubleTapDistance();

    m_eventDebugTimer = new QTimer(this);
    connect(m_eventDebugTimer, &QTimer::timeout, this, &MyScrollArea::dumpEventCounts);
    m_eventDebugTimer->start(3000);
}

// 双击 guard：检测到双击后，在 m_doubleTapGuardUntil（now+500ms）之前返回 true。
// 用于抑制 QQuickTapHandler 的 longPressed 误触发——Android 上 passiveGrab 失败导致
// 第一次点击的 longPressTimer 无法在 TouchEnd 时取消，500ms 后仍会触发 longPressed。
bool MyScrollArea::isDoubleTapGuardActive() const
{
    if (m_doubleTapGuardUntil == 0)
        return false;
    return QDateTime::currentMSecsSinceEpoch() < m_doubleTapGuardUntil;
}

bool MyScrollArea::childMouseEventFilter(QQuickItem* child, QEvent* event)
{
    Q_UNUSED(child)

    static const QMap<QEvent::Type, const char*> eventNames = {
        { QEvent::TouchBegin, "TouchBegin" },
        { QEvent::TouchUpdate, "TouchUpdate" },
        { QEvent::TouchEnd, "TouchEnd" },
        { QEvent::TouchCancel, "TouchCancel" },
        { QEvent::MouseButtonPress, "MouseButtonPress" },
        { QEvent::MouseButtonRelease, "MouseButtonRelease" },
        { QEvent::MouseMove, "MouseMove" },
    };
    auto it = eventNames.find(event->type());
    if (it != eventNames.end()) {
        m_eventCounts[it.value()]++;
    } else {
        m_eventCounts[QLatin1String("Other:") + QString::number(int(event->type()))]++;
    }

    switch (event->type()) {
    case QEvent::TouchBegin: {
        auto* te = static_cast<QTouchEvent*>(event);
        if (!te->points().isEmpty()) {
            QPointF scenePos = te->points().first().scenePosition();
            m_touchStartScene = scenePos;
            m_touchScenePos = scenePos;
            m_scrollStartPos = scrollPos();
            m_touchActive = true;
            m_scrolling = false;

            ulong now = te->timestamp();
            qreal dx = scenePos.x() - m_lastTapScene.x();
            qreal dy = scenePos.y() - m_lastTapScene.y();
            qreal distSq = dx * dx + dy * dy;
            if (m_lastTapTimestamp > 0
                && (now - m_lastTapTimestamp) < (ulong)m_doubleTapInterval
                && distSq < (qreal)m_doubleTapDistance * m_doubleTapDistance) {
                m_lastTapTimestamp = 0;
                m_doubleTapGuardUntil = QDateTime::currentMSecsSinceEpoch() + 999;
                Q_EMIT doubleTapped(scenePos);
            }
        }
        return false;
    }
    case QEvent::TouchUpdate: {
        if (m_touchActive) {
            auto* te = static_cast<QTouchEvent*>(event);
            if (!te->points().isEmpty()) {
                QPointF current = te->points().first().scenePosition();
                m_touchScenePos = current;
                qreal dist = qAbs(current.y() - m_touchStartScene.y());

                if (!m_scrolling && dist >= DRAG_THRESHOLD) {
                    m_scrolling = true;
                    m_scrollStartPos = scrollPos();
                    m_touchStartScene = current;
                }

                if (m_scrolling) {
                    QPointF delta = m_touchStartScene - current;
                    setScrollPos(m_scrollStartPos + delta);
                }
            }
        }
        return false;
    }
    case QEvent::TouchEnd: {
        auto* te = static_cast<QTouchEvent*>(event);
        if (m_touchActive && !m_scrolling) {
            m_lastTapScene = m_touchStartScene;
            m_lastTapTimestamp = te->timestamp();
        }
        m_touchActive = false;
        m_scrolling = false;
        return false;
    }
    default:
        break;
    }

    return false;
}

void MyScrollArea::dumpEventCounts()
{
    if (m_eventCounts.isEmpty())
        return;
    QString msg;
    for (auto it = m_eventCounts.constBegin(); it != m_eventCounts.constEnd(); ++it) {
        if (!msg.isEmpty()) {
            msg += ' ';
        }
        msg += it.key() + ':' + QString::number(it.value());
    }
    qWarning().noquote() << msg;
    m_eventCounts.clear();
}
