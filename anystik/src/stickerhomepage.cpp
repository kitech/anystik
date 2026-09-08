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
#include <QskComboBox.h>
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
#include <QDesktopServices>
#include <QUrl>

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

// Android 打开目录：经 ShareActivity.openDir 拉起文件管理器，
// 成功返回 true，失败返回 false（调用方据此提示）。
static bool jOpenDir(const QString& path)
{
#ifdef Q_OS_ANDROID
    bool ok = false;
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([&ok, path]() {
        QJniObject context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid()) return;
        ok = QJniObject::callStaticMethod<jboolean>(
            "io/fedlet/mobutil/ShareActivity", "openDir",
            "(Landroid/content/Context;Ljava/lang/String;)Z",
            context.object(), QJniObject::fromString(path).object());
    }).waitForFinished();
    return ok;
#else
    Q_UNUSED(path)
    return false;
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

    auto* title = new QskTextLabel(tr("表情包"), topBar);
    title->setAlignment(Qt::AlignCenter);
    title->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    auto* pasteBtn = new QskPushButton(tr("粘贴"), topBar);
    pasteBtn->setPreferredWidth(68);
    pasteBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    connect(pasteBtn, &QskAbstractButton::clicked,
        this, &StickerHomePage::requestPasteSticker);

    auto* importBtn = new QskPushButton(tr("导入"), topBar);
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

    // ── 搜索框 + 当前贴纸计数（同一行左右分块）──
    auto* searchRow = new QskLinearBox(Qt::Horizontal, layout);
    searchRow->setSpacing(8);

    m_searchField = new QskTextField(searchRow);
    m_searchField->setPlaceholderText(tr("搜索贴纸 / emoji..."));
    m_searchField->setPreferredHeight(44);
    m_searchField->setBoxShapeHint(QskTextField::Panel,
        QskBoxShapeMetrics(10, Qt::AbsoluteSize));
    m_searchField->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    m_countLabel = new QskTextLabel(tr("0 个"), searchRow);
    m_countLabel->setPreferredWidth(72);
    m_countLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);

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

    m_packCombo = new QskComboBox(tabBarBox);
    m_packCombo->setPreferredWidth(150);
    m_packCombo->setSizePolicy(QskSizePolicy::Preferred, QskSizePolicy::Expanding);
    m_packCombo->setPlaceholderText(tr("更多分组…"));
    connect(m_packCombo, &QskComboBox::currentIndexChanged,
        this, &StickerHomePage::onPackComboChanged);
    // QskComboBox 弹菜单偶发失败对策（见 QskComboBox.cpp）：
    // A) window()==nullptr 时 setPopupOpen(true) 静默失败；
    // B) 关闭 fade 未结束前重开，openPopup 被残留 menu 拦截且 PopupOpen 态卡牢。
    // 每次按下先复位，再于下一事件循环兜底重开；setPopupOpen 幂等，不影响正常路径。
    connect(m_packCombo, &QskComboBox::pressed, this, [this]() {
        m_packCombo->setPopupOpen(false);
        QTimer::singleShot(0, this, [this]() {
            if (m_packCombo->window())
                m_packCombo->setPopupOpen(true);
        });
    });

    // ── 贴纸网格 ──
    m_grid = new StickerGridWidget(layout);
    m_grid->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Expanding);

    connect(m_grid, &StickerGridWidget::stickerClicked,
        this, [this](const StickerBrief& brief, int index) {
            const StickerMeta meta = StickerStore::instance()->stickerMeta(brief.filePath);
            const QString mime = meta.mime.isEmpty()
                ? QStringLiteral("unknown") : meta.mime;
            const qint64 size = brief.size ? qint64(brief.size) : meta.sizeBytes;
            StickerStore::instance()->touchSticker(brief.id);
            bool ok = StickerStore::instance()->copyStickerToClipboard(brief.filePath);
            showToast(ok
                ? tr("已复制到剪贴板 index=%1 size=%2 mime=%3")
                      .arg(index).arg(size).arg(mime)
                : tr("复制失败 index=%1 size=%2 mime=%3")
                      .arg(index).arg(size).arg(mime));
        });

    connect(m_grid, &StickerGridWidget::stickerDoubleClicked,
        this, &StickerHomePage::openPreview);

    connect(m_grid, &StickerGridWidget::stickerLongPressed,
        this, &StickerHomePage::showStickerMenu);

    // ── 数据 ──
    connect(StickerStore::instance(), &StickerStore::dataChanged,
        this, [this]() { refreshTabBar(); reloadActive(); });

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

    // 分离「粘贴板」与其余分组
    m_comboPacks.clear();
    m_pastePackId.clear();
    for (const auto& pack : m_packs) {
        if (pack.title == QString::fromUtf8("粘贴板")) {
            m_pastePackId = pack.id;
        } else {
            m_comboPacks.append(pack);
        }
    }

    // 固定 tabs：全部 / 最近 / 粘贴板（存在时）
    const int current = m_tabBar->currentIndex();
    m_tabBar->clear(true);
    m_tabBar->addTab(tr("全部"));
    m_tabBar->addTab(tr("最近"));
    if (!m_pastePackId.isEmpty()) {
        m_tabBar->addTab(tr("粘贴板"));
    }
    m_tabBar->setCurrentIndex(qMin(current, m_tabBar->count() - 1));

    // 其余包 → 下拉：清空重建，恢复当前选择（-1 即占位符态）
    m_packCombo->clear();
    for (const auto& pack : m_comboPacks) {
        m_packCombo->addOption(pack.title);
    }
    int comboIdx = -1;
    for (int i = 0; i < m_comboPacks.size(); ++i) {
        if (m_comboPacks[i].id == m_activeTab) {
            comboIdx = i;
            break;
        }
    }
    m_packCombo->setCurrentIndex(comboIdx);
    m_packCombo->setVisible(!m_comboPacks.isEmpty());
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
    } else if (index == 2 && !m_pastePackId.isEmpty()) {
        m_activeTab = m_pastePackId;
        loadPackStickers(m_pastePackId);
    }
}

