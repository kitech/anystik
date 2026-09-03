cmake_minimum_required(VERSION 3.16)
project(anystik LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Quick Network Concurrent)
find_package(QSkinny REQUIRED)

if(ANDROID)
    macro(_fix_qskinny_abi_target old_target new_target)
        if(NOT TARGET "${old_target}" AND TARGET "${new_target}")
            add_library("${old_target}" INTERFACE IMPORTED)
            target_link_libraries("${old_target}" INTERFACE "${new_target}")
        endif()
    endmacro()
    _fix_qskinny_abi_target(Qt6Gui_arm64-v8a   Qt6::Gui)
    _fix_qskinny_abi_target(Qt6Qml_arm64-v8a   Qt6::Qml)
    _fix_qskinny_abi_target(Qt6Quick_arm64-v8a Qt6::Quick)
    _fix_qskinny_abi_target(Qt6OpenGL_arm64-v8a Qt6::OpenGL)
endif()

set(qlapp_dir ${CMAKE_CURRENT_SOURCE_DIR}/../qldox)
set(qlcomp_dir ${CMAKE_CURRENT_SOURCE_DIR}/../qlcomp)

qt_add_executable(anystik
    src/main.cpp
    src/android_tls_bootstrap.h
    src/android_tls_bootstrap.cpp
    src/androidutils.h
    src/androidutils.cpp
    src/keepalive.h
    src/keepalive.cpp
    src/networkmonitor.h
    src/networkmonitor.cpp
    src/page.h
    src/page.cpp
    src/pagemanager.h
    src/pagemanager.cpp
    src/logmodel.h
    src/logmodel.cpp
    src/logpage.h
    src/logpage.cpp
    src/loginpage.h
    src/loginpage.cpp
    src/menuoverlay.h
    src/menuoverlay.cpp
    src/mainpage.h
    src/mainpage.cpp
    src/settingspage.h
    src/settingspage.cpp
    src/aboutpage.h
    src/aboutpage.cpp
    src/shareintentreceiver.h
    src/shareintentreceiver.cpp
    src/pushhandler.h
    src/pushhandler.cpp
    src/pushstatusbar.h
    src/pushstatusbar.cpp
    src/phonemonitor.h
    src/phonemonitor.cpp
    src/mytaphandler.h
    src/mytaphandler.cpp
    src/myscrollarea.h
    src/myscrollarea.cpp
    src/stickerstore.h
    src/stickerstore.cpp
    src/bundledpackspage.h
    src/bundledpackspage.cpp
    src/stickerlist.h
    src/stickerlist.cpp
    src/stickerhomepage.h
    src/stickerhomepage.cpp
    src/stickerpreviewoverlay.h
    src/stickerpreviewoverlay.cpp
    src/dialogpopup.h
    src/dialogpopup.cpp
    src/migrationdialog.h
    src/migrationdialog.cpp
    src/toastpopup.h
    src/toastpopup.cpp
    src/davobfus.h
    src/davobfus.cpp
    ${qlapp_dir}/cJSON.c
    ${qlapp_dir}/message_db.cpp
    ${qlapp_dir}/storage.cpp
    ${qlapp_dir}/channel_db.cpp
    ${qlapp_dir}/pending_db.cpp
    ${qlapp_dir}/sticker_db.cpp
    ${qlapp_dir}/cache_db.cpp
    ${qlapp_dir}/cache_fs.cpp
    ${qlapp_dir}/eventpoller.cpp
    ${qlapp_dir}/unknownparser.cpp
    ${qlcomp_dir}/compatcore34.cpp
    ${qlcomp_dir}/limelog.cpp
    ${qlcomp_dir}/md5.c
)

if(APPLE)
    # macOS：macpasteboard.mm 走 Objective-C++ 直接读 NSPasteboard 原始 GIF
    #（旧方案，运行时 ANYS_USE_MM_PASTEBOARD=1 切回）+ 默认纯 C++ QUtiMimeConverter 方案
    enable_language(OBJCXX)
    target_sources(anystik PRIVATE
        src/macpasteboard.h
        src/macpasteboard.mm
        src/macgifconverter.h
        src/macgifconverter.cpp
    )
    target_link_libraries(anystik PRIVATE "-framework AppKit")
endif()

