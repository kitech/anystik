#ifndef MAIN_PAGE_H
#define MAIN_PAGE_H

#include "page.h"

// 空壳：原聊天列表页（频道列表/消息导航/推送状态）已移除，
// 此页仅保留类骨架并在构建集内编译，不注册、不使用。
class MainPage : public Page
{
    Q_OBJECT
public:
    explicit MainPage(QQuickItem* parent = nullptr);

protected:
    void onCreate(const QVariantMap& launchArgs,
                  const QVariantMap& savedState) override;
    void onNewIntent(const QVariantMap& launchArgs) override;
};

#endif // MAIN_PAGE_H
