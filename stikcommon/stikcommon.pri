# stikcommon/stikcommon.pri —— 共享非 UI 层供应文件（qmake 侧）
#
# 规约（移植计划 §8）：
#   * 本文件**只列"已验证通过"的共享件**。未按批次迁入的模块不允许出现在这里，
#     qlstik 侧一律用 stikcommon.pri 的空骨架（模块清单按批追加，每批一处 git mv）。
#   * 每个迁入的模块对 anystik 都是零逻辑改动（阈值见 移植计划 §7）。
#   * 折叠 myvendor.pri（vendor 源）与 qldox.pri（qltox 的 cJSON / EventPoller）。
#
# 输出：STIKCOMMON_SOURCES / STIKCOMMON_HEADERS / STIKCOMMON_INCLUDES / STIKCOMMON_CFLAGS

STIKCOMMON_DIR = $$PWD

STIKCOMMON_SOURCES  =
STIKCOMMON_HEADERS  =
STIKCOMMON_INCLUDES = $$STIKCOMMON_DIR

# ── 已迁入的共享模块（批次 1..6 逐条追加）─────────────────────────────
# 批次 1  myi18n / eifreader / davobfus
# 批次 2  phonedb / davbisync_baseline / sitelistclient / imagesearchclient
# 批次 3  imagetmpuploader / davbisync / imageaiutil(+httpua.h)
# 批次 4  networkmonitor
# 批次 5  stickerstore / settings_trace.h
# 批次 6  extras/cpp/cso
#
# 追加范式（例）：
#   STIKCOMMON_SOURCES += $$STIKCOMMON_DIR/myi18n.cpp
#   STIKCOMMON_HEADERS += $$STIKCOMMON_DIR/myi18n.h

include($$STIKCOMMON_DIR/../myvendor/myvendor.pri)
include($$STIKCOMMON_DIR/qldox.pri)

STIKCOMMON_SOURCES  += $$MYVENDOR_SOURCES $$QLDOX_SOURCES
STIKCOMMON_HEADERS  += $$QLDOX_HEADERS
STIKCOMMON_INCLUDES += $$MYVENDOR_INCLUDES $$QLDOX_INCLUDES

STIKCOMMON_CFLAGS = $$MYVENDOR_CFLAGS
