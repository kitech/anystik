#include "mainwindow.h"

#include "buildinfo.h"
#include "config.h"
#include "CustomTitleBar.h"
#include "FramelessHelper.h"
#include "EmbeddedMenuBar.h"
#include "lambdaslot.h"
#include "sharedstatusbar.h"
#include "systemtrayicon.h"
#include "translator.h"
#include "ThemeManager.h"
#include "StyleParams.h"
#include "appsetup.h"
#include "pagemanager.h"
#include "stickerlistpage.h"

#include <qpixmap.h>
#include <qevent.h>
#include <qtimer.h>

#ifdef QT3_BUILD
#include <qmessagebox.h>
#else
#include <QMessageBox>
#include <QAction>
#endif

#include "app_icon.xpm"

// 顶级菜单顺序（setTopMenuText 依赖此顺序，勿随意调整）
static const char* const kTopMenuKeys[4] = {
    "menu.file", "menu.demo", "menu.appearance", "menu.help"
};

// 三套语言代码，与 titlebar 语言下拉索引一一对应
static const char* const kLangCodes[3] = { "zh-CN", "zh-TW", "en-US" };

// ═══════════════ Qt3/Qt4+ 组合框差异 ═══════════════
// Qt3: insertItem(text, index) / setCurrentItem / currentItem
// Qt4+: insertItem(index, text) / setCurrentIndex / currentIndex
static void comboAddItem(QComboBox* cb, const QString& text)
{
#ifdef QT3_BUILD
    cb->insertItem(text, cb->count());
#else
    cb->insertItem(cb->count(), text);
#endif
}

static void comboSetCurrent(QComboBox* cb, int index)
{
#ifdef QT3_BUILD
    cb->setCurrentItem(index);
#else
    cb->setCurrentIndex(index);
#endif
}

static int comboCurrent(const QComboBox* cb)
{
#ifdef QT3_BUILD
    return cb->currentItem();
#else
    return cb->currentIndex();
#endif
}

// qHasPendingEvents 的双端垫片：qltox 用 Qt 自带 qHasPendingEvents()；
// Qt3 的 QApplication 有 hasPendingEvents()（/opt/qt338sh/include/qapplication.h:153），
// Qt5/6 已移除 → 非 Qt3 返回 false，靠 m_paintCounter>5 兜底（见 event()）。
static bool qlstikHasPendingEvents()
{
#ifdef QT3_BUILD
    return qApp->hasPendingEvents();
#else
    return false;
#endif
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent
#ifdef QT3_BUILD
        // Qt3：QMainWindow(parent, name, flags)
        , "qlstik"
        , Qt::WType_TopLevel | Qt::WStyle_Customize | Qt::WSubWindow
              | Qt::WStyle_MinMax | Qt::WStyle_SysMenu | Qt::WStyle_NoBorder
#endif
#ifndef QT3_BUILD
        // Qt4/5/6：QMainWindow(parent, flags)
        , Qt::FramelessWindowHint
#endif
    )
    , framelessHelper(0)
    , titleBar(0)
    , tray(0)
    , pageStack(0)
    , pageMgr(0)
    , statusLabel(0)
    , langCombo(0)
    , styleCombo(0)
    , darkCheck(0)
    , demoLeftLabel(0)
    , demoLeftBtn(0)
    , demoRightBtn(0)
    , demoRightLabel(0)
    , demoEdit(0)
    , forceQuit(false)
    , m_firstPaintLogged(false)
    , m_paintCounter(0)
    , m_initialLoadDone(false)
{
    // 窗口尺寸对齐 anystik/src/main.cpp:404 的 setPreferredSize({420, 680})。
    // ⚠ setPreferredSize 是 QSkinny 概念，Qt3 无等价 API，resize() 是语义最接近的映射
    //   （一次性设初始尺寸，后续用户可自由拉伸）。同 anystik 一样**不设**最小/最大尺寸
    //   —— 加了反而偏离基准。见移植计划 §6.3l L.1。
    // ⚠ 实测：本页 pageStack 的 minimumSizeHint 只有 362×194，但整个壳的最小宽度
    //   由 CustomTitleBar（EmbeddedMenuBar + 语言/皮肤 combo）撑到 996，故首帧仍
    //   是 996×680。这是外壳问题，不是贴纸页问题；要真正 420 宽需改 qlcomp 的
    //   CustomTitleBar（影响所有 qltox 派生程序），未在本次改动范围内。
    resize(420, 680);

    buildStatusBar();
    buildCentralWidget();
    buildAppearancePanel();
    buildMenus();
    buildTray();

    // 首帧门：捕获本窗口后代的首个 Paint（见 eventFilter；门命中后即移除）。
    qApp->installEventFilter(this);

    // 兜底：离屏/无 Paint 场景下，2s 后无论如何也执行延迟加载（onDeferredLoad
    // 内有一次性格守卫，与首帧门命中路径互斥）。
    QTimer::singleShot(2000, this, SLOT(onDeferredLoad()));

    QObject::connect(&Translator::instance(), SIGNAL(languageChanged()),
                     this, SLOT(retranslateUi()));
}

