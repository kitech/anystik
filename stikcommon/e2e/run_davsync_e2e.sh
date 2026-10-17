#!/bin/bash
# stikcommon/e2e/run_davsync_e2e.sh —— 真 wsgidav 上的 SyncEngine 端到端。
#
# 布局：
#   SERVER_ROOT/anystik/pastes/_stikmeta.svg   ← 预置「云端」载体（cloud-only）
#   LOCAL_BASE/pastes/regular.png              ← 本地独有普通文件（应上传）
#   LOCAL_BASE/pastes/_stikmeta.svg            ← 本地载体（local-only；与云端尺寸不同 → 触发合并）
# 断言见 davsync_e2e.cpp 顶部。通过时打印 "E2E-OK: fails=0"。
set -euo pipefail
cd "$(dirname "$0")/.."        # → stikcommon/

QTDIR=/opt/qt338sh
OUTDIR=e2e/e2e-build
mkdir -p "$OUTDIR"

export SERVER_ROOT="${SERVER_ROOT:-/tmp/opencode/e2e-srv}"
export LOCAL_BASE="${LOCAL_BASE:-/tmp/opencode/e2e-local}"
export XDG_DATA_HOME="${XDG_DATA_HOME:-/tmp/opencode/e2e-data}"
export DAVSYNC_LOG="${DAVSYNC_LOG:-/tmp/opencode/davsync_access.log}"
PORT="${E2E_PORT:-8098}"

# 本机可能设 http_proxy（如 127.0.0.1:9339）：curl 与探针内的 libcurl 会把
# 127.0.0.1 的自检/DAV 请求也发给代理 → 代理对 loopback 返 502，服务被误判「未起」。
# e2e 只连本机，强制绕过代理。
export no_proxy="127.0.0.1,localhost${no_proxy:+,${no_proxy}}"
export NO_PROXY="$no_proxy"

# ── 重置目录与数据 ───────────────────────────────────────────────
rm -rf "$SERVER_ROOT" "$LOCAL_BASE" "$XDG_DATA_HOME"
mkdir -p "$SERVER_ROOT/anystik/pastes" "$LOCAL_BASE/pastes" "$XDG_DATA_HOME"

# 云端预置载体：两行 JSONL（cloud-only 两个 rel）
printf '%s\n%s' \
  '{"rel":"pastes/cloud-only.png","mtime":1000,"desc":"y"}' \
  '{"rel":"pastes/cloud-only-2.png","mtime":900,"desc":"z"}' \
  > "$SERVER_ROOT/anystik/pastes/_stikmeta.svg"

# 本地普通文件（任意字节；上传侧不做图片校验）
printf 'PNG-BYTES-regular' > "$LOCAL_BASE/pastes/regular.png"

# 本地载体：一行 JSONL（local-only 一个 rel）；尺寸与云端不同 → 走合并
printf '%s' \
  '{"rel":"pastes/local-only.png","mtime":2000,"desc":"x"}' \
  > "$LOCAL_BASE/pastes/_stikmeta.svg"

# ── 起服务器并自检 ───────────────────────────────────────────────
SERVER_ROOT="$SERVER_ROOT" DAVSYNC_VENV="${DAVSYNC_VENV:-/tmp/opencode/wdav-venv}" \
    bash e2e/start_wdav.sh "$SERVER_ROOT" "$PORT"
SRV_PID="$(cat "$SERVER_ROOT/.wdav.pid")"
cleanup() { kill "$SRV_PID" 2>/dev/null || true; }
trap cleanup EXIT

BASE_URL="http://127.0.0.1:$PORT"
ok=0
for i in $(seq 1 40); do
    code="$(curl -s -o /dev/null -w '%{http_code}' -u user:pass -X OPTIONS "$BASE_URL/" || true)"
    if [ "$code" = "200" ]; then ok=1; break; fi
    sleep 0.25
done
if [ "$ok" != "1" ]; then
    echo "server did not come up; tail log:"
    tail -n 40 "$SERVER_ROOT/wdav_server.log" || true
    exit 1
fi

# ── 编译探针 ─────────────────────────────────────────────────────
CXX="g++ -std=c++14 -g -O0 -w"
FLAGS="-DQT3_BUILD -DQT_NO_DEBUG -DQT_SHARED -DQT_THREAD_SUPPORT \
  -I. -Ie2e -I../vendor -I../vendor/libnsgif \
  -I../vendor/uc_apng_loader -I$QTDIR/include -I../qlappcom -I../qlcomp"

PRODUCTS="
../qlappcom/eventpoller.cpp
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
jsonl_lww_store.cpp
pack_meta_store.cpp
qzipreader_shim.cpp
qimagereader_shim.cpp
qimagesmoothscale.cpp
../vendor/pugixml/pugixml.cpp
davbisync.cpp
davlocalsource.cpp
davbisync_baseline.cpp
"

echo "=== moc ==="
$QTDIR/bin/moc qwebdavdirparserlite.h -o "$OUTDIR/moc_parser.cpp"
$QTDIR/bin/moc davbisync.h -o "$OUTDIR/moc_davbisync.cpp"
$QTDIR/bin/moc e2e/proberecv.h -o "$OUTDIR/moc_proberecv.cpp"

$CXX $FLAGS -c "$OUTDIR/moc_parser.cpp" -o "$OUTDIR/moc_parser.o"
$CXX $FLAGS -c "$OUTDIR/moc_davbisync.cpp" -o "$OUTDIR/moc_davbisync.o"
$CXX $FLAGS -c "$OUTDIR/moc_proberecv.cpp" -o "$OUTDIR/moc_proberecv.o"
OBJS="$OUTDIR/moc_parser.o $OUTDIR/moc_davbisync.o $OUTDIR/moc_proberecv.o"

echo "=== 编译产品代码 ==="
for src in $PRODUCTS; do
    o="$OUTDIR/prod_$(basename "${src%.cpp}").o"
    $CXX $FLAGS -c "$src" -o "$o"
    OBJS="$OBJS $o"
done
$CXX $FLAGS -c ../qlcomp/cJSON.c -o "$OUTDIR/prod_cJSON.o"
OBJS="$OBJS $OUTDIR/prod_cJSON.o"
for csrc in ../vendor/libnsgif/gif.c ../vendor/libnsgif/lzw.c \
            ../qlcomp/md5.c sha1.c; do
    o="$OUTDIR/prod_$(basename "${csrc%.c}").o"
    gcc -std=gnu99 -g -O0 -w $FLAGS -DSHA1HANDSOFF -c "$csrc" -o "$o"
    OBJS="$OBJS $o"
done

echo "=== 编译探针 ==="
$CXX $FLAGS -c e2e/davsync_e2e.cpp -o "$OUTDIR/davsync_e2e.o"
OBJS="$OBJS $OUTDIR/davsync_e2e.o"

echo "=== 链接 ==="
$CXX -o "$OUTDIR/davsync_e2e" $OBJS \
  -L$QTDIR/lib -lqt-mt -lcurl -lssl -lcrypto -lz -lwebp -lwebpdemux -lpthread \
  -ldl -lX11 -lXss

echo "=== 运行探针 ==="
rc=0
DAVSYNC_LOCAL_BASE="$LOCAL_BASE" DAVSYNC_LOG="$DAVSYNC_LOG" \
    LD_LIBRARY_PATH=$QTDIR/lib "$OUTDIR/davsync_e2e" "$BASE_URL" || rc=$?

echo "=== 访问日志 ==="
cat "$DAVSYNC_LOG" || true
exit $rc
