#include "settingspage.h"
#include "compat34.h"

#include "config.h"
#include "translator.h"       // _() / Translator::instance()
#include "ThemeManager.h"     // setStyle / applyTheme / isDarkMode / styleId
#include "StyleParams.h"      // registeredStyles()
#include "appsetup.h"         // QtappSetup::installQtTranslations
#include "qstandardpaths_shim.h"

#include <qwidget.h>
#include <qlabel.h>
#include <qpushbutton.h>
#include <qcombobox.h>
#include <qcheckbox.h>
#include <qlineedit.h>
#include <qframe.h>
#include <qlayout.h>
#include <qdir.h>

#include <vector>

// 三套语言代码/显示名，与 mainwindow.cpp:38 的 kLangCodes 对齐（那份是文件静态，
// 设置页自带一份副本）。
static const char* const kLangCodes[3]  = { "zh-CN", "zh-TW", "en-US" };
static const char* const kLangLabels[3] = { "简体中文", "English", "繁體中文" };

static int langIndexOf(const QString& code)
{
    for (int i = 0; i < 3; i++) {
        if (code == QString::fromUtf8(kLangCodes[i])) { return i; }
    }
    return 0;
}

static int styleIndexOf(const QString& id)
{
    const std::vector<StyleParams::Definition>& s = StyleParams::registeredStyles();
    for (int i = 0; i < (int)s.size(); i++) {
        if (id == QString::fromUtf8(s[i].id)) { return i; }
    }
    return 0;
}

// 当前贴纸根目录（与 stickerlistpage.cpp 的 stickerBaseDir() 同源；只读配置，不碰 DB）
static QString currentStorageRoot()
{
    const QString saved = Config::value(QString::fromUtf8("storageRoot"));
    if (!saved.isEmpty() && QDir(saved).exists()) { return saved; }
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

// 相册目标（anystik storageRootPath(Pictures) = Pictures/anystik）。
// Qt5+ 由 Qt 提供 PicturesLocation；Qt3/Qt4 垫片只实现 AppLocalDataLocation，
// 故 PicturesLocation 取空时回退 ~/Pictures。
static QString picturesStorageRoot()
{
    QString pics = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (pics.isEmpty()) { pics = qGetHomePath() + QString::fromUtf8("/Pictures"); }
    return pics + QString::fromUtf8("/anystik");
}

static QString privateStorageRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

// ── 行构造助手 ──
static QWidget* mkRow(QWidget* body, QBoxLayout* bodyLayout, const QString& label)
{
    QWidget* r = new QWidget(body);
    QBoxLayout* h = qNewBoxLayout(r, QBoxLayout::LeftToRight, 0, 0);
    h->setSpacing(12);
    qSetMargins(h, 0, 0, 0, 0);
    QLabel* l = new QLabel(label, r);
    l->setFixedWidth(140);            // 对齐 anystik 的 preferredWidth(140)
    h->addWidget(l, 0);
    bodyLayout->addWidget(r, 0);
    return r;
}

static QBoxLayout* rl(QWidget* r) { return (QBoxLayout*)r->layout(); }

static void newSep(QWidget* body, QBoxLayout* bodyLayout)
{
    QFrame* line = new QFrame(body);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    bodyLayout->addWidget(line, 0);
}

// 只读值标签：可拷贝 + 横向可缩（不撑宽 → 关横向滚动也不会把右侧按钮挤出视口）
static QLabel* makeValueLabel(QWidget* parent)
{
    QLabel* l = new QLabel(parent);
    qSetLabelSelectable(l);                                         // Qt4+ 可选中；Qt3 双击复制
    l->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); // 横向最小宽=0，绝不撑宽
#ifndef QT3_BUILD
    l->setWordWrap(true);                                           // Qt4+ 折行
#endif
    return l;
}

// QComboBox 跨版本（与 stickerlistpage.cpp:469-483 同源）
static void comboAdd(QComboBox* c, const QString& text)
{
#ifdef QT3_BUILD
    c->insertItem(text);
#else
    c->addItem(text);
#endif
}

static void comboSet(QComboBox* c, int index)
{
#ifdef QT3_BUILD
    c->setCurrentItem(index);
#else
    c->setCurrentIndex(index);
#endif
}