void StickerHomePage::onPackComboChanged(int index)
{
    if (!m_searchField->text().trimmed().isEmpty()) {
        return; // 搜索状态优先
    }
    if (index < 0 || index >= m_comboPacks.size()) {
        return;
    }
    m_activeTab = m_comboPacks[index].id;
    loadPackStickers(m_activeTab);
}

void StickerHomePage::reloadActive()
{
    if (m_activeTab == QStringLiteral("__recent")) {
        loadRecentStickers();
    } else if (m_activeTab.isEmpty()) {
        loadAllStickers();
    } else {
        // 兜底：分组已被卸载/停用 → 回退「全部」，避免网格空白
        bool stillExists = false;
        for (const auto& p : m_packs) {
            if (p.id == m_activeTab) { stillExists = true; break; }
        }
        if (!stillExists) {
            m_activeTab = QStringLiteral("");
            loadAllStickers();
            return;
        }
        loadPackStickers(m_activeTab);
    }
}

void StickerHomePage::loadAllStickers()
{
    QVector<StickerBrief> all;
    for (const auto& pack : m_packs) {
        all += StickerStore::instance()->stickers(pack.id);
    }
    m_grid->setStickers(all);
    updateStickerCount();
}

void StickerHomePage::loadRecentStickers()
{
    m_grid->setStickers(StickerStore::instance()->recent(60));
    updateStickerCount();
}

void StickerHomePage::loadPackStickers(const QString& packId)
{
    m_grid->setStickers(StickerStore::instance()->stickers(packId));
    updateStickerCount();
}

void StickerHomePage::doSearch(const QString& keyword)
{
    if (keyword.isEmpty()) {
        onTabChanged(m_tabBar->currentIndex());
        return;
    }
    m_grid->setStickers(StickerStore::instance()->search(keyword));
    updateStickerCount();
}

