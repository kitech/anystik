#ifndef MENU_OVERLAY_H
#define MENU_OVERLAY_H

#include <QQuickItem>
#include <QPointer>

class QskMenu;

class MenuOverlay : public QQuickItem
{
    Q_OBJECT
public:
    /*
     * Transparent full-area overlay that intercepts pointer events
     * outside a QskMenu, enabling click-to-dismiss on sibling controls.
     *
     * Problem:
     *   QSkinny's CloseOnPressOutside relies on QskInputGrabber (child of
     *   the popup). Qt Quick's hit-test skips the popup subtree when the
     *   event position falls outside the popup's bounding rect. Since the
     *   menu popup is only as large as its content, clicks on sibling
     *   controls never reach the InputGrabber.
     *
     * Solution:
     *   Insert a transparent item between the page content (below) and the
     *   menu (above) in the scene graph. Clicks on menu items reach the
     *   menu first (higher z-order via stackBefore). Clicks outside land
     *   on this overlay, which calls menu->close() and ignores the event
     *   so it propagates to the intended target control.
     *
     * Origin:
     *   Qt Quick Controls 2's QQuickOverlay (z-index 1000001,
     *   qtdeclarative/src/quicktemplates/qquickoverlay.cpp) uses the
     *   same pattern: a full-window transparent item intercepting pointer
     *   events for all open popups.
     */
    MenuOverlay(QskMenu* menu);

protected:
    bool event(QEvent* event) override;

private:
    bool handlePress(const QPointF& scenePos);
    QPointer<QskMenu> m_menu;
};

#endif