SettingsPage::SettingsPage(QWidget* parent)
    : Page(parent)
    , m_langCombo(0)
    , m_transitionCombo(0)
    , m_styleCombo(0)
    , m_darkCheck(0)
    , m_fontCombo(0)
    , m_debugCheck(0)
    , m_phoneCombo(0)
    , m_pushCheck(0)
    , m_backendCombo(0)
    , m_gotifyUrlEdit(0)
    , m_gotifyTokenEdit(0)
    , m_currentRootValue(0)
    , m_picsRootValue(0)
    , m_privateRootValue(0)
    , m_migratePicsBtn(0)
    , m_migratePrivateBtn(0)
    , m_davUrlEdit(0)
    , m_davUserEdit(0)
    , m_davPassEdit(0)
{
    buildUi();
}

SettingsPage::~SettingsPage() {}

void SettingsPage::buildUi()
{
    QBoxLayout* root = qNewBoxLayout(this, QBoxLayout::TopToBottom, 0, 0);
    root->setSpacing(6);
    qSetMargins(root, 0, 0, 0, 0);

    // ── TopBar：← 返回 + 标题「设置」 ──
    QWidget* topBar = new QWidget(this);
    QBoxLayout* tb = qNewBoxLayout(topBar, QBoxLayout::LeftToRight, 0, 0);
    tb->setSpacing(8);
    qSetMargins(tb, 4, 4, 4, 4);
    QPushButton* back = new QPushButton(QString::fromUtf8("←"), topBar);
    back->setFixedSize(44, 44);
    connect(back, SIGNAL(clicked()), this, SLOT(onBackClicked()));
    QLabel* title = new QLabel(QString::fromUtf8("设置"), topBar);
    title->setAlignment(Qt::AlignCenter);
    tb->addWidget(back, 0);
    tb->addWidget(title, 1);
    root->addWidget(topBar, 0);

    // ── 滚动主体（样板：qlcomp/jsonview.cpp:88-104）──
#ifdef QT3_BUILD
    QScrollView* scroll = new QScrollView(this);
    scroll->setResizePolicy(QScrollView::AutoOneFit);
    scroll->setHScrollBarMode(QScrollView::AlwaysOff);   // 无左右滚动
    QWidget* body = new QWidget();
    scroll->addChild(body);
#else
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // 无左右滚动
    QWidget* body = new QWidget();
    scroll->setWidget(body);
#endif
    QBoxLayout* bl = qNewBoxLayout(body, QBoxLayout::TopToBottom, 0, 0);
    bl->setSpacing(6);
    qSetMargins(bl, 8, 8, 8, 8);
    root->addWidget(scroll, 1);

    // Row0 语言
    {
        QWidget* r = mkRow(body, bl, QString::fromUtf8("语言"));
        m_langCombo = new QComboBox(r);
        for (int i = 0; i < 3; i++) { comboAdd(m_langCombo, QString::fromUtf8(kLangLabels[i])); }
        rl(r)->addWidget(m_langCombo, 1);
    }
    newSep(body, bl);

    // Row1 页面切换动画（占位：可点无响应）
    {
        QWidget* r = mkRow(body, bl, QString::fromUtf8("页面切换动画"));
        m_transitionCombo = new QComboBox(r);
        comboAdd(m_transitionCombo, QString::fromUtf8("滑动"));
        comboAdd(m_transitionCombo, QString::fromUtf8("2D"));
        comboAdd(m_transitionCombo, QString::fromUtf8("3D"));
        comboAdd(m_transitionCombo, QString::fromUtf8("透视"));
        comboSet(m_transitionCombo, 3);
        rl(r)->addWidget(m_transitionCombo, 1);
    }
    newSep(body, bl);

    // Row2 主题（真）
    {
        QWidget* r = mkRow(body, bl, QString::fromUtf8("主题"));
        m_styleCombo = new QComboBox(r);
        const std::vector<StyleParams::Definition>& s = StyleParams::registeredStyles();
        for (int i = 0; i < (int)s.size(); i++) {
            comboAdd(m_styleCombo, _(qFromUtf8(s[i].displayKey)));
        }
        rl(r)->addWidget(m_styleCombo, 1);
    }
    newSep(body, bl);

    // Row3 深色模式（真）
    {
        QWidget* r = mkRow(body, bl, QString::fromUtf8("深色模式"));
        m_darkCheck = new QCheckBox(r);
        rl(r)->addWidget(m_darkCheck, 1);
    }
    newSep(body, bl);

    // Row4 字号（占位：可点无响应）
    {
        QWidget* r = mkRow(body, bl, QString::fromUtf8("字号"));
        m_fontCombo = new QComboBox(r);
        comboAdd(m_fontCombo, QString::fromUtf8("小"));
        comboAdd(m_fontCombo, QString::fromUtf8("中"));
        comboAdd(m_fontCombo, QString::fromUtf8("大"));
        comboAdd(m_fontCombo, QString::fromUtf8("超大"));
        comboSet(m_fontCombo, 1);
        rl(r)->addWidget(m_fontCombo, 1);
    }
    newSep(body, bl);

    // Row5 调试底色（占位：可点无响应）
    {
        QWidget* r = mkRow(body, bl, QString::fromUtf8("调试底色"));
        m_debugCheck = new QCheckBox(r);
        rl(r)->addWidget(m_debugCheck, 1);
    }
    newSep(body, bl);

    // Row6 来电应答（占位：可点无响应）
    {
        QWidget* r = mkRow(body, bl, QString::fromUtf8("来电应答"));
        m_phoneCombo = new QComboBox(r);
        comboAdd(m_phoneCombo, QString::fromUtf8("关闭"));
        comboAdd(m_phoneCombo, QString::fromUtf8("手动"));
        comboAdd(m_phoneCombo, QString::fromUtf8("自动"));
        comboSet(m_phoneCombo, 0);
        rl(r)->addWidget(m_phoneCombo, 1);
    }
    newSep(body, bl);

    // Row7 推送通知（占位：可点无响应）
    {
        QWidget* r = mkRow(body, bl, QString::fromUtf8("推送通知"));
        m_pushCheck = new QCheckBox(r);
        rl(r)->addWidget(m_pushCheck, 1);
    }
    newSep(body, bl);

    // Row8 推送后端（占位：可点无响应）
    {
        QWidget* r = mkRow(body, bl, QString::fromUtf8("推送后端"));
        m_backendCombo = new QComboBox(r);
        comboAdd(m_backendCombo, QString::fromUtf8("自动（系统默认）"));
        comboAdd(m_backendCombo, QString::fromUtf8("Gotify 直连（即将支持）"));
        comboSet(m_backendCombo, 0);
        rl(r)->addWidget(m_backendCombo, 1);
    }
    newSep(body, bl);

    // Row9 Gotify URL（占位：可点无响应）
    {
        QWidget* r = mkRow(body, bl, QString::fromUtf8("Gotify URL"));
        m_gotifyUrlEdit = new QLineEdit(r);
        rl(r)->addWidget(m_gotifyUrlEdit, 1);
    }
    newSep(body, bl);

    // Row10 Gotify Token（占位：可点无响应）
    {
        QWidget* r = mkRow(body, bl, QString::fromUtf8("Gotify Token"));
        m_gotifyTokenEdit = new QLineEdit(r);
        rl(r)->addWidget(m_gotifyTokenEdit, 1);
    }
    newSep(body, bl);

    // Row11 当前存储（只读展示）
    {
        QWidget* r = mkRow(body, bl, QString::fromUtf8("当前存储"));
        m_currentRootValue = makeValueLabel(r);
        rl(r)->addWidget(m_currentRootValue, 1);
    }
    newSep(body, bl);

    // Row12 迁移到相册（展示；按钮可点无响应）
    {
        QWidget* r = mkRow(body, bl, QString::fromUtf8("迁移到相册"));
        m_picsRootValue = makeValueLabel(r);
        rl(r)->addWidget(m_picsRootValue, 1);
        m_migratePicsBtn = new QPushButton(QString::fromUtf8("迁移"), r);
        rl(r)->addWidget(m_migratePicsBtn, 0);
    }
    newSep(body, bl);

    // Row13 迁移回私用（展示；按钮可点无响应）
    {
        QWidget* r = mkRow(body, bl, QString::fromUtf8("迁移回私用"));
        m_privateRootValue = makeValueLabel(r);
        rl(r)->addWidget(m_privateRootValue, 1);
        m_migratePrivateBtn = new QPushButton(QString::fromUtf8("迁移"), r);
        rl(r)->addWidget(m_migratePrivateBtn, 0);
    }
    newSep(body, bl);

    // Row14 DAV URL（真）
    {
        QWidget* r = mkRow(body, bl, QString::fromUtf8("DAV URL"));
        m_davUrlEdit = new QLineEdit(r);
        rl(r)->addWidget(m_davUrlEdit, 1);
    }
    newSep(body, bl);

    // Row15 DAV 用户（真）
    {
        QWidget* r = mkRow(body, bl, QString::fromUtf8("DAV 用户"));
        m_davUserEdit = new QLineEdit(r);
        rl(r)->addWidget(m_davUserEdit, 1);
    }
    newSep(body, bl);

    // Row16 DAV 密码（真）
    {
        QWidget* r = mkRow(body, bl, QString::fromUtf8("DAV 密码"));
        m_davPassEdit = new QLineEdit(r);
        m_davPassEdit->setEchoMode(QLineEdit::Password);
        rl(r)->addWidget(m_davPassEdit, 1);
    }
}

