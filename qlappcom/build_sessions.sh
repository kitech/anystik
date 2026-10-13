#!/bin/bash
# aigpt_sessions 独立构建（Qt-free 单测）。串行执行，勿与其他 build 并行。
set -e
cd "$(dirname "$0")"

B=build-sessions
mkdir -p "$B" /tmp/opencode

CXX="g++ -std=c++11 -g -O0 -w"
CC="gcc -std=c11 -g -O0 -w"

echo "=== 编译 aigpt_sessions（qlcomp sha2 + cJSON + 本体）==="
$CC -c ../qlcomp/obsd_sha2.c -I../qlcomp -o "$B/obsd_sha2.o"
$CC -c cJSON.c -o "$B/cjson.o"
$CXX -c aigpt_sessions.cpp -I. -I../qlcomp -o "$B/aigpt_sessions.o"
$CXX -c test_aigptbot_sessions.cpp -I. -I../qlcomp -o "$B/test_aigptbot_sessions.o"

echo "=== 链接并运行单测 ==="
$CXX "$B/test_aigptbot_sessions.o" "$B/aigpt_sessions.o" "$B/obsd_sha2.o" "$B/cjson.o" \
    -o "$B/test_aigptbot_sessions"
"$B/test_aigptbot_sessions"

echo "=== aigpt_sessions 构建完成 ==="
