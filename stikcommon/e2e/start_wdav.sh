#!/bin/bash
# stikcommon/e2e/start_wdav.sh <rootdir> <port>
# 起一个真实 wsgidav（4.x + cheroot）供同步 e2e；访问日志写 $DAVSYNC_LOG。
# 打印 BASE_URL=http://127.0.0.1:<port>，调用方 curl 自检后再跑探针。
set -euo pipefail

ROOT="${1:?usage: start_wdav.sh <rootdir> <port>}"
PORT="${2:-8098}"
HERE="$(cd "$(dirname "$0")" && pwd)"
VENV="${DAVSYNC_VENV:-/tmp/opencode/wdav-venv}"
export DAVSYNC_LOG="${DAVSYNC_LOG:-/tmp/opencode/davsync_access.log}"
: > "$DAVSYNC_LOG"

if [ ! -x "$VENV/bin/wsgidav" ]; then
    python3 -m venv "$VENV"
    "$VENV/bin/pip" install -q --upgrade pip
    "$VENV/bin/pip" install -q "wsgidav>=4.3,<5" cheroot
fi

mkdir -p "$ROOT"
cat > "$ROOT/wdav.yaml" <<EOF
provider_mapping:
  "/": "$ROOT"
dir_browser:
  enable: false
add_content_length: true
simple_dc:
  user_mapping:
    "*":
      user:
        password: "pass"
middleware_stack:
  - "logmw.LogMw"
  - "wsgidav.mw.cors.Cors"
  - "wsgidav.error_printer.ErrorPrinter"
  - "wsgidav.http_authenticator.HTTPAuthenticator"
  - "wsgidav.dir_browser.WsgiDavDirBrowser"
  - "wsgidav.request_resolver.RequestResolver"
verbose: 1
EOF

PYTHONPATH="$HERE" "$VENV/bin/wsgidav" \
    --host 127.0.0.1 --port "$PORT" --config "$ROOT/wdav.yaml" \
    > "$ROOT/wdav_server.log" 2>&1 &
echo $! > "$ROOT/.wdav.pid"
echo "BASE_URL=http://127.0.0.1:$PORT"