void SettingsPage::onCreate(const QVariantMap& launchArgs,
                            const QVariantMap& savedState)
{
    Q_UNUSED(launchArgs);
    Q_UNUSED(savedState);

    // 先恢复取值（挂信号之前，避免程序化 set 触发写回）
    comboSet(m_langCombo, langIndexOf(Config::uiLang()));
    comboSet(m_styleCombo, styleIndexOf(qFromUtf8(ThemeManager::styleId())));
    m_darkCheck->setChecked(ThemeManager::isDarkMode());

    m_davUrlEdit->setText(Config::value(QString::fromUtf8("davUrl")));
    m_davUserEdit->setText(Config::value(QString::fromUtf8("davUser")));
    m_davPassEdit->setText(Config::value(QString::fromUtf8("davPass")));

    // 存储行：只读展示（迁移不实现）
    const QString cur  = currentStorageRoot();
    const QString pics = picturesStorageRoot();
    const QString priv = privateStorageRoot();
    m_currentRootValue->setText(cur);
    m_picsRootValue->setText(pics);
    m_privateRootValue->setText(priv);
    qSetToolTip(m_currentRootValue, cur);      // 完整路径（Qt3 列内裁尾时可查）
    qSetToolTip(m_picsRootValue, pics);
    qSetToolTip(m_privateRootValue, priv);

    // 挂信号（Qt3 组合框是 activated(int)，Qt4+ 才是 currentIndexChanged(int)）
#ifdef QT3_BUILD
    connect(m_langCombo,  SIGNAL(activated(int)), this, SLOT(onLangChanged(int)));
    connect(m_styleCombo, SIGNAL(activated(int)), this, SLOT(onStyleChanged(int)));
#else
    connect(m_langCombo,  SIGNAL(currentIndexChanged(int)), this, SLOT(onLangChanged(int)));
    connect(m_styleCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(onStyleChanged(int)));
#endif
    connect(m_darkCheck,  SIGNAL(toggled(bool)), this, SLOT(onDarkToggled(bool)));
    connect(m_davUrlEdit, SIGNAL(textChanged(const QString&)), this, SLOT(onDavUrlChanged(const QString&)));
    connect(m_davUserEdit, SIGNAL(textChanged(const QString&)), this, SLOT(onDavUserChanged(const QString&)));
    connect(m_davPassEdit, SIGNAL(textChanged(const QString&)), this, SLOT(onDavPassChanged(const QString&)));
}

