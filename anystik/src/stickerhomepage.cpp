#include "stickerhomepage.h"
#include "stickerlist.h"
#include "stickerpreviewoverlay.h"
#include "menuoverlay.h"
#include "dialogpopup.h"
#include "toastpopup.h"
#include "pushstatusbar.h"
#include "androidutils.h"
#include "pagemanager.h"

#include <QskLinearBox.h>
#include <QskTextLabel.h>
#include <QskPushButton.h>
#include <QskTextField.h>
#include <QskTabBar.h>
#include <QskTabButton.h>
#include <QskMenu.h>
#include <QskLabelData.h>
#include <QskDialog.h>
#include <QskBoxShapeMetrics.h>
#include <QskPopup.h>
#include <QskSimpleListBox.h>
#include <QskFunctions.h>
#include <QFontMetricsF>

#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QVariantMap>
#include <QQuickWindow>
#include <QWindow>
#include <QGuiApplication>
#include <QClipboard>
#include <QSettings>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

static constexpr int FLAG_KEEP_SCREEN_ON = 0x80;

static void jniKeepScreenOn(bool on) {
#ifdef Q_OS_ANDROID
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([on]() {
        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (!activity.isValid()) return;
        QJniObject window = activity.callObjectMethod(
            "getWindow", "()Landroid/view/Window;");
        if (!window.isValid()) return;
        if (on)
            window.callMethod<void>("addFlags", "(I)V", FLAG_KEEP_SCREEN_ON);
        else
            window.callMethod<void>("clearFlags", "(I)V", FLAG_KEEP_SCREEN_ON);
    }).waitForFinished();
#else
    Q_UNUSED(on)
#endif
}

StickerHomePage::StickerHomePage(QQuickItem* parent)
    : Page(parent)
{
}

void StickerHomePage::onCreate(const QVariantMap& launchArgs,
                               const QVariantMap& savedState)
{
    Q_UNUSED(launchArgs)
    Q_UNUSED(savedState)

    StickerStore::instance()->ensureInit();

    // 恢复 keepScreenOn，延迟到窗口就绪后应用
    m_keepScreenOn = QSettings().value("keepScreenOn", true).toBool();
    QTimer::singleShot(50, this, [this]() { jniKeepScreenOn(m_keepScreenOn); });

    setAutoLayoutChildren(true);
    auto* layout = new QskLinearBox(Qt::Vertical, this);
    layout->setPanel(true);
    layout->setSpacing(6);

    // ── TopBar ──
    auto* topBar = new QskLinearBox(Qt::Horizontal, layout);
    topBar->setPanel(true);
    topBar->setPreferredHeight(56);
    topBar->setSpacing(8);

    auto* title = new QskTextLabel(QString::fromUtf8("表情包"), topBar);
    title->setAlignment(Qt::AlignCenter);
    title->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    auto* pasteBtn = new QskPushButton(QString::fromUtf8("粘贴"), topBar);
    pasteBtn->setPreferredWidth(68);
    pasteBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    connect(pasteBtn, &QskAbstractButton::clicked,
        this, &StickerHomePage::requestPasteSticker);

    auto* importBtn = new QskPushButton(QString::fromUtf8("导入"), topBar);
    importBtn->setPreferredWidth(68);
    importBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    connect(importBtn, &QskAbstractButton::clicked,
        this, &StickerHomePage::requestImportFolder);

    auto* moreBtn = new QskPushButton(QString::fromUtf8("⋯"), topBar);
    moreBtn->setPreferredSize(44, 44);
    moreBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    connect(moreBtn, &QskAbstractButton::clicked, this,
        [this, moreBtn]() {
            showOptionsMenu(moreBtn->mapToItem(this, QPointF(0, moreBtn->height())));
        });

    // ── Push 连接状态（Android 可见，桌面包视情况自隐藏）──
    new PushStatusBar(layout);

    // ── 搜索框 ──
    m_searchField = new QskTextField(layout);
    m_searchField->setPlaceholderText(QString::fromUtf8("搜索贴纸 / emoji..."));
    m_searchField->setPreferredHeight(44);
    m_searchField->setBoxShapeHint(QskTextField::Panel,
        QskBoxShapeMetrics(10, Qt::AbsoluteSize));

    connect(m_searchField, &QskTextField::textChanged, this,
        [this]() {            // QSkinny 新/旧版 textChanged() 签名通用（0 参 functor 兼容任意信号元数）
            m_searchDebounce.start(350);
        });

    m_searchDebounce.setSingleShot(true);
    connect(&m_searchDebounce, &QTimer::timeout, this,
        [this]() { doSearch(m_searchField->text().trimmed()); });

    // ── 分组 Tab ──
    auto* tabBarBox = new QskLinearBox(Qt::Horizontal, layout);
    tabBarBox->setPanel(true);
    tabBarBox->setPreferredHeight(48);

    m_tabBar = new QskTabBar(Qt::TopEdge, tabBarBox);
    m_tabBar->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Expanding);
    m_tabBar->setAutoFitTabs(true);

    connect(m_tabBar, &QskTabBar::currentIndexChanged,
        this, &StickerHomePage::onTabChanged);

    // ── 贴纸网格 ──
    m_grid = new StickerGridWidget(layout);
    m_grid->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Expanding);

    connect(m_grid, &StickerGridWidget::stickerClicked,
        this, [this](const StickerBrief& brief) {
            StickerStore::instance()->touchSticker(brief.id);
            bool ok = StickerStore::instance()->copyStickerToClipboard(brief.filePath);
            showToast(ok ? QString::fromUtf8("已复制到剪贴板")
                         : QString::fromUtf8("复制失败"));
        });

    connect(m_grid, &StickerGridWidget::stickerDoubleClicked,
        this, &StickerHomePage::openPreview);

    connect(m_grid, &StickerGridWidget::stickerLongPressed,
        this, &StickerHomePage::showStickerMenu);

    // ── 数据 ──
    connect(StickerStore::instance(), &StickerStore::dataChanged,
        this, [this]() { refreshTabBar(); onTabChanged(m_tabBar->currentIndex()); });

    refreshTabBar();
    m_tabBar->setCurrentIndex(0);
    onTabChanged(m_tabBar->currentIndex());
}

