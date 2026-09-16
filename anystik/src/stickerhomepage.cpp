#include "stickerhomepage.h"
#include "stickerlist.h"
#include "stickerpreviewoverlay.h"
#include "imagesearch.h"
#include "imagesearchpopup.h"
#include "menuoverlay.h"
#include "dialogpopup.h"
#include "toastpopup.h"
#include "pushstatusbar.h"
#include "settings_trace.h"
#include "androidutils.h"
#include "pagemanager.h"
#include "mysearchline.h"
#include "qwebdav.h"

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
#include <QUrl>
#include <QWindow>
#include <QGuiApplication>
#include <QClipboard>
#include <QSettings>
#include <QDesktopServices>
#include <QHoverEvent>
#include <QSet>
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
    // 悬停级联子菜单：离开其区域/父菜单时的防抖关闭。原 singleShot(0) 会在
    // hover 事件批处理的间隙误执行 → 子菜单被反复关闭又重开（重复淡入）。
    m_subCloseTimer.setSingleShot(true);
    m_subCloseTimer.setInterval(225);
    connect(&m_subCloseTimer, &QTimer::timeout,
            this, [this]() { closeSubMenu(); });
}

void StickerHomePage::onCreate(const QVariantMap& launchArgs,
                               const QVariantMap& savedState)
{
    Q_UNUSED(launchArgs)
    Q_UNUSED(savedState)

    StickerStore::instance()->ensureInit();

    // 恢复 keepScreenOn，延迟到窗口就绪后应用
    m_keepScreenOn = QSettings().value("keepScreenOn", true).toBool();
    trace_settings("home-keepScreenOn-read");
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

    m_title = new QskTextLabel(tr("😐 表情包"), topBar);
    m_title->setAlignment(Qt::AlignCenter);
    m_title->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    m_pasteBtn = new QskPushButton(tr("粘贴"), topBar);
    m_pasteBtn->setPreferredWidth(68);
    m_pasteBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    connect(m_pasteBtn, &QskAbstractButton::clicked,
        this, &StickerHomePage::requestPasteSticker);

    m_importBtn = new QskPushButton(tr("导入"), topBar);
    m_importBtn->setPreferredWidth(68);
    m_importBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    connect(m_importBtn, &QskAbstractButton::clicked,
        this, &StickerHomePage::requestImportFolder);

    m_syncBtn = new QskPushButton(tr("同步"), topBar);
    m_syncBtn->setPreferredWidth(68);
    m_syncBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    connect(m_syncBtn, &QskAbstractButton::clicked, this, [this]() {
        if (m_syncEngine && m_syncEngine->isRunning()) {
            ensureSyncPopup(false);
            return;
        }
        const QSettings dav;
        trace_settings("home-dav-read");
        const QString davUrl  = dav.value("davUrl").toString();
        const QString davUser = dav.value("davUser").toString();
        const QString davPass = dav.value("davPass").toString();
        if (davUrl.isEmpty()) {
            showToast(tr("请先在设置页填写 WebDAV 地址"));
            return;
        }
        const QUrl url(davUrl);
        if (!url.isValid() || url.host().isEmpty()) {
            showToast(tr("WebDAV 地址无效"));
            return;
        }
        const bool https = QUrl(davUrl).scheme().startsWith(QLatin1String("https"));

        if (!m_syncEngine) {
            m_syncEngine = new SyncEngine(this);
            connect(m_syncEngine, &SyncEngine::finished, this,
                    [this](int exitCode, const QString& summary) {
                        m_syncBtn->setText(tr("同步"));
                        if (exitCode == davbisync::FinishOk) {
                            showToast(tr("同步完成"));
                        } else if (exitCode == davbisync::FinishCancelled) {
                            showToast(tr("已取消"));
                        } else {
                            showToast(tr("同步失败：%1").arg(summary));
                        }
                        qInfo() << "[sync] finished exit=" << exitCode << summary;
                    });
        }

        ensureSyncPopup();
        m_syncEngine->setConnectionSettings(
            https ? 2 : 1,
            url.host(),
            url.path(),
            davUser,
            davPass);
        if (m_syncEngine->startSync()) {
            m_syncBtn->setText(tr("同步中..."));
        }
    });

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

    m_searchLine = new MySearchLine(searchRow);
    m_searchLine->setPlaceholderText(tr("搜索贴纸 / emoji..."));

    m_countLabel = new QskTextLabel(tr("%1 个 · %2 包").arg(0).arg(0), searchRow);
    m_countLabel->setPreferredWidth(72);
    m_countLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);

    m_searchDebounce.setSingleShot(true);
    connect(m_searchLine, &MySearchLine::textChanged, this,
        [this]() { m_searchDebounce.start(350); });
    connect(&m_searchDebounce, &QTimer::timeout, this,
        [this]() { doSearch(m_searchLine->text().trimmed()); });

    // ── 分组 Tab ──
    auto* tabBarBox = new QskLinearBox(Qt::Horizontal, layout);
    tabBarBox->setPanel(true);
    tabBarBox->setPreferredHeight(48);

    m_tabBar = new QskTabBar(Qt::TopEdge, tabBarBox);
    m_tabBar->setSizePolicy(QskSizePolicy::MinimumExpanding, QskSizePolicy::Expanding);
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

    // ── 底部导航栏：首页 / 生成表情 / 设置 ──
    m_bottomBar = new QskLinearBox(Qt::Horizontal, layout);
    m_bottomBar->setPanel(true);
    m_bottomBar->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    m_bottomBar->setFixedHeight(36);
    m_bottomBar->setSpacing(4);

    m_bottomHome = new QskPushButton(tr("首页"), m_bottomBar);
    m_bottomHome->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    connect(m_bottomHome, &QskAbstractButton::clicked, this, []() {
        // 首页即本页，占位无操作
    });

    m_bottomGen = new QskPushButton(tr("生成表情"), m_bottomBar);
    m_bottomGen->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    connect(m_bottomGen, &QskAbstractButton::clicked, this, [this]() {
        if (pageManager())
            pageManager()->open("stikergen");
    });

    m_bottomSettings = new QskPushButton(tr("设置"), m_bottomBar);
    m_bottomSettings->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    connect(m_bottomSettings, &QskAbstractButton::clicked, this, [this]() {
        if (pageManager())
            pageManager()->open("settings");
    });

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

