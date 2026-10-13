#include "stickerpicker.h"
#include "translator.h"
#include <qfile.h>
#include <qpixmap.h>
#include <qimage.h>
#include <qfileinfo.h>
#include <qdir.h>
#include <ctime>

#ifdef QT3_BUILD
#include <qscrollview.h>
#include <qimage.h>
#endif

static QString thumbCachePath(const std::string& sid, const std::string& fp) {
    QFileInfo fi(qFromUtf8(fp));
#ifdef QT3_BUILD
    QString cacheDir = fi.dirPath(false) + "/.thumbs";
#else
    QString cacheDir = fi.absolutePath() + "/.thumbs";
#endif
    QDir().mkdir(cacheDir);
    return cacheDir + "/" + qFromUtf8(sid) + ".png";
}

QPixmap StickerPicker::loadThumbnail(const StickerRow& s, int size) {
    QString thumbFile = thumbCachePath(s.id, s.file_path);
    if (QFile::exists(thumbFile)) {
        QPixmap pm(thumbFile);
        if (!pm.isNull()) { return pm; }
    }
    QPixmap pm(qFromUtf8(s.file_path));
    if (pm.isNull()) { return QPixmap(); }
#ifdef QT3_BUILD
    QImage img = pm.convertToImage();
    if (img.isNull()) { return QPixmap(); }
    QImage scaled = img.smoothScale(size, size);
    QPixmap thumb;
    thumb.convertFromImage(scaled);
#else
    QPixmap thumb = pm.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
#endif
    thumb.save(thumbFile, "PNG");
    return thumb;
}

QPushButton* StickerPicker::makeStickerBtn(const StickerRow& s, int btnSize, QWidget* parent) {
    QPushButton* btn = new QPushButton(parent);
    btn->setFixedSize(btnSize, btnSize);
    QPixmap thumb = loadThumbnail(s, btnSize - 4);
    if (!thumb.isNull()) {
#ifdef QT3_BUILD
        btn->setIconSet(thumb);
#else
        btn->setIcon(QIcon(thumb));
        btn->setIconSize(QSize(btnSize - 8, btnSize - 8));
#endif
    } else {
        btn->setText(qFromUtf8("?"));
    }
    return btn;
}

StickerPicker::StickerPicker(QWidget* parent)
    : QWidget(parent
#ifdef QT3_BUILD
              , 0, WType_Popup
#endif
             ) {
    m_windowMode = false;
    setMinimumSize(240, 100);
    init();
}

StickerPicker::StickerPicker(const QString& title, QWidget* parent)
    : QWidget(parent
#ifdef QT3_BUILD
              , 0, WType_Dialog
#endif
             ) {
    m_windowMode = true;
    qSetWindowTitle(this, title);
    setMinimumSize(320, 400);
    resize(360, 460);
    init();
}

void StickerPicker::init() {
    QVBoxLayout* outer = new QVBoxLayout(this);
    outer->setSpacing(0);
    qSetLayoutMargin(outer, 0);

    // tab bar
    m_tabBar = new QWidget(this);
    QHBoxLayout* tabLayout = new QHBoxLayout(m_tabBar);
    tabLayout->setSpacing(0);
    qSetLayoutMargin(tabLayout, 0);
    outer->addWidget(m_tabBar);

    // page stack
    m_pageStack = new StackedWidget(this);
    outer->addWidget(m_pageStack, 1);

    // recent section
    m_recentSection = new QWidget(this);
    QVBoxLayout* recentLayout = new QVBoxLayout(m_recentSection);
    qSetLayoutMargin(recentLayout, 4);
    QLabel* recentLabel = new QLabel(_("stickerpicker.recent"), m_recentSection);
    recentLabel->setFont(QFont(recentLabel->font().family(), -1, QFont::Bold));
    recentLayout->addWidget(recentLabel);
    outer->addWidget(m_recentSection);

    // manage button at bottom
    QPushButton* manageBtn = new QPushButton(_("stickerpicker.manage"), this);
    manageBtn->setFixedHeight(28);
    connect(manageBtn, SIGNAL(clicked()), this, SLOT(hide()));
    outer->addWidget(manageBtn);
}

void StickerPicker::loadPacks() {
    if (!m_db) { return; }
    m_packs = m_db->list_packs(1);
    rebuildTabBar();
}