// ═══════════════════════════════════════════════════════════════════
// 数据加载与 Tab 切换
// ═══════════════════════════════════════════════════════════════════

void StickerHomePage::refreshTabBar()
{
    m_packs = StickerStore::instance()->packs();

    const int current = m_tabBar->currentIndex();
    m_tabBar->clear(true);

    m_tabBar->addTab(QString::fromUtf8("全部"));
    m_tabBar->addTab(QString::fromUtf8("最近"));
    for (const auto& pack : m_packs) {
        m_tabBar->addTab(pack.title);
    }

    if (current < m_tabBar->count()) {
        m_tabBar->setCurrentIndex(current);
    } else {
        m_tabBar->setCurrentIndex(0);
    }
}

void StickerHomePage::onTabChanged(int index)
{
    if (!m_searchField->text().trimmed().isEmpty()) {
        return; // 搜索状态优先
    }

    if (index <= 0) {
        m_activeTab = QStringLiteral("");
        loadAllStickers();
    } else if (index == 1) {
        m_activeTab = QStringLiteral("__recent");
        loadRecentStickers();
    } else {
        int packIdx = index - 2;
        if (packIdx >= 0 && packIdx < m_packs.size()) {
            m_activeTab = m_packs[packIdx].id;
            loadPackStickers(m_activeTab);
        }
    }
}

void StickerHomePage::loadAllStickers()
{
    QVector<StickerBrief> all;
    for (const auto& pack : m_packs) {
        all += StickerStore::instance()->stickers(pack.id);
    }
    m_grid->setStickers(all);
}

void StickerHomePage::loadRecentStickers()
{
    m_grid->setStickers(StickerStore::instance()->recent(60));
}

void StickerHomePage::loadPackStickers(const QString& packId)
{
    m_grid->setStickers(StickerStore::instance()->stickers(packId));
}

void StickerHomePage::doSearch(const QString& keyword)
{
    if (keyword.isEmpty()) {
        onTabChanged(m_tabBar->currentIndex());
        return;
    }
    m_grid->setStickers(StickerStore::instance()->search(keyword));
}

