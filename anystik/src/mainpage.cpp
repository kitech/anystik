#include "mainpage.h"

MainPage::MainPage(QQuickItem* parent)
    : Page(parent)
{
}

void MainPage::onCreate(const QVariantMap& launchArgs,
                        const QVariantMap& savedState)
{
    Q_UNUSED(launchArgs)
    Q_UNUSED(savedState)
}

void MainPage::onNewIntent(const QVariantMap& launchArgs)
{
    Q_UNUSED(launchArgs)
}