MainWindow::~MainWindow()
{
}

// ═══════════════ SharedStatusBar（底部浮窗状态栏）═══════════════

void MainWindow::buildStatusBar()
{
    SharedStatusBar::instance()->show();
    addDemoWidgets();
    SharedStatusBar::instance()->showMessage(_("demo.ready"), 3000);
}

// 移植 qltox mainwindow.cpp:452 initDemoWidgets()
// 左端 addWidget / 右端 addPermanentWidget，验证 SharedStatusBar 两端布局
void MainWindow::addDemoWidgets()
{
    if (hasDemoWidgets()) { return; }
    SharedStatusBar* bar = SharedStatusBar::instance();

    demoLeftLabel = new QLabel(qFromUtf8("Left"), bar);
    bar->addWidget(demoLeftLabel);

    demoLeftBtn = new QPushButton(qFromUtf8("LBtn"), bar);
    demoLeftBtn->setFixedWidth(36);
    bar->addWidget(demoLeftBtn);

    demoRightLabel = new QLabel(qFromUtf8("Right"), bar);
    bar->addPermanentWidget(demoRightLabel);

    demoRightBtn = new QPushButton(qFromUtf8("RBtn"), bar);
    demoRightBtn->setFixedWidth(36);
    bar->addPermanentWidget(demoRightBtn);

    demoEdit = new QLineEdit(bar);
    demoEdit->setFixedWidth(100);
#ifndef QT3_BUILD
    demoEdit->setPlaceholderText(qFromUtf8("input..."));
#endif
    bar->addPermanentWidget(demoEdit);
}

void MainWindow::removeDemoWidgets()
{
    SharedStatusBar* bar = SharedStatusBar::instance();
    if (demoLeftLabel)  { bar->removeWidget(demoLeftLabel);  delete demoLeftLabel;  demoLeftLabel = 0; }
    if (demoLeftBtn)    { bar->removeWidget(demoLeftBtn);    delete demoLeftBtn;    demoLeftBtn = 0; }
    if (demoRightLabel) { bar->removeWidget(demoRightLabel); delete demoRightLabel; demoRightLabel = 0; }
    if (demoRightBtn)   { bar->removeWidget(demoRightBtn);   delete demoRightBtn;   demoRightBtn = 0; }
    if (demoEdit)       { bar->removeWidget(demoEdit);       delete demoEdit;       demoEdit = 0; }
}

bool MainWindow::hasDemoWidgets() const
{
    return demoLeftLabel != 0;
}

// ═══════════════ 中央区域：CustomTitleBar + 状态文本 ═══════════════

