#ifndef MAINWINDOW_H
#define MAINWINDOW_H

// qlstik 桌面壳 —— 主窗口。
// 头文件规约（移植计划 §10 UI 铁律）：
//   * 只写 Qt3 原生小写头 + qlcomp compat34.h 白名单
//   * 不写 CamelCase Qt 头（Qt3 无此形态）
//   * 不写 Qt3 不存在的类（QStackedWidget→StackedWidget、QScrollArea→ScrollArea …）
//
// 本文件承载「壳移植」第一批：演示菜单 / 翻译 / 皮肤 / dark /
// SharedStatusBar / SystemTrayIcon，实现移植自 qltox/mainwindow.cpp 的对应区段
// （qltox 只读参考，不改）。
//
// 菜单文案不重建、只就地改写，原因（Qt3 实测约束）：
//   * QMenuData::clear()（Qt3）会保留旧分隔线 → 反复重建会堆积分隔线；
//   * QPopupMenu::insertItem()（Qt3）返回的是**分配 id**（非位置），
//     而 Qt4+ 的 QAction* 才是稳定句柄。
//   故构建时把每项的 id（Qt3）或 QAction*（Qt4+）记进 menuItemRefs，
//   retranslateUi() 据此 changeItem / setText。

#include "compat34.h"

#include <qmainwindow.h>
#include <qwidget.h>
#include <qlabel.h>
#include <qpushbutton.h>
#include <qlineedit.h>

#include <vector>

class FramelessHelper;
class CustomTitleBar;
class SystemTrayIcon;
class PageManager;
// ⚠ StackedWidget 是 compat34.h 里的 typedef（Qt3 = QWidgetStack），**不能**前向声明

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget* parent = 0);
    virtual ~MainWindow();

protected:
    // 托盘可用时关闭改为隐藏到托盘；真正退出走 quitApp()（forceQuit 绕过本拦截）
    virtual void closeEvent(QCloseEvent* event);
    // 首帧门：观测窗口内首个 Paint。⚠ 顶层 QMainWindow 被中央控件完全覆盖，
    // 自身收不到 Paint（实测），故用 app 事件过滤器捕获「本窗口后代」的 Paint。
    virtual bool eventFilter(QObject* watched, QEvent* event);

private slots:
    // 外观三件套（titlebar）：语言 / 皮肤 / 深色
    void onTitleUilang(int index);
    void onTitleStyle(int index);
    void onTitleDark(bool on);
    void onDarkMenuItem();
    // 翻译
    void retranslateUi();
    // 托盘
    void trayActivated(int reason);
    void trayShowMainWindow();
    void quitApp();
    void onStickerCountsChanged(const QString& labelText, int stickers);
    // 演示菜单
    void onMenu1Stub();
    void onDemoStatusInfo();
    void onDemoStatusWarning();
    void onDemoStatusError();
    void onDemoStatusClear();
    void onDemoTrayBubble();
    void onDemoToggleStatusWidgets();
    void onAboutApp();
    // 首帧门命中 → processEvents → 延迟 150ms 加载
    void onFirstPaintComplete();
    void onDeferredLoad();

private:
    // 菜单项句柄：Qt3 记 id（changeItem 只认 id，且需要所属菜单），Qt4+ 记 QAction*
    // （见文件头注释）
    struct MenuItemRef {
        const char* key;        // 翻译键
        bool darkToggle;        // 深色开关项：文案随状态在 on/off 间切换
        void* owner;            // 所属菜单（Qt3 changeItem 用；Qt4+ 仅留档）
#ifdef QT3_BUILD
        int id;
#else
        QAction* action;
#endif
        MenuItemRef() : key(0), darkToggle(false), owner(0)
#ifdef QT3_BUILD
            , id(-1)
#else
            , action(0)
#endif
        {}
    };

    void buildStatusBar();
    void buildCentralWidget();
    void buildAppearancePanel();
    void buildMenus();
    void buildAppearanceMenus();   // ⚠ 必须在 buildMenus() 之后调用
    void buildTray();

    // 菜单构建辅助（menu 用 void* 以避免头文件依赖 Qt3/Qt4 菜单 typedef）
    void addMenuItem(void* menu, const QObject* receiver, const char* slot,
                     const char* key, bool darkToggle = false);
    void* addTopMenu(const char* key);
    void* addSubMenu(void* parentMenu, const QString& title, const char* key);
    void addMenuSeparator(void* menu);
    void refreshMenuTexts();
    void setTopMenuText(int index, const QString& text);
    QString darkToggleLabel() const;

    // 演示用状态栏控件（演示菜单可反复增删）
    void addDemoWidgets();
    void removeDemoWidgets();
    bool hasDemoWidgets() const;

    // 语言/皮肤当前值索引（tooltip 显示用）
    static int langIndexOf(const QString& langCode);
    static int styleIndexOf(const QString& styleId);
    // 三按钮 tooltip：功能 + 当前值（语言/外观/深色变化时刷新）
    void updateAppearanceTooltips();
    // 托盘右键菜单禁用表头：app 名 - 计数标签
    void updateTrayHeader();

    FramelessHelper* framelessHelper;
    CustomTitleBar* titleBar;
    SystemTrayIcon* tray;

    // 页面栈（贴纸家等业务页都挂这里，替掉原先的占位 statusLabel）
    StackedWidget* pageStack;
    PageManager* pageMgr;
    QLabel* statusLabel;
    // 三个下拉菜单按钮：QPushButton 双端都支持挂菜单（Qt3 setPopup / Qt4+ setMenu），
    // 且与 CustomTitleBar 右侧 ─ □ ✕ 同为 QPushButton，尺寸/外观天然一致。
    QPushButton* m_langBtn;
    QPushButton* m_styleBtn;
    QPushButton* m_darkBtn;
    void* m_langMenu;
    void* m_styleMenu;
    void* m_darkMenu;

    QLabel* demoLeftLabel;
    QPushButton* demoLeftBtn;
    QLabel* demoRightLabel;
    QPushButton* demoRightBtn;
    QLineEdit* demoEdit;

    std::vector<MenuItemRef> menuItemRefs;   // 叶子项 + 子菜单标题
    std::vector<MenuItemRef> topMenuRefs;    // 顶级菜单标题（顺序 = kTopMenuKeys）
    bool forceQuit;
    MenuItemRef trayHeaderRef;               // 托盘表头项（动态文案，不入 refreshMenuTexts）
    QString m_countText;                     // 最近一次计数标签文本

    // ── 首帧门状态（照搬 qltox mainwindow.h:145-146）──
    bool m_firstPaintLogged;   // 门是否已触发（一次性）
    int  m_paintCounter;       // 已观测 Paint 次数（>5 兜底防饥饿）
    bool m_initialLoadDone;    // 延迟加载是否已执行（一次性，含 2s 兜底）
};

#endif // MAINWINDOW_H