void StickerPicker::rebuildTabBar() {
    for (size_t i = 0; i < m_tabButtons.size(); i++) { delete m_tabButtons[i]; }
    m_tabButtons.clear();
    for (size_t i = 0; i < m_packPages.size(); i++) { delete m_packPages[i]; }
    m_packPages.clear();
    m_btnStickerId.clear();
    m_btnPackId.clear();

    QHBoxLayout* tabLayout = (QHBoxLayout*)m_tabBar->layout();

    if (m_packs.empty()) {
        m_tabBar->hide();

        QWidget* page = new QWidget(m_pageStack);
        QVBoxLayout* vl = new QVBoxLayout(page);
        qSetLayoutMargin(vl, 4);
        vl->setSpacing(2);
        QLabel* empty = new QLabel(_("stickerpicker.empty"), page);
        empty->setAlignment(Qt::AlignCenter);
        vl->addWidget(empty);
        m_pageStack->addWidget(page);
        m_packPages.push_back(page);
        qStackSetCurrent(m_pageStack, page);

        setFixedSize(280, 200);
        return;
    }
    m_tabBar->show();
    setFixedSize(320, 400);

    for (size_t i = 0; i < m_packs.size(); i++) {
        QString title = qFromUtf8(m_packs[i].title);
        if (title.length() > 8) { title = title.left(7) + "..."; }

        QPushButton* btn = new QPushButton(title, m_tabBar);
        btn->setFixedHeight(24);
        btn->setFlat(true);
        connect(btn, SIGNAL(clicked()), this, SLOT(onTabClicked()));
        tabLayout->addWidget(btn);
        m_tabButtons.push_back(btn);

        std::vector<StickerRow> stickers;
        if (m_db) { stickers = m_db->list_stickers(m_packs[i].id.c_str()); }
        QWidget* page = new QWidget(m_pageStack);
        buildPackPage(stickers, page);
        m_pageStack->addWidget(page);
        m_packPages.push_back(page);
    }

    m_activePack = 0;
    qStackSetCurrent(m_pageStack, m_packPages[0]);
    rebuildRecentSection();
}

void StickerPicker::buildPackPage(const std::vector<StickerRow>& stickers, QWidget* page) {
    QVBoxLayout* vl = new QVBoxLayout(page);
    qSetLayoutMargin(vl, 4);
    vl->setSpacing(2);

    if (stickers.empty()) {
        QLabel* empty = new QLabel(_("stickerpicker.empty"), page);
        empty->setAlignment(Qt::AlignCenter);
        vl->addWidget(empty);
        return;
    }

#ifdef QT3_BUILD
    QScrollView* scroll = new QScrollView(page);
    QWidget* gridW = new QWidget(scroll);
#else
    QScrollArea* scroll = new QScrollArea(page);
    QWidget* gridW = new QWidget(scroll);
#endif
    QGridLayout* grid = new QGridLayout(gridW);
    grid->setSpacing(2);
    qSetLayoutMargin(grid, 0);

    int n = (int)stickers.size();
    for (int i = 0; i < n; i++) {
        int row = i / GRID_COLS;
        int col = i % GRID_COLS;
        QPushButton* btn = makeStickerBtn(stickers[i], THUMB_SIZE, gridW);
        m_btnStickerId[btn] = stickers[i].id;
        m_btnPackId[btn] = stickers[i].pack_id;
        connect(btn, SIGNAL(clicked()), this, SLOT(onStickerClicked()));
        grid->addWidget(btn, row, col);
    }

#ifdef QT3_BUILD
    scroll->addChild(gridW);
    scroll->setResizePolicy(QScrollView::AutoOne);
#else
    scroll->setWidget(gridW);
    scroll->setWidgetResizable(true);
#endif
    vl->addWidget(scroll);
}

void StickerPicker::rebuildRecentSection() {
    if (!m_db) { return; }

    delete m_recentGrid;
    m_recentGrid = nullptr;

    std::vector<StickerRow> recent = m_db->list_recent_stickers(8);
    if (recent.empty()) {
        m_recentSection->hide();
        return;
    }
    m_recentSection->show();

    m_recentGrid = new QWidget(m_recentSection);
    QHBoxLayout* hrow = new QHBoxLayout(m_recentGrid);
    hrow->setSpacing(2);
    qSetLayoutMargin(hrow, 0);

    for (size_t i = 0; i < recent.size(); i++) {
        QPushButton* btn = makeStickerBtn(recent[i], 36, m_recentGrid);
        m_btnStickerId[btn] = recent[i].id;
        m_btnPackId[btn] = recent[i].pack_id;
        connect(btn, SIGNAL(clicked()), this, SLOT(onStickerClicked()));
        hrow->addWidget(btn);
    }
    hrow->addStretch();
    ((QBoxLayout*)m_recentSection->layout())->addWidget(m_recentGrid);
}

void StickerPicker::showAt(const QPoint& pos) {
    if (m_windowMode) {
        show();
        raise();
    } else {
        move(pos);
        show();
        raise();
    }
}

void StickerPicker::onTabClicked() {
    QPushButton* btn = (QPushButton*)sender();
    for (size_t i = 0; i < m_tabButtons.size(); i++) {
        if (m_tabButtons[i] == btn) {
            m_activePack = (int)i;
            break;
        }
    }
    qStackSetCurrent(m_pageStack, m_packPages[m_activePack]);
}

void StickerPicker::onStickerClicked() {
    QObject* btn = (QObject*)sender();
    auto it_id = m_btnStickerId.find(btn);
    auto it_pack = m_btnPackId.find(btn);
    if (it_id == m_btnStickerId.end() || it_pack == m_btnPackId.end()) { return; }

    QString stickerId = qFromUtf8(it_id->second);
    QString packId = qFromUtf8(it_pack->second);

    QString filePath;
    if (m_db) {
        auto row = m_db->get_sticker(it_id->second.c_str());
        if (row) {
            filePath = qFromUtf8(row->file_path);
            m_db->touch_sticker(row->id.c_str(), time(nullptr));
        }
    }

    emit stickerSelected(packId, stickerId, filePath);
    hide();
}
