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

# ══ 批次 3a：imagetmpuploader（QNAM multipart/raw 上传 + QJson 解析）══
# Qt3 用 qnam_shim+qconnect_slots+qba_shim+qjson_shim；Qt6 走原生。Qt4 跳过——
# 与 phonedb 同因（QNetworkReply::finished 在 Qt4.8 是 protected Q_SIGNAL，
# 新式 connect 编不过），见上面 phonedb 注释与移植计划 §6.3b。
# 模块内 QT3_BUILD 分支收口点：kUploadReadMode（IO_ReadOnly）、utf8()、
# payload 用 QCString+setNum 拼（QByteArray=QMemArray<char> 无 operator+/number）、
# using ::connect；QByteArrayLiteral 由 qba_shim.h 归属 Qt3→QCString。
isEmpty(QT_VERSION) {
    STIKCOMMON_SOURCES  += $$ANYSTIK_SRC_DIR/imagetmpuploader.cpp
    STIKCOMMON_HEADERS  += $$ANYSTIK_SRC_DIR/imagetmpuploader.h
} else:!lessThan(QT_VERSION, 5.0.0) {
    STIKCOMMON_SOURCES  += $$ANYSTIK_SRC_DIR/imagetmpuploader.cpp
    STIKCOMMON_HEADERS  += $$ANYSTIK_SRC_DIR/imagetmpuploader.h
}

# ══ 批次 3b：imageaiutil（QNAM 视觉描述 + QJson + QImageReader 探测 + 轮询）══
# Qt4 跳过：与 phonedb/imagetmpuploader 同因（QNetworkReply::finished 在 Qt4.8
#     是 protected Q_SIGNAL，新式 connect 编不过，见 §6.3b 记录）。另 Qt4 无
#     qInfo()（5.5+）/QImageReader.format() 语义亦有差异，一并按 skip 处理。
# Qt3 new surfaces：qqueue_shim（QQueue<Request>）、qurl_shim（QUrlQuery /
#     qToPercentEncoding / qResolveUrl / qUrlIsRelative）、qimagereader_shim
#     （format 探测）、qdebug_shim（qInfo/qWarning 流）、qbytearray_shim
#     （qToBase64）；QTimer setInterval/start、QMetaObject::invokeMethod PMF
#     在源内 QT3_BUILD 收口；startNext/pollAiHorde 在 QT3_BUILD 纳入 slots 分区
#     （Qt3QMetaObject::invokeMethod 与老式 SIGNAL/SLOT 连接只认槽名）。
isEmpty(QT_VERSION) {
    STIKCOMMON_SOURCES  += $$ANYSTIK_SRC_DIR/imageaiutil.cpp
    STIKCOMMON_HEADERS  += $$ANYSTIK_SRC_DIR/imageaiutil.h
} else:!lessThan(QT_VERSION, 5.0.0) {
    STIKCOMMON_SOURCES  += $$ANYSTIK_SRC_DIR/imageaiutil.cpp
    STIKCOMMON_HEADERS  += $$ANYSTIK_SRC_DIR/imageaiutil.h
}

# ══ 批次 2c：sitelistclient / imagesearchclient（QNAM + QRegularExpression）══
# Qt4 跳过：与 phonedb/imagetmpuploader/imageaiutil 同因（QNetworkReply::finished
#     在 Qt4.8 是 protected Q_SIGNALS，新式 connect 编不过），见上面 phonedb 注释
#     与移植计划 §6.3b。Qt3 走 qnam_shim + qconnect_slots + qregularexpression_shim
#     + qurl_shim + qstring_shim + qcoreapplication_shim；Qt5+ 走原生。
# 模块内 QT3_BUILD 分支收口点：
#   sitelistclient  —— <img> 正则 / QUrl 相对解析 / QFace JSON 索引 / UA+Referer
#   imagesearchclient —— m= 与 data-bem= 两级正则 / QUrl::toPercentEncoding /
#                       QStringList::removeDuplicates（Qt3 侧稳定去重垫片）
# 两个 pimpl 已从 QObject 降为**普通 C++ 类**（Q_OBJECT / moc 在 .cpp 里），
# 因为 Qt3 qmake 1.07a 不对 .cpp 跑 moc，`connect` 的 context 改用 owner。
isEmpty(QT_VERSION) {
    STIKCOMMON_SOURCES  += $$ANYSTIK_SRC_DIR/sitelistclient.cpp \
                           $$ANYSTIK_SRC_DIR/imagesearchclient.cpp
    STIKCOMMON_HEADERS  += $$ANYSTIK_SRC_DIR/sitelistclient.h \
                           $$ANYSTIK_SRC_DIR/imagesearchclient.h
} else:!lessThan(QT_VERSION, 5.0.0) {
    STIKCOMMON_SOURCES  += $$ANYSTIK_SRC_DIR/sitelistclient.cpp \
                           $$ANYSTIK_SRC_DIR/imagesearchclient.cpp
    STIKCOMMON_HEADERS  += $$ANYSTIK_SRC_DIR/sitelistclient.h \
                           $$ANYSTIK_SRC_DIR/imagesearchclient.h
}

