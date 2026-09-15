#ifndef MYTAPHANDLER_H
#define MYTAPHANDLER_H

#include <QObject>
#include <QPointF>
#include <QTimer>

class QQuickItem;
class QEvent;

class MyTapHandler : public QObject
{
    Q_OBJECT
public:
    explicit MyTapHandler(QObject* parent = nullptr);

    // Scroll box calls from childMouseEventFilter().
    // Returns true if event was consumed (DblClick).
    bool filterChildEvent(QQuickItem* child, QEvent* event);

    // Scroll box calls from event().
    // Returns true if event was consumed.
    bool filterEvent(QEvent* event);

Q_SIGNALS:
    void singleClicked(const QPointF& scenePos);
    void doubleClicked(const QPointF& scenePos);
    void longPressed(const QPointF& scenePos);

private:
    QTimer m_clickTimer;
    QTimer m_longPressTimer;
    QPointF m_pressScenePos;
    QPointF m_clickScenePos;
    bool m_longPressFired = false;
};

#endif // MYTAPHANDLER_H
