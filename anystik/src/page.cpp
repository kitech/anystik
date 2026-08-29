#include "page.h"

Page::Page(QQuickItem* parent)
    : QskControl(parent)
{
}

Page::~Page()
{
}

void Page::onCreate(const QVariantMap&, const QVariantMap&) {}
void Page::onStart() {}
void Page::onResume() {}
void Page::onPause() {}
void Page::onStop() {}
void Page::onDestroy() {}
void Page::onRestart() {}
void Page::onNewIntent(const QVariantMap&) {}
void Page::onSaveInstanceState(QVariantMap&) {}
void Page::onRestoreInstanceState(const QVariantMap&) {}

void Page::finish()
{
    emit finishRequested();
}
