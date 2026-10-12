#include "stickermanager.h"
#include "translator.h"
#include <qfile.h>
#include <qpixmap.h>
#include <qimage.h>
#include <qfiledialog.h>
#include <qfileinfo.h>
#include <qdir.h>
#include <qmessagebox.h>
#include <cstdlib>
#include <ctime>

#ifdef QT3_BUILD
#include <qscrollview.h>
#endif

static std::string qStrToStd(const QString& s) {
#ifdef QT3_BUILD
    return std::string(s.utf8());
#else
    return std::string(qToUtf8(s).constData());
#endif
}

static std::string randomId() {
    char buf[17];
    for (int i = 0; i < 16; i++) {
        int r = rand() % 36;
        buf[i] = (r < 10) ? ('0' + r) : ('a' + r - 10);
    }
    buf[16] = 0;
    return std::string(buf);
}

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

QPixmap StickerManager::loadThumbnail(const StickerRow& s, int size) {
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

QPushButton* StickerManager::makeStickerBtn(const StickerRow& s, int btnSize, QWidget* parent) {
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

StickerManager::StickerManager(QWidget* parent)
    : QWidget(parent
#ifdef QT3_BUILD
              , 0
#endif
             ) {
    m_windowMode = false;
    init();
}

StickerManager::StickerManager(const QString& title, QWidget* parent)
    : QWidget(parent
#ifdef QT3_BUILD
              , 0, WType_Dialog
#endif
             ) {
    m_windowMode = true;
    qSetWindowTitle(this, title);
    init();
}

void StickerManager::init() {
    setMinimumSize(600, 450);
    resize(720, 520);

    QVBoxLayout* outer = new QVBoxLayout(this);
    outer->setSpacing(4);
    qSetLayoutMargin(outer, 8);

    // toolbar
    QWidget* toolbar = new QWidget(this);
    QHBoxLayout* tbLayout = new QHBoxLayout(toolbar);
    tbLayout->setSpacing(4);
    qSetLayoutMargin(tbLayout, 0);

    m_addPackBtn = new QPushButton(_("stickermanager.add_pack"), toolbar);
    connect(m_addPackBtn, SIGNAL(clicked()), this, SLOT(onAddPackClicked()));
    tbLayout->addWidget(m_addPackBtn);

    m_removePackBtn = new QPushButton(_("stickermanager.remove_pack"), toolbar);
    connect(m_removePackBtn, SIGNAL(clicked()), this, SLOT(onRemovePackClicked()));
    tbLayout->addWidget(m_removePackBtn);

    m_addStickersBtn = new QPushButton(_("stickermanager.add_stickers"), toolbar);
    connect(m_addStickersBtn, SIGNAL(clicked()), this, SLOT(onAddStickersClicked()));
    tbLayout->addWidget(m_addStickersBtn);

    m_removeStickerBtn = new QPushButton(_("stickermanager.remove_sticker"), toolbar);
    connect(m_removeStickerBtn, SIGNAL(clicked()), this, SLOT(onRemoveStickerClicked()));
    tbLayout->addWidget(m_removeStickerBtn);

    tbLayout->addStretch();
    outer->addWidget(toolbar);

    // pack tab bar
    m_packBar = new QWidget(this);
    QHBoxLayout* packLayout = new QHBoxLayout(m_packBar);
    packLayout->setSpacing(2);
    qSetLayoutMargin(packLayout, 0);
    outer->addWidget(m_packBar);

    // grid container with scroll
#ifdef QT3_BUILD
    QScrollView* scroll = new QScrollView(this);
#else
    QScrollArea* scroll = new QScrollArea(this);
#endif
    m_gridContainer = new QWidget(scroll);
    QVBoxLayout* gl = new QVBoxLayout(m_gridContainer);
    gl->setSpacing(2);
    qSetLayoutMargin(gl, 4);
    QLabel* placeholder = new QLabel(_("stickermanager.select_pack"), m_gridContainer);
    placeholder->setAlignment(Qt::AlignCenter);
    gl->addWidget(placeholder);

#ifdef QT3_BUILD
    scroll->addChild(m_gridContainer);
    scroll->setResizePolicy(QScrollView::AutoOne);
#else
    scroll->setWidget(m_gridContainer);
    scroll->setWidgetResizable(true);
#endif
    outer->addWidget(scroll, 1);

    // status bar
    QWidget* statusBar = new QWidget(this);
    QHBoxLayout* sbLayout = new QHBoxLayout(statusBar);
    sbLayout->setSpacing(8);
    qSetLayoutMargin(sbLayout, 2);

    m_statusLabel = new QLabel("", statusBar);
    m_totalLabel = new QLabel("", statusBar);
    sbLayout->addWidget(m_statusLabel);
    sbLayout->addStretch();
    sbLayout->addWidget(m_totalLabel);
    outer->addWidget(statusBar);
}

void StickerManager::loadData() {
    if (!m_db) { return; }
    srand(time(nullptr));
    m_packs = m_db->list_packs(1);
    rebuildPackList();

    int total = m_db->count_stickers(nullptr);
    m_totalLabel->setText(
        qFromUtf8(std::to_string(total)) + " " + _("stickermanager.total_stickers"));
}

void StickerManager::rebuildPackList() {
    // delete old pack tab buttons
    for (size_t i = 0; i < m_packTabs.size(); i++) { delete m_packTabs[i]; }
    m_packTabs.clear();
    m_btnStickerId.clear();
    m_activePack = -1;

    if (m_packs.empty()) {
        m_statusLabel->setText(_("stickermanager.no_packs"));
        m_gridContainer->hide();
        return;
    }
    m_gridContainer->show();

    // Delete old layout and create a fresh one
    QLayout* oldLayout = m_packBar->layout();
    if (oldLayout) { delete oldLayout; }
    QHBoxLayout* packLayout = new QHBoxLayout(m_packBar);
    packLayout->setSpacing(2);
    qSetLayoutMargin(packLayout, 0);

    for (size_t i = 0; i < m_packs.size(); i++) {
        QString title = qFromUtf8(m_packs[i].title);
        QPushButton* btn = new QPushButton(title, m_packBar);
        btn->setFixedHeight(28);
#ifdef QT3_BUILD
        btn->setToggleButton(true);
#else
        btn->setCheckable(true);
#endif
        connect(btn, SIGNAL(clicked()), this, SLOT(onPackTabClicked()));
        packLayout->addWidget(btn);
        m_packTabs.push_back(btn);
    }
    packLayout->addStretch();

    if (!m_packTabs.empty()) {
        m_activePack = 0;
#ifdef QT3_BUILD
        m_packTabs[0]->setOn(true);
#else
        m_packTabs[0]->setChecked(true);
#endif
        std::vector<StickerRow> stickers = m_db->list_stickers(m_packs[0].id.c_str());
        buildStickerGrid(stickers);
        m_statusLabel->setText(qFromUtf8(m_packs[0].title));
    }
}

void StickerManager::buildStickerGrid(const std::vector<StickerRow>& stickers) {
    m_btnStickerId.clear();

    // delete all child widgets of m_gridContainer
    std::vector<QObject*> toDelete;
#ifdef QT3_BUILD
    QObjectList* kids = const_cast<QObjectList*>(m_gridContainer->children());
    for (uint i = 0; i < kids->count(); i++) {
        QObject* obj = kids->at(i);
        if (obj->isWidgetType() && obj != m_gridContainer->layout()) {
            toDelete.push_back(obj);
        }
    }
#else
    const QObjectList& kids = m_gridContainer->children();
    for (int i = 0; i < kids.size(); i++) {
        QObject* obj = kids.at(i);
        if (obj->isWidgetType() && obj != m_gridContainer->layout()) {
            toDelete.push_back(obj);
        }
    }
#endif
    for (size_t i = 0; i < toDelete.size(); i++) {
        delete toDelete[i];
    }
    QLayout* oldLayout = m_gridContainer->layout();
    if (oldLayout) { delete oldLayout; }

    QGridLayout* grid = new QGridLayout(m_gridContainer);
    grid->setSpacing(4);
    qSetLayoutMargin(grid, 4);

    if (stickers.empty()) {
        QLabel* empty = new QLabel(_("stickermanager.no_stickers"), m_gridContainer);
        empty->setAlignment(Qt::AlignCenter);
        grid->addWidget(empty, 0, 0);
        return;
    }

    int n = (int)stickers.size();
    for (int i = 0; i < n; i++) {
        int row = i / GRID_COLS;
        int col = i % GRID_COLS;
        QPushButton* btn = makeStickerBtn(stickers[i], THUMB_SIZE, m_gridContainer);
        m_btnStickerId[btn] = stickers[i].id;
        connect(btn, SIGNAL(clicked()), this, SLOT(onRemoveStickerClicked()));
        grid->addWidget(btn, row, col);
    }

    int total = m_db ? m_db->count_stickers(nullptr) : 0;
    m_totalLabel->setText(
        qFromUtf8(std::to_string(total)) + " " + _("stickermanager.total_stickers"));
}

void StickerManager::onPackTabClicked() {
    QPushButton* btn = (QPushButton*)sender();
    for (size_t i = 0; i < m_packTabs.size(); i++) {
#ifdef QT3_BUILD
        m_packTabs[i]->setOn(m_packTabs[i] == btn);
#else
        m_packTabs[i]->setChecked(m_packTabs[i] == btn);
#endif
        if (m_packTabs[i] == btn) { m_activePack = (int)i; }
    }

    if (m_activePack >= 0 && m_activePack < (int)m_packs.size()) {
        std::vector<StickerRow> stickers = m_db->list_stickers(m_packs[m_activePack].id.c_str());
        buildStickerGrid(stickers);
        m_statusLabel->setText(qFromUtf8(m_packs[m_activePack].title));
    }
}

void StickerManager::onAddPackClicked() {
#ifdef QT3_BUILD
    QString dir = QFileDialog::getExistingDirectory(
        qFromUtf8("选择贴纸包目录"), this, 0, qFromUtf8("选择包含贴纸图片的文件夹"));
#else
    QString dir = QFileDialog::getExistingDirectory(
        this, _("stickermanager.select_pack_dir"), QString());
#endif
    if (dir.isEmpty()) { return; }

    QDir d(dir);
    QString folderName = d.dirName();
    if (folderName.isEmpty()) { folderName = "pack"; }

    std::string packId = randomId();

#ifdef QT3_BUILD
    d.setNameFilter("*.png *.jpg *.jpeg *.webp *.gif");
    d.setFilter(QDir::Files | QDir::Readable);
    QStringList files = d.entryList();
#else
    d.setNameFilters(QStringList() << "*.png" << "*.jpg" << "*.jpeg" << "*.webp" << "*.gif");
    QStringList files = d.entryList();
#endif

    if (files.isEmpty()) {
        QMessageBox::information(this, _("stickermanager.no_images_title"),
                                  _("stickermanager.no_images_msg"));
        return;
    }

    StickerPackRow pack;
    pack.id = packId;
    pack.title = qStrToStd(folderName);
    pack.installed = 1;
    pack.position = (int)m_packs.size();

    if (!m_db->begin_write_transaction()) { return; }
    if (!m_db->add_pack(pack)) { m_db->commit_transaction(); return; }

    for (int i = 0; i < files.size(); i++) {
        StickerRow s;
        s.id = packId + "_" + std::to_string(i);
        s.pack_id = packId;
        s.file_path = qStrToStd(dir + "/" + files[i]);
        s.position = i;
        s.emoji = "";

        QPixmap pm(dir + "/" + files[i]);
        if (!pm.isNull()) {
            s.width = pm.width();
            s.height = pm.height();
        }
        m_db->add_sticker(s);
    }
    m_db->commit_transaction();

    loadData();
    emit stickerDbChanged();
}

void StickerManager::onRemovePackClicked() {
    if (m_activePack < 0 || m_activePack >= (int)m_packs.size()) { return; }

    const std::string& packId = m_packs[m_activePack].id;
    QString msg = qFromUtf8(qStrToStd(_("stickermanager.confirm_remove_pack")) +
                            " \"" + m_packs[m_activePack].title + "\"?");

    if (QMessageBox::question(this, _("stickermanager.confirm_title"), msg,
#ifdef QT3_BUILD
                               QMessageBox::Yes, QMessageBox::No) != QMessageBox::Yes) {
#else
                               QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
#endif
        return;
    }

    m_db->delete_stickers_by_pack(packId.c_str());
    m_db->delete_pack(packId.c_str());
    loadData();
    emit stickerDbChanged();
}

void StickerManager::onAddStickersClicked() {
    if (m_activePack < 0 || m_activePack >= (int)m_packs.size()) { return; }

    QStringList files = pickImageFiles();
    if (files.isEmpty()) { return; }

    const std::string& packId = m_packs[m_activePack].id;
    int nextPos = m_db->count_stickers(packId.c_str());

    if (!m_db->begin_write_transaction()) { return; }

    for (int i = 0; i < files.size(); i++) {
        StickerRow s;
        s.id = packId + "_" + std::to_string(nextPos + i);
        s.pack_id = packId;
        s.file_path = qStrToStd(files[i]);
        s.position = nextPos + i;

        QPixmap pm(files[i]);
        if (!pm.isNull()) {
            s.width = pm.width();
            s.height = pm.height();
        }
        m_db->add_sticker(s);
    }
    m_db->commit_transaction();

    std::vector<StickerRow> stickers = m_db->list_stickers(packId.c_str());
    buildStickerGrid(stickers);
    int total = m_db->count_stickers(nullptr);
    m_totalLabel->setText(
        qFromUtf8(std::to_string(total)) + " " + _("stickermanager.total_stickers"));
    emit stickerDbChanged();
}

void StickerManager::onRemoveStickerClicked() {
    QObject* btn = (QObject*)sender();
    auto it = m_btnStickerId.find(btn);
    if (it == m_btnStickerId.end()) { return; }

    QString msg = qFromUtf8(qStrToStd(_("stickermanager.confirm_remove_sticker")));
    if (QMessageBox::question(this, _("stickermanager.confirm_title"), msg,
#ifdef QT3_BUILD
                               QMessageBox::Yes, QMessageBox::No) != QMessageBox::Yes) {
#else
                               QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
#endif
        return;
    }

    m_db->delete_sticker(it->second.c_str());

    if (m_activePack >= 0 && m_activePack < (int)m_packs.size()) {
        std::vector<StickerRow> stickers = m_db->list_stickers(m_packs[m_activePack].id.c_str());
        buildStickerGrid(stickers);
    }
    int total = m_db->count_stickers(nullptr);
    m_totalLabel->setText(
        qFromUtf8(std::to_string(total)) + " " + _("stickermanager.total_stickers"));
    emit stickerDbChanged();
}

QStringList StickerManager::pickImageFiles() {
#ifdef QT3_BUILD
    return QFileDialog::getOpenFileNames(
        qFromUtf8("选择贴纸图片"), QString(),
        this, 0, qFromUtf8("Images (*.png *.jpg *.jpeg *.webp *.gif)"));
#else
    return QFileDialog::getOpenFileNames(
        this, _("stickermanager.select_images"), QString(),
        qFromUtf8("Images (*.png *.jpg *.jpeg *.webp *.gif)"));
#endif
}
