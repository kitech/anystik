#ifndef IPINFO_PAGE_H
#define IPINFO_PAGE_H

#include "page.h"
#include <QStringList>
#include "multilinetextedit.h"

class QskTextLabel;
class QskPushButton;
class QNetworkAccessManager;
class QNetworkReply;

// 全平台 IP 信息页：本机地址列表（netut JSON）+ 5 源出口 IP 对比
class IPInfoPage : public Page
{
    Q_OBJECT
public:
    IPInfoPage(QQuickItem* parent = nullptr);
    Q_INVOKABLE void retranslateUi() override;

protected:
    void onCreate(const QVariantMap& launchArgs,
                  const QVariantMap& savedState) override;

private:
    void refreshLocal();
    void fetchExits();
    void doRefresh();

    QskTextLabel* m_title = nullptr;
    MultiLineTextEdit* m_localLabel = nullptr;
    MultiLineTextEdit* m_exitLabel = nullptr;
    QskTextLabel* m_statusLabel = nullptr;
    QskPushButton* m_refreshBtn = nullptr;
    QNetworkAccessManager* m_nam = nullptr;
    QVector<QNetworkReply*> m_replies;
    QStringList m_exitLines;
    int m_done = 0;
};

#endif // IPINFO_PAGE_H