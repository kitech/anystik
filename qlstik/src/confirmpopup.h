#ifndef CONFIRMPOPUP_H
#define CONFIRMPOPUP_H

#include "compat34.h"

#include <qdialog.h>

// 确认弹窗 —— 移植 anysk qskcomp/dialogpopup.{h,cpp}。
// anysk 基于 QskPopup（非模态、回调、关窗 deleteLater 防 UAF）；本类换成
// 桌面惯用的**模态** QDialog：静态 ask() 阻塞 exec()，返回值即用户选择，
// 无回调悬垂问题，Esc / ✕ 天然取消（QDialog 原生）。
// ⚠ Qt3 的 QDialog **没有任何信号**（accepted/rejected/finished 均为 Qt4+；
//   done/accept/reject 是 protected virtual slot，qdialog.h:104-106 实证）——
//   故不用信号，只用 accept()/reject() 槽 + exec() 返回值。按钮用 SIGNAL/SLOT
//   字符串连接 protected 槽（moc 调用不查 C++ 访问级，双端同路径）。
class ConfirmPopup : public QDialog {
    Q_OBJECT
public:
    // 等价 anysk DialogPopup::show(...) 的回调形态：返回 true=点「是」。
    // parent 推荐传调用方页面。
    static bool ask(QWidget* parent,
                    const QString& title, const QString& text,
                    const QString& yesText, const QString& noText);

private:
    ConfirmPopup(QWidget* parent, const QString& title, const QString& text,
                 const QString& yesText, const QString& noText);
};

#endif // CONFIRMPOPUP_H