void StickerHomePage::retranslateUi()
{
    if (!m_title)
        return;

    m_title->setText(tr("😐 表情包"));
    m_pasteBtn->setText(tr("粘贴"));
    m_importBtn->setText(tr("导入"));
    m_syncBtn->setText(tr("同步"));
    m_searchLine->setPlaceholderText(tr("搜索贴纸 / emoji..."));
    m_packCombo->setPlaceholderText(tr("更多分组…"));
    m_bottomHome->setText(tr("首页"));
    m_bottomGen->setText(tr("生成表情"));
    m_bottomSettings->setText(tr("设置"));
    refreshTabBar();
    updateStickerCount();
}

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
    if (!m_searchLine->text().trimmed().isEmpty()) {
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
    if (!m_searchLine->text().trimmed().isEmpty()) {
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
    const auto& sticks = m_grid->stickers();
    QSet<QString> packs;
    for (const auto& s : sticks)
        packs.insert(s.packId);
    m_countLabel->setText(tr("%1 个 · %2 包")
        .arg(sticks.size()).arg(packs.size()));
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
    m_ctxMenu = menu;
    menu->installEventFilter(this);
    const int idxCopy = menu->addOption(QskLabelData(tr("复制")));
    const int idxScaleSub = menu->addOption(QskLabelData(tr("缩放拷贝 ›")));
    m_ctxScaleSubIdx = idxScaleSub;
    const int idxPreview = menu->addOption(QskLabelData(tr("预览")));
    const int idxCopyMeta = menu->addOption(QskLabelData(tr("复制元信息")));
    const int idxShare = menu->addOption(QskLabelData(tr("分享")));
    const int idxDelete = menu->addOption(QskLabelData(tr("删除")));
    const int idxSearch = menu->addOption(QskLabelData(tr("搜索相似 ›")));
    m_ctxSearchSubIdx = idxSearch;

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
        const qreal minW = qskHorizontalAdvance(fm, QString::fromUtf8("复制元信息"))
                         + pad + 10;
        menu->setStrutSizeHint(QskMenu::Panel, QSizeF(minW, 0));
    }

    // 缩放复制（抽为辅助，二级缩放子菜单与未来入口共用）
    auto copyScaled = [this](qreal scale, const QString& toast) {
        StickerStore::instance()->touchSticker(m_ctxBrief.id);
        const bool ok = StickerStore::instance()
            ->copyStickerScaledToClipboard(m_ctxBrief.filePath, scale);
        showToast(ok ? toast : tr("复制失败"));
    };

    connect(menu, &QskMenu::triggered, this,
        [this, menu, idxCopy, idxScaleSub, idxPreview, idxCopyMeta, idxShare, idxDelete, idxSearch](int index) {
        if (index == idxCopy) {
            StickerStore::instance()->touchSticker(m_ctxBrief.id);
            bool ok = StickerStore::instance()->copyStickerToClipboard(m_ctxBrief.filePath);
            showToast(ok ? tr("已复制")
                         : tr("复制失败"));
        } else if (index == idxScaleSub) {
            // 缩放拷贝：级联打开二级缩放子菜单（主菜单保持打开）
            openSubMenu(menu, idxScaleSub);
            return; // 主菜单不关闭，保证悬停级联的“父保持打开”语义
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
        } else if (index == idxSearch) {
            // 搜索相似：级联打开二级搜索引擎子菜单（主菜单保持打开），触摸兜底
            openSubMenu(menu, idxSearch);
            return; // 主菜单不关闭，保证悬停级联的“父保持打开”语义
        }
        menu->close();
    });

    {
        auto* overlay = new MenuOverlay(menu);
        m_ctxOverlay = overlay;
        connect(menu, &QObject::destroyed, overlay, &QObject::deleteLater);
        // 菜单关闭即移除铺满父页的 overlay：DeleteOnClose=false 时菜单 close()
        // 不触发 destroyed，若不在此删除，overlay 残留可见会让
        // MyScrollArea::isCoveredByOverlay 恒命中 → 滚轮永久失效（重启才恢复）。
        connect(menu, &QskPopup::closed, overlay, &QObject::deleteLater);
        connect(menu, &QskPopup::closed, this, [this]() {
            if (m_ctxSub && m_ctxSub->isOpen())
                m_ctxSub->close(); // 父菜单关闭时连带收掉子菜单
            m_ctxMenu = nullptr;
            m_ctxOverlay = nullptr;
            m_ctxSubRow = -1;
            m_subCloseTimer.stop();
        });
    }
    menu->open();
}