// ═══════════════════════════════════════════════════════════════════
// 贴纸长按菜单 / 预览
// ═══════════════════════════════════════════════════════════════════

void StickerHomePage::showStickerMenu(const StickerBrief& brief,
                                      const QPointF& scenePos)
{
    m_ctxBrief = brief;

    for (auto* old : findChildren<QskMenu*>())
        old->deleteLater();
    for (auto* old : findChildren<MenuOverlay*>())
        old->deleteLater();

    auto* menu = new QskMenu(this);
    menu->setModal(true);
    menu->setPopupFlag(QskPopup::DeleteOnClose, false);
    const int idxCopy = menu->addOption(QskLabelData(QString::fromUtf8("复制")));
    const int idxCopy05 = menu->addOption(QskLabelData(QString::fromUtf8("复制x0.5")));
    const int idxCopy20 = menu->addOption(QskLabelData(QString::fromUtf8("复制x2.0")));
    const int idxPreview = menu->addOption(QskLabelData(QString::fromUtf8("预览")));
    const int idxCopyMeta = menu->addOption(QskLabelData(QString::fromUtf8("复制元信息")));
    const int idxShare = menu->addOption(QskLabelData(QString::fromUtf8("分享")));
    const int idxDelete = menu->addOption(QskLabelData(QString::fromUtf8("删除")));
    menu->setOrigin(scenePos);

    // 菜单宽由 skinlet 按最长文本自动测量；补 strut 下限防被皮肤压缩
    {
        const QFontMetricsF fm(menu->effectiveFont(QskMenu::Text));
        const qreal pad = menu->paddingHint(QskMenu::Segment).left()
                        + menu->paddingHint(QskMenu::Segment).right();
        const qreal minW = qskHorizontalAdvance(fm, QString::fromUtf8("复制元信息"))
                         + pad + 10;
        menu->setStrutSizeHint(QskMenu::Panel, QSizeF(minW, 0));
    }

    connect(menu, &QskMenu::triggered, this,
        [this, menu, idxCopy, idxCopy05, idxCopy20, idxPreview, idxCopyMeta, idxShare, idxDelete](int index) {
        if (index == idxCopy) {
            StickerStore::instance()->touchSticker(m_ctxBrief.id);
            bool ok = StickerStore::instance()->copyStickerToClipboard(m_ctxBrief.filePath);
            showToast(ok ? QString::fromUtf8("已复制")
                         : QString::fromUtf8("复制失败"));
        } else if (index == idxCopy05) {
            StickerStore::instance()->touchSticker(m_ctxBrief.id);
            bool ok = StickerStore::instance()->copyStickerScaledToClipboard(m_ctxBrief.filePath, 0.5);
            showToast(ok ? QString::fromUtf8("已复制x0.5")
                         : QString::fromUtf8("复制失败"));
        } else if (index == idxCopy20) {
            StickerStore::instance()->touchSticker(m_ctxBrief.id);
            bool ok = StickerStore::instance()->copyStickerScaledToClipboard(m_ctxBrief.filePath, 2.0);
            showToast(ok ? QString::fromUtf8("已复制x2.0")
                         : QString::fromUtf8("复制失败"));
        } else if (index == idxPreview) {
            openPreview(m_ctxBrief);
        } else if (index == idxCopyMeta) {
            const StickerMeta meta =
                StickerStore::instance()->stickerMeta(m_ctxBrief.filePath);
            QGuiApplication::clipboard()->setText(formatStickerMeta(meta));
            showToast(QString::fromUtf8("已复制元信息"));
        } else if (index == idxShare) {
            if (!StickerStore::instance()->shareStickerFile(m_ctxBrief.filePath)) {
                showToast(QString::fromUtf8("桌面暂不支持分享"));
            }
        } else if (index == idxDelete) {
            const StickerBrief brief = m_ctxBrief;
            // QskMenu 自带 DeleteOnClose，close() 的 deleteLater 会在 question()
            // 的嵌套事件循环里被冲刷 → 本菜单于 QskPopup::event() 派发栈内被销毁，
            // :491 window() 打悬垂指针 SIGSEGV。确认框延迟到本次派发完成后弹出。
            QTimer::singleShot(0, this, [this, brief]() { confirmDeleteSticker(brief); });
        }
        menu->close();
    });

    {
        auto* overlay = new MenuOverlay(menu);
        connect(menu, &QObject::destroyed, overlay, &QObject::deleteLater);
    }
    menu->open();
}

