#include "multilinetextedit.h"
#include <QskBox.h>
#include <QskBoxShapeMetrics.h>
#include <QskFontRole.h>
#include <QskQuick.h>
#include <QskTextLabel.h>

#include <private/qquicktextedit_p.h>

#include <QCoreApplication>
#include <QEvent>
#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickItem>
#include <QRectF>

MultiLineTextEdit::MultiLineTextEdit(QQuickItem* parent)
    : QskControl(parent)
{
    setPolishOnResize(true);
    setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Constrained);

    setFocusPolicy(Qt::StrongFocus);                  // 外壳为焦点对象（qskinny 输入法走线）
    setFlag(QQuickItem::ItemAcceptsInputMethod);      // 外壳是 IM 目标（仿 QskTextInput）

    m_background = new QskBox(this);
    m_background->setBoxShapeHint(QskBox::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));

    m_placeholder = new QskTextLabel(this);
    m_placeholder->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_placeholder->setFontRole(QskFontRole::Caption);

    m_edit = new QQuickTextEdit(this);
    m_edit->setWrapMode(QQuickTextEdit::WrapAnywhere);
    m_edit->setClip(true);
    m_edit->setFocusOnPress(false);                   // 焦点/输入法由外壳接管
    m_edit->setFlag(QQuickItem::ItemAcceptsInputMethod, false);
    m_edit->setAcceptedMouseButtons(Qt::NoButton);    // 鼠标事件由外壳转发

    connect(m_edit, &QQuickTextEdit::textChanged, this, [this]() {
        if (m_maxLength > 0) {
            const QString t = m_edit->text();
            if (t.size() > m_maxLength) {
                m_edit->setText(t.left(m_maxLength));
                m_edit->setCursorPosition(m_maxLength);
            }
        }
        updatePlaceholder();
        Q_EMIT textEdited();
    });
}

void MultiLineTextEdit::setText(const QString& text)
{
    m_edit->setText(text);
    if (!text.isEmpty())
        m_edit->setCursorPosition(text.size());
    updatePlaceholder();
}

QString MultiLineTextEdit::text() const
{
    return m_edit->text();
}

void MultiLineTextEdit::setMaxLength(int max)
{
    m_maxLength = max;
}

void MultiLineTextEdit::setPlaceholderText(const QString& text)
{
    m_placeholder->setText(text);
    updatePlaceholder();
}

void MultiLineTextEdit::activate()
{
    setFocus(true);
    m_engaged = true;
    m_edit->setCursorVisible(true);
    qskInputMethodSetVisible(this, true);
    updatePlaceholder();
}

bool MultiLineTextEdit::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride)
        return QCoreApplication::sendEvent(m_edit, event);
    return Inherited::event(event);
}

void MultiLineTextEdit::keyPressEvent(QKeyEvent* event)
{
    if (!m_engaged) {
        m_engaged = true;
        m_edit->setCursorVisible(true);
        updatePlaceholder();
    }
    QCoreApplication::sendEvent(m_edit, event);
}

void MultiLineTextEdit::keyReleaseEvent(QKeyEvent* event)
{
    QCoreApplication::sendEvent(m_edit, event);
}

void MultiLineTextEdit::inputMethodEvent(QInputMethodEvent* event)
{
    QCoreApplication::sendEvent(m_edit, event);
}

QVariant MultiLineTextEdit::inputMethodQuery(Qt::InputMethodQuery q) const
{
    return m_edit->inputMethodQuery(q);
}

QVariant MultiLineTextEdit::inputMethodQuery(Qt::InputMethodQuery q, const QVariant& a) const
{
    return m_edit->inputMethodQuery(q, a);
}

void MultiLineTextEdit::focusInEvent(QFocusEvent* event)
{
    m_engaged = true;
    m_edit->setCursorVisible(true);
    updatePlaceholder();
    Inherited::focusInEvent(event);
}

void MultiLineTextEdit::focusOutEvent(QFocusEvent* event)
{
    m_engaged = false;
    m_edit->setCursorVisible(false);
    qskInputMethodSetVisible(this, false);
    updatePlaceholder();
    Inherited::focusOutEvent(event);
}

void MultiLineTextEdit::mousePressEvent(QMouseEvent* event)
{
    QCoreApplication::sendEvent(m_edit, event);
    if (!m_engaged)
        activate();
}

void MultiLineTextEdit::mouseMoveEvent(QMouseEvent* event)
{
    QCoreApplication::sendEvent(m_edit, event);
}

void MultiLineTextEdit::mouseReleaseEvent(QMouseEvent* event)
{
    QCoreApplication::sendEvent(m_edit, event);
}

void MultiLineTextEdit::mouseDoubleClickEvent(QMouseEvent* event)
{
    QCoreApplication::sendEvent(m_edit, event);
}

void MultiLineTextEdit::updateLayout()
{
    if (!m_colorApplied) {
        // 文字色对齐皮肤（placeholder 为 QskTextLabel，默认生效皮肤文本色）
        const QColor c = m_placeholder->textColor();
        if (c.isValid()) {
            m_edit->setColor(c);
            m_colorApplied = true;
        }
    }
    const QRectF r = contentsRect();
    m_background->setGeometry(r);
    const QRectF textRect = r.adjusted(8, 4, -8, -4);   // 显式几何 → 折行成立
    m_edit->setX(textRect.x());                          // QQuickTextEdit 为裸 QQuickItem，无 setGeometry
    m_edit->setY(textRect.y());
    m_edit->setWidth(textRect.width());
    m_edit->setHeight(textRect.height());
    m_placeholder->setGeometry(textRect);
}

void MultiLineTextEdit::updatePlaceholder()
{
    m_placeholder->setVisible(
        m_edit->text().isEmpty() && !m_engaged);
}