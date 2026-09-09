#ifndef STICKER_HOME_PAGE_H
#define STICKER_HOME_PAGE_H

#include "page.h"
#include "stickerstore.h"
#include <QPointer>
#include <QTimer>

class QskTextField;
class QskTabBar;
class QskComboBox;
class QskPopup;
class QskTextLabel;
class QskPushButton;
class QskLinearBox;
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
    Q_INVOKABLE void retranslateUi() override;
    void refreshTabBar();
    void onTabChanged(int index);
    void onPackComboChanged(int index);
    void reloadActive();
    void loadAllStickers();
    void loadRecentStickers();
    void loadPackStickers(const QString& packId);
    void doSearch(const QString& keyword);
    void updateStickerCount();

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
    QskTextLabel* m_countLabel = nullptr;
    QskTextLabel* m_title = nullptr;
    QskPushButton* m_pasteBtn = nullptr;
    QskPushButton* m_importBtn = nullptr;
    QskTabBar* m_tabBar = nullptr;
    QskComboBox* m_packCombo = nullptr;
    StickerGridWidget* m_grid = nullptr;
    bool m_keepScreenOn = true;

    // ── 底部导航栏：首页 / 生成表情 / 设置 ──
    QskLinearBox* m_bottomBar = nullptr;
    QskPushButton* m_bottomHome = nullptr;
    QskPushButton* m_bottomGen = nullptr;
    QskPushButton* m_bottomSettings = nullptr;

    QTimer m_searchDebounce;
    QVector<StickerPackBrief> m_packs;
    QVector<StickerPackBrief> m_comboPacks;  // 与下拉项顺序一一对应（除「粘贴板」外）
    QString m_pastePackId;                   // 识别到的「粘贴板」pack id，无则空
    StickerBrief m_ctxBrief;      // 长按的贴纸上下文
    StickerPackBrief m_ctxPack;   // 分组管理上下文
    QString m_activeTab;          // "" = 全部, "__recent" = 最近, 否则 packId（含粘贴板）
};

#endif // STICKER_HOME_PAGE_H