void StickerHomePage::openPreview(const StickerBrief& brief)
{
    for (auto* old : findChildren<StickerPreviewOverlay*>())
        old->deleteLater();
    auto* overlay = new StickerPreviewOverlay(this);
    overlay->show(brief);
    connect(overlay, &StickerPreviewOverlay::deleteRequested,
        this, [this](const StickerBrief& b) {
        // 确认框开嵌套事件循环，延迟到本次按钮事件派发完成后弹出，避免再入。
        QTimer::singleShot(0, this, [this, b]() { confirmDeleteSticker(b); });
    });
    connect(overlay, &StickerPreviewOverlay::closed,
            overlay, &QObject::deleteLater);
}

// 通用删除（软删除：DB 置 deleted=1，文件保留）——网格长按菜单与预览页共用
void StickerHomePage::confirmDeleteSticker(const StickerBrief& brief)
{
    ConfirmPopup::show(this, QString::fromUtf8("删除贴纸"),
        QString::fromUtf8("确定删除这张贴纸？"),
        QString::fromUtf8("删除"), QString::fromUtf8("取消"),
        [this, brief](bool accepted) {
            if (!accepted) {
                return;
            }
            if (StickerStore::instance()->deleteSticker(brief.id)) {
                showToast(QString::fromUtf8("已删除"));
                for (auto* o : findChildren<StickerPreviewOverlay*>())
                    o->deleteLater();
                onTabChanged(m_tabBar->currentIndex());
            } else {
                showToast(QString::fromUtf8("删除失败"));
            }
        });
}

// ═══════════════════════════════════════════════════════════════════
// 顶栏菜单：导入 / 分组管理 / 系统入口
// ═══════════════════════════════════════════════════════════════════

void StickerHomePage::showOptionsMenu(const QPointF& origin)
{
    for (auto* old : findChildren<QskMenu*>())
        old->deleteLater();
    for (auto* old : findChildren<MenuOverlay*>())
        old->deleteLater();

    auto* menu = new QskMenu(this);
    menu->setModal(true);
    menu->setPopupFlag(QskPopup::DeleteOnClose, false);

    int idxBundled = 1;
    int idxImport = 0;
    int idxPaste = 2;
    int idxManage = -1;
    menu->addOption(QskLabelData(QString::fromUtf8("导入表情包文件夹")));
    menu->addOption(QskLabelData(QString::fromUtf8("表情包目录")));
    menu->addOption(QskLabelData(QString::fromUtf8("粘贴添加")));
    if (!m_packs.isEmpty()) {
        idxManage = 3;
        menu->addOption(QskLabelData(QString::fromUtf8("分组管理")));
    }
    menu->addSeparator();
    const int idxLog = menu->addOption(QskLabelData(QString::fromUtf8("App Log")));
    const int idxSettings = menu->addOption(QskLabelData(QString::fromUtf8("Settings")));
    const int idxAbout = menu->addOption(QskLabelData(QString::fromUtf8("About")));
    menu->addSeparator();
    const int idxKeep = menu->addOption(QskLabelData(
        m_keepScreenOn ? QString::fromUtf8("✓ Keep Screen On")
                       : QString("  Keep Screen On")));
    menu->setOrigin(origin);

    connect(menu, &QskMenu::triggered, this, [this, idxBundled, idxImport, idxPaste, idxManage,
        idxLog, idxSettings, idxAbout, idxKeep](int index) {
        if (index == idxImport) {
            requestImportFolder();
        } else if (index == idxBundled) {
            pageManager()->open("bundledpacks");
        } else if (index == idxPaste) {
            requestPasteSticker();
        } else if (idxManage >= 0 && index == idxManage) {
            showPackManageMenu();
        } else if (index == idxLog) {
            pageManager()->open("logs");
        } else if (index == idxSettings) {
            pageManager()->open("settings");
        } else if (index == idxAbout) {
            pageManager()->open("about");
        } else if (index == idxKeep) {
            m_keepScreenOn = !m_keepScreenOn;
            jniKeepScreenOn(m_keepScreenOn);
            QSettings().setValue("keepScreenOn", m_keepScreenOn);
            qDebug() << "[StickerHomePage] keepScreenOn:" << m_keepScreenOn;
        }

        if (auto* m = qobject_cast<QskMenu*>(sender()))
            m->close();
    });

    {
        auto* overlay = new MenuOverlay(menu);
        connect(menu, &QObject::destroyed, overlay, &QObject::deleteLater);
    }
    menu->open();
}

