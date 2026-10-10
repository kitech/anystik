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
#include "qwebdavtransport.h"

#ifdef QT3_BUILD
#include "qcoreapplication_shim.h"   // Qt3 下补 QCoreApplication::setOrganizationName
#else
#include <QCoreApplication>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>          // setenv

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

    // 中文输入：X11 下所有 Qt 版本统一走 XIM。桌面默认 QT_IM_MODULE=fcitx 是
    // Qt4/5/6 的输入法插件名，Qt3 无该输入上下文插件 → 拿不到 IME；须在
    // QApplication 之前强制 xim（fcitx 进程提供 XIM，XMODIFIERS 指向它）。
    // 照抄 qltox/main.cpp:47-52。
#ifdef __linux__
    setenv("QT_IM_MODULE", "xim", true);
    setenv("XMODIFIERS", "@im=fcitx", true);
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
    // 组织名/应用名必须与 anystik 完全一致（anystik/src/main.cpp:163-164）：
    // 贴纸库是 StickerStore::ensureInit 按 QStandardPaths::AppLocalDataLocation
    // 建的，只有 org=fedlet app=anystik 才落到 ~/.local/share/fedlet/anystik，
    // 两个版本才能共享读写同一份数据（message.db / cache.db / packs / pastes）。
    // 应用名若留空还会回退到可执行名（qlstik / q3stik / q4stik / q6stik），
    // 使各 Qt 版本分叉，故必须显式固定。
    // Qt3 无 QCoreApplication，用 stikcommon 垫片补齐静态 org/app API；
    // Qt4+ 用原生静态 API。两侧调用形式一致。
    QCoreApplication::setOrganizationName(QString::fromUtf8("fedlet"));
    QCoreApplication::setApplicationName(QString::fromUtf8("anystik"));

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

    // 语言：先注册 lang/ 搜索路径再加载。Translator 构造时已按
    // <exe目录>/lang → ./lang → <exe上级>/qltox/lang → <项目根>/lang 找；
    // 但本程序 exe 在 build-qt3/，语言文件在 <exe上级>/lang（即 qlstik/lang），
    // 与 qltox 的 <项目根>/lang 组织一致，故补注册这一条，运行目录不再影响加载。
    Translator::instance().addTranslationPath(app.applicationDirPath() + "/../lang");
    Translator::instance().addTranslationPath(app.applicationDirPath() + "/lang");
    QString savedLang = Config::uiLang();
    Translator::instance().loadLanguage(savedLang);
    QtappSetup::installQtTranslations(savedLang);

    // curl 异步引擎（qldox/eventpoller，curl_multi + 独立泵线程）：
    // 共享模块的 QNAM 网络调用在 Qt3 下由 compat/qt3/qnam_shim 转接到此引擎。
    EventPoller::start();

    // WebDAV 动词传输泵（批次 3c-1）。为什么必须独立于 EventPoller：
    // qldox/eventpoller.cpp 是只读依赖，全文没有 CURLOPT_CUSTOMREQUEST，
    // 只在 `if (req.method == "POST")` 分支设 POSTFIELDS，故非 GET/POST 动词
    // （MKCOL/MOVE/DELETE/OPTIONS/HEAD/PROPFIND）经它发出会一律退化成 GET
    // 且请求体被丢弃。qldox 不可改，故自建一个真正认动词的 curl_multi 泵，
    // 由 QNetworkAccessManager::issue() 分流：GET/POST 仍走 EventPoller，
    // 其余走本泵。两个泵互不干扰，start/stop 也各自独立。
    QWebdavTransport::start();
    if (!QWebdavTransport::isReady()) {
        // 不致命：只影响 WebDAV 侧功能，普通 HTTP 仍走 EventPoller。
        fprintf(stderr, "[warn] QWebdavTransport 未就绪，WebDAV 动词请求将失败\n");
    }

    MainWindow window;
    qSetAppIcon(app_icon);
    window.show();
    qSetAppIcon(app_icon);   // show 后再设一次，触发 WM 重读 _NET_WM_ICON

    int rc = app.exec();
    // stop 顺序与 start 相反：先停自建泵（等在途 WebDAV 请求收尾），
    // 再停 EventPoller。
    QWebdavTransport::stop();
    EventPoller::stop();
    return rc;
}