// 主列表右键/长按菜单的二级悬停级联子菜单（模拟经典 Qt 子菜单）：当前支持
// “缩放拷贝”与“搜索相似”两个入口，内容按父行 entryIndex 分支。QskMenu 无原生
// 子菜单（0.8.0 仅有 addOption/addSeparator，cascading 只是定位模式），此处用
// 独立二级 QskMenu 模拟：父菜单保持打开，子菜单悬挂该行右缘、行中对齐；悬停
// 子菜单保持，悬停到其他父行/空白时 225ms 防抖关闭。
void StickerHomePage::openSubMenu(QskMenu* parent, int entryIndex)
{
    // 已开：同行仅按父菜单该项行的当前几何重锚；跨行（“缩放拷贝/搜索相似”
    // 两个子菜单行互斥切换）→ 先关旧子菜单再重建。
    if (m_ctxSub && m_ctxSub->isOpen()) {
        if (m_ctxSubRow == entryIndex) {
            applySubMenuAnchor(parent, m_ctxSub, entryIndex);
            return;
        }
        m_ctxSub->close();
        m_ctxSub = nullptr;
        m_ctxSubRow = -1;
    }

    // 清理上次残留（如有）；主菜单必须保留
    if (m_ctxSub)
        m_ctxSub->deleteLater();

    // 隐藏主菜单 overlay，改由父子对 overlay 统一裁决
    // （否则按下子菜单时会被主菜单 overlay 判为“菜单外”而误关父菜单）
    if (m_ctxOverlay)
        m_ctxOverlay->setVisible(false);

    auto* sub = new QskMenu(this);
    // 非 modal 且不 CloseOnPressOutside：QskPopup::updateInputGrabber 不会为其
    // 创建铺满页面的 InputGrabber，才不致于抢走父菜单的 hover（Qt 只把 hover
    // 发给最顶层接收者，模态子菜单一开父菜单即 HoverLeave 且再无 HoverMove，
    // 导致子菜单开了又关、反复重弹）。点击外部关闭由 MenuOverlay 兜底。
    sub->setModal(false);
    sub->setPopupFlag(QskPopup::CloseOnPressOutside, false);
    sub->setPopupFlag(QskPopup::DeleteOnClose, false);
    m_ctxSub = sub;
    m_ctxSubRow = entryIndex;
    sub->installEventFilter(this);

    QString fallbackW; // 预锚估算用最长文本（sizeConstraint 为空时兜底）
    auto copyScaled = [this](qreal scale, const QString& toast) {
        StickerStore::instance()->touchSticker(m_ctxBrief.id);
        const bool ok = StickerStore::instance()
            ->copyStickerScaledToClipboard(m_ctxBrief.filePath, scale);
        showToast(ok ? toast : tr("复制失败"));
    };

    if (entryIndex == m_ctxScaleSubIdx) {
        const int idx01 = sub->addOption(QskLabelData(QStringLiteral("复制x0.1")));
        const int idx025 = sub->addOption(QskLabelData(QStringLiteral("复制x0.25")));
        const int idx05 = sub->addOption(QskLabelData(QStringLiteral("复制x0.5")));
        const int idx20 = sub->addOption(QskLabelData(QStringLiteral("复制x2.0")));
        fallbackW = QStringLiteral("复制x0.25");
        connect(sub, &QskMenu::triggered, this,
            [this, sub, parent, copyScaled, idx01, idx025, idx05, idx20](int index) {
                if (index == idx01) copyScaled(0.1, tr("已复制x0.1"));
                else if (index == idx025) copyScaled(0.25, tr("已复制x0.25"));
                else if (index == idx05) copyScaled(0.5, tr("已复制x0.5"));
                else if (index == idx20) copyScaled(2.0, tr("已复制x2.0"));
                // 经典语义：选择子项后整组收拢
                parent->close();
                sub->close();
            });
    } else if (entryIndex == m_ctxSearchSubIdx) {
        // 搜索相似：四个搜索引擎。Google/Bing/Yandex 走“托管上传 → 引擎页”；
        // DuckDuckGo 无 URL 图搜接口，降级为直接打开图搜页（需手动上传）。
        const int idxGoogle = sub->addOption(QskLabelData(tr("Google")));
        const int idxBing = sub->addOption(QskLabelData(tr("Bing")));
        const int idxYandex = sub->addOption(QskLabelData(tr("Yandex")));
        const int idxDdg = sub->addOption(QskLabelData(tr("DuckDuckGo")));
        fallbackW = QStringLiteral("DuckDuckGo");
        const QString filePath = m_ctxBrief.filePath;
        connect(sub, &QskMenu::triggered, this,
            [this, sub, parent, idxGoogle, idxBing, idxYandex, idxDdg, filePath](int index) {
                parent->close();
                sub->close();
                if (index == idxGoogle) {
                    startImageSearch(0, filePath);
                } else if (index == idxBing) {
                    startImageSearch(1, filePath);
                } else if (index == idxYandex) {
                    startImageSearch(2, filePath);
                } else if (index == idxDdg) {
                    showToast(tr("DuckDuckGo 需手动上传"));
                    QDesktopServices::openUrl(QUrl(
                        QStringLiteral("https://duckduckgo.com/?iax=images&ia=images")));
                }
            });
    }

    // 宽度下限（与主菜单同款防压缩）
    {
        const QFontMetricsF fm(sub->effectiveFont(QskMenu::Text));
        const qreal pad = sub->paddingHint(QskMenu::Segment).left()
                        + sub->paddingHint(QskMenu::Segment).right();
        sub->setStrutSizeHint(QskMenu::Panel,
            QSizeF(qskHorizontalAdvance(fm, fallbackW) + pad + 10, 0));
    }

    // 预锚：打开前即按真实尺寸（sizeConstraint）+ 父菜单已布局几何精确定位。
    // 注意 QskMenu::updateResources()（每次 polish 用 origin 覆盖 setPosition）
    // 只会采用 setOrigin 设置的坐标 —— 这里统一用 setOrigin。
    {
        const QFontMetricsF fm(sub->effectiveFont(QskMenu::Text));
        const qreal padT = sub->paddingHint(QskMenu::Segment).top();
        const qreal padB = sub->paddingHint(QskMenu::Segment).bottom();
        const qreal rowH = fm.height() + padT + padB;
        const QSizeF sc = sub->sizeConstraint();
        const qreal estW = sc.width() > 0 ? sc.width()
            : (qskHorizontalAdvance(fm, fallbackW)
               + sub->paddingHint(QskMenu::Segment).left()
               + sub->paddingHint(QskMenu::Segment).right() + 10);
        const qreal estH = sc.height() > 0 ? sc.height()
            : (4.0 * rowH + sub->paddingHint(QskMenu::Panel).top()
               + sub->paddingHint(QskMenu::Panel).bottom());

        const QPointF po = parent->mapToScene(QPointF(0.0, 0.0));
        const qreal pw = parent->width() > 0 ? parent->width()
                                             : parent->sizeConstraint().width();
        constexpr qreal gap = 4.0;

        qreal x = po.x() + pw + gap;
        qreal y = po.y();
        const QRectF cell = parent->cellRect(entryIndex);
        if (cell.isValid())
            y += cell.center().y() - (padT + rowH / 2.0);
        else
            y = (entryIndex + 0.5) * rowH - (padT + rowH / 2.0);

        const QRectF bounds = window() ? window()->contentItem()->boundingRect()
                                       : QRectF();
        if (bounds.isValid()) {
            // 右侧放不下 → 完全翻到父菜单左侧（gap 分隔），只有窗口连左侧也放
            // 不下时才钳制到窗口左缘（此时才允许最小覆盖）
            if (x + estW > bounds.right())
                x = qMax(bounds.left(), po.x() - gap - estW);
            if (y + estH > bounds.bottom())
                y = qMax(bounds.top(), bounds.bottom() - estH);
        }
        sub->setOrigin(sub->window() ? sub->parentItem()->mapFromScene(QPointF(x, y))
                                     : QPointF(x, y));
    }

    // opened：按真实几何校正锚点（行中对齐 + 越界侧翻/底部钳制）
    connect(sub, &QskPopup::opened, this,
        [this, sub, parent, entryIndex]() {
            if (sub != m_ctxSub)
                return;
            applySubMenuAnchor(parent, sub, entryIndex);
        });

    {
        auto* overlay = new MenuOverlay(sub, parent);
        connect(sub, &QObject::destroyed, overlay, &QObject::deleteLater);
        connect(sub, &QskPopup::closed, overlay, &QObject::deleteLater);
    }
    connect(sub, &QskPopup::closed, this, [this]() {
        if (m_ctxMenu && m_ctxMenu->isOpen() && m_ctxOverlay)
            m_ctxOverlay->setVisible(true);
        m_ctxSub = nullptr;
        m_ctxSubRow = -1;
    });
    sub->open();
}

