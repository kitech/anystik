#ifndef LOG_PAGE_H
#define LOG_PAGE_H

#include "page.h"
#include "logmodel.h"

class LogListView;

class LogPage : public Page
{
    Q_OBJECT
public:
    LogPage(QQuickItem* parent = nullptr);

protected:
    void onCreate(const QVariantMap& launchArgs,
                  const QVariantMap& savedState) override;

private:
    LogListView* m_logList = nullptr;
};

#endif