# ══ 批次 2a 垫片 ══
# 头文件无条件登记（moc/依赖扫描用），.cpp 严格按版本条件挂载，避免 Qt5+ 出现
# "QSaveFile/QJson/QStandardPaths 重复定义"。条件范式对齐 qlstik.pro：
# Qt3 的 qmake(1.07a) 无 QT_VERSION 变量，用 isEmpty(QT_VERSION) 自判。
STIKCOMMON_HEADERS  += $$STIKCOMMON_DIR/qjson_shim.h \
                       $$STIKCOMMON_DIR/qstandardpaths_shim.h \
                       $$STIKCOMMON_DIR/qsavefile_shim.h \
                       $$STIKCOMMON_DIR/qnam_shim.h \
                       $$STIKCOMMON_DIR/qconnect_slots.h \
                       $$STIKCOMMON_DIR/qba_shim.h \
                       $$STIKCOMMON_DIR/qqueue_shim.h \
                       $$STIKCOMMON_DIR/qurl_shim.h \
                       $$STIKCOMMON_DIR/qimagereader_shim.h \
                       $$STIKCOMMON_DIR/qdebug_shim.h \
                       $$STIKCOMMON_DIR/qbytearray_shim.h \
                       $$STIKCOMMON_DIR/qwebdavtransport.h \
                       $$STIKCOMMON_DIR/qwebdavlite.h \
                       $$STIKCOMMON_DIR/qwebdavdirparserlite.h \
                       $$STIKCOMMON_DIR/qwebdavitemlite.h \
                       $$STIKCOMMON_DIR/qsslprobe.h \
                       $$STIKCOMMON_DIR/qcabundle.h \
                       $$STIKCOMMON_DIR/qdatetime_shim.h \
                       $$STIKCOMMON_DIR/qregularexpression_shim.h \
                       $$STIKCOMMON_DIR/qglobaltype_shim.h

# ── 批次 3c-1 QWebdavLite：自建 verb-aware 传输 + DAV 窄切面 ──
# qwebdavtransport 为何必须存在：qldox/eventpoller.cpp 是只读依赖，全文没有
# CURLOPT_CUSTOMREQUEST，仅在 `if (req.method == "POST")` 分支设 POSTFIELDS。
# 故凡非 GET/POST 的动词（MKCOL/MOVE/DELETE/OPTIONS/HEAD/PROPFIND）经它发出
# 都会退回 curl 默认行为 → 一律 GET，且带体请求的 body 被丢弃。
# e2e 实测（改造前）：9 个请求服务端全部收到 GET，PUT 的 9 字节 body 变 0。
# qldox 不可改（项目约束），故 qlstik 侧自带 curl_multi 泵并由
# QNetworkAccessManager::issue() 分流；GET/POST 仍走 EventPoller（那边本来就对）。
STIKCOMMON_SOURCES += $$STIKCOMMON_DIR/qwebdavlite.cpp \
                      $$STIKCOMMON_DIR/qwebdavtransport.cpp

# ⚠ 2026-09-30 补记：qwebdavdirparserlite.cpp **原先根本没挂载**——上面
# STIKCOMMON_HEADERS 只登记了 .h，.cpp 漏了，于是 qlstik 应用构建里从没有这个
# 文件（build-qt3/ 与 build-qt6/ 下均无其 .o，实测确认）。此前所有「207 解析层
# 已完成、Qt3/Qt6 双门绿」的结论都只覆盖 /tmp 下独立探针（探针自己编这个 .cpp），
# **双门从未编译或链接过它**。这是验证缺口，不是「已完成」的真实状态。
#
# 现无条件挂载：2026-09-30 同时把类改成了真 QObject（加 Q_OBJECT + 原生
# finished()/errorChanged()，QObject 基类提供 deleteLater/disconnect/
# &QObject::destroyed/父子接管），否则 davbisync 这唯一真实消费者用不了它
# （实测 4 处全挂，见 qwebdavdirparserlite.h 顶部「为何现在有 QObject 基类」）。
# moc 由 qlstik.pro 的 CONFIG += moc 生成，头已在上面 HEADERS 段登记。
# 无条件挂载的前提是本 .cpp 已跨版本：其头已按 QT3_BUILD 条件包含小写/驼峰头。
STIKCOMMON_SOURCES += $$STIKCOMMON_DIR/qwebdavdirparserlite.cpp