// 按父菜单该项行的真实几何重锚子菜单：行中对齐（父行中线 == 子菜单首行中线）、
// 紧贴父右缘，右/底部放不下时侧翻到父左侧/上移钳制。
// 位置统一走 setOrigin（QskMenu::updateResources 每次 polish 用 origin 覆盖
// setPosition）；外力 setPosition 无效，需 setOrigin + polish 才生效。
void StickerHomePage::applySubMenuAnchor(QskMenu* parent, QskMenu* sub, int entryIndex)
{
    if (!sub->window() || !parent->isOpen())
        return;
    const QRectF pcell = parent->cellRect(entryIndex);
    const QRectF scell = sub->cellRect(0);
    if (!pcell.isValid() || !scell.isValid())
        return;

    const QPointF po = parent->mapToScene(QPointF(0.0, 0.0));
    const qreal pw = parent->width() > 0 ? parent->width()
                                         : parent->sizeConstraint().width();
    constexpr qreal gap = 4.0;

    qreal x = po.x() + pw + gap;
    qreal y = po.y() + pcell.center().y() - scell.center().y();

    const QRectF bounds = sub->window()->contentItem()->boundingRect();
    if (bounds.isValid()) {
        if (x + sub->width() > bounds.right())
            x = qMax(bounds.left(), po.x() - gap - sub->width());
        if (y + sub->height() > bounds.bottom())
            y = qMax(bounds.top(), bounds.bottom() - sub->height());
    }

    const QPointF origin = sub->parentItem()->mapFromScene(QPointF(x, y));
    if (qFuzzyCompare(origin.x(), sub->origin().x()) &&
        qFuzzyCompare(origin.y(), sub->origin().y()))
        return; // 无需重锚，避免每次鼠标移动都触发 polish

    sub->setOrigin(origin);
    sub->polish();
}

