# qlappcom.pri — 应用公共模块（qltox 等共享）
# Usage: include(../qlappcom/qlappcom.pri)
# 注意：test_*.cpp / doctest.h / webcreds_cli.cpp 走独立 g++ 脚本，不在此列。

QLAPPCOM_CPP = $$PWD/aigptbot.cpp $$PWD/aigpt_sessions.cpp $$PWD/avatar_manager.cpp \
    $$PWD/cache_db.cpp $$PWD/cache_fs.cpp $$PWD/channel_db.cpp $$PWD/edgedrm.cpp $$PWD/eventpoller.cpp \
    $$PWD/fanyibot.cpp $$PWD/imageinputconfirm.cpp $$PWD/loadingbar.cpp $$PWD/linkpreview.cpp \
    $$PWD/media_shmem_cache.cpp $$PWD/message_db.cpp $$PWD/netproxy.cpp \
    $$PWD/pasteuploader.cpp $$PWD/pending_db.cpp $$PWD/photoviewer.cpp \
    $$PWD/recorder.cpp $$PWD/seen_unknown.cpp $$PWD/sound.c \
    $$PWD/sticker_db.cpp $$PWD/stickermanager.cpp $$PWD/stickerpicker.cpp \
    $$PWD/storage.cpp $$PWD/translate_util.cpp $$PWD/ttsbot.cpp $$PWD/unknownparser.cpp \
    $$PWD/webcreds.cpp $$PWD/webcredsdialog.cpp $$PWD/websearchbot.cpp

QLAPPCOM_HDR = $$PWD/aigptbot.h $$PWD/aigpt_sessions.h $$PWD/avatar_manager.h \
    $$PWD/cache_db.h $$PWD/cache_fs.h $$PWD/channel_db.h $$PWD/edgedrm.h $$PWD/eventpoller.h \
    $$PWD/fanyibot.h $$PWD/imageinputconfirm.h $$PWD/loadingbar.h $$PWD/linkpreview.h \
    $$PWD/media_shmem_cache.h $$PWD/message_db.h $$PWD/netproxy.h \
    $$PWD/pasteuploader.h $$PWD/pending_db.h $$PWD/photoviewer.h \
    $$PWD/recorder.h $$PWD/seen_unknown.h $$PWD/sound.h \
    $$PWD/sticker_db.h $$PWD/stickermanager.h $$PWD/stickerpicker.h \
    $$PWD/storage.h $$PWD/translate_util.h $$PWD/ttsbot.h $$PWD/unknownparser.h \
    $$PWD/webcreds.h $$PWD/webcredsdialog.h $$PWD/websearchbot.h \
    $$PWD/pureconsts.hpp

HEADERS += $$QLAPPCOM_HDR
SOURCES += $$QLAPPCOM_CPP
INCLUDEPATH += $$PWD