void MainWindow::buildCentralWidget()
{
    QWidget* central = new QWidget(this);
    QBoxLayout* lay = qNewBoxLayout(central, QBoxLayout::TopToBottom, 0, 0);
    qSetMargins(lay, -1, 0, -1, -1);   // 只清顶部；左右/底保留 style 默认

    titleBar = new CustomTitleBar(central);
    titleBar->setLabel(_("app_title"));
    lay->addWidget(titleBar, 0);

    statusLabel = new QLabel(central);
    qSetLabelSelectable(statusLabel);
    statusLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    statusLabel->setText(qFromUtf8(
        "qlstik shell\n"
        "shared: stikcommon.pri -> myvendor.pri + qldox.pri (cJSON + EventPoller)\n"
        "ui: qlcomp/qlite.pri (CustomTitleBar / EmbeddedMenuBar / SharedStatusBar /\n"
        "    SystemTrayIcon / LimeStyle / ThemeManager / Translator)\n"
        "next: batch 1 (myi18n / eifreader / davobfus) -> batch 2 (QNAM shim on EventPoller)"));
    statusLabel->hide();      // 已被页面栈取代，仅留给演示菜单的 setText 用

    // ── 页面栈：业务页统一由 PageManager 管理生命周期与导航 ──
    pageStack = new StackedWidget(central);
    lay->addWidget(pageStack, 1);
    // ⚠ 不设背景角色：Qt3 的 QWidgetStack 无 setBackgroundRole，QPalette 也没有
    //   Base 角色（那是 Qt4+）。贴纸网格在 paintEvent 里自填底色，不需要壳刷底。

    pageMgr = new PageManager(pageStack, this);
    // 贴纸家：CachePolicy::LRU（page.h:108-109）—— 切走进缓存池、回来不重解码
    PageRegistration reg;
    reg.policy = CachePolicy::LRU;
    pageMgr->registerPage(QString::fromUtf8("stickerList"),
                          []() -> Page* { return new StickerListPage(); },
                          reg);
    pageMgr->open(QString::fromUtf8("stickerList"));

    setCentralWidget(central);

    // 无边框窗口：拖动/缩放/贴边/系统菜单
    framelessHelper = new FramelessHelper(this);
    framelessHelper->setup(this);
    titleBar->connectFramelessHelper(framelessHelper);

    // ≡ 按钮：切换 menubar 显隐
    QObject::connect(titleBar, SIGNAL(appMenuClicked()), titleBar, SLOT(toggleMenu()));
}

// ═══════════════ 外观三件套（语言 / 皮肤 / 深色）═══════════════
// 移植 qltox mainwindow.cpp:700-760

void MainWindow::buildAppearancePanel()
{
    QWidget* panel = new QWidget(titleBar);
    QBoxLayout* lay = qNewBoxLayout(panel, QBoxLayout::LeftToRight, 2, 0);

    langCombo = new QComboBox(panel);
    langCombo->setFixedHeight(30);
    fillLangCombo();
    lay->addWidget(langCombo, 0);

    styleCombo = new QComboBox(panel);
    styleCombo->setFixedHeight(30);
    fillStyleCombo();
    lay->addWidget(styleCombo, 0);

    darkCheck = new QCheckBox(_("theme_dark"), panel);
    darkCheck->setFixedHeight(30);
    qSetChecked(darkCheck, ThemeManager::isDarkMode());
    lay->addWidget(darkCheck, 0);

    QObject::connect(langCombo, SIGNAL(activated(int)), this, SLOT(onTitleUilang(int)));
    QObject::connect(styleCombo, SIGNAL(activated(int)), this, SLOT(onTitleStyle(int)));
    QObject::connect(darkCheck, SIGNAL(toggled(bool)), this, SLOT(onTitleDark(bool)));

    titleBar->addTitleWidget(panel, 0);
}

int MainWindow::langIndexOf(const QString& langCode)
{
    for (int i = 0; i < 3; i++) {
        if (langCode == qFromUtf8(kLangCodes[i])) { return i; }
    }
    return 0;
}

int MainWindow::styleIndexOf(const QString& styleId)
{
    const std::vector<StyleParams::Definition>& styles = StyleParams::registeredStyles();
    for (int i = 0; i < (int)styles.size(); i++) {
        if (styleId == qFromUtf8(styles[i].id)) { return i; }
    }
    return 0;
}

void MainWindow::fillLangCombo()
{
    if (!langCombo) { return; }
    langCombo->clear();
    comboAddItem(langCombo, _("lang.zh_CN"));
    comboAddItem(langCombo, _("lang.zh_TW"));
    comboAddItem(langCombo, _("lang.en_US"));
    comboSetCurrent(langCombo, langIndexOf(Translator::instance().currentLang()));
}