void StickerHomePage::closeSubMenu()
{
    if (m_ctxMenu && m_ctxMenu->isOpen() && m_ctxOverlay)
        m_ctxOverlay->setVisible(true);
    if (m_ctxSub && m_ctxSub->isOpen())
        m_ctxSub->close();
    m_ctxSub = nullptr;
    m_ctxSubRow = -1;
}

void StickerHomePage::scheduleSubClose()
{
    m_subCloseTimer.start();
}

void StickerHomePage::cancelSubClose()
{
    m_subCloseTimer.stop();
}

bool StickerHomePage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_ctxMenu) {
        switch (event->type()) {
        case QEvent::HoverMove: {
            cancelSubClose();
            const auto& he = *static_cast<QHoverEvent*>(event);
            const int idx = m_ctxMenu->indexAtPosition(he.position());
            if (idx == m_ctxScaleSubIdx || idx == m_ctxSearchSubIdx)
                openSubMenu(m_ctxMenu, idx);
            else if (m_ctxSub && m_ctxSub->isOpen())
                scheduleSubClose(); // 移动子菜单时可能划过其他父行，延迟关闭（sloppy）
            break;
        }
        case QEvent::HoverLeave:
            scheduleSubClose();
            break;
        default:
            break;
        }
    } else if (watched == m_ctxSub) {
        switch (event->type()) {
        case QEvent::HoverEnter:
        case QEvent::HoverMove:
            // 指针已进入子菜单：保住它，取消父菜单 HoverLeave 的防抖关闭
            cancelSubClose();
            break;
        case QEvent::HoverLeave:
            // 防抖关闭：若指针随后回到父菜单对应子行，会被父 HoverMove 取消
            scheduleSubClose();
            break;
        default:
            break;
        }
    }
    return false;
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
            trace_settings("home-keepScreenOn");
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

