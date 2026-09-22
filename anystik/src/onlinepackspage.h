#ifndef ONLINE_PACKS_PAGE_H
#define ONLINE_PACKS_PAGE_H

#include "page.h"

class QskTextLabel;
class QskComboBox;
class QskTextField;
class QskPushButton;
class SearchResultGrid;
class ImageSearchClient;
class SiteListClient;
class RemoteImagePreview;

// 在线表情页：
//  - 第一行 combo：站点(0..6) + 搜索引擎 Google图片(7)/Bing图片(8)/Yandex图片(9)
//    ；🌐 打开所选：站点→主页，搜索引擎→带关键词的图片搜索页；
//    「浏览」按钮（无需关键词）对站点 0..6 做应用内列表加载
//  - 第二行：关键词输入（默认/持久化「斗图表情最新最热」）+ 搜索按钮
//    ；Google 反爬强、无法应用内解析 → 浏览器打开；Bing/Yandex → 应用内后台
//    搜索（隐藏 GET + 解析原图 URL），结果网格展示，点击=页内预览（不导入）
//  - 网格下方「加载更多」按钮：UI 预留，暂时空响应
class OnlinePacksPage : public Page
{
    Q_OBJECT
public:
    OnlinePacksPage(QQuickItem* parent = nullptr);

    Q_INVOKABLE void retranslateUi() override;

protected:
    void onCreate(const QVariantMap& launchArgs,
                  const QVariantMap& savedState) override;
    void onStop() override;

private:
    void doSearch();
    void doBrowse();
    void openCurrentInBrowser();

    QskTextLabel* m_title = nullptr;
    QskComboBox* m_siteCombo = nullptr;
    QskTextField* m_keywordEdit = nullptr;
    QskPushButton* m_searchBtn = nullptr;
    QskPushButton* m_browseBtn = nullptr;
    QskPushButton* m_loadMoreBtn = nullptr;
    QskTextLabel* m_statusLabel = nullptr;
    SearchResultGrid* m_resultGrid = nullptr;
    ImageSearchClient* m_searchClient = nullptr;
    SiteListClient* m_siteClient = nullptr;
    RemoteImagePreview* m_preview = nullptr;
};

#endif // ONLINE_PACKS_PAGE_H