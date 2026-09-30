# qlstik 桌面工程（Qt Widgets 移植版）—— 镜像 ../qldox/qltox.pro 的跨 Qt3/4/6 写法
# 编译优先级：Qt3 → Qt4 → Qt6（同一个 .pro，三套脚本）
#
# 依赖（include 由各 .pri 提供）：
#   ../stikcommon/stikcommon.pri  → 共享非 UI 层（按批次迁入）+ 折叠
#                                    ../myvendor/myvendor.pri 与 qldox.pri
#                                    （qldox.pri → ../qldox 即 ../doxhttpd/qltox，
#                                      只取 cJSON + eventpoller 的 curl 异步引擎）
#   ../qlcomp/qlite.pri           → 壳组件（LimeStyle / FramelessHelper / 托盘 / emoji /
#                                    ThemeManager / ConfigDialog / translator …）
#
# qlstik 自带的 Qt3 垫片层随批次按需引入（批次 2 迁入首个网络模块时才建），
# 不做全局 -include 预包含：qlcomp 自身已是 Qt3 干净的小写头（QT3_BUILD 分支），
# 批次 0a 无共享模块，暂不需要任何垫片。

TEMPLATE = app
TARGET = qlstik
QT = core gui widgets network
CONFIG += moc
CONFIG += sdk_no_version_check

VERSION = 0.6.0

QLSTIK_DIR = $$PWD

GIT_COMMIT = $$system(git -C $$PWD rev-parse --short=7 HEAD 2>/dev/null)
isEmpty(GIT_COMMIT): GIT_COMMIT = unknown
DEFINES += GIT_COMMIT=$$GIT_COMMIT

GIT_DIRTY = $$system(git -C $$PWD status --porcelain 2>/dev/null)
!isEmpty(GIT_DIRTY): DEFINES += GIT_DIRTY

QMAKE_MACOSX_DEPLOYMENT_TARGET = 11.7

# moc / obj 平铺在 build 目录（镜像 qltox.pro）
MOC_DIR = .
OBJECTS_DIR = .

# ══ 共享层（stikcommon 聚合 myvendor + qldox）══
include(../stikcommon/stikcommon.pri)

# ══ 壳组件 ══
INCLUDEPATH += ../qlcomp
include(../qlcomp/qlite.pri)

# 注意：必须用 += 而非 =。qlite.pri 在上面的 include 里已用 += 追加了壳组件的
# SOURCES/HEADERS，用 = 会把它清空。
SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/config.cpp \
    $$STIKCOMMON_SOURCES

HEADERS += \
    src/mainwindow.h \
    src/config.h \
    src/buildinfo.h \
    $$STIKCOMMON_HEADERS

# src/main.cpp 与 src/mainwindow.cpp 都 #include "app_icon.xpm"（内嵌 XPM，
# 托盘/任务栏图标不依赖 exe 旁的外部文件），故把工程根目录加进搜索路径
INCLUDEPATH += \
    src \
    $$PWD \
    $$STIKCOMMON_INCLUDES

# 应用图标（macOS 用 icns；Windows 用 .rc；Linux 走内嵌 xpm）
QMAKE_ICON = app_icon.icns
RC_FILE = app_icon.rc

QMAKE_CXXFLAGS += $$STIKCOMMON_CFLAGS
QMAKE_CFLAGS   += $$STIKCOMMON_CFLAGS

# ══ Qt 版本探测（镜像 qltox.pro）：Qt3 qmake(1.07a) 无 QT_VERSION 变量 ══
!isEmpty(QT_VERSION) {
    message("qlstik: Building for Qt4+ - QT3_BUILD not defined")
} else {
    message("qlstik: Building for Qt3 - adding QT3_BUILD")
    DEFINES += QT3_BUILD
    # qlcomp 的 compat34.h / compatcore34.h 全部靠 QT3_BUILD 走 Qt3 小写头分支，
    # 所以这个宏必须在 include(qlite.pri) 之前可见（qlite.pri 自身用
    # isEmpty(QT_VERSION) 自判，与本宏无关，故顺序不影响它的托盘分支选择）。
    # *nix/bsd/mac/wsl
    QMAKE_EXE = $$system(ps -p $PPID -o args= | head -1 | awk '{print $1}')
    QTDIR_AUTO = $$system(dirname $(dirname $$QMAKE_EXE))
    isEmpty(QTDIR) {
        message("Auto QTDIR ... $$QTDIR_AUTO")
        QTDIR = $$QTDIR_AUTO
        INCLUDEPATH += $$QTDIR/include
        QMAKE_INCDIR_QT    = $$QTDIR/include
        QMAKE_LIBDIR_QT    = $$QTDIR/lib
        QMAKE_MOC          = $$QTDIR/bin/moc
        QMAKE_UIC          = $$QTDIR/bin/uic
        QMAKE_QMAKE        = $$QMAKE_EXE
        QMAKE              = $$QMAKE_EXE
        QMAKE_LRELEASE     = $$QTDIR/bin/lrelease
    }
}

