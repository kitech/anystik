#ifndef DIALOG_POPUP_H
#define DIALOG_POPUP_H

#include <QskPopup.h>
#include <functional>
#include <memory>
#include <QString>
#include <QStringList>

class QskBox;
class QskLinearBox;
class QskTextLabel;

/*
 * 应用自建确认/选择弹层。
 *
 * 为何不用 QskDialog::question()/select():
 *   - 默认 TopLevelWindow 策略在 Android 上弹出独立顶层 QWindow,收不到触摸,
 *     No/Yes 按钮无法选中(实机报告)。
 *   - 切 EmbeddedBox 后,库内 qskMessageSubWindow 走 exec() + DeleteOnClose,
 *     点击按钮时 deleteLater 在嵌套事件循环返回前被冲刷,随后读取
 *     clickedAction() 造成悬垂指针 UAF(SIGSEGV, 已实测)。
 *
 * 本组件与 StickerHomePage 的 QskMenu 长按菜单同机制:
 *   同 QQuickWindow 内 QskPopup,非阻塞回调,DeleteOnClose 关闭,不依赖库内
 *   exec()。按钮沿用 showRenameDialog 已在用的 QskPopup 内 QskPushButton
 *   点击路径(model 背景压暗 + 实心 QskBox 面板,消除透明感)。
 */
class ConfirmPopup : public QskPopup
{
    Q_OBJECT
public:
    static ConfirmPopup* show(QQuickItem* parent, const QString& title,
                              const QString& text,
                              const QString& yesText, const QString& noText,
                              const std::function<void(bool)>& onResult);

protected:
    void updateLayout() override;

private:
    ConfirmPopup(const QString& title, const QString& text,
                 const QString& yesText, const QString& noText,
                 QQuickItem* parent);
    void updateGeometry();
    void finish(bool accepted);

    QskBox* m_panel = nullptr;
    QskLinearBox* m_layout = nullptr;
    QskTextLabel* m_titleLabel = nullptr;
    QskTextLabel* m_textLabel = nullptr;
    std::shared_ptr<std::function<void(bool)>> m_onResult;
    bool m_finished = false;
};

/* 单列表选择弹层(桌面/Android 通用,非阻塞)。 */
class SelectPopup : public QskPopup
{
    Q_OBJECT
public:
    static SelectPopup* show(QQuickItem* parent, const QString& title,
                             const QStringList& items,
                             const std::function<void(const QString&)>& onPick,
                             bool canCancel = true);

protected:
    void updateLayout() override;

private:
    SelectPopup(const QString& title, const QStringList& items,
                bool canCancel, QQuickItem* parent);
    void updateGeometry();
    void finish(const QString& chosen);

    QskBox* m_panel = nullptr;
    QskLinearBox* m_layout = nullptr;
    QskTextLabel* m_titleLabel = nullptr;
    std::shared_ptr<std::function<void(const QString&)>> m_onPick;
    bool m_finished = false;
};

#endif // DIALOG_POPUP_H