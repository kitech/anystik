#ifndef STICKER_GEN_PAGE_H
#define STICKER_GEN_PAGE_H

#include "page.h"

class QskTextLabel;

// 生成表情占位页：入口为贴纸主页底部导航「生成表情」，
// 当前仅有标题与「功能开发中」提示，后续在此填充生成逻辑。
class StickerGenPage : public Page
{
    Q_OBJECT
public:
    StickerGenPage(QQuickItem* parent = nullptr);

    Q_INVOKABLE void retranslateUi() override;

protected:
    void onCreate(const QVariantMap& launchArgs,
                  const QVariantMap& savedState) override;

private:
    QskTextLabel* m_title = nullptr;
    QskTextLabel* m_hint = nullptr;
};

#endif // STICKER_GEN_PAGE_H