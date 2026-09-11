#ifndef TABFOCUS_H
#define TABFOCUS_H

#include <QObject>

class QQuickItem;

// 手动 Tab 焦点管理器。
// QSKinny 默认所有控件 focusPolicy = Qt::NoFocus（activeFocusOnTab = false），
// Qt 的原生 tab 焦点链是全空的，Tab 键按了无效。本过滤器在窗口级接管
// Tab/Shift+Tab，每次按键时对当前可见控件树做 pre-order DFS，找下一个
// 可聚焦控件并 setFocus(Qt::TabFocusReason)，天然覆盖动态创建/重建的
// 组件（QSKinny 官方 QskWindow::keyPressEvent 也是这个思路）。
class MyTabFocus : public QObject
{
    Q_OBJECT
public:
    explicit MyTabFocus(QObject* parent = nullptr);

    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    static QQuickItem* nextTabbable(QQuickItem* root,
        QQuickItem* from, int dir);
    static bool isTabbable(QQuickItem* item);
};

#endif // TABFOCUS_H