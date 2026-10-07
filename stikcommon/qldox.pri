# stikcommon/qldox.pri —— qldox（qltox 非 UI 家族）供应文件（qmake 侧）
# qldox → ../qldox（软链到 ../doxhttpd/qltox，**只读不改**，见 移植计划 §13）
#
# 批次 0a 只取 qlstik 直接需要的两件：
#   cJSON        —— JSON 统一底座（QJson 垫片将来建在它之上，不引 Qt5 的 QJson）
#   eventpoller  —— curl_multi 异步请求引擎（QNAM 垫片的唯一后端，见 移植计划 §5）
#
# 批次 5（stickerstore）追加 **db 层 7 件**。此前注释写「qltox 其余 storage /
# message_db / channel_db / cache_* / sticker_db 属 tox 自身的聊天存储，qlstik
# 用共享的 StickerStore，不需要」——该判断在批次 3c-2 成立（当时只抽了
# davlocalsource 窄切面绕开），批次 5 要挂 StickerStore 本体后不成立：
#   * stickerstore.cpp:82  Storage::instance().init(...)
#   * stickerstore.cpp:99/290/410/2457/2478  Storage::instance().msgDb()
#     （:91 注释「聊天功能已移除」，此处只借 msgDb 一次性清聊天域空表）
#   * stickerstore.cpp:483  Storage::instance().stickerDb()
# 而 Storage::init() 在 storage.cpp:349-356 依次调 init_channel_db /
# init_message_db / init_pending_db / init_sticker_db / init_cache_db，
# storage.cpp:259 还要 cache_fs 的 initCacheFsDirs —— 故 7 件缺一不可，
# 不能只挑 sticker_db。
#
# 零改动可编的依据（qldox 自身已在两端构建过，本 pri 只挂不改）：
#   * Qt3：qldox/build-qt3/ 下 storage.o / channel_db.o / message_db.o /
#     pending_db.o / sticker_db.o / cache_db.o / cache_fs.o 均已产出
#     （qldox/buildqt3.sh 用 /opt/qt338sh）
#   * Qt6：qldox/build-qt6/ 下同样 7 个 .o 已产出
#     （qldox/buildqt6.sh 用 /opt/qt/6.7.3/gcc_64）
#   * storage.cpp 的唯一 Qt 依赖 mediaCacheKey 自带 #ifdef QT3_BUILD 双分支
#     （storage.cpp:11-20，Qt3 用 findRev/utf8、Qt4+ 用 lastIndexOf/toUtf8）
#   * sticker_db.cpp（485 行）Qt 符号 0 个，完全 Qt-free
#   * SQLite 已在 qlstik.pro:153-163 配好（HAVE_SQLITE + -lsqlite3）
#
# 输出：QLDOX_SOURCES / QLDOX_HEADERS / QLDOX_INCLUDES

QLDOX_DIR = $$PWD/../qldox

QLDOX_SOURCES = \
    $$PWD/../qlcomp/cJSON.c \
    $$QLDOX_DIR/eventpoller.cpp \
    $$QLDOX_DIR/storage.cpp \
    $$QLDOX_DIR/channel_db.cpp \
    $$QLDOX_DIR/message_db.cpp \
    $$QLDOX_DIR/pending_db.cpp \
    $$QLDOX_DIR/sticker_db.cpp \
    $$QLDOX_DIR/cache_db.cpp \
    $$QLDOX_DIR/cache_fs.cpp

# eventpoller.h 无 Q_OBJECT（纯 QThread 子类，无自定义信号槽），
# 列进 HEADERS 与 qltox.pro 保持一致，moc 不会产出内容。
# db 层 7 件的头一并列出：*.h 均无 Q_OBJECT，moc 不产出内容，但 moc 靠 HEADERS
# 里的 .h 找 include 依赖，漏列会让 moc 看不到跨头依赖。
QLDOX_HEADERS = \
    $$PWD/../qlcomp/cJSON.h \
    $$QLDOX_DIR/eventpoller.h \
    $$QLDOX_DIR/storage.h \
    $$QLDOX_DIR/channel_db.h \
    $$QLDOX_DIR/message_db.h \
    $$QLDOX_DIR/pending_db.h \
    $$QLDOX_DIR/sticker_db.h \
    $$QLDOX_DIR/cache_db.h \
    $$QLDOX_DIR/cache_fs.h

QLDOX_INCLUDES = $$QLDOX_DIR
