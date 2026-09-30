#!/bin/bash
# qlstik/build_tests.sh —— 单元测试套件 B（Qt3 单端）
#
# 范式与 stikcommon/build_tests.sh 同构（那套的来源是 qldox/build_tests.sh，
# 本仓旧范式，源码已丢）：
#   * 每个 test_*.cpp **单独编译**，只给最小 include，便于定位是哪个炸
#   * 全部链成**单个** run_tests 二进制
#   * -w 压掉 Qt3 垫片噪声，让测试输出不被警告淹没
#
# 与套件 A 的差别（已核实的事实，不是选择）：
#   * 测试文件在 qlstik/test/（该目录原本已存在且为空），故 -I../stikcommon 之外
#     还要能解析 config.h —— 它在 qlstik/src/，故加 -Isrc。
#   * 被测产品只 config.cpp 一个；mainwindow/main 是 GUI，不进单测。
#     但 config.cpp 依赖 stikcommon 的 qMkdir（qglobaltype_shim.h，纯头）
#     与 cJSON（qldox/cJSON.c）。
#   * 不需要 moc：qlstik/src 下无 Q_OBJECT 类。
set -e
cd "$(dirname "$0")"

QTDIR=/opt/qt338sh
OUTDIR=tests-build
mkdir -p "$OUTDIR"

CXX="g++ -std=c++11 -g -O0 -w"
# -Isrc  : config.h
# -I../stikcommon : qglobaltype_shim.h（qMkdir）、qstring_shim.h 等垫片
# -I../qldox      : cJSON.h
# -I../qlcomp      : compat34.h
# -I../anystik/vendor : doctest
FLAGS="-DQT3_BUILD -DQT_NO_DEBUG -DQT_SHARED -DQT_THREAD_SUPPORT \
  -I. -Itest -Isrc -I../stikcommon -I../qldox -I../qlcomp \
  -I../anystik/vendor -I$QTDIR/include"

# ── 测试文件（新增一个就在这里加一行）────────────────────────────────
TESTS="
test/test_config.cpp
"

# ── 被测产品代码 ────────────────────────────────────────────────────
# compat34.cpp 带来 qMkdir / qToUtf8 / qFromUtf8 / toEventType34 的定义
# （config.cpp 与测试脚手架都要用；qglobaltype_shim.h 只是纯头，无需编译）
PRODUCTS="
src/config.cpp
../qlcomp/compat34.cpp
../qldox/cJSON.c
"

echo "=== 编译产品代码 ==="
OBJS=""
for src in $PRODUCTS; do
    case "$src" in
        *.h) continue ;;                 # 纯头，无需编译
    esac
    o="$OUTDIR/prod_$(basename "${src%.cpp}").o"
    $CXX $FLAGS -c "$src" -o "$o"
    OBJS="$OBJS $o"
done

echo "=== 编译测试（逐个，出错即定位到文件）==="
$CXX $FLAGS -c test/test_main.cpp -o "$OUTDIR/test_main.o"
OBJS="$OBJS $OUTDIR/test_main.o"
for t in $TESTS; do
    echo "  -- $t"
    $CXX $FLAGS -c "$t" -o "$OUTDIR/$(basename "${t%.cpp}").o"
    OBJS="$OBJS $OUTDIR/$(basename "${t%.cpp}").o"
done

echo "=== 链接 ==="
# -lX11：compat34.cpp 会链到 XOpenDisplay（limelog/QApplication 相关符号），
#         漏了报 "DSO missing from command line"，与测试代码无关。
$CXX -o "$OUTDIR/run_tests" $OBJS -L$QTDIR/lib -lqt-mt -lX11

echo "=== 运行 ==="
LD_LIBRARY_PATH=$QTDIR/lib "$OUTDIR/run_tests" "$@"