# ══ C++ 底线：Qt3 = c++14；Qt4/5 = c++14；Qt6 = c++17 ══
# Qt4 从 c++11 提到 c++14：批次 1 的 libobfuscate（davobfus）用 relaxed constexpr，
# c++11 下编不过。qmake 3.x 不支持按文件 CXXFLAGS，只能全局提；qlcomp 在 c++14
# 下实测无回归（build-qt4 全绿）。
# C 源一律 gnu 模式（不能用 -std=c11/c17）：myvendor/netut/ipaddr_list.c 依赖
# getnameinfo / NI_MAXHOST / IFF_UP，这些在 __STRICT_ANSI__ 下不声明；
# anystik 侧 CMake 也是 gcc 默认 gnu 模式编它，保持行为一致。
QMAKE_CXXFLAGS += -O0
QMAKE_CFLAGS   += -O0
!isEmpty(QT_VERSION) {
    greaterThan(QT_VERSION, 5.0.0) {
        QMAKE_CXXFLAGS += -std=c++17
        QMAKE_CFLAGS   += -std=gnu17
    } else {
        QMAKE_CXXFLAGS += -std=c++14
        QMAKE_CFLAGS   += -std=gnu11
    }
} else {
    QMAKE_CXXFLAGS += -std=c++14
    QMAKE_CFLAGS   += -std=gnu11
}
QMAKE_CXXFLAGS += -fstack-protector-strong
QMAKE_CFLAGS   += -fstack-protector-strong

# asan double memory usage!!!
contains(CONFIG, asan) {
    QMAKE_CXXFLAGS += -fsanitize=address
    QMAKE_CFLAGS   += -fsanitize=address
    QMAKE_LFLAGS   += -fsanitize=address
}

!win32: QMAKE_LFLAGS += -rdynamic

# ══ 依赖检测（镜像 qltox.pro）══
# FreeType2（qlcomp 的 emoji 渲染）
FREETYPE_LIBS = $$system(pkg-config --libs freetype2 2>/dev/null)
!isEmpty(FREETYPE_LIBS) {
    QMAKE_CXXFLAGS += $$system(pkg-config --cflags freetype2 2>/dev/null)
    LIBS += -lcurl -ldl $$FREETYPE_LIBS
    message("FreeType2: detected via pkg-config")
} else {
    INCLUDEPATH += /usr/include/freetype2
    LIBS += -lcurl -lfreetype -ldl
    message("FreeType2: pkg-config not found, using fallback paths")
}

# libcurl：EventPoller 的 curl_multi 后端（需 >= 7.66 才有 curl_multi_poll）
LIBS += -lcurl

# SQLite（StickerStore / qltox 系 *_db；批次 5 起为必需，批次 0a 先按可选探测）
SQLITE_CFLAGS = $$system(pkg-config --cflags sqlite3 2>/dev/null)
SQLITE_LIBS   = $$system(pkg-config --libs sqlite3 2>/dev/null)
!isEmpty(SQLITE_LIBS) {
    INCLUDEPATH += $$SQLITE_CFLAGS
    LIBS += $$SQLITE_LIBS
    DEFINES += HAVE_SQLITE
    message("SQLite: $$SQLITE_LIBS")
} else {
    message("SQLite: not found via pkg-config (批次 5 StickerStore 之前不影响构建)")
}

# zlib（批次 3 起 zipu 解压用；先挂上，后续批次不必再动 .pro）
LIBS += -lz

# OpenSSL：批次 3c-1 的 qsslprobe 走裸 socket + SSL_connect 取真实证书（Qt3 的
# QSslSocket 缺 QSslConfiguration/自定义 CA 装载能力），qcabundle 也要用
# X509 解析，故需显式链 libssl/libcrypto。--as-needed 下传递依赖不算数，必须显式。
LIBS += -lssl -lcrypto

# hjson（qlcomp 的 hjson_wrap.cpp / jsonview 需要；与 qltox 同一份 vcpkg 产物）
macx {
    INCLUDEPATH += /opt/vcpkg/installed/x64-osx-dynamic/include
    LIBS += -L/opt/vcpkg/installed/x64-osx-dynamic/lib -Wl,-rpath,/opt/vcpkg/installed/x64-osx-dynamic/lib -lhjson
} else {
    INCLUDEPATH += /opt/vcpkg/installed/x64-linux-dynamic/include
    LIBS += -L/opt/vcpkg/installed/x64-linux-dynamic/lib -Wl,-rpath,/opt/vcpkg/installed/x64-linux-dynamic/lib -lhjson
}

# X11：qlite.pri 已带 -lXss -lX11；这里显式补 -lX11（--as-needed 下传递依赖不算）
unix:!macx: LIBS += -lX11

# dlopen/dlsym/dlclose（qlcomp 插件与 emoji 字体加载）
LIBS += -ldl

# Emoji rendering with FreeType（color emoji fonts）
DEFINES += EMOJI_RENDER_QT34

# 安装
target.path = /usr/local/bin
INSTALLS += target
