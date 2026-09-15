#ifndef SCROLLFADER_H
#define SCROLLFADER_H

#include <QObject>
#include <QColor>

class QskScrollView;
class QTimer;
class QVariantAnimation;

// 桌面（皮肤=Fusion）专属：滚动条“滚动时短暂显现 → 空闲淡出”。
// QSKinny 0.8.0 的 Fusion 皮肤把滚动条设计成仅 hover/pressed 时由内边距
// 折叠展开为可见滑块（QskFusionSkin.cpp setupScrollView），滚轮滚动不触发
// hover，故平时与滚动时都不可见。本类在实例层覆盖滚动条子控件 hint：
//   1) padding=0 解除折叠，滑块恒 8px；
//   2) 三态渐变色全置透明，显隐完全由 scrollPos 活动驱动的 alpha 动画接管。
class ScrollFader : public QObject
{
    Q_OBJECT
public:
    // 对 view 附加一个 ScrollFader（幂等）。仅当当前皮肤为 “Fusion” 时生效，
    // 其他皮肤（Fluent2/Material3 等）自有滚动条设计，不叠加。
    static void attach(QskScrollView* view);

private:
    explicit ScrollFader(QskScrollView* view);

    bool isScrollable() const;
    void setHandleAlpha(qreal t);
    void onScroll();
    void startFadeOut();

    QskScrollView* m_view = nullptr;
    QTimer* m_idleTimer = nullptr;
    QVariantAnimation* m_fade = nullptr;
    QColor m_baseColor;
};

#endif // SCROLLFADER_H