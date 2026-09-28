#ifndef QSK_MULTILINE_TEXT_EDIT_H
#define QSK_MULTILINE_TEXT_EDIT_H

#include <QskControl.h>
#include <QMouseEvent>
#include <QString>
#include <QVariant>

class QQuickTextEdit;
class QskBox;
class QskTextLabel;

// 真多行编辑器：内嵌 QQuickTextEdit。回车/Ctrl+回车 插 \n、显式几何下自动折行、
// 光标/中文 IME 全原生（QskTextInput/QskTextField 内嵌单行 QQuickTextInput 做不到）。
// 依赖 Qt6::QuickPrivate（<private/qquicktextedit_p.h>），复用工程须自行链接。
//
// ✅ 只读（readOnly）采用【拦截式】实现（对齐 Qt/QML TextEdit::readOnly 惯例：
//    "the text cannot be edited by user interaction"，仅禁改文本，其他交互皆保留）：
//  - 内部 QQuickTextEdit 保持【始终可编辑】→ interactionFlags/拖选/复制/光标全原生不破坏；
//  - 仅在接收层吞掉"改动文本"的动作（打印键/Delete/Backspace/Enter/Ctrl+V/X/Shift+Insert、
//    IME commit、中键 X11 粘贴、拖放），其余（鼠标拖选、Shift+方向 键盘选区、Ctrl+A/C、
//    光标移动、滚动、右键菜单）照常可用。
//  ⚠ 勿用 QQuickTextEdit::setReadOnly —— 历史已实测它会重建 interactionFlags、
//    致只读态无法拖选。此处拦截式完全绕开该路径。
class MultiLineTextEdit : public QskControl
{
    Q_OBJECT
public:
    explicit MultiLineTextEdit(QQuickItem* parent = nullptr);

    void setText(const QString& text);
    QString text() const;
    void setMaxLength(int max);
    void setPlaceholderText(const QString& text);
    void activate();                    // 获取焦点 + 唤起输入法面板
    void setReadOnly(bool on);          // 只读=禁改文本，选中/复制/光标/右键照常（Qt 惯例）
    bool isReadOnly() const;

Q_SIGNALS:
    void textEdited();

protected:
    using Inherited = QskControl;

    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery q) const override;
    QVariant inputMethodQuery(Qt::InputMethodQuery q, const QVariant& a) const;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;   // 仅右键弹菜单；左键由 m_edit 原生处理
    void updateLayout() override;

private:
    void updatePlaceholder();
    void showContextMenu(const QPointF& scenePos);

    QskBox* m_background = nullptr;
    QskTextLabel* m_placeholder = nullptr;
    QQuickTextEdit* m_edit = nullptr;
    int m_maxLength = 0;
    bool m_colorApplied = false;
    bool m_engaged = false;      // 外壳：编辑态（占位符/光标/输入法面板开关）
    bool m_readOnly = false;     // 只读=禁改文本（见头注释：拦截式，勿用 QQuickTextEdit::setReadOnly）
};

#endif // QSK_MULTILINE_TEXT_EDIT_H