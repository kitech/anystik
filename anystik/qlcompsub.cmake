# qlcomp 供应子文件：供任意工程 include 复用
set(QLCOMP_DIR ${CMAKE_CURRENT_LIST_DIR}/../qlcomp)

set(QLCOMP_SOURCES
    ${QLCOMP_DIR}/compatcore34.cpp
    ${QLCOMP_DIR}/limelog.cpp
    ${QLCOMP_DIR}/assertf.cpp
    ${QLCOMP_DIR}/generic_slot.cpp
    ${QLCOMP_DIR}/qthooks.cpp
    ${QLCOMP_DIR}/tagutil.cpp
    ${QLCOMP_DIR}/md5.c
    ${QLCOMP_DIR}/qcrc64.cpp
)

set(QLCOMP_INCLUDE_DIRS ${QLCOMP_DIR})