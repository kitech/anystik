#ifndef PHONEQ_PAGE_H
#define PHONEQ_PAGE_H

#include "page.h"

class QskTextLabel;
class QskTextField;
class QskPushButton;

// 全平台手机号码归属地查询页（离线库，运行时下载）
class PhoneQPage : public Page
{
    Q_OBJECT
public:
    PhoneQPage(QQuickItem* parent = nullptr);
    Q_INVOKABLE void retranslateUi() override;

protected:
    void onCreate(const QVariantMap& launchArgs,
                  const QVariantMap& savedState) override;

private:
    void doQuery();
    void updateStatusLabel();

    QskTextLabel* m_title = nullptr;
    QskTextField* m_numberEdit = nullptr;
    QskPushButton* m_queryBtn = nullptr;
    QskTextLabel* m_resultLabel = nullptr;
    QskTextLabel* m_statusLabel = nullptr;
};

#endif // PHONEQ_PAGE_H