# 同步 e2e（真 wsgidav）

在真实 WebDAV 服务器（wsgidav 4.x + cheroot）上跑 `SyncEngine`，断言
「动词/路径序列」与 `progress`/`finished`/`treeScanned` 信号，并覆盖
可合并边车（`_stikmeta.svg`）的折叠合并路径。

前置：`python3`（含 `venv` 模块）。首次运行需联网装 `wsgidav`/`cheroot`
到 `$DAVSYNC_VENV`（默认 `/tmp/opencode/wdav-venv`）。

    bash stikcommon/e2e/run_davsync_e2e.sh

产物：
- `/tmp/opencode/davsync_access.log`：`METHOD path` 序列
- 探针 stdout：`E2E-OK: fails=0` 表示通过

环境变量：`DAVSYNC_VENV`、`DAVSYNC_LOG`、`E2E_PORT`。
