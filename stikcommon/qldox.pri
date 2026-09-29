# stikcommon/qldox.pri —— qldox（qltox 非 UI 家族）供应文件（qmake 侧）
# qldox → ../qldox（软链到 ../doxhttpd/qltox，**只读不改**，见 移植计划 §13）
#
# 批次 0a 只取 qlstik 直接需要的两件：
#   cJSON        —— JSON 统一底座（QJson 垫片将来建在它之上，不引 Qt5 的 QJson）
#   eventpoller  —— curl_multi 异步请求引擎（QNAM 垫片的唯一后端，见 移植计划 §5）
# qltox 其余 storage / message_db / channel_db / cache_* / sticker_db / unknownparser
# 属 tox 自身的聊天存储，qlstik 用共享的 StickerStore，不需要；确有需要时在此按需追加。
#
# 输出：QLDOX_SOURCES / QLDOX_HEADERS / QLDOX_INCLUDES

QLDOX_DIR = $$PWD/../qldox

QLDOX_SOURCES = \
    $$QLDOX_DIR/cJSON.c \
    $$QLDOX_DIR/eventpoller.cpp

# eventpoller.h 无 Q_OBJECT（纯 QThread 子类，无自定义信号槽），
# 列进 HEADERS 与 qltox.pro 保持一致，moc 不会产出内容。
QLDOX_HEADERS = \
    $$QLDOX_DIR/cJSON.h \
    $$QLDOX_DIR/eventpoller.h

QLDOX_INCLUDES = $$QLDOX_DIR
