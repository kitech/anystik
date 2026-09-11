#include "tabfocus.h"

#include <QskAbstractButton.h>
#include <QskComboBox.h>
#include <QskTextInput.h>

#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>

#include <functional>

MyTabFocus::MyTabFocus(QObject* parent)
    : QObject(parent)
{
}

bool MyTabFocus::isTabbable(QQuickItem* item)
{
    if (item == nullptr || !item->isVisible() || !item->isEnabled())
        return false;

    if (qobject_cast<QskTextInput*>(item)
        || qobject_cast<QskComboBox*>(item)
        || qobject_cast<QskAbstractButton*>(item))
        return true;

    // 兜底尊重任何显式 Opt-in
    return item->activeFocusOnTab();
}

QQuickItem* MyTabFocus::nextTabbable(
    QQuickItem* root, QQuickItem* from, int dir)
{
    if (root == nullptr)
        return nullptr;

    QList<QQuickItem*> list;
    std::function<void(QQuickItem*)> collect = [&](QQuickItem* item) {
        if (isTabbable(item))
            list.append(item);
        const auto children = item->childItems();
        for (auto* child : children)
            collect(child);
    };
    collect(root);

    if (list.isEmpty())
        return nullptr;

    int start = -1;
    if (from != nullptr)
    {
        for (int i = 0; i < list.size(); ++i)
        {
            if (list[i] == from || list[i]->isAncestorOf(from))
            {
                start = i;
                break;
            }
        }
    }

    const int idx = (start >= 0) ? start : (dir > 0 ? -1 : list.size());
    int next = idx + dir;
    while (next < 0 || next >= list.size())
        next += (dir > 0) ? -list.size() : list.size();

    return list[next];
}

bool MyTabFocus::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == nullptr || event->type() != QEvent::KeyPress)
        return false;

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->modifiers() & (Qt::ControlModifier | Qt::AltModifier))
        return false;

    int dir = 0;
    switch (keyEvent->key())
    {
        case Qt::Key_Tab:       dir = 1;   break;
        case Qt::Key_Backtab:   dir = -1;  break;
        default:                return false;
    }

    auto* window = qobject_cast<QQuickWindow*>(watched);
    if (window == nullptr)
        return false;

    auto* target = nextTabbable(window->contentItem(),
        window->activeFocusItem(), dir);
    if (target == nullptr)
        return false;

    target->setFocus(true, Qt::TabFocusReason);

    // Tab 进入文本框后保证可直接键入
    if (auto* textInput = qobject_cast<QskTextInput*>(target))
    {
        textInput->setActivationModes(textInput->activationModes()
            | QskTextInput::ActivationOnFocus
            | QskTextInput::ActivationOnKey);
    }

    keyEvent->accept();
    return true;
}