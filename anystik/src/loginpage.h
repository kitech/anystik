#ifndef LOGIN_PAGE_H
#define LOGIN_PAGE_H

#include "page.h"

class LoginPage : public Page
{
public:
    LoginPage(QQuickItem* parent = nullptr);

protected:
    void onCreate(const QVariantMap& launchArgs,
                  const QVariantMap& savedState) override;
};

#endif
