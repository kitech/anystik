#include "multilinetextedit.h"
#include <QskBox.h>
#include <QskBoxShapeMetrics.h>
#include <QskFontRole.h>
#include <QskLabelData.h>
#include <QskMenu.h>
#include <QskPopup.h>
#include <QskQuick.h>
#include <QskTextLabel.h>

#include <private/qquicktextedit_p.h>

#include <QColor>
#include <QCoreApplication>
#include <QEvent>
#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>

// ── 只读拦截层 ──
// 内部编辑件保持始终可编辑（interactionFlags/拖选不破坏），只在接收层吞"改文本"动作，
// 其余（导航/选区/复制/光标/右键）透传原生。勿用 QQuickTextEdit::setReadOnly。
namespace {

inline bool isTextEditingKey(const QKeyEvent* e)
{
    switch (e->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return true;
    default:
        break;
    }

    const Qt::KeyboardModifiers m = e->modifiers();
    const bool ctrl = m & Qt::ControlModifier;
    const bool alt  = m & Qt::AltModifier;

    if (ctrl && (e->key() == Qt::Key_V || e->key() == Qt::Key_X))
        return true;                        // Ctrl+V 粘贴 / Ctrl+X 剪切
    if ((m & Qt::ShiftModifier) && e->key() == Qt::Key_Insert)
        return true;                        // Shift+Insert 粘贴
    if (!e->text().isEmpty() && !ctrl && !alt)
        return true;                        // 直接打印字符（含键盘直输路径）

    return false;                           // 方向/Home/End/PageUp/Down、Ctrl+A/C/Insert 等放行
}

class ReadOnlyTextEdit : public QQuickTextEdit
{
  public:
    explicit ReadOnlyTextEdit(QQuickItem* parent = nullptr)
        : QQuickTextEdit(parent)
    {
    }

    void setReadOnly(bool on)
    {
        m_readOnly = on;
        setFlag(QQuickItem::ItemAcceptsDrops, !on);   // 只读拒拖放文本粘贴
    }

    bool isReadOnly() const { return m_readOnly; }

  protected:
    void keyPressEvent(QKeyEvent* e) override
    {
        if (m_readOnly && isTextEditingKey(e)) {
            e->accept();                    // 吞掉编辑键，文本不动
            return;
        }
        Inherited::keyPressEvent(e);        // 导航/选区分区/Ctrl+AC/光标移动等原生
    }

    void inputMethodEvent(QInputMethodEvent* e) override
    {
        if (m_readOnly && !e->commitString().isEmpty())
            return;                         // 吞 IME 上屏（中文候选提交）
        Inherited::inputMethodEvent(e);     // 预编辑/选区放行
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (m_readOnly && e->button() == Qt::MiddleButton) {
            e->accept();                    // 吞中键 X11 粘贴
            return;
        }
        Inherited::mousePressEvent(e);      // 左右键照常（落光标/拖选）
    }

  private:
    using Inherited = QQuickTextEdit;
    bool m_readOnly = false;
};

} // namespace

MultiLineTextEdit::MultiLineTextEdit(QQuickItem* parent)
    : QskControl(parent)
{
    setPolishOnResize(true);
    setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Constrained);
    setMinimumHeight(96);   // 默认保底高度：未显式 setFixedHeight 时至少可见，防 0 高

    setFocusPolicy(Qt::StrongFocus);                  // 外壳为焦点对象（qskinny 输入法走线）
    setFlag(QQuickItem::ItemAcceptsInputMethod);      // 外壳是 IM 目标（仿 QskTextInput）

    m_background = new QskBox(this);
    m_background->setBoxShapeHint(QskBox::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));

    m_placeholder = new QskTextLabel(this);
    m_placeholder->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_placeholder->setFontRole(QskFontRole::Caption);

    m_edit = new ReadOnlyTextEdit(this);
    m_edit->setWrapMode(QQuickTextEdit::WrapAnywhere);
    m_edit->setClip(true);
    m_edit->setFocusOnPress(true);                    // 点击即 activeFocus（Qt 原生 forceActiveFocus 落位光标）
    m_edit->setSelectByMouse(true);       // 可拖选（含只读结果区：拦截式只读不破坏选择）
    m_edit->setSelectByKeyboard(true);    // Shift+方向 选区 + Ctrl+C
    m_edit->setPersistentSelection(true); // 拖选/右键期间选区高亮不丢
    m_edit->setSelectionColor(QColor(0, 120, 215)); // 显式高亮，避免皮肤白底不可辨
    m_edit->setSelectedTextColor(Qt::white);
    m_edit->setFlag(QQuickItem::ItemAcceptsInputMethod, false);
    m_edit->setAcceptedMouseButtons(Qt::AllButtons);  // 全部鼠标键原生支持：
                                                      // 左=置位/拖选/编辑，中=原生光标/粘贴，
                                                      // 右=内部 ignore 冒泡外壳弹菜单

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

void MultiLineTextEdit::setReadOnly(bool on)
{
    if (m_readOnly == on)
        return;
    m_readOnly = on;
    static_cast<ReadOnlyTextEdit*>(m_edit)->setReadOnly(on);
}

bool MultiLineTextEdit::isReadOnly() const
{
    return m_readOnly;
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
    m_edit->forceActiveFocus(Qt::MouseFocusReason);  // setFocus 只在本 scope 生效，forceActiveFocus 沿焦点链递归取键盘焦点
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
    if (m_readOnly && isTextEditingKey(event))
        return;                         // 只读兜底：焦点在外壳时也拦编辑键
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
    if (m_readOnly && !event->commitString().isEmpty())
        return;                         // 只读兜底：焦点在外壳时也拦 IME 上屏
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
    if (event->button() == Qt::RightButton) {
        showContextMenu(mapToScene(event->position()));
        event->accept();
    }
    // 左键由 m_edit 原生处理（置位/拖选/编辑，focusOnPress 自动获取焦点）
}

void MultiLineTextEdit::showContextMenu(const QPointF& scenePos)
{
    auto* menu = new QskMenu(this);
    menu->setModal(true);
    menu->setPopupFlag(QskPopup::DeleteOnClose, false);
    const int idxCopy = menu->addOption(QskLabelData(tr("复制")));
    const int idxSelectAll = menu->addOption(QskLabelData(tr("全选")));

    // 越界钳制：origin 靠近底部时向上翻转（同 stickerhomepage 主菜单惯例）
    QPointF origin = scenePos;
    const qreal menuH = menu->sizeConstraint().height();
    if (menuH > 0.0) {
        const QRectF bounds = window() ? window()->contentItem()->boundingRect()
                                       : QRectF();
        if (bounds.isValid() && origin.y() + menuH > bounds.bottom())
            origin.setY(qMax(bounds.top(), origin.y() - menuH));
    }
    menu->setOrigin(origin);

    connect(menu, &QskMenu::triggered, this,
        [this, menu, idxCopy, idxSelectAll](int index) {
            if (index == idxCopy)
                m_edit->copy();
            else if (index == idxSelectAll)
                m_edit->selectAll();
            menu->close();
        });
    menu->open();
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