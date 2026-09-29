# myvendor 供应文件（qmake 侧）：供 qlstik 等工程 include 复用
# 镜像 myvendor/tousesub.cmake 的源清单与 include 目录。
MYVENDOR_DIR = $$PWD

MYVENDOR_CFLAGS = -DBLOOM_VERSION_MAJOR=2 -DBLOOM_VERSION_MINOR=0

MYVENDOR_SOURCES = \
    $$MYVENDOR_DIR/netut/ipaddr_list.c \
    $$MYVENDOR_DIR/bloom/bloom.c \
    $$MYVENDOR_DIR/bloom/murmur2/MurmurHash2.c \
    $$MYVENDOR_DIR/uuid/uuid4.c \
    $$MYVENDOR_DIR/byteut/bytes2hum.c \
    $$MYVENDOR_DIR/phoneloc/phoneloc.c

MYVENDOR_INCLUDES = \
    $$MYVENDOR_DIR/byteut \
    $$MYVENDOR_DIR/bloom/murmur2 \
    $$MYVENDOR_DIR/bloom \
    $$MYVENDOR_DIR/uuid \
    $$MYVENDOR_DIR/phoneloc