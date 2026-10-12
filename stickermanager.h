#ifndef STICKERMANAGER_H
#define STICKERMANAGER_H

#include "compat34.h"
#include "sticker_db.h"
#include <vector>
#include <map>

class StickerManager : public QWidget {
    Q_OBJECT
public:
    StickerManager(QWidget* parent = 0);                     // 嵌入模式
    StickerManager(const QString& title, QWidget* parent);   // 独立窗口模式
    void setStickerDb(StickerDbSyncInterface* db) { m_db = db; }
    void loadData();

signals:
    void stickerDbChanged();

private slots:
    void onPackTabClicked();
    void onAddPackClicked();
    void onRemovePackClicked();
    void onAddStickersClicked();
    void onRemoveStickerClicked();

private:
    void init();
    void rebuildPackList();
    void buildStickerGrid(const std::vector<StickerRow>& stickers);
    QStringList pickImageFiles();
    QPixmap loadThumbnail(const StickerRow& s, int size);
    QPushButton* makeStickerBtn(const StickerRow& s, int btnSize, QWidget* parent);

    StickerDbSyncInterface* m_db = nullptr;
    std::vector<StickerPackRow> m_packs;
    std::vector<QPushButton*> m_packTabs;
    std::map<QObject*, std::string> m_btnStickerId;

    QWidget* m_packBar;
    QPushButton* m_addPackBtn;
    QPushButton* m_removePackBtn;
    QPushButton* m_addStickersBtn;
    QPushButton* m_removeStickerBtn;
    QWidget* m_gridContainer;
    QLabel* m_statusLabel;
    QLabel* m_totalLabel;

    bool m_windowMode = false;
    int m_activePack = -1;

    enum { GRID_COLS = 5, THUMB_SIZE = 100 };
};

#endif