void MainWindow::fillStyleCombo()
{
    if (!styleCombo) { return; }
    styleCombo->clear();
    const std::vector<StyleParams::Definition>& styles = StyleParams::registeredStyles();
    for (int i = 0; i < (int)styles.size(); i++) {
        comboAddItem(styleCombo, _(qFromUtf8(styles[i].displayKey)));
    }
    comboSetCurrent(styleCombo, styleIndexOf(QString(ThemeManager::styleId())));
}

// ═══════════════ 菜单栏（文件 / 演示 / 外观 / 帮助）═══════════════

QString MainWindow::darkToggleLabel() const
{
    return ThemeManager::isDarkMode() ? _("action.dark_off") : _("action.dark_on");
}

void MainWindow::addMenuItem(void* menu, const QObject* receiver, const char* slot,
                             const char* key, bool darkToggle)
{
    QString text = darkToggle ? darkToggleLabel() : _(qFromUtf8(key));
    MenuItemRef ref;
    ref.key = key;
    ref.darkToggle = darkToggle;
    ref.owner = menu;
#ifdef QT3_BUILD
    QPopupMenu* m = static_cast<QPopupMenu*>(menu);
    ref.id = m->insertItem(text, receiver, slot);
#else
    QMenu* m = static_cast<QMenu*>(menu);
    ref.action = m->addAction(text, receiver, slot);
#endif
    menuItemRefs.push_back(ref);
}

// 顶级菜单：自己建而不走 EmbeddedMenuBar::addMenu，因为后者不返回句柄，
// 而 Qt3 改文案只能按 id（QMenuData::changeItem）来定位。
void* MainWindow::addTopMenu(const char* key)
{
    EmbeddedMenuBar* mb = titleBar->menuBar();
    QString title = _(qFromUtf8(key));
    MenuItemRef ref;
    ref.key = key;
    ref.owner = mb;
    void* sub = 0;
#ifdef QT3_BUILD
    QPopupMenu* menu = new QPopupMenu(mb);
    menu->setFont(mb->font());
    ref.id = mb->insertItem(title, menu);
    sub = menu;
#else
    QMenu* menu = mb->addMenu(title);
    sub = menu;
#endif
    topMenuRefs.push_back(ref);
    return sub;
}

void* MainWindow::addSubMenu(void* parentMenu, const QString& title, const char* key)
{
    EmbeddedMenuBar* mb = titleBar->menuBar();
    MenuItemRef ref;
    ref.key = key;
    ref.owner = parentMenu;
    void* sub = 0;
#ifdef QT3_BUILD
    QPopupMenu* pm = static_cast<QPopupMenu*>(parentMenu);
    QPopupMenu* subMenu = new QPopupMenu(mb);
    subMenu->setFont(mb->font());
    ref.id = pm->insertItem(title, subMenu);
    sub = subMenu;
#else
    QMenu* pm = static_cast<QMenu*>(parentMenu);
    QMenu* subMenu = pm->addMenu(title);
    ref.action = subMenu->menuAction();
    sub = subMenu;
#endif
    menuItemRefs.push_back(ref);
    return sub;
}

void MainWindow::addMenuSeparator(void* menu)
{
#ifdef QT3_BUILD
    static_cast<QPopupMenu*>(menu)->insertSeparator();
#else
    static_cast<QMenu*>(menu)->addSeparator();
#endif
}

void MainWindow::setTopMenuText(int index, const QString& text)
{
    if (index < 0 || index >= (int)topMenuRefs.size()) { return; }
#ifdef QT3_BUILD
    // Qt3：QMenuBar 继承 QMenuData，只有 changeItem(id, text)
    EmbeddedMenuBar* mb = static_cast<EmbeddedMenuBar*>(topMenuRefs[index].owner);
    mb->changeItem(topMenuRefs[index].id, text);
#else
    // Qt4 无 QMenu::menuAction()，menubar 顶级项按位置取（顺序固定，与 kTopMenuKeys 一致）
    EmbeddedMenuBar* mb = titleBar->menuBar();
    QList<QAction*> acts = mb->actions();
    if (index < acts.size()) { acts[index]->setText(text); }
#endif
}

