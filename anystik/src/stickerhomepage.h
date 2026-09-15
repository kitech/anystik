#ifndef STICKER_HOME_PAGE_H
#define STICKER_HOME_PAGE_H

#include "page.h"
#include "stickerstore.h"
#include "davbisync.h"
#include "syncprogresspopup.h"
#include <QPointer>
#include <QTimer>

class MySearchLine;
class QskTabBar;
class QskComboBox;
class QskPopup;
class QskTextLabel;
class QskPushButton;
class QskLinearBox;
class QskMenu;
class StickerGridWidget;

class StickerHomePage : public Page
{
    Q_OBJECT
public:
    StickerHomePage(QQuickItem* parent = nullptr);

protected:
    void onCreate(const QVariantMap& launchArgs,
                  const QVariantMap& savedState) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

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
    void openSubMenu(QskMenu* parent, int entryIndex);
    void closeSubMenu();
    void applySubMenuAnchor(QskMenu* parent, QskMenu* sub, int entryIndex);
    void scheduleSubClose();
    void cancelSubClose();
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
    void ensureSyncPopup(bool reset = true);

    void showToast(const QString& text);

    QskTextLabel* m_countLabel = nullptr;
    QskTextLabel* m_title = nullptr;
    QskPushButton* m_pasteBtn = nullptr;
    QskPushButton* m_importBtn = nullptr;
    QskPushButton* m_syncBtn = nullptr;
    QskTabBar* m_tabBar = nullptr;
    QskComboBox* m_packCombo = nullptr;
    MySearchLine* m_searchLine = nullptr;
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
    QPointer<QskMenu> m_ctxMenu;      // 正在显示的主右键菜单
    QPointer<QQuickItem> m_ctxOverlay; // 主菜单的 MenuOverlay（子菜单打开期间隐藏）
    QPointer<QskMenu> m_ctxSub;        // 当前级联子菜单（缩放/搜索，同时只存在一个）
    int m_ctxSubRow = -1;              // 当前子菜单所属父行 index（同行重锚/跨行重建）
    int m_ctxScaleSubIdx = -1;         // 主菜单“缩放拷贝”项的 index（hover 判定用）
    int m_ctxSearchSubIdx = -1;        // 主菜单“搜索相似”项的 index（hover 判定用）
    QTimer m_subCloseTimer;            // 子菜单离开防抖关闭（悬停进入时重启/取消）
    SyncEngine* m_syncEngine = nullptr;  // 懒创建；finished 恢复按钮
    QPointer<SyncProgressPopup> m_syncPopup; // 同步进度浮动窗口（closed → deleteLater）
};

#endif // STICKER_HOME_PAGE_H