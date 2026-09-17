#ifndef ONLINE_PACKS_PAGE_H
#define ONLINE_PACKS_PAGE_H

#include "page.h"

class QskTextLabel;

// 在线表情占位页：入口为贴纸主页底部导航「在线表情」，
// 当前仅有标题与「功能开发中」提示，后续在此填充在线表情包浏览/下载逻辑。
class OnlinePacksPage : public Page
{
    Q_OBJECT
public:
    OnlinePacksPage(QQuickItem* parent = nullptr);

    Q_INVOKABLE void retranslateUi() override;

protected:
    void onCreate(const QVariantMap& launchArgs,
                  const QVariantMap& savedState) override;

private:
    QskTextLabel* m_title = nullptr;
    QskTextLabel* m_hint = nullptr;
};

#endif // ONLINE_PACKS_PAGE_H