void MainWindow::refreshMenuTexts()
{
    for (size_t i = 0; i < menuItemRefs.size(); i++) {
        const MenuItemRef& ref = menuItemRefs[i];
        if (!ref.key) { continue; }
        QString text = ref.darkToggle ? darkToggleLabel() : _(qFromUtf8(ref.key));
#ifdef QT3_BUILD
        // Qt3 只能按 id 改文案（QMenuData::changeItem），id 由 insertItem 分配
        static_cast<QPopupMenu*>(ref.owner)->changeItem(ref.id, text);
#else
        if (ref.action) { ref.action->setText(text); }
#endif
    }
    for (int i = 0; i < (int)topMenuRefs.size(); i++) {
        setTopMenuText(i, _(qFromUtf8(topMenuRefs[i].key)));
    }
}

void MainWindow::buildMenus()
{
    EmbeddedMenuBar* mb = titleBar->menuBar();
    menuItemRefs.clear();
    topMenuRefs.clear();

    // ── 文件(&F) ──
    MenuWidget34* file = static_cast<MenuWidget34*>(addTopMenu("menu.file"));
    addMenuItem(file, this, SLOT(onMenu1Stub()), "action.new");
    addMenuSeparator(file);
    addMenuItem(file, this, SLOT(quitApp()), "action.quit");

    // ── 演示(&D)：状态栏 + 托盘壳自检 ──
    MenuWidget34* demo = static_cast<MenuWidget34*>(addTopMenu("menu.demo"));
    addMenuItem(demo, this, SLOT(onDemoStatusInfo()), "demo.status_info");
    addMenuItem(demo, this, SLOT(onDemoStatusWarning()), "demo.status_warning");
    addMenuItem(demo, this, SLOT(onDemoStatusError()), "demo.status_error");
    addMenuSeparator(demo);
    addMenuItem(demo, this, SLOT(onDemoStatusClear()), "demo.status_clear");
    addMenuItem(demo, this, SLOT(onDemoToggleStatusWidgets()), "demo.status_widgets");
    addMenuSeparator(demo);
    addMenuItem(demo, this, SLOT(onDemoTrayBubble()), "demo.tray_bubble");

    // ── 外观(&V)：皮肤子菜单 + 深色切换 + 语言子菜单 ──
    MenuWidget34* appearance = static_cast<MenuWidget34*>(addTopMenu("menu.appearance"));

    void* skins = addSubMenu(appearance, _("menu.skins"), "menu.skins");
    const std::vector<StyleParams::Definition>& styles = StyleParams::registeredStyles();
    for (int i = 0; i < (int)styles.size(); i++) {
        // 菜单项在 Qt3/4 都不能带参数（QAction 版的 int 参数是 checked 态），
        // 用 LambdaSlot 把索引带进槽
        LambdaSlot* slot = new LambdaSlot(static_cast<QObject*>(skins),
                                          [this, i]() { onTitleStyle(i); });
        addMenuItem(skins, slot, SLOT(call()), styles[i].displayKey);
    }
    addMenuSeparator(appearance);
    addMenuItem(appearance, this, SLOT(onDarkMenuItem()), "theme_dark", true);

    void* langs = addSubMenu(appearance, _("menu.languages"), "menu.languages");
    static const char* const kLangItemKeys[3] = {
        "lang.zh_CN", "lang.zh_TW", "lang.en_US"
    };
    for (int i = 0; i < 3; i++) {
        LambdaSlot* slot = new LambdaSlot(static_cast<QObject*>(langs),
                                          [this, i]() { onTitleUilang(i); });
        addMenuItem(langs, slot, SLOT(call()), kLangItemKeys[i]);
    }

    // ── 帮助(&H) ──
    MenuWidget34* help = static_cast<MenuWidget34*>(addTopMenu("menu.help"));
    addMenuItem(help, this, SLOT(onAboutApp()), "menu.about_qlstik");
    addMenuSeparator(help);
    addMenuItem(help, qApp, SLOT(aboutQt()), "menu.aboutqt");

    mb->finalize();
}

