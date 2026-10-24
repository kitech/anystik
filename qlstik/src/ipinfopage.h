#ifndef QLSTIK_IPINFO_PAGE_H
#define QLSTIK_IPINFO_PAGE_H

#include "page.h"

#include <qstring.h>
#include <qstringlist.h>
#include <vector>

class QLabel;
class QPushButton;
class QNetworkAccessManager;
class QNetworkReply;
class QTextEdit;

#ifdef QT3_BUILD
class QScrollView;
#else
class QScrollArea;
#endif

class IPInfoPage : public Page
{
    Q_OBJECT
public:
    IPInfoPage(QWidget* parent = 0);

protected:
    void onCreate();

private slots:
    void doRefresh();
    void onExitReplyFinished();

private:
    void refreshLocal();
    void fetchExits();
    void updateExitText();

    QLabel* m_title;
    QLabel* m_localTitle;
    QLabel* m_exitTitle;
    QTextEdit* m_localEdit;
    QTextEdit* m_exitEdit;
#ifdef QT3_BUILD
    QScrollView* m_localScroll;
    QScrollView* m_exitScroll;
#else
    QScrollArea* m_localScroll;
    QScrollArea* m_exitScroll;
#endif
    QPushButton* m_refreshBtn;
    QLabel* m_statusLabel;

    QNetworkAccessManager* m_nam;
    std::vector<QNetworkReply*> m_replies;
    QStringList m_exitLines;
    int m_done;
};

#endif // QLSTIK_IPINFO_PAGE_H
