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
# 两种落地形态：
#   L0（临时）—— 仍在 anystik/src，本文件按原路径引用，L2 验证通过后改指 $$STIKCOMMON_DIR
#   L2（终态）—— 已物理搬入本目录
#
# 追加范式（例）：
#   STIKCOMMON_SOURCES += $$STIKCOMMON_DIR/myi18n.cpp
#   STIKCOMMON_HEADERS += $$STIKCOMMON_DIR/myi18n.h

include($$STIKCOMMON_DIR/../myvendor/myvendor.pri)
include($$STIKCOMMON_DIR/qldox.pri)

STIKCOMMON_SOURCES  += $$MYVENDOR_SOURCES $$QLDOX_SOURCES
STIKCOMMON_HEADERS  += $$QLDOX_HEADERS
STIKCOMMON_INCLUDES += $$MYVENDOR_INCLUDES $$QLDOX_INCLUDES

# ══ 批次 1：myi18n / davobfus（eifreader 暂缓：Qt 3.5 的 QByteArray 是
#     typedef QMemArray<char>，无法用垫片补 Qt4 方法，需真移植，见移植计划 §6.3）══
ANYSTIK_SRC_DIR = $$STIKCOMMON_DIR/../anystik/src

# myi18n：L0 引用 anystik/src（Qt3 侧靠 stikcommon/*_shim.h 兜类级缺口）
STIKCOMMON_SOURCES  += $$ANYSTIK_SRC_DIR/myi18n.cpp
STIKCOMMON_HEADERS  += $$ANYSTIK_SRC_DIR/myi18n.h

# davobfus：已拷入本目录（手写源，anystik/src 侧是 .cpp.tmpl 模板 +
# 真实 key 占位；qlstik 不注入 key，只验证代码可编可链）
# 需 c++14（libobfuscate 用 relaxed constexpr）；qmake 3.x 无按文件 CXXFLAGS，
# 已在 qlstik.pro 把 Qt4/5 底线从 c++11 提到 c++14 统一满足。
STIKCOMMON_SOURCES  += $$STIKCOMMON_DIR/davobfus.cpp
STIKCOMMON_HEADERS  += $$STIKCOMMON_DIR/davobfus.h

# settings_trace：myi18n 的运行时依赖，anystik 侧定义在 main.cpp（mac 探针，
# 非 mac 空转）；qtlsik 侧在此给同义空实现，L2 与 anystik main.cpp 合并
STIKCOMMON_SOURCES  += $$STIKCOMMON_DIR/settings_trace.cpp

# Qt3 垫片（平铺，无聚合头、无 -include 预包含；见移植计划 §6.3）
# 不可用同名派生的：QSettings/QLocale 被 qapp.h 提前引入，只能改调用点
STIKCOMMON_HEADERS  += $$STIKCOMMON_DIR/qcoreapplication_shim.h \
                       $$STIKCOMMON_DIR/qhash_shim.h \
                       $$STIKCOMMON_DIR/qstring_shim.h \
                       $$STIKCOMMON_DIR/qvector_shim.h

# ══ 批次 2a：davbisync_baseline（QJson / QStandardPaths / QSaveFile）══
# 三个类都是 Qt5 才引入：Qt3.5 与 Qt4.8.7 的 QtCore 下均无对应头文件，
# 故三个垫片对 Qt3 与 Qt4 同时启用（计划原文"QSaveFile 仅 Qt3、Qt4.1+ 原生"
# 有误，已核实更正为 Qt5.1 才引入）。Qt5+ 走原生，垫片 .cpp 不参与编译。
STIKCOMMON_SOURCES  += $$ANYSTIK_SRC_DIR/davbisync_baseline.cpp
STIKCOMMON_HEADERS  += $$ANYSTIK_SRC_DIR/davbisync_baseline.h

# ══ 批次 2b：phonedb（QNAM 消费端：QNetworkRequest/Manager/Reply +
#     qconnect_slots 新式 connect + cookie jar）══
# Qt4 原生 QNAM 有硬限制：QNetworkReply::finished 是 protected Q_SIGNAL
#     （Qt4.8 Q_SIGNALS=protected，Qt5 才改 public），新式 connect 在 Qt4
#     编不过；且 Qt4 无 setTransferTimeout（5.15+）。故 Qt4 分支暂不挂载
#     phonedb（Qt3 用 qnam_shim 垫片、Qt6 用原生 QNAM），待 qnam_shim 扩展
#     Qt4 支援（移植计划 §6.3b 记录）。Qt3 由不含 QT_VERSION 自判。
# phonedb.cpp 的 Qt3 差异（QIODevice::ReadOnly→IO_ReadOnly、QDir().mkpath→qMkdir、
#     constData→data、QFile::write→writeBlock、toUtf8→utf8、QString::size→length）
#     已在源内用 QT3_BUILD 分支收口。phonedb.h 的 Q_OBJECT/Q_SIGNALS 在 Qt3 由
#     qt3 moc 处理生成 moc_phonedb.cpp，新式 connect 走 qconnect_slots.h。
# 注：isEmpty(QT_VERSION)=Qt3；Qt5+/Qt6 也编译（原生 QNAM）。
isEmpty(QT_VERSION) {
    STIKCOMMON_SOURCES  += $$ANYSTIK_SRC_DIR/phonedb.cpp
    STIKCOMMON_HEADERS  += $$ANYSTIK_SRC_DIR/phonedb.h
} else:!lessThan(QT_VERSION, 5.0.0) {
    STIKCOMMON_SOURCES  += $$ANYSTIK_SRC_DIR/phonedb.cpp
    STIKCOMMON_HEADERS  += $$ANYSTIK_SRC_DIR/phonedb.h
}

# ══ 批次 2a 垫片 ══
# 头文件无条件登记（moc/依赖扫描用），.cpp 严格按版本条件挂载，避免 Qt5+ 出现
# "QSaveFile/QJson/QStandardPaths 重复定义"。条件范式对齐 qlstik.pro：
# Qt3 的 qmake(1.07a) 无 QT_VERSION 变量，用 isEmpty(QT_VERSION) 自判。
STIKCOMMON_HEADERS  += $$STIKCOMMON_DIR/qjson_shim.h \
                       $$STIKCOMMON_DIR/qstandardpaths_shim.h \
                       $$STIKCOMMON_DIR/qsavefile_shim.h \
                       $$STIKCOMMON_DIR/qnam_shim.h \
                       $$STIKCOMMON_DIR/qconnect_slots.h

isEmpty(QT_VERSION) {
    # Qt3.5
    STIKCOMMON_SOURCES += $$STIKCOMMON_DIR/qjson_shim.cpp \
                         $$STIKCOMMON_DIR/qstandardpaths_shim.cpp \
                         $$STIKCOMMON_DIR/qsavefile_shim.cpp \
                         $$STIKCOMMON_DIR/qnam_shim.cpp
} else:lessThan(QT_VERSION, 5.0.0) {
    # Qt4.8.7
    STIKCOMMON_SOURCES += $$STIKCOMMON_DIR/qjson_shim.cpp \
                         $$STIKCOMMON_DIR/qstandardpaths_shim.cpp \
                         $$STIKCOMMON_DIR/qsavefile_shim.cpp
}

# myi18n 的 settings_trace.h 在 anystik 侧；davobfus 的 libobfuscate 在 vendor
STIKCOMMON_INCLUDES += $$ANYSTIK_SRC_DIR \
                       $$ANYSTIK_SRC_DIR/../vendor/include

STIKCOMMON_CFLAGS = $$MYVENDOR_CFLAGS