// ═══════════════ 系统托盘 ═══════════════
// 移植 qltox mainwindow.cpp:784-800 + 1563-1600

void MainWindow::buildTray()
{
    if (!SystemTrayIcon::isSystemTrayAvailable()) { return; }

    tray = new SystemTrayIcon(QPixmap(app_icon), this);
    tray->setToolTip(_("app_title"));
    tray->setVisible(true);

    QObject::connect(tray, SIGNAL(activated(int)), this, SLOT(trayActivated(int)));
    QObject::connect(tray, SIGNAL(messageClicked()), this, SLOT(trayShowMainWindow()));

    PopupMenu* trayMenu = new PopupMenu(this);
    addMenuItem(trayMenu, this, SLOT(trayShowMainWindow()), "tray.open");
    addMenuSeparator(trayMenu);
    addMenuItem(trayMenu, this, SLOT(quitApp()), "tray.quit");
    tray->setContextMenu(trayMenu);
}

void MainWindow::trayActivated(int reason)
{
    // Trigger(左键)/DoubleClick(双击) → 恢复主窗口；
    // Context(右键) 由 SystemTrayIcon 内部弹上下文菜单；MiddleClick 无动作
    if (reason == SystemTrayIcon::Trigger || reason == SystemTrayIcon::DoubleClick) {
        trayShowMainWindow();
    }
}

void MainWindow::trayShowMainWindow()
{
    show();
    raise();
    qActivateWindow(this);
}

void MainWindow::quitApp()
{
    forceQuit = true;
    close();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!forceQuit && tray && tray->isVisible()) {
        hide();
        event->ignore();
        return;
    }
    event->accept();
}

