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
#include <qcheckbox.h>
#include <qcombobox.h>
#include <qpushbutton.h>
#include <qlineedit.h>

#include <vector>

class FramelessHelper;
class CustomTitleBar;
class SystemTrayIcon;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget* parent = 0);
    virtual ~MainWindow();

protected:
    // 托盘可用时关闭改为隐藏到托盘；真正退出走 quitApp()（forceQuit 绕过本拦截）
    virtual void closeEvent(QCloseEvent* event);

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
    // 演示菜单
    void onMenu1Stub();
    void onDemoStatusInfo();
    void onDemoStatusWarning();
    void onDemoStatusError();
    void onDemoStatusClear();
    void onDemoTrayBubble();
    void onDemoToggleStatusWidgets();
    void onAboutApp();

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

    // 语言/皮肤下拉内容（构造与 retranslateUi 共用）
    void fillLangCombo();
    void fillStyleCombo();
    static int langIndexOf(const QString& langCode);
    static int styleIndexOf(const QString& styleId);

    FramelessHelper* framelessHelper;
    CustomTitleBar* titleBar;
    SystemTrayIcon* tray;
    QLabel* statusLabel;
    QComboBox* langCombo;
    QComboBox* styleCombo;
    QCheckBox* darkCheck;

    QLabel* demoLeftLabel;
    QPushButton* demoLeftBtn;
    QLabel* demoRightLabel;
    QPushButton* demoRightBtn;
    QLineEdit* demoEdit;

    std::vector<MenuItemRef> menuItemRefs;   // 叶子项 + 子菜单标题
    std::vector<MenuItemRef> topMenuRefs;    // 顶级菜单标题（顺序 = kTopMenuKeys）
    bool forceQuit;
};

#endif // MAINWINDOW_H
