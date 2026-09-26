# qldox（qlapp 应用库）供应子文件：供任意工程 include 复用
set(QLAPP_DIR ${CMAKE_CURRENT_LIST_DIR}/../qldox)

set(QLAPP_SOURCES
    ${QLAPP_DIR}/cJSON.c
    ${QLAPP_DIR}/message_db.cpp
    ${QLAPP_DIR}/storage.cpp
    ${QLAPP_DIR}/channel_db.cpp
    ${QLAPP_DIR}/pending_db.cpp
    ${QLAPP_DIR}/sticker_db.cpp
    ${QLAPP_DIR}/cache_db.cpp
    ${QLAPP_DIR}/cache_fs.cpp
    ${QLAPP_DIR}/eventpoller.cpp
    ${QLAPP_DIR}/unknownparser.cpp
)

set(QLAPP_INCLUDE_DIRS ${QLAPP_DIR})