void StickerHomePage::showPackManageMenu()
{
    QStringList names;
    for (const auto& pack : m_packs) {
        names.append(pack.title);
    }

    SelectPopup::show(this, QString::fromUtf8("选择分组"), names,
        [this, names](const QString& selected) {
            if (selected.isEmpty()) {
                return;
            }
            int idx = names.indexOf(selected);
            if (idx < 0 || idx >= m_packs.size()) {
                return;
            }
            m_ctxPack = m_packs[idx];

            auto* menu = new QskMenu(this);
            menu->setModal(true);
            menu->setPopupFlag(QskPopup::DeleteOnClose, false);
            menu->addOption(QskLabelData(QString::fromUtf8("重命名")));
            menu->addOption(QskLabelData(QString::fromUtf8("删除分组")));
            const QPointF center(this->width() / 2, this->height() / 2);
            menu->setOrigin(center);

            connect(menu, &QskMenu::triggered, this, [this](int index) {
                if (index == 0) {
                    showRenameDialog(m_ctxPack);
                } else if (index == 1) {
                    removePack(m_ctxPack);
                }
                if (auto* m = qobject_cast<QskMenu*>(sender()))
                    m->close();
            });

            {
                auto* overlay = new MenuOverlay(menu);
                connect(menu, &QObject::destroyed, overlay, &QObject::deleteLater);
            }
            menu->open();
        });
}

void StickerHomePage::showRenameDialog(const StickerPackBrief& pack)
{
    auto* popup = new QskPopup(this);
    popup->setModal(true);
    popup->setOverlay(true);
    popup->setPopupFlag(QskPopup::DeleteOnClose, true);

    auto* box = new QskLinearBox(Qt::Vertical, popup);
    box->setPreferredWidth(280);
    box->setPreferredHeight(150);
    box->setSpacing(10);

    auto* label = new QskTextLabel(QString::fromUtf8("重命名分组"), box);
    label->setAlignment(Qt::AlignCenter);

    auto* field = new QskTextField(pack.title, box);
    field->setPlaceholderText(QString::fromUtf8("分组名称"));
    field->setPreferredWidth(240);
    field->setBoxShapeHint(QskTextField::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));

    auto* btnBox = new QskLinearBox(Qt::Horizontal, box);
    btnBox->setSpacing(10);

    auto* cancelBtn = new QskPushButton(QString::fromUtf8("取消"), btnBox);
    cancelBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    connect(cancelBtn, &QskAbstractButton::clicked, popup, &QskPopup::close);

    auto* okBtn = new QskPushButton(QString::fromUtf8("确定"), btnBox);
    okBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    connect(okBtn, &QskAbstractButton::clicked, popup, [this, field, popup]() {
        if (StickerStore::instance()->renamePack(m_ctxPack.id, field->text())) {
            showToast(QString::fromUtf8("已重命名"));
        } else {
            showToast(QString::fromUtf8("重命名失败"));
        }
        popup->close();
    });

    connect(popup, &QskPopup::closed, popup, &QObject::deleteLater);
    popup->open();

    field->setFocus(true);
}

void StickerHomePage::removePack(const StickerPackBrief& pack)
{
    ConfirmPopup::show(this, QString::fromUtf8("删除分组"),
        QString::fromUtf8("确定删除「%1」及其全部贴纸？\n文件不会被删除。").arg(pack.title),
        QString::fromUtf8("删除"), QString::fromUtf8("取消"),
        [this, pack](bool accepted) {
            if (accepted) {
                if (StickerStore::instance()->deletePack(pack.id)) {
                    showToast(QString::fromUtf8("已删除分组"));
                }
            }
        });
}