# ── 批次 3c-2：davbisync 的本地贴纸库窄切面 ──
# davbisync.cpp 原本 include stickerstore.h 并有约 20 处 StickerStore::instance()
# 调用，而 stickerstore.cpp 有 4196 行且自身尚未适配 Qt3（属批次 5）。故抽出
# 只含那 9 个方法的接口，见 davlocalsource.h 顶部的依赖面实测。
STIKCOMMON_SOURCES += $$STIKCOMMON_DIR/davlocalsource.cpp
STIKCOMMON_HEADERS += $$STIKCOMMON_DIR/davlocalsource.h

# ── 批次 3c-2：davbisync 本体（SyncEngine）─────────────────────────────
# ⚠ 这两个文件是 **stikcommon 自己的移植副本**，不是 anystik 的原件。
# 原件 anystik/src/davbisync.{h,cpp} 保持零改动（2026-09-30 曾因直接改原件
# 导致 anystik 链接失败——原件依赖 vendor/qwebdav/*，而 vendor 那套已回滚、
# 且构建系统不允许为移植改 anystik/CMakeLists.txt）。
# 移植副本与原件的差异全部集中在 Qt 版本垫片层，逻辑逐行对应，改动记在
# davbisync.cpp 顶部的 QT3_BUILD 分支注释里。
# 依赖：davlocalsource（本地库窄切面）+ qwebdavlite + qwebdavdirparserlite
#      + qwebdavitemlite（均已挂载）+ davbisync_baseline（批次 2a，引用原件）
STIKCOMMON_SOURCES += $$STIKCOMMON_DIR/davbisync.cpp
STIKCOMMON_HEADERS += $$STIKCOMMON_DIR/davbisync.h

# ── 批次 3c-1 第五阶段：207 解析层（pugixml 后端，替代 QDom）────────────
# 注（2026-09-30）：下方 qwebdavdirparserlite.cpp 的挂载补记见「批次 3c-1 QWebdavLite」
# 段；DavLocalSource 属批次 3c-2，与本阶段的 davbisync 本体接入是两件事。
# 为何不用 QDom：Qt3 与 Qt5/6 的 QString/QDom 语义不一致，且都是**静默产生错误
# 数据**而非报错（utf8()/latin1() 共用静态转换缓冲区互相覆盖；QString(const char*)
# 在 Qt3 按 Latin-1 展开 UTF-8 字节；QDom::elementsByTagName 只比本地名、忽略命名
# 空间）。dav207pugi.cpp 全程 std::string，不过 Qt，两版行为一致。
# 第三方库本体在 anystik/vendor/pugixml（MIT，1.15），需把 vendor 根加进 include
# 路径才能让 "pugixml/pugixml.hpp" 解析（vendor/include 已在下面登记，但 pugixml
# 不在其下）。
STIKCOMMON_SOURCES += $$STIKCOMMON_DIR/dav207pugi.cpp \
                      $$ANYSTIK_SRC_DIR/../vendor/pugixml/pugixml.cpp
STIKCOMMON_HEADERS += $$STIKCOMMON_DIR/dav207iface.h
STIKCOMMON_INCLUDES += $$ANYSTIK_SRC_DIR/../vendor

# qdatetime_shim：qwebdavlite 的 put() 要发 RFC1123 Date 头（qDateTimeToUtc +
# qFormatDateTime）。该 .cpp 通篇是 Qt3 专用写法（QChar::upper / QString::lower /
# stripWhiteSpace / QDateTime::currentDateTime(Qt::TimeSpec) 等，Qt5+ 全被移除），
# 只能在 Qt3 编译；Qt4+ 侧这两个函数改由 qdatetime_shim.h 内的 inline 原生实现
# 提供（详见该头的版本分支注释），故 .cpp 仍按 isEmpty(QT_VERSION) 挂载。
#
# ⚠ 踩过的坑：曾把本行挪到无条件挂载（以为 .cpp 跨版本，实测 6 处 Qt3 写法），
#   结果 Qt6 侧编 qdatetime_shim.o 直接失败。凡「.cpp 挂不挂」要跟头里
#   「声明可见性」对齐，否则 Qt6 链接期必出 undefined reference。
isEmpty(QT_VERSION) {
    STIKCOMMON_SOURCES += $$STIKCOMMON_DIR/qdatetime_shim.cpp
}

# ── SSL 证书探测与 CA bundle（QWebdavLite 的自签证书接受链依赖）──
# qsslprobe：裸 socket + SSL_connect 取真实证书（不经 Qt3 缺失的 QSslSocket 能力）
# qcabundle：合并系统根证书与用户接受的自签证书，经 CURL_CA_BUNDLE 激活
isEmpty(QT_VERSION) {
    STIKCOMMON_SOURCES += $$STIKCOMMON_DIR/qsslprobe.cpp \
                         $$STIKCOMMON_DIR/qcabundle.cpp
}

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
