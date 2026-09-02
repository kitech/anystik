#ifndef TOAST_POPUP_H
#define TOAST_POPUP_H

#include <QskPopup.h>
#include <QString>

class QskBox;
class QskLinearBox;
class QskTextLabel;

/*
 * Android 风格 toast：非模态、底部居中、延时自动消失。
 * 平台分发：Android 走原生 showAndroidToast（系统浮层，避免 qskinny
 * 顶层窗口/触摸坑）；其余平台用 QQuickWindow 内 QskPopup 桌面浮层，
 * 与 ConfirmPopup/SelectPopup 同机制（非阻塞、closed→deleteLater）。
 */
class ToastPopup : public QskPopup
{
    Q_OBJECT
public:
    static void show(QQuickItem* parent, const QString& text);

protected:
    void updateLayout() override;

private:
    ToastPopup(const QString& text, QQuickItem* parent);
    void updateGeometry();

    QskBox* m_panel = nullptr;
    QskLinearBox* m_layout = nullptr;
    QskTextLabel* m_label = nullptr;
};

#endif // TOAST_POPUP_H
