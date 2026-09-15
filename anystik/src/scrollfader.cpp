#include "scrollfader.h"

#include <QskScrollView.h>
#include <QskAspect.h>
#include <QskGradient.h>
#include <QskSkinManager.h>

#include <QTimer>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QtMath>

namespace {
constexpr qreal kVisibleAlpha = 235.0; // 显现时滑块 alpha（0-255）
constexpr int kIdleMs = 1800;          // 停滚 1800ms 后开始淡出（保留 ~1s+ 易抓滑块）
constexpr int kFadeMs = 300;           // 淡出动画时长
}

void ScrollFader::attach(QskScrollView* view)
{
    if (!view || view->property("_anystik.scrollFader").toBool())
        return;

    if (qskSkinManager && qskSkinManager->skinName() != QStringLiteral("Fusion"))
        return;   // 其他皮肤自有滚动条设计，不叠加

    view->setProperty("_anystik.scrollFader", true);
    new ScrollFader(view);   // 以 view 为父对象，随 view 析构
}

ScrollFader::ScrollFader(QskScrollView* view)
    : QObject(view)
    , m_view(view)
{
    using Q = QskScrollView;

    // 1) 解除 Fusion 的 padding 折叠：滑块恒为 8px 条宽，不再依赖 hover 展开
    view->setPaddingHint(Q::VerticalScrollBar, 0.0);

    // 2) 三态渐变色全置透明；显隐/配色完全由本类的 alpha 动画接管，
    //    鼠标悬停滚动条条带也不再闪现 Fusion 默认 Mid 色
    const QColor clear(Qt::transparent);
    view->setGradientHint(Q::VerticalScrollHandle, QskGradient(clear));
    view->setGradientHint(Q::VerticalScrollHandle | Q::Hovered, QskGradient(clear));
    view->setGradientHint(Q::VerticalScrollHandle | Q::Pressed, QskGradient(clear));

    // 滑块源色：恒定中性灰，亮/暗主题均可辨（后续可改由皮肤调色板派生）
    m_baseColor = QColor(0x70, 0x70, 0x70);

    m_idleTimer = new QTimer(this);
    m_idleTimer->setSingleShot(true);
    m_idleTimer->setInterval(kIdleMs);
    connect(m_idleTimer, &QTimer::timeout, this, &ScrollFader::startFadeOut);

    m_fade = new QVariantAnimation(this);
    m_fade->setDuration(kFadeMs);
    m_fade->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_fade, &QVariantAnimation::valueChanged, this,
        [this](const QVariant& v) { setHandleAlpha(1.0 - v.toReal()); });
    connect(m_fade, &QVariantAnimation::finished, this,
        [this]() { setHandleAlpha(0.0); });

    // 滚动活动驱动显现；内容不可滚动时强制透明
    connect(view, &QskScrollView::scrollPosChanged, this, &ScrollFader::onScroll);
    connect(view, &QskScrollView::scrollableSizeChanged, this,
        [this]() { setHandleAlpha(0.0); });

    setHandleAlpha(0.0);
}

bool ScrollFader::isScrollable() const
{
    if (!m_view || !m_view->isVisible())
        return false;
    return m_view->scrollableSize().height()
        > m_view->viewContentsRect().height() + 1.0;
}

void ScrollFader::setHandleAlpha(qreal t)
{
    if (!m_view)
        return;
    QColor c = m_baseColor;
    c.setAlpha(qRound(qBound(0.0, t, 1.0) * kVisibleAlpha));
    m_view->setGradientHint(QskScrollView::VerticalScrollHandle, QskGradient(c));
}

void ScrollFader::onScroll()
{
    if (!isScrollable()) {
        setHandleAlpha(0.0);
        return;
    }

    // 滚动中：立即显现并持续展示，空闲 kIdleMs 后淡出
    m_fade->stop();
    setHandleAlpha(1.0);
    m_idleTimer->start();
}

void ScrollFader::startFadeOut()
{
    if (isScrollable())
        m_fade->start();
    else
        setHandleAlpha(0.0);
}