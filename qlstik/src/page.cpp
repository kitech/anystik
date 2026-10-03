#include "page.h"

#ifdef QT3_BUILD
#include <qvariant.h>
#endif

Page::Page(QWidget* parent)
    : QWidget(parent)
    , m_pageManager(0)
    , m_state(PageState::None)
    , m_isFinishing(false)
    , m_resultCode(-1)  // -1 对应 Android RESULT_CANCELED
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

void Page::setResult(int resultCode, const QVariantMap& data)
{
    m_resultCode = resultCode;
    m_resultData = data;
}

void Page::finish()
{
    emit finishRequested();
}