// ── 同步进度浮动窗口：懒创建；closed → deleteLater（QPointer 自动置空）──
void StickerHomePage::ensureSyncPopup(bool reset)
{
    if (!m_syncEngine) {
        return;
    }
    if (!m_syncPopup) {
        m_syncPopup = new SyncProgressPopup(m_syncEngine, this);
        connect(m_syncPopup, &QskPopup::closed,
                m_syncPopup, &QObject::deleteLater);
        connect(m_syncPopup, &QskPopup::closed, this, [this]() {
            if (m_syncEngine && !m_syncEngine->isRunning()) {
                m_syncEngine->deleteLater();
                m_syncEngine = nullptr;
            }
        });
    }
    if (reset) {
        m_syncPopup->resetForRun();
    }
    m_syncPopup->open();
}

// ── 搜索相似（以图搜图）：托管上传 + 打开系统浏览器 ──
void StickerHomePage::startImageSearch(int engine, const QString& filePath)
{
    m_pendingEngine = engine;

    if (!m_search) {
        m_search = new ImageSearch(this);
        connect(m_search, &ImageSearch::progressChanged, this,
            [this](int percent, qint64 sent, qint64 total, const QString& status) {
                if (m_searchPopup) {
                    m_searchPopup->setProgress(percent, sent, total, status);
                }
            });
        connect(m_search, &ImageSearch::uploaded, this,
            [this](const QString& url) {
                if (m_searchPopup) {
                    m_searchPopup->setUploadedUrl(url);
                }
                openSearchEngine(m_pendingEngine, url);
            });
        connect(m_search, &ImageSearch::failed, this,
            [this](const QString& reason) {
                if (m_searchPopup) {
                    m_searchPopup->setFailed(reason);
                }
            });
    }

    if (!m_searchPopup) {
        m_searchPopup = new ImageSearchPopup(this);
        connect(m_searchPopup, &QskPopup::closed,
                m_searchPopup, &QObject::deleteLater);
    }

    // 文件名 + 目标引擎（DDG 走降级分支不经此处）
    static const char* kEngineNames[] = { "Google", "Bing", "Yandex", "DuckDuckGo" };
    const int engineIdx = qBound(0, engine, 3);
    m_searchPopup->resetForRun(QFileInfo(filePath).fileName(),
        QString::fromLatin1(kEngineNames[engineIdx]));
    // 弹出的时序让步：父/子菜单刚 close（含淡出），下一事件循环再开浮层更稳
    QTimer::singleShot(0, this, [this]() {
        if (m_searchPopup) {
            m_searchPopup->open();
        }
    });
    m_search->upload(filePath);
}

void StickerHomePage::openSearchEngine(int engine, const QString& imageUrl)
{
    const QString enc = QString::fromLatin1(QUrl::toPercentEncoding(imageUrl));
    QUrl url;
    switch (engine) {
        case 0: // Google
            url = QUrl(QStringLiteral("https://www.google.com/searchbyimage?image_url=")
                + enc);
            break;
        case 1: // Bing
            url = QUrl(QStringLiteral(
                "https://www.bing.com/images/searchbyimage?cbir=sbi&imgurl=") + enc);
            break;
        default: // Yandex
            url = QUrl(QStringLiteral(
                "https://yandex.com/images/search?url=") + enc
                + QStringLiteral("&rpt=imageview"));
            break;
    }
    if (url.isValid()) {
        QDesktopServices::openUrl(url);
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