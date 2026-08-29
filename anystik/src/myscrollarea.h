#ifndef MYSCROLLAREA_H
#define MYSCROLLAREA_H

#include <QskScrollArea.h>
#include <QPointF>
#include <QMap>
#include <QString>
#include <QtGlobal>

class QTimer;

class MyScrollArea : public QskScrollArea
{
    Q_OBJECT
public:
    explicit MyScrollArea(QQuickItem* parent = nullptr);

    // 上一次触摸事件的场景坐标（供外部获取长按时的触摸位置）
    QPointF lastTouchScenePos() const { return m_touchScenePos; }

    // 双击 guard：检测到双击后返回 true，防止 QQuickTapHandler 的 longPressTimer
    // 在双击序列中误触发 longPressed（Android 上 setPassiveGrab 失败导致 timer 无法取消）
    bool isDoubleTapGuardActive() const;

Q_SIGNALS:
    void doubleTapped(QPointF scenePos);

protected:
    bool childMouseEventFilter(QQuickItem* child, QEvent* event) override;

private:
    static constexpr qreal DRAG_THRESHOLD = 20.0;  // 拖拽判定阈值（像素），超过此距离开始滚动

    int m_doubleTapInterval = 0;   // 系统双击时间间隔（ms）
    int m_doubleTapDistance = 0;    // 系统双击距离阈值（像素）

    QPointF m_touchStartScene;      // 本次触摸的起始场景坐标
    QPointF m_scrollStartPos;       // 开始滚动时的 scrollPos 快照
    QPointF m_touchScenePos;        // 最新触摸点场景坐标（供 lastTouchScenePos 访问）
    QPointF m_lastTapScene;         // 上一次点击的场景坐标（用于双击距离判断）
    ulong m_lastTapTimestamp = 0;   // 上一次 TouchEnd 的时间戳（用于双击时间判断，0=无）
    ulong m_doubleTapGuardUntil = 0; // 双击 guard 截止时间戳（ms），now+500，0=无 guard
    bool m_touchActive = false;     // 是否有活跃的触摸（TouchBegin ~ TouchEnd）
    bool m_scrolling = false;       // 当前触摸是否已进入滚动状态

    QTimer* m_eventDebugTimer = nullptr;
    QMap<QString, int> m_eventCounts;
    void dumpEventCounts();
};

#endif
