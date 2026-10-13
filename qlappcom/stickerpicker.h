#ifndef STICKERPICKER_H
#define STICKERPICKER_H

#include "compat34.h"
#include "sticker_db.h"
#include <vector>
#include <map>

class StickerPicker : public QWidget {
    Q_OBJECT
public:
    StickerPicker(QWidget* parent);                     // Popup 模式
    StickerPicker(const QString& title, QWidget* parent);  // 独立窗口模式
    void loadPacks();
    void showAt(const QPoint& pos);
    void setStickerDb(StickerDbSyncInterface* db) { m_db = db; }

signals:
    void stickerSelected(const QString& packId, const QString& stickerId,
                         const QString& filePath);

private slots:
    void onTabClicked();
    void onStickerClicked();

private:
    void init();
    void rebuildTabBar();
    void buildPackPage(const std::vector<StickerRow>& stickers, QWidget* page);
    void rebuildRecentSection();
    QPixmap loadThumbnail(const StickerRow& s, int size);
    QPushButton* makeStickerBtn(const StickerRow& s, int btnSize, QWidget* parent);

    StickerDbSyncInterface* m_db = nullptr;
    std::vector<StickerPackRow> m_packs;
    std::vector<QPushButton*> m_tabButtons;
    std::vector<QWidget*> m_packPages;

    QWidget* m_tabBar;
    StackedWidget* m_pageStack;
    QWidget* m_recentSection;
    QWidget* m_recentGrid = nullptr;

    int m_activePack = 0;
    bool m_windowMode = false;

    std::map<QObject*, std::string> m_btnStickerId;
    std::map<QObject*, std::string> m_btnPackId;

    enum { GRID_COLS = 4, THUMB_SIZE = 72 };
};

#endif