# ── vendor: cJSON header ──
target_include_directories(anystik PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/include
    ${qlapp_dir}
    ${qlcomp_dir}
)

target_link_libraries(anystik PRIVATE Qsk::QSkinny Qt6::Quick Qt6::Network Qt6::Core Qt6::QuickPrivate Qt6::CorePrivate Qt6::Concurrent)

# ── extras/go 共享库 (c-shared) ──
set(GO_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extras/go)
set(GO_OUT_DIR ${CMAKE_BINARY_DIR})

if(ANDROID)
    set(GO_ENV
        GOOS=android GOARCH=arm64 CGO_ENABLED=1
        CC=${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android24-clang
    )
    set(GO_LIB ${GO_OUT_DIR}/libgoso.so)
elseif(APPLE)
    # macos-14+ runner 为 arm64；硬编码 amd64 产出 x86_64 dylib 导致链接架构不符
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(GO_ENV GOOS=darwin GOARCH=arm64 CGO_ENABLED=1)
    else()
        set(GO_ENV GOOS=darwin GOARCH=amd64 CGO_ENABLED=1)
    endif()
    set(GO_LIB ${GO_OUT_DIR}/libgoso.dylib)
elseif(WIN32)
    set(GO_ENV GOOS=windows GOARCH=amd64 CGO_ENABLED=1)
    set(GO_LIB ${GO_OUT_DIR}/libgoso.dll)
else()
    set(GO_ENV GOOS=linux GOARCH=amd64 CGO_ENABLED=1 CC=gcc)
    set(GO_LIB ${GO_OUT_DIR}/libgoso.so)
endif()

set(GO_HEADER ${GO_OUT_DIR}/goso.h)

add_custom_command(
    OUTPUT ${GO_LIB} ${GO_HEADER}
    COMMAND ${CMAKE_COMMAND} -E env ${GO_ENV}
            go build -buildmode=c-shared -o ${GO_LIB} .
    WORKING_DIRECTORY ${GO_SRC_DIR}
    DEPENDS ${GO_SRC_DIR}/goso.go
    COMMENT "Building Go shared library (goso)"
)
add_custom_target(goso_build DEPENDS ${GO_LIB})

add_library(goso SHARED IMPORTED GLOBAL)
set_target_properties(goso PROPERTIES
    IMPORTED_LOCATION ${GO_LIB}
    IMPORTED_NO_SONAME TRUE
)
add_dependencies(goso goso_build)

# ── extras/cpp 共享库 ──
add_library(cso SHARED
    extras/cpp/cso.cpp
    extras/cpp/cso.h
)

# ── 主程序链接 extras ──
target_link_libraries(anystik PRIVATE goso cso)

# ── vendor: curl + openssl 动态库 (Android) ──
if(ANDROID)
    add_library(libcurl SHARED IMPORTED GLOBAL)
    set_target_properties(libcurl PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/vendor/lib/${ANDROID_ABI}/libcurl.so
        IMPORTED_NO_SONAME TRUE)

    add_library(libssl SHARED IMPORTED GLOBAL)
    set_target_properties(libssl PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/vendor/lib/${ANDROID_ABI}/libssl.so
        IMPORTED_NO_SONAME TRUE)

    add_library(libcrypto SHARED IMPORTED GLOBAL)
    set_target_properties(libcrypto PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/vendor/lib/${ANDROID_ABI}/libcrypto.so
        IMPORTED_NO_SONAME TRUE)

    target_link_libraries(anystik PRIVATE libcurl libssl libcrypto)
else()
    find_package(CURL REQUIRED)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(anystik PRIVATE CURL::libcurl OpenSSL::SSL OpenSSL::Crypto)
endif()

# ── vendor: SQLite 预编译库 ──
if(ANDROID)
    add_library(sqlite3 SHARED IMPORTED GLOBAL)
    set_target_properties(sqlite3 PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/vendor/lib/${ANDROID_ABI}/libsqlite3.so
        IMPORTED_NO_SONAME TRUE
    )
    target_link_libraries(anystik PRIVATE sqlite3)
    set_target_properties(anystik PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(anystik PRIVATE log android)
elseif(UNIX)
    find_package(SQLite3 REQUIRED)
    target_link_libraries(anystik PRIVATE SQLite::SQLite3)
endif()
