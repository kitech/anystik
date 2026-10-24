#ifndef QLSTIK_PHONEQ_PAGE_H
#define QLSTIK_PHONEQ_PAGE_H

#include "page.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QTextEdit;

class PhoneQPage : public Page
{
    Q_OBJECT
public:
    PhoneQPage(QWidget* parent = 0);

protected:
    void onCreate(const QVariantMap& launchArgs, const QVariantMap& savedState);

private slots:
    void doQuery();
    void onDbStatusChanged();
    void onBackClicked();

private:
    void updateStatusLabel();
    void updateMetaLabel();

    QLabel* m_title;
    QLineEdit* m_numberEdit;
    QPushButton* m_queryBtn;
    QTextEdit* m_resultEdit;
    QLabel* m_metaLabel;
    QLabel* m_statusLabel;
};

#endif // QLSTIK_PHONEQ_PAGE_H