void StickerHomePage::updateStickerCount()
{
    if (!m_countLabel || !m_grid)
        return;
    const int n = m_grid->stickers().size();
    m_countLabel->setText(tr("%1 个").arg(n));
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
    const int idxCopy = menu->addOption(QskLabelData(tr("复制")));
    const int idxCopy01 = menu->addOption(QskLabelData(tr("复制x0.1")));
    const int idxCopy025 = menu->addOption(QskLabelData(tr("复制x0.25")));
    const int idxCopy05 = menu->addOption(QskLabelData(tr("复制x0.5")));
    const int idxCopy20 = menu->addOption(QskLabelData(tr("复制x2.0")));
    const int idxPreview = menu->addOption(QskLabelData(tr("预览")));
    const int idxCopyMeta = menu->addOption(QskLabelData(tr("复制元信息")));
    const int idxShare = menu->addOption(QskLabelData(tr("分享")));
    const int idxDelete = menu->addOption(QskLabelData(tr("删除")));

    // 菜单无边界翻转逻辑（QskMenu.cpp 仅 setPosition(origin)）——origin 靠近
    // 视图底部时剩余高度不足，菜单会被窗口裁剪。按实际尺寸向上翻转并钳制顶部。
    QPointF origin = scenePos;
    const qreal menuH = menu->sizeConstraint().height();
    if (menuH > 0) {
        const QRectF bounds = window() ? window()->contentItem()->boundingRect()
                                       : QRectF();
        if (bounds.isValid() && origin.y() + menuH > bounds.bottom()) {
            origin.setY(qMax(bounds.top(), origin.y() - menuH));
        }
    }
    menu->setOrigin(origin);

    // 兜底：打开后实测底部越界再校准一次（估算误差场景）
    connect(menu, &QskPopup::opened, this, [this, menu]() {
        if (!menu->window() || menu->height() <= 0)
            return;
        const QRectF bounds = menu->window()->contentItem()->boundingRect();
        const qreal bottom = menu->mapToScene(QPointF(0, menu->height())).y();
        if (bottom > bounds.bottom()) {
            menu->setPosition(menu->x(),
                              qMax(bounds.top(), bottom - menu->height()));
        }
    });

    // 菜单宽由 skinlet 按最长文本自动测量；补 strut 下限防被皮肤压缩
    {
        const QFontMetricsF fm(menu->effectiveFont(QskMenu::Text));
        const qreal pad = menu->paddingHint(QskMenu::Segment).left()
                        + menu->paddingHint(QskMenu::Segment).right();
        const qreal minW = qskHorizontalAdvance(fm, QString::fromUtf8("复制x0.25"))
                         + pad + 10;
        menu->setStrutSizeHint(QskMenu::Panel, QSizeF(minW, 0));
    }

    connect(menu, &QskMenu::triggered, this,
        [this, menu, idxCopy, idxCopy01, idxCopy025, idxCopy05, idxCopy20, idxPreview, idxCopyMeta, idxShare, idxDelete](int index) {
        if (index == idxCopy) {
            StickerStore::instance()->touchSticker(m_ctxBrief.id);
            bool ok = StickerStore::instance()->copyStickerToClipboard(m_ctxBrief.filePath);
            showToast(ok ? tr("已复制")
                         : tr("复制失败"));
        } else if (index == idxCopy01) {
            StickerStore::instance()->touchSticker(m_ctxBrief.id);
            bool ok = StickerStore::instance()->copyStickerScaledToClipboard(m_ctxBrief.filePath, 0.1);
            showToast(ok ? tr("已复制x0.1")
                         : tr("复制失败"));
        } else if (index == idxCopy025) {
            StickerStore::instance()->touchSticker(m_ctxBrief.id);
            bool ok = StickerStore::instance()->copyStickerScaledToClipboard(m_ctxBrief.filePath, 0.25);
            showToast(ok ? tr("已复制x0.25")
                         : tr("复制失败"));
        } else if (index == idxCopy05) {
            StickerStore::instance()->touchSticker(m_ctxBrief.id);
            bool ok = StickerStore::instance()->copyStickerScaledToClipboard(m_ctxBrief.filePath, 0.5);
            showToast(ok ? tr("已复制x0.5")
                         : tr("复制失败"));
        } else if (index == idxCopy20) {
            StickerStore::instance()->touchSticker(m_ctxBrief.id);
            bool ok = StickerStore::instance()->copyStickerScaledToClipboard(m_ctxBrief.filePath, 2.0);
            showToast(ok ? tr("已复制x2.0")
                         : tr("复制失败"));
        } else if (index == idxPreview) {
            openPreview(m_ctxBrief);
        } else if (index == idxCopyMeta) {
            const StickerMeta meta =
                StickerStore::instance()->stickerMeta(m_ctxBrief.filePath);
            QGuiApplication::clipboard()->setText(formatStickerMeta(meta));
            showToast(tr("已复制元信息"));
        } else if (index == idxShare) {
            if (!StickerStore::instance()->shareStickerFile(m_ctxBrief.filePath)) {
                showToast(tr("桌面暂不支持分享"));
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
        // 菜单关闭即移除铺满父页的 overlay：DeleteOnClose=false 时菜单 close()
        // 不触发 destroyed，若不在此删除，overlay 残留可见会让
        // MyScrollArea::isCoveredByOverlay 恒命中 → 滚轮永久失效（重启才恢复）。
        connect(menu, &QskPopup::closed, overlay, &QObject::deleteLater);
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
    ConfirmPopup::show(this, tr("删除贴纸"),
        tr("确定删除这张贴纸？"),
        tr("删除"), tr("取消"),
        [this, brief](bool accepted) {
            if (!accepted) {
                return;
            }
            if (StickerStore::instance()->deleteSticker(brief.id)) {
                showToast(tr("已删除"));
                for (auto* o : findChildren<StickerPreviewOverlay*>())
                    o->deleteLater();
                reloadActive();
            } else {
                showToast(tr("删除失败"));
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
    int idxOpenFolder = 3;
    int idxManage = -1;
    menu->addOption(QskLabelData(tr("导入表情包文件夹")));
    menu->addOption(QskLabelData(tr("表情包目录")));
    menu->addOption(QskLabelData(tr("粘贴添加")));
    menu->addOption(QskLabelData(tr("打开目录")));
    if (!m_packs.isEmpty()) {
        idxManage = 4;
        menu->addOption(QskLabelData(tr("分组管理")));
    }
    menu->addSeparator();
    const int idxLog = menu->addOption(QskLabelData(tr("App Log")));
    const int idxSettings = menu->addOption(QskLabelData(tr("Settings")));
    const int idxAbout = menu->addOption(QskLabelData(tr("About")));
    menu->addSeparator();
    const int idxKeep = menu->addOption(QskLabelData(
        m_keepScreenOn ? tr("✓ Keep Screen On")
                       : tr("  Keep Screen On")));
    menu->setOrigin(origin);

    connect(menu, &QskMenu::triggered, this, [this, idxBundled, idxImport, idxPaste, idxOpenFolder,
        idxManage, idxLog, idxSettings, idxAbout, idxKeep](int index) {
        if (index == idxImport) {
            requestImportFolder();
        } else if (index == idxBundled) {
            pageManager()->open("bundledpacks");
        } else if (index == idxPaste) {
            requestPasteSticker();
        } else if (index == idxOpenFolder) {
            openStickerFolder();
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
        connect(menu, &QskPopup::closed, overlay, &QObject::deleteLater);
    }
    menu->open();
}

void StickerHomePage::showPackManageMenu()
{
    QStringList names;
    for (const auto& pack : m_packs) {
        names.append(pack.title);
    }

    SelectPopup::show(this, tr("选择分组"), names,
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
            menu->addOption(QskLabelData(tr("重命名")));
            menu->addOption(QskLabelData(tr("删除分组")));
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
                connect(menu, &QskPopup::closed, overlay, &QObject::deleteLater);
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

    auto* label = new QskTextLabel(tr("重命名分组"), box);
    label->setAlignment(Qt::AlignCenter);

    auto* field = new QskTextField(pack.title, box);
    field->setPlaceholderText(tr("分组名称"));
    field->setPreferredWidth(240);
    field->setBoxShapeHint(QskTextField::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));

    auto* btnBox = new QskLinearBox(Qt::Horizontal, box);
    btnBox->setSpacing(10);

    auto* cancelBtn = new QskPushButton(tr("取消"), btnBox);
    cancelBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    connect(cancelBtn, &QskAbstractButton::clicked, popup, &QskPopup::close);

    auto* okBtn = new QskPushButton(tr("确定"), btnBox);
    okBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    connect(okBtn, &QskAbstractButton::clicked, popup, [this, field, popup]() {
        if (StickerStore::instance()->renamePack(m_ctxPack.id, field->text())) {
            showToast(tr("已重命名"));
        } else {
            showToast(tr("重命名失败"));
        }
        popup->close();
    });

    connect(popup, &QskPopup::closed, popup, &QObject::deleteLater);
    popup->open();

    field->setFocus(true);
}

void StickerHomePage::removePack(const StickerPackBrief& pack)
{
    ConfirmPopup::show(this, tr("删除分组"),
        tr("确定删除「%1」及其全部贴纸？\n文件不会被删除。").arg(pack.title),
        tr("删除"), tr("取消"),
        [this, pack](bool accepted) {
            if (accepted) {
                if (StickerStore::instance()->deletePack(pack.id)) {
                    showToast(tr("已删除分组"));
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
    showToast(tr("Android 请通过「分享到 anystik」导入图片"));
#else
    showDirPicker();
#endif
}

void StickerHomePage::requestPasteSticker()
{
    QString err;
    if (StickerStore::instance()->pasteFromClipboard(&err)) {
        showToast(tr("已粘贴到「粘贴板」"));
    } else {
        showToast(err.isEmpty() ? tr("粘贴失败") : err);
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

    auto* upBtn = new QskPushButton(tr("↑ 上级"), btnBox);
    upBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));

    auto* cancelBtn = new QskPushButton(tr("取消"), btnBox);
    cancelBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));

    auto* okBtn = new QskPushButton(tr("导入此目录"), btnBox);
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
                showToast(tr("导入成功"));
            } else {
                showToast(err.isEmpty() ? tr("导入失败")
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

void StickerHomePage::openStickerFolder()
{
    const QString baseDir = StickerStore::instance()->currentStickerBaseDir();
    if (baseDir.isEmpty()) {
        showToast(tr("无法获取目录路径"));
        return;
    }

#ifdef Q_OS_ANDROID
    if (!jOpenDir(baseDir))
        showToast(tr("无法打开贴纸目录"));
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(baseDir));
#endif
}

#include "moc_stickerhomepage.cpp"