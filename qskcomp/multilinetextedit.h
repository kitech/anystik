#ifndef QSK_MULTILINE_TEXT_EDIT_H
#define QSK_MULTILINE_TEXT_EDIT_H

#include <QskControl.h>
#include <QString>
#include <QVariant>

class QQuickTextEdit;
class QskBox;
class QskTextLabel;

// 真多行编辑器：内嵌 QQuickTextEdit。回车/Ctrl+回车 插 \n、显式几何下自动折行、
// 光标/中文 IME 全原生（QskTextInput/QskTextField 内嵌单行 QQuickTextInput 做不到）。
// 依赖 Qt6::QuickPrivate（<private/qquicktextedit_p.h>），复用工程须自行链接。
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
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void updateLayout() override;

private:
    void updatePlaceholder();

    QskBox* m_background = nullptr;
    QskTextLabel* m_placeholder = nullptr;
    QQuickTextEdit* m_edit = nullptr;
    int m_maxLength = 0;
    bool m_colorApplied = false;
    bool m_engaged = false;      // 外壳：编辑态（占位符/光标/输入法面板开关）
};

#endif // QSK_MULTILINE_TEXT_EDIT_H