#!/bin/bash
# stikcommon/build_tests.sh —— 单元测试套件 A（Qt3 单端）
#
# 范式来源：qldox/build_tests.sh（本仓旧范式）
#   * 每个 test_*.cpp **单独编译**、只给最小 include，便于定位是哪个炸
#   * 全部链成**单个** run_tests 二进制
#   * 编译参数 -w：压掉 Qt3 垫片噪声，让测试输出不被警告淹没
#
# 与旧范式的差别（均为已核实的事实，不是选择）：
#   * 框架为 doctest 2.4.11（anystik/vendor/doctest），旧 run_tests 二进制内嵌
#     同版本串。过滤用 doctest 内置命令行选项，不自写解析层。
#   * 旧脚本未给 test_main.cpp 任何 -I，说明当年 doctest.h 就与 test_*.cpp 同目录、
#     靠 `#include "..."` 的相对查找命中；本脚本改放 anystik/vendor（集中管理），
#     故需显式 -I../anystik/vendor。
#   * 旧脚本 -std=c++11；沿用。
set -e
cd "$(dirname "$0")"

QTDIR=/opt/qt338sh
OUTDIR=tests-build
mkdir -p "$OUTDIR"

CXX="g++ -std=c++11 -g -O0 -w"
# 测试与被测产品代码同在 stikcommon/，故 -I. ；doctest 在 anystik/vendor
FLAGS="-DQT3_BUILD -DQT_NO_DEBUG -DQT_SHARED -DQT_THREAD_SUPPORT \
  -I. -I../anystik/vendor -I$QTDIR/include -I../qldox -I../qlcomp"

# ── 测试文件（新增一个就在这里加一行）────────────────────────────────
TESTS="
test_qwebdavlite.cpp
test_dav207.cpp
test_qdatetime_shim.cpp
test_qurl_shim.cpp
test_qregularexpression_shim.cpp
test_shims.cpp
test_qconnect_slots.cpp
test_qmkdir.cpp
"

# ── 被测产品代码：链接 run_tests 所需的 stikcommon/qlcomp/qldox 部分 ──
# 不用 qlstik/build-qt3/*.o：那些 .o 是应用构建产物，会把当时的内联/宏展开烤进去；
# 单测要的是「当前头文件」的效果，故全源码编译（与 qldox/build_tests.sh 同理由）。
PRODUCTS="
../qldox/eventpoller.cpp
../qlcomp/compat34.cpp
../qlcomp/limelog.cpp
qwebdavlite.cpp
qwebdavtransport.cpp
qwebdavdirparserlite.cpp
qnam_shim.cpp
qsslprobe.cpp
qcabundle.cpp
qdatetime_shim.cpp
dav207pugi.cpp
qsavefile_shim.cpp
qjson_shim.cpp
qstandardpaths_shim.cpp
../anystik/vendor/pugixml/pugixml.cpp
"

echo "=== moc ==="
# QWebdavDirParserLite 是本套件涉及的唯一 Q_OBJECT 类；缺 moc 产物会在链接期
# 报 vtable / staticMetaObject 缺失（编译能过，极易误判为测试代码写错）
$QTDIR/bin/moc qwebdavdirparserlite.h -o "$OUTDIR/moc_parser.cpp"

OBJS="$OUTDIR/moc_parser.o"
# moc 只产出 .cpp，必须显式编成 .o 才能进链接（漏这一步的表现是链接期报
# "cannot find tests-build/moc_parser.o"，而 moc 那步看着是成功的，容易误判）
$CXX $FLAGS -c "$OUTDIR/moc_parser.cpp" -o "$OUTDIR/moc_parser.o"

echo "=== 编译产品代码 ==="
for src in $PRODUCTS; do
    o="$OUTDIR/prod_$(basename "${src%.cpp}").o"
    $CXX $FLAGS -c "$src" -o "$o"
    OBJS="$OBJS $o"
done
# cJSON 是 C 源（qldox/cJSON.c），qjson_shim.cpp 依赖它
$CXX $FLAGS -c ../qldox/cJSON.c -o "$OUTDIR/prod_cJSON.o"
OBJS="$OBJS $OUTDIR/prod_cJSON.o"

echo "=== 编译测试（逐个，出错即定位到文件）==="
$CXX $FLAGS -c test_main.cpp -o "$OUTDIR/test_main.o"
OBJS="$OBJS $OUTDIR/test_main.o"
for t in $TESTS; do
    echo "  -- $t"
    $CXX $FLAGS -c "$t" -o "$OUTDIR/${t%.cpp}.o"
    OBJS="$OBJS $OUTDIR/${t%.cpp}.o"
done

echo "=== 链接 ==="
$CXX -o "$OUTDIR/run_tests" $OBJS \
  -L$QTDIR/lib -lqt-mt -lcurl -lssl -lcrypto -lz -lpthread \
  -ldl -lX11 -lXss

echo "=== 运行 ==="
LD_LIBRARY_PATH=$QTDIR/lib "$OUTDIR/run_tests" "$@"