void SettingsPage::onBackClicked()
{
    finish();
}

void SettingsPage::onLangChanged(int index)
{
    if (index < 0 || index > 2) { return; }
    const QString code = qFromUtf8(kLangCodes[index]);
    Config::setUiLang(code);
    Translator::instance().loadLanguage(code);
    QtappSetup::installQtTranslations(code);
}

void SettingsPage::onStyleChanged(int index)
{
    const std::vector<StyleParams::Definition>& s = StyleParams::registeredStyles();
    if (index < 0 || index >= (int)s.size()) { return; }
    ThemeManager::setStyle(s[index].id, ThemeManager::isDarkMode());
    Config::setValue(QString::fromUtf8("style"), qFromUtf8(s[index].id));
}

void SettingsPage::onDarkToggled(bool on)
{
    if (on == ThemeManager::isDarkMode()) { return; }
    ThemeManager::applyTheme(on);
    Config::setDarkMode(on);
}

void SettingsPage::onDavUrlChanged(const QString& text)  { Config::setValue(QString::fromUtf8("davUrl"),  text); }
void SettingsPage::onDavUserChanged(const QString& text) { Config::setValue(QString::fromUtf8("davUser"), text); }
void SettingsPage::onDavPassChanged(const QString& text) { Config::setValue(QString::fromUtf8("davPass"), text); }
