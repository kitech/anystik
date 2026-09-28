#include "page.h"

Page::Page(QQuickItem* parent)
    : QskControl(parent)
{
    setFocusPolicy(Qt::StrongFocus);   // 页面可聚焦 → 点亮焦点链头（StackBox→Page→结果区）
    Lang::instance().registerRetranslatable(this);
}

Page::~Page()
{
    Lang::instance().unregister(this);
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
