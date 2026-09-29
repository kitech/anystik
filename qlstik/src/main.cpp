#include "compat34.h"
#include "buildinfo.h"
#include "mainwindow.h"
#include "config.h"

#include "appsetup.h"
#include "translator.h"
#include "LimeStyle.h"
#include "ThemeManager.h"
#include "systemtrayicon.h"
#include "eventpoller.h"

#include <stdio.h>
#include <string.h>

#ifdef __linux__
#include <malloc.h>
#endif

#include "app_icon.xpm"

int main(int argc, char* argv[])
{
#ifdef __linux__
    mallopt(M_MMAP_THRESHOLD, 32768);
    mallopt(M_ARENA_MAX, 2);
#endif

    // 简单命令行解析（Qt3 友好的手写方式，参考 qltox/main.cpp）
    bool showVersion = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            showVersion = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: qlstik [options]\n"
                   "Options:\n"
                   "  -v, --version    Show version and exit\n"
                   "  -h, --help       Show this help\n");
            return 0;
        }
    }

    QApplication app(argc, argv);
#ifndef QT3_BUILD
    app.setApplicationVersion("0.6.0");
#endif

    if (showVersion) {
        printf("qlstik 0.6.0 (GIT_COMMIT=%s)\n", QLSTIK_GIT_COMMIT_STR);
        return 0;
    }

    QtappSetup::setup(app);

    // 主题：先装 LimeStyle（其构造函数注册 5 套皮肤到 StyleParams），
    // 再按 config 的 style/dark 应用；缺省跟 qltox 一致：qtFusion + 深色
    app.setStyle(new LimeStyle);
    // Qt3 的 QByteArray(=QMemArray<char>) 没有 constData()，只有 data()
    QByteArray styleId = qToUtf8(Config::styleId());
    ThemeManager::setStyle(styleId.data(), Config::darkMode());

    // 语言：先注册 lang/ 搜索路径再加载；Translator 构造时已按
    // <exe目录>/lang → ./lang → <exe上级>/qltox/lang → <项目根>/lang 找
    Translator::instance().addTranslationPath(app.applicationDirPath() + "/lang");
    QString savedLang = Config::uiLang();
    Translator::instance().loadLanguage(savedLang);
    QtappSetup::installQtTranslations(savedLang);

    // curl 异步引擎（qldox/eventpoller，curl_multi + 独立泵线程）：
    // 共享模块的 QNAM 网络调用在 Qt3 下由 compat/qt3/qnam_shim 转接到此引擎。
    EventPoller::start();

    MainWindow window;
    qSetAppIcon(app_icon);
    window.show();
    qSetAppIcon(app_icon);   // show 后再设一次，触发 WM 重读 _NET_WM_ICON

    int rc = app.exec();
    EventPoller::stop();
    return rc;
}
