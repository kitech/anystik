#ifndef ABOUT_PAGE_H
#define ABOUT_PAGE_H

#include "page.h"

class AboutPage : public Page
{
public:
    AboutPage(QQuickItem* parent = nullptr);

protected:
    void onCreate(const QVariantMap& launchArgs,
                  const QVariantMap& savedState) override;
};

#endif
