#ifndef STICKER_HOME_PAGE_H
#define STICKER_HOME_PAGE_H

#include "page.h"
#include "stickerstore.h"
#include <QPointer>
#include <QTimer>

class QskTextField;
class QskTabBar;
class QskPopup;
class StickerGridWidget;

class StickerHomePage : public Page
{
    Q_OBJECT
public:
    StickerHomePage(QQuickItem* parent = nullptr);

protected:
    void onCreate(const QVariantMap& launchArgs,
                  const QVariantMap& savedState) override;

private:
    void refreshTabBar();
    void onTabChanged(int index);
    void loadAllStickers();
    void loadRecentStickers();
    void loadPackStickers(const QString& packId);
    void doSearch(const QString& keyword);

    void showStickerMenu(const StickerBrief& brief, const QPointF& scenePos);
    void openPreview(const StickerBrief& brief);
    void confirmDeleteSticker(const StickerBrief& brief);
    void showOptionsMenu(const QPointF& origin);
    void showPackManageMenu();

    void requestImportFolder();
    void requestPasteSticker();
    void showRenameDialog(const StickerPackBrief& pack);
    void removePack(const StickerPackBrief& pack);
    void openStickerFolder();

    void showDirPicker();

    void showToast(const QString& text);

    QskTextField* m_searchField = nullptr;
    QskTabBar* m_tabBar = nullptr;
    StickerGridWidget* m_grid = nullptr;
    bool m_keepScreenOn = true;

    QTimer m_searchDebounce;
    QVector<StickerPackBrief> m_packs;
    StickerBrief m_ctxBrief;      // 长按的贴纸上下文
    StickerPackBrief m_ctxPack;   // 分组管理上下文
    QString m_activeTab;          // "" = 全部, "__recent" = 最近, 否则 packId
};

#endif // STICKER_HOME_PAGE_H