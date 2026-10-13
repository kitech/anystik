#!/bin/bash
# webcreds 独立构建（Qt-free：CLI + 单测）。串行执行，勿与其他 build 并行。
set -e
cd "$(dirname "$0")"

B=build-webcreds
mkdir -p "$B"

CXX="g++ -std=c++11 -g -O0 -w"
CC="gcc -std=c11 -g -O0 -w"

echo "=== 编译 webcreds（qlcomp aes/sha2 + cJSON + 本体）==="
$CC -c ../qlcomp/aes.c -I../qlcomp -o "$B/aes.o"
$CC -c ../qlcomp/obsd_sha2.c -I../qlcomp -o "$B/obsd_sha2.o"
$CC -c cJSON.c -o "$B/cjson.o"
$CXX -c webcreds.cpp -I. -I../qlcomp -o "$B/webcreds.o"
$CXX -c webcreds_cli.cpp -I. -I../qlcomp -o "$B/webcreds_cli.o"
$CXX -c test_webcreds.cpp -I. -I../qlcomp -o "$B/test_webcreds.o"

echo "=== 链接 CLI ==="
$CXX "$B/webcreds_cli.o" "$B/webcreds.o" "$B/aes.o" "$B/obsd_sha2.o" "$B/cjson.o" -o webcreds

echo "=== 链接并运行单测 ==="
$CXX "$B/test_webcreds.o" "$B/webcreds.o" "$B/aes.o" "$B/obsd_sha2.o" "$B/cjson.o" -o "$B/test_webcreds"
"$B/test_webcreds"

echo "=== CLI smoke（临时目录，不碰真实 ~/.config/qltox）==="
D=$(mktemp -d /tmp/opencode/webcreds_cli.XXXXXX)
./webcreds --file "$D/a.enc" --token "$D/a.key" create \
    --field deepseek --value secret-plain --mode plain \
    --field gemini   --value secret-token --mode token \
    --field grok     --value secret-pass  --mode pass --pass pw123
echo "--- show ---"
./webcreds --file "$D/a.enc" --token "$D/a.key" show
echo "--- check ---"
./webcreds --file "$D/a.enc" --token "$D/a.key" check || true
echo "--- set/deepseek 改 token ---"
./webcreds --file "$D/a.enc" --token "$D/a.key" set --field deepseek --value secret-token2 --mode token
echo "--- reveal → token 字段可见 ---"
./webcreds --file "$D/a.enc" --token "$D/a.key" show --reveal
echo "--- rm/wipe ---"
./webcreds --file "$D/a.enc" --token "$D/a.key" rm --field deepseek
./webcreds --file "$D/a.enc" --token "$D/a.key" wipe --yes
rm -rf "$D"
echo "=== webcreds 构建完成 ==="