// ═══════════════ 首帧门 ═══════════════
// 目的：等「窗口首批绘制已发出且事件队列排干」后再做重活（贴纸列表全量查询 +
// 缩略图解码），避免同步加载阻塞首帧导致界面残缺（用户 2026-10-04 需求）。
// 机制逐条照搬 qltox/mainwindow.cpp:1533-1560（只读参考，不改 qltox）。
//   * Qt 无「绘制完成/已上屏」公开信号；Qt3 无 QEvent::Expose（qevent.h 只有
//     Paint=12/Show=17），只能观测 Paint。
//   * hasPendingEvents() 为真说明该轮事件尚未排干，先不触发。
//   * m_paintCounter>5 兜底：光标闪烁等持续事件会让队列永不排干。
// ⚠ 实测：顶层 QMainWindow 被中央控件完全覆盖，自身**收不到 Paint**，故不能像
//   qltox 那样直接在 MainWindow::event() 里观测。改用 qApp 事件过滤器，捕获
//   「本窗口后代」的 Paint（用 topLevelWidget()==this 排除托盘/弹窗等其它顶层）。
bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (!m_firstPaintLogged && event->type() == QEvent::Paint) {
        QWidget* w = dynamic_cast<QWidget*>(watched);
        if (w && w->topLevelWidget() == this) {
            m_paintCounter++;
            if (!qlstikHasPendingEvents() || m_paintCounter > 5) {
                m_firstPaintLogged = true;
                qApp->removeEventFilter(this);
                QTimer::singleShot(0, this, SLOT(onFirstPaintComplete()));
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onFirstPaintComplete()
{
    if (m_initialLoadDone) { return; }
    // 排干 show/paint 一轮，再留 150ms 给窗口系统异步上屏。
    // 依据：X11 绘制异步；QObject::processEvents 文档；SO #44396099 指出
    // QTest::qWaitForWindowExposed 内部亦为循环 processEvents。
    qApp->processEvents();
    QTimer::singleShot(150, this, SLOT(onDeferredLoad()));
}

void MainWindow::onDeferredLoad()
{
    if (m_initialLoadDone) { return; }
    m_initialLoadDone = true;
    if (pageMgr) { pageMgr->notifyFirstFrame(); }
}

// ═══════════════ 外观：语言 / 皮肤 / 深色 ═══════════════

void MainWindow::onTitleUilang(int index)
{
    if (index < 0 || index > 2) { return; }
    QString langCode = qFromUtf8(kLangCodes[index]);
    Config::setValue("uilang", langCode);
    // loadLanguage 内部发 languageChanged → retranslateUi()
    Translator::instance().loadLanguage(langCode);
    QtappSetup::installQtTranslations(langCode);
    if (langCombo) { comboSetCurrent(langCombo, index); }
}

void MainWindow::onTitleStyle(int index)
{
    const std::vector<StyleParams::Definition>& styles = StyleParams::registeredStyles();
    if (index < 0 || index >= (int)styles.size()) { return; }
    ThemeManager::setStyle(styles[index].id, ThemeManager::isDarkMode());
    Config::setValue("style", qFromUtf8(styles[index].id));
    if (styleCombo) { comboSetCurrent(styleCombo, index); }
}

void MainWindow::onTitleDark(bool on)
{
    if (on == ThemeManager::isDarkMode()) { return; }
    ThemeManager::applyTheme(on);
    Config::setValue("dark", on ? "true" : "false");
    if (darkCheck) { qSetChecked(darkCheck, on); }
    refreshMenuTexts();   // 深色开关项文案随状态变
}

void MainWindow::onDarkMenuItem()
{
    // 走 checkbox 的 toggled 信号统一入口，避免主题/持久化两处实现
    if (darkCheck) {
        qSetChecked(darkCheck, !ThemeManager::isDarkMode());
    } else {
        bool on = !ThemeManager::isDarkMode();
        ThemeManager::applyTheme(on);
        Config::setValue("dark", on ? "true" : "false");
        refreshMenuTexts();
    }
}

// ═══════════════ 翻译 ═══════════════

void MainWindow::retranslateUi()
{
    QString title = _("app_title");
    qSetWindowTitle(this, title);
    if (titleBar) { titleBar->setLabel(title); }
    if (tray) { tray->setToolTip(title); }
    if (darkCheck) { darkCheck->setText(_("theme_dark")); }

    fillLangCombo();
    fillStyleCombo();
    refreshMenuTexts();
}

// ═══════════════ 演示菜单 ═══════════════

void MainWindow::onMenu1Stub()
{
    SharedStatusBar::instance()->showMessage(_("demo.status_info"), 3000);
}

void MainWindow::onDemoStatusInfo()
{
    SharedStatusBar::instance()->showMessageTyped(_("demo.status_info"), StatusInfo, 3000);
}

void MainWindow::onDemoStatusWarning()
{
    SharedStatusBar::instance()->showMessageTyped(_("demo.status_warning"), StatusWarning, 3000);
}

void MainWindow::onDemoStatusError()
{
    SharedStatusBar::instance()->showMessageTyped(_("demo.status_error"), StatusError, 3000);
}

void MainWindow::onDemoStatusClear()
{
    SharedStatusBar::instance()->clearMessage();
}

void MainWindow::onDemoToggleStatusWidgets()
{
    if (hasDemoWidgets()) {
        removeDemoWidgets();
        SharedStatusBar::instance()->showMessage(_("demo.status_clear"), 3000);
    } else {
        addDemoWidgets();
        SharedStatusBar::instance()->showMessage(_("demo.ready"), 3000);
    }
}

void MainWindow::onDemoTrayBubble()
{
    if (!tray || !tray->isVisible() || !SystemTrayIcon::supportsMessages()) {
        SharedStatusBar::instance()->showMessage(_("demo.tray_bubble"), 3000);
        return;
    }
    tray->showMessage(_("app_title"), _("demo.tray_bubble"),
                      SystemTrayIcon::Information, 5000);
}

void MainWindow::onAboutApp()
{
    QString text = QString(
        "<h3>qlstik</h3>"
        "<p>Anystik desktop shell (Qt Widgets).</p>"
        "<p>Qt: %1 | Build: %2</p>").arg(qVersion()).arg(QLSTIK_GIT_COMMIT_STR);
    QMessageBox::about(this, _("menu.about_qlstik"), text);
}
