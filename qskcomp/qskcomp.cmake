# qskcomp：QSKinny 自绘组件库（源码与 include 目录声明，供任意工程复用）
set(QSKCOMP_DIR ${CMAKE_CURRENT_LIST_DIR})

set(QSKCOMP_SOURCES
    ${QSKCOMP_DIR}/mysearchline.h
    ${QSKCOMP_DIR}/mysearchline.cpp
    ${QSKCOMP_DIR}/tabfocus.h
    ${QSKCOMP_DIR}/tabfocus.cpp
    ${QSKCOMP_DIR}/scrollfader.h
    ${QSKCOMP_DIR}/scrollfader.cpp
    ${QSKCOMP_DIR}/myscrollarea.h
    ${QSKCOMP_DIR}/myscrollarea.cpp
    ${QSKCOMP_DIR}/mytaphandler.h
    ${QSKCOMP_DIR}/mytaphandler.cpp
    ${QSKCOMP_DIR}/dialogpopup.h
    ${QSKCOMP_DIR}/dialogpopup.cpp
    ${QSKCOMP_DIR}/menuoverlay.h
    ${QSKCOMP_DIR}/menuoverlay.cpp
    ${QSKCOMP_DIR}/loglistview.h
    ${QSKCOMP_DIR}/loglistview.cpp
    ${QSKCOMP_DIR}/logmodel.h
    ${QSKCOMP_DIR}/logmodel.cpp
    ${QSKCOMP_DIR}/multilinetextedit.h
    ${QSKCOMP_DIR}/multilinetextedit.cpp
)

set(QSKCOMP_INCLUDE_DIRS ${QSKCOMP_DIR})