// ═══════════════════════════════════════════════════════════════════
// 导入
// ═══════════════════════════════════════════════════════════════════

void StickerHomePage::requestImportFolder()
{
#ifdef Q_OS_ANDROID
    showToast(QString::fromUtf8("Android 请通过「分享到 anystik」导入图片"));
#else
    showDirPicker();
#endif
}

void StickerHomePage::requestPasteSticker()
{
    QString err;
    if (StickerStore::instance()->pasteFromClipboard(&err)) {
        showToast(QString::fromUtf8("已粘贴到「粘贴板」"));
    } else {
        showToast(err.isEmpty() ? QString::fromUtf8("粘贴失败") : err);
    }
}

// ── 轻量目录选择器（纯 QSkinny，无 QtWidgets 依赖）──
void StickerHomePage::showDirPicker()
{
    auto* picker = new QskPopup(this);
    picker->setModal(true);
    picker->setOverlay(true);
    picker->setPopupFlag(QskPopup::DeleteOnClose, true);

    auto* box = new QskLinearBox(Qt::Vertical, picker);
    box->setPreferredWidth(320);
    box->setPreferredHeight(420);
    box->setSpacing(8);

    auto* pathLabel = new QskTextLabel(box);
    pathLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    pathLabel->setWrapMode(QskTextOptions::Wrap);

    auto* listBox = new QskSimpleListBox(box);
    listBox->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Expanding);

    auto* btnBox = new QskLinearBox(Qt::Horizontal, box);
    btnBox->setSpacing(10);

    auto* upBtn = new QskPushButton(QString::fromUtf8("↑ 上级"), btnBox);
    upBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));

    auto* cancelBtn = new QskPushButton(QString::fromUtf8("取消"), btnBox);
    cancelBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));

    auto* okBtn = new QskPushButton(QString::fromUtf8("导入此目录"), btnBox);
    okBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    okBtn->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    auto* currentDir = new QDir(QDir::home().path());
    Q_UNUSED(currentDir)

    auto refresh = [listBox, pathLabel, currentDir]() {
        pathLabel->setText(currentDir->absolutePath());
        QStringList entries;
        const QFileInfoList dirs = currentDir->entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name);
        for (const auto& fi : dirs) {
            if (fi.isSymLink() && !QDir(fi.absoluteFilePath()).exists())
                continue;
            entries.append(fi.fileName() + "/");
        }
        listBox->setEntries(entries);
    };

    refresh();

    auto enterDir = [currentDir, refresh, picker](const QString& name) {
        QString clean = name.endsWith('/') ? name.chopped(1) : name;
        QDir target = *currentDir;
        if (target.cd(clean)) {
            *currentDir = target;
            refresh();
        } else {
             picker->close();
        }
    };

    connect(upBtn, &QskAbstractButton::clicked, [currentDir, refresh]() {
        if (currentDir->cdUp())
            refresh();
    });
    connect(cancelBtn, &QskAbstractButton::clicked, picker, &QskPopup::close);
    connect(okBtn, &QskAbstractButton::clicked, picker,
        [this, currentDir, picker]() {
            QString err;
            if (StickerStore::instance()->importDirectory(
                    currentDir->absolutePath(), &err)) {
                showToast(QString::fromUtf8("导入成功"));
            } else {
                showToast(err.isEmpty() ? QString::fromUtf8("导入失败")
                                        : err);
            }
            picker->close();
        });

    connect(listBox, &QskSimpleListBox::selectedEntryChanged,
        [enterDir](const QString& name) {
            if (!name.isEmpty())
                enterDir(name);
        });

    connect(picker, &QskPopup::closed, picker, &QObject::deleteLater);
    picker->open();
}

void StickerHomePage::showToast(const QString& text)
{
    ToastPopup::show(this, text);
    qDebug() << "[StickerHomePage]" << text;
}

#include "moc_stickerhomepage.cpp"