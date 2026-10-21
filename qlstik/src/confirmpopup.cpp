#include "confirmpopup.h"

#include <qlabel.h>
#include <qlayout.h>
#include <qpushbutton.h>

// anysk DialogPopup 布局照搬：标题标签 + 正文标签 + 右下 [否/是] 两钮。
// Qt3 QDialog 构造 (parent, name, modal) 与 Qt6 (parent) 形参不同，
// 统一走 QDialog(parent) + setModal(true)（Qt3 qdialog.h:92 setModal 实证）。
ConfirmPopup::ConfirmPopup(QWidget* parent, const QString& title,
                           const QString& text,
                           const QString& yesText, const QString& noText)
    : QDialog(parent)
{
    setModal(true);
    qSetWindowTitle(this, title);

    QVBoxLayout* root = new QVBoxLayout(this);
    qSetMargins(root, 16, 16, 16, 16);
    root->setSpacing(10);

    QLabel* titleLabel = new QLabel(title, this);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    root->addWidget(titleLabel);

    QLabel* bodyLabel = new QLabel(text, this);
#ifndef QT3_BUILD
    bodyLabel->setWordWrap(true);   // Qt3 QLabel 无 setWordWrap（头文件实证）
#endif
    root->addWidget(bodyLabel);

    QHBoxLayout* buttons = new QHBoxLayout(0);
    buttons->addStretch(1);
    QPushButton* noBtn = new QPushButton(noText, this);
    QPushButton* yesBtn = new QPushButton(yesText, this);
    buttons->addWidget(noBtn);
    buttons->addWidget(yesBtn);
    root->addLayout(buttons);

    // accept/reject 是 QDialog protected slot：字符串 connect 经 moc 调用，
    // 不受 C++ 访问级限制，Qt3/Qt6 同一路径。✕/Esc 走 QDialog 原生 reject。
    connect(noBtn, SIGNAL(clicked()), this, SLOT(reject()));
    connect(yesBtn, SIGNAL(clicked()), this, SLOT(accept()));
    yesBtn->setDefault(true);
    yesBtn->setFocus();
}

bool ConfirmPopup::ask(QWidget* parent, const QString& title,
                       const QString& text, const QString& yesText,
                       const QString& noText)
{
    ConfirmPopup dlg(parent, title, text, yesText, noText);
    return dlg.exec() == QDialog::Accepted;
}
