#include "myscrollarea.h"
#include <QskEvent.h>
#include <QskScrollView.h>
#include <QskAspect.h>
#include <QskAnimationHint.h>
#include <QEasingCurve>
#include <QEvent>
#include <QTouchEvent>
#include <QWheelEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSizeF>
#include <QtMath>
#include <QGuiApplication>
#include <QStyleHints>
#include <QDateTime>
#include <QTimer>
#include <QtDebug>

// 滚轮齿动画缓动（Qt 原生：OutExpo）
static constexpr QEasingCurve::Type kWheelEasing = QEasingCurve::OutExpo;

MyScrollArea::MyScrollArea(QQuickItem* parent)
    : QskScrollArea(parent)
{
    auto* hints = QGuiApplication::styleHints();
    m_doubleTapInterval = hints->mouseDoubleClickInterval();
    m_doubleTapDistance = hints->touchDoubleTapDistance();

    setWheelEnabled(true);

    // 让 scrollTo() 走 QSKinny ScrollAnimator 插值滑行（已核实：setAnimation 内部
    // 置 animator 位，flickHint() 可解析该 Hint，动画时长/缓动即由此处控制）
    setAnimationHint(QskScrollView::Viewport | QskAspect::Metric,
                     QskAnimationHint(WHEEL_ANIM_MS, kWheelEasing));

    m_eventDebugTimer = new QTimer(this);
    connect(m_eventDebugTimer, &QTimer::timeout, this, &MyScrollArea::dumpEventCounts);
    m_eventDebugTimer->start(3000);

    // 父项可能已在 scene graph 中、windowChanged 早已触发，先立即安装一次，
    // 后续 window 变化仍通过信号跟进。
    storeWindow(window());
    connect(this, &QQuickItem::windowChanged, this, &MyScrollArea::storeWindow);
}

// 跟随本项的 scene window，在其上安装/移除事件过滤器。过滤器在
// QCoreApplication::notify 中先于 QQuickWindow::event 运行，可在滚轮
// 进入 Qt6 投递链路（被纯 QQuickItem 叶子截断）之前接管。
void MyScrollArea::storeWindow(QQuickWindow* window)
{
    if (m_filterWindow) {
        m_filterWindow->removeEventFilter(this);
    }

    m_filterWindow = window;

    if (m_filterWindow) {
        m_filterWindow->installEventFilter(this);
    }
}

#ifndef QT_NO_WHEELEVENT
void MyScrollArea::wheelEvent(QWheelEvent* event)
{
    if (!isWheelEnabled()) {
        QskScrollArea::wheelEvent(event);
        return;
    }

    // 触控板像素滚动：步长小且密集，直接平滑跟随，不叠加动画层
    const QPointF pixel = event->pixelDelta();
    if (!pixel.isNull()) {
        const QSizeF view = viewContentsRect().size();
        const QSizeF content = scrollableSize();
        const qreal maxX = qMax<qreal>(0, content.width() - view.width());
        const qreal maxY = qMax<qreal>(0, content.height() - view.height());
        const QPointF pos = scrollPos() - pixel;
        setScrollPos(QPointF(
            qBound<qreal>(0, pos.x(), maxX),
            qBound<qreal>(0, pos.y(), maxY)));
        event->accept();
        return;
    }

    // 普通鼠标滚轮：Qt 原生 72px/齿，经 scrollTo 动画滑行
    const qreal steps = event->angleDelta().y() / qreal(QWheelEvent::DefaultDeltasPerStep);
    if (qFuzzyIsNull(steps))
        return;

    const QSizeF view = viewContentsRect().size();
    const qreal maxY = qMax<qreal>(0, scrollableSize().height() - view.height());
    const qreal newY = qBound<qreal>(0, scrollPos().y() - steps * WHEEL_PIXELS_PER_NOTCH, maxY);

    scrollTo(QPointF(scrollPos().x(), newY));
    event->accept();
}
#endif

// 窗口级滚轮接管。事件位置此时仍是窗口局部坐标（==场景坐标），经
// mapFromScene 转为本地坐标后判断是否落在本滚动区域内。仅处理纵向滚轮；
// 含 isVisible 守卫防止 QskStackBox 隐藏页（几何与显示页重叠）误吞。
bool MyScrollArea::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_filterWindow || event->type() != QEvent::Wheel)
        return false;

    auto* we = static_cast<QWheelEvent*>(event);
    if (!we || !isWheelEnabled() || !isVisible())
        return false;

    const QPointF localPos = mapFromScene(we->position());
    if (!contentsRect().contains(localPos))
        return false;

    // 触控板像素滚动：直接平滑跟随
    const QPointF pixel = we->pixelDelta();
    if (!pixel.isNull()) {
        if (qFuzzyIsNull(pixel.y()))
            return false;

        const QSizeF view = viewContentsRect().size();
        const qreal maxY = qMax<qreal>(0, scrollableSize().height() - view.height());
        const qreal newY = qBound<qreal>(0, scrollPos().y() - pixel.y(), maxY);
        setScrollPos(QPointF(scrollPos().x(), newY));

        we->accept();

        if (!m_wheelDiagDone) {
            m_wheelDiagDone = true;
            qWarning() << "[MyScrollArea] wheel(pixel) consumed by window filter,"
                       << "local" << localPos << "newY" << newY;
        }

        return true;
    }

    // 普通鼠标滚轮：Qt 原生 72px/齿，scrollTo 动画滑行
    const qreal steps = we->angleDelta().y() / qreal(QWheelEvent::DefaultDeltasPerStep);
    if (qFuzzyIsNull(steps))
        return false;

    const QSizeF view = viewContentsRect().size();
    const qreal maxY = qMax<qreal>(0, scrollableSize().height() - view.height());
    const qreal newY = qBound<qreal>(0, scrollPos().y() - steps * WHEEL_PIXELS_PER_NOTCH, maxY);

    scrollTo(QPointF(scrollPos().x(), newY));
    we->accept();

    if (!m_wheelDiagDone) {
        m_wheelDiagDone = true;
        qWarning() << "[MyScrollArea] wheel consumed by window filter,"
                   << "local" << localPos << "newY" << newY;
    }

    return true;
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
