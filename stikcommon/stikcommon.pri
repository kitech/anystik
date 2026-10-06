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

# ══ 批次 1：myi18n / davobfus / eifreader ══
# eifreader 原为 1c 暂缓项（「Qt 3.5 的 QByteArray 是 typedef QMemArray<char>，
# 无法用垫片补 Qt4 方法」）。批次 5 复查后暂缓理由已不成立：
#   * eifreader.h:4-14 与 eifreader.cpp:5-19 都已有 #ifdef QT3_BUILD 双分支
#     include（上次会话铺的），Qt3 走 qcstring/qstring/qstringlist 小写头
#   * 其 QHash 依赖已有 stikcommon/qhash_shim.h（856 字节）兜住
#   * eifreader.cpp:3 的 compoundfilereader.h 在 anystik/vendor/include/，
#     482 行、Qt 符号 0 个（只 include <algorithm>/<stdint.h>/<string.h>/
#     <exception>/<stdexcept>/<functional>/<string>），头文件-only 且 Qt-free
#   * qlstik.pro 已含 -I../../anystik/vendor/include，include 路径无需新增
# 函数体里尚无 Qt3 分支，剩余的 Qt4 QByteArray 方法（constData/indexOf/
# reserve/append 等 8 处）由编译实测决定是否还需补垫，不预先下结论。
STIK_VENDOR_DIR = $$STIKCOMMON_DIR/../vendor

# myi18n：已物理迁入本目录（L2）；Qt3 侧靠 stikcommon/*_shim.h 兜类级缺口
STIKCOMMON_SOURCES  += $$STIKCOMMON_DIR/myi18n.cpp
STIKCOMMON_HEADERS  += $$STIKCOMMON_DIR/myi18n.h

# eifreader：批次 5 因 StickerStore 的 runInstallEif 依赖而提前解冻（见移植计划 §6.3j）
STIKCOMMON_SOURCES  += $$STIKCOMMON_DIR/eifreader.cpp
STIKCOMMON_HEADERS  += $$STIKCOMMON_DIR/eifreader.h

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
STIKCOMMON_SOURCES  += $$STIKCOMMON_DIR/davbisync_baseline.cpp
STIKCOMMON_HEADERS  += $$STIKCOMMON_DIR/davbisync_baseline.h

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
    STIKCOMMON_SOURCES  += $$STIKCOMMON_DIR/phonedb.cpp
    STIKCOMMON_HEADERS  += $$STIKCOMMON_DIR/phonedb.h
} else:!lessThan(QT_VERSION, 5.0.0) {
    STIKCOMMON_SOURCES  += $$STIKCOMMON_DIR/phonedb.cpp
    STIKCOMMON_HEADERS  += $$STIKCOMMON_DIR/phonedb.h
}

# ══ 批次 3a：imagetmpuploader（QNAM multipart/raw 上传 + QJson 解析）══
# Qt3 用 qnam_shim+qconnect_slots+qba_shim+qjson_shim；Qt6 走原生。Qt4 跳过——
# 与 phonedb 同因（QNetworkReply::finished 在 Qt4.8 是 protected Q_SIGNAL，
# 新式 connect 编不过），见上面 phonedb 注释与移植计划 §6.3b。
# 模块内 QT3_BUILD 分支收口点：kUploadReadMode（IO_ReadOnly）、utf8()、
# payload 用 QCString+setNum 拼（QByteArray=QMemArray<char> 无 operator+/number）、
# using ::connect；QByteArrayLiteral 由 qba_shim.h 归属 Qt3→QCString。
isEmpty(QT_VERSION) {
    STIKCOMMON_SOURCES  += $$STIKCOMMON_DIR/imagetmpuploader.cpp
    STIKCOMMON_HEADERS  += $$STIKCOMMON_DIR/imagetmpuploader.h
} else:!lessThan(QT_VERSION, 5.0.0) {
    STIKCOMMON_SOURCES  += $$STIKCOMMON_DIR/imagetmpuploader.cpp
    STIKCOMMON_HEADERS  += $$STIKCOMMON_DIR/imagetmpuploader.h
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
    STIKCOMMON_SOURCES  += $$STIKCOMMON_DIR/imageaiutil.cpp
    STIKCOMMON_HEADERS  += $$STIKCOMMON_DIR/imageaiutil.h
} else:!lessThan(QT_VERSION, 5.0.0) {
    STIKCOMMON_SOURCES  += $$STIKCOMMON_DIR/imageaiutil.cpp
    STIKCOMMON_HEADERS  += $$STIKCOMMON_DIR/imageaiutil.h
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
    STIKCOMMON_SOURCES  += $$STIKCOMMON_DIR/sitelistclient.cpp \
                           $$STIKCOMMON_DIR/imagesearchclient.cpp
    STIKCOMMON_HEADERS  += $$STIKCOMMON_DIR/sitelistclient.h \
                           $$STIKCOMMON_DIR/imagesearchclient.h
} else:!lessThan(QT_VERSION, 5.0.0) {
    STIKCOMMON_SOURCES  += $$STIKCOMMON_DIR/sitelistclient.cpp \
                           $$STIKCOMMON_DIR/imagesearchclient.cpp
    STIKCOMMON_HEADERS  += $$STIKCOMMON_DIR/sitelistclient.h \
                           $$STIKCOMMON_DIR/imagesearchclient.h
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
                       $$STIKCOMMON_DIR/qformatsniff_shim.h \
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
                      $$STIK_VENDOR_DIR/pugixml/pugixml.cpp
STIKCOMMON_HEADERS += $$STIKCOMMON_DIR/dav207iface.h
STIKCOMMON_INCLUDES += $$STIK_VENDOR_DIR

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

# ── 批次 5：QZipReader 分版本实现 ───────────────────────────────────────
# 需求：stickerstore.cpp:3715 用 QZipReader 解贴纸包。Qt5/6 用原生
# <QtCore/private/qzipreader_p.h>，Qt3 无此私有类（Qt3 把 zip 读写留在
# qlstik 不需要的 QZip/QZipSource 里，且 3.5.0 的 QZipReader 尚不存在）。
# 故 Qt3 侧用 stikcommon/qzipreader_shim.{h,cpp}——按 Qt 6.7 的
# src/corelib/io/qzip.cpp 逐函数移植的行为等价实现。
#
# 对拍证据（2026-10-01，~/ztprobe）：
#   同一份 parity.cpp 分别链接 Qt6 原生与 Qt3 shim，对 10 个 fixture
#   （normal / stored / symlink / dirs / traversal / comment / bomb /
#     corrupt / notzip / 不存在）× 2 种目标目录状态（预建 / 不预建）
#   输出 151 行，stdout **0 差异**：fileInfoList 的 filePath/isValid/
#   isDir/isFile/isSymLink/permissions/size/crc 全同，extractAll 返回值全同，
#   落盘后的树形结构与文件 mode 全同（含 Qt6 的既有怪癖：混合平铺/嵌套条目时
#   QString::left(-1) 返回整串导致给平铺文件建同名目录而写文件失败）。
#   唯一有意差异：Qt6 对损坏/非 zip 会 qWarning 两行诊断，shim 不打；
#   属 stderr 诊断输出，不影响行为。
#
# 顺带纠正了一处历史误判：Qt3 与 Qt6 的 QFileInfo::PermissionSpec **数值编码
# 不同**——Qt3 是八进制字面量（ReadOwner=04000=2048），Qt6 是十六进制
# （ReadOwner=0x4000=16384）。同一组 rwx 位在两侧数值不同（0x6600 vs 0xd80
# 即同一个 0o6600），比对时必须按语义而非数值。
#
# .h 无条件登记（moc/依赖扫描），.cpp 严格按版本挂载，避免 Qt5+ 重复定义
# QZipReader（范式对齐上面的 qdatetime_shim.cpp）。
STIKCOMMON_HEADERS += $$STIKCOMMON_DIR/qzipreader_shim.h
isEmpty(QT_VERSION) {
    STIKCOMMON_SOURCES += $$STIKCOMMON_DIR/qzipreader_shim.cpp
}

# ── 批次 5：QTemporaryFile 分版本实现 ─────────────────────────────────────
# 需求：stickerstore.cpp 的 verifyScaledResult（1155 行）与 buildGifBytes
# （1429 行）各建一个 QTemporaryFile 落盘缩放结果。QTemporaryFile 随 Qt 4.3
# 引入，Qt3.5 无此类。故 Qt3 侧用 stikcommon/qtemporaryfile_shim.h：按「模板名
# XXXXXX 段填 pid+时间戳+序号+随机数，用 QFile::exists() 探测唯一名」的思路
# 拼出等价行为，并补 Qt4+ 的 fileName()/setFileName() 成员名。
#
# 实测（Qt3.5 + /opt/qt338sh 真机，探针 /tmp/vtest/ttmp.cpp）：
#   · 模板 XXXXXX 被替换、fileName() 拿到路径、open()+write+flush+close 后
#     重新打开读回内容一致；
#   · setAutoRemove(false) 析构后文件仍在，默认 autoRemove=true 析构后消失；
#   · 无 XXXXXX 段的模板 open() 失败（与 Qt 原生一致）；
#   · 连开 50 个全部不重名。
# ⚠ 已知弱于 Qt 原生之处：Qt 原生用 open(O_CREAT|O_EXCL) 原子建文件，本垫片是
#   exists() 探测后再 open()，两步间有竞态窗口。批次 5 这两处都是单线程临时
#   文件（文件名含 pid+时间戳+序号），撞名概率可忽略；若日后出现并发/多线程
#   创建临时文件的场景，须改回 O_EXCL 原子建。
# ⚠ Qt3 的打开模式是 int + IO_* 宏（QIODevice::OpenMode 枚举类与 ReadWrite
#   成员是 Qt4 才有的），故本垫片 open() 收 int，与 qtemporaryfile_shim.h
#   文件头「不写模板」的原因同理（QFile 只有 open(int)，qfile.h:78）。
#
# .h 无条件登记（依赖扫描），内容整体由 QT_VERSION < 0x040300 门控，Qt4.3+
# 侧整块不参与编译，故无需 .cpp。
STIKCOMMON_HEADERS += $$STIKCOMMON_DIR/qtemporaryfile_shim.h

# ── 批次 5：QDirIterator 分版本实现 ──────────────────────────────────────
# 需求：stickerstore.cpp:3822 用 QDirIterator 递归收集 *.eif / *.EIF
# （「zip 里再套一层 eif」的兼容展开）。QDirIterator 随 Qt 4.2 引入，Qt3.5 无。
# Qt3 侧用 stikcommon/qdiriterator_shim.h：构造时一次性用 Qt3 QDir 递归收集。
#
# 关键差异与处置：
#   · Qt3 的 QFileInfoList 是 QPtrList<QFileInfo>（指针列表），迭代形态与 Qt4+
#     不同；故垫片改用 QDir::entryList() 取名再自建 QFileInfo。
#   · Qt3 QValueList::at(i) 返回迭代器而非元素，须用 operator[]。
#   · Qt3 的 QDir 无 NoDotAndDotDot（qdir.h:61），且其空 nameFilter 不匹配任何项，
#     递归列子目录须传 "*" 并显式跳过 "." / ".."；调用点的 QDir::NoDotAndDotDot
#     已在 stickerstore.cpp 去掉（对 "*.eif" 名字过滤等价）。
#
# 实测（Qt3.5 真机，探针 /tmp/vtest/tdir.cpp vs Qt6 原生 /tmp/vtest/tdir6.cpp）：
#   同一棵含嵌套/同名目录（dir.eif/）/非匹配后缀的树，两者输出文件列表 **0 差异**
#   （都找到 5 个，含递归进入不匹配名目录 dir.eif 内的 inside.eif）。
#
# .h 无条件登记（依赖扫描），内容整体由 QT_VERSION < 0x040200 门控，故无需 .cpp。
STIKCOMMON_HEADERS += $$STIKCOMMON_DIR/qdiriterator_shim.h

# ── 批次 5：QMimeDatabase / QMimeType 分版本实现 ─────────────────────────
# 需求：stickerstore.cpp 在 probeImageValidity 失败回退（1860）与成功后精化
# mime（1913）两处调用 mimeTypeForFile(QFileInfo) + isValid()/name()/comment()。
# 此二类随 Qt5.0 引入，Qt3.5 无。Qt3 侧用 stikcommon/qmimedatabase_shim.h：
#   · 优先级实测对齐 Qt6：**已知扩展名 > 内容幻数 > 无效**
#     （misnamed.gif 内为 PNG 字节→仍 image/gif；real.apng 内为普通 PNG→仍
#      image/apng；real.zzz 内为 PNG→image/png）。comment 串逐字取自 Qt6 真值。
#   · comment() 只在 name() 以 "image/" 开头时被读；非图片返回无效 QMimeType
#     对该调用点与 Qt6 的非 image/* 结果等价（走 else 分支）。
# 对拍证据（Qt3 shim vs Qt6.7.3 原生，/tmp/vtest/mime/tmime{,6}.cpp）：按调用点
# 可观测行为归一化（image/ 取 name+comment，否则 OTHER）后 **0 差异**。
#
# 另：Qt3 无 QFileInfo::suffix()（Qt4.0 引入），stickerstore 5 处 fi.suffix()
# 由 stikcommon/qfileinfo_shim.h 的 qFileInfoSuffix() 收口
# （实测 Qt6 suffix() ≡ Qt3 extension(false)）。
#
# .h 无条件登记（依赖扫描），内容由 QT_VERSION 门控（qmimedatabase < 0x050000；
# qfileinfo 仅内部两分支），故均无需 .cpp。
STIKCOMMON_HEADERS += $$STIKCOMMON_DIR/qmimedatabase_shim.h \
                      $$STIKCOMMON_DIR/qfileinfo_shim.h

# ── 批次 5：QClipboard / QMimeData / QGuiApplication 分版本实现 ───────────
# 需求：stickerstore.cpp 桌面端剪贴板读写 6 处（1969/2134/2607-2626/2666/
# 2722/2873）。Qt3 的 QClipboard 只有 data()/setData(QMimeSource*)（无
# mimeData()/setMimeData），且 QMimeData、QGuiApplication 均不存在（前者
# 4.2 引入、后者 5.0 引入），QClipboard 是 QObject 加不了成员。
# Qt3 侧用 stikcommon/qclipboard_shim.h：
#   · Qt3 版 QMimeData（public 继承 QMimeSource）承载 MIME→字节表 + QList<QUrl>
#     + QImage，实现 format()/encodedData() 供 setData；setImageData() 把位图编成
#     PNG 作为 "image/png" 格式（Qt3 PNG 是内建 codec，实测 save/load 通过）。
#   · Qt3Clipboard 包装真实 QClipboard 成 mimeData()/setMimeData()/image()/
#     setImage()；QGuiApplication 垫片 clipboard() 返回它，调用点零改动。
#   · qUrlToLocalFile/qUrlFromLocalFile：Qt3 QUrl 无 toLocalFile/fromLocalFile，
#     用 path()（已解码）与 QUrl(QString)（裸路径默认 file 协议）。
#
# 实测（Qt3.5 真机 Xvfb，/tmp/vtest/clip/*.cpp）：
#   · setData(QMimeSource*) 接管所有权：替换/clear 时会 delete 旧源（town3.cpp）。
#     故 setMimeData 只 new 后交 Qt3，自己不再 delete。
#   · setData 与 setImage 互斥（后清前）——动画路径把 PNG 回退作为格式写进
#     QMimeSource，而非另调 setImage。
#   · 图像剪贴板经 data() 源已自带 image/png（及 bmp/jpeg…），encodedData 返回真
#     PNG（timg3.cpp）。
#   · 端到端探针 tclipshim.cpp：formats=[image/gif|image/png|text/uri-list]，
#     gif 字节往返 len=7、png 回退 hdr=8950、urls 解析本地路径正确、setImage 4x4
#     读回、图像剪贴板 has image/png。Qt6 侧空操作（tinc6.cpp）通过。
#
# Qt4.2..4.x 只需 QGuiApplication 垫片（QClipboard 已有 mimeData），本头已分支；
# 仓库 stickerstore 不在 Qt4 构建。.h 无条件登记（依赖扫描），整块由 QT_VERSION 门控。
STIKCOMMON_HEADERS += $$STIKCOMMON_DIR/qclipboard_shim.h

# ── 批次 5：QCryptographicHash 分版本实现 ─────────────────────────────────
# 需求：stickerstore.cpp 用 QCryptographicHash 生成贴纸包/文件/URL 的稳定 ID，
# 全仓仅 5 处且只用 Md5/Sha1：fileIdFor(697)、packIdFromTitle(1609)、
# 安装包 ID(2240)、urlHex(2892) 走静态 hash().toHex()；fileMd5(3314) 走
# 增量 addData()+result()。QCryptographicHash 随 Qt 4.1 引入，Qt3.5 无。
#
# Qt3 侧用 stikcommon/qcryptographichash_shim.h：
#   · 枚举只实现 Md5/Sha1；静态 hash() 返回 Qt3HashBytes（公开继承 QByteArray），
#     补出 Qt3 的 QMemArray 没有的 toHex()（**小写**，对齐 Qt6）与 left()，
#     使 stickerstore.cpp 那 4 处 `.toHex().left(n)` 无需改调用形态。
#   · 增量路径走 md5.c 的 MD5_Init/Update/Final（**注意** md5 是
#     Final(digest,ctx)，sha1 是 Final(ctx,digest)，两者参数顺序相反）。
#   · 依赖 qlcomp/md5.c 提供 MD5_*（qlite.pri:19 已编，qlstik.pro:47 已 -I）。
#     故这里**不**把 md5.c 列入 SOURCES，否则 MD5_* 重复定义。
#   · sha1.c/sha1.h 是 Steve Reid 的 100% Public Domain SHA-1（上游
#     github.com/henryk/tinydnssec），全仓无既有 SHA-1，作为新文件在 Qt3 下编。
#
# QByteArrayView（Qt 5.10 引入）另有 stikcommon/qbytearrayview_shim.h，
# 只补 fileMd5(3317) 用到的 `QByteArrayView(const char*, int)` 形态。
#
# 实测（Qt3.5 + /opt/qt338sh 真机，探针 /tmp/vtest/thash.cpp 复刻 5 处调用点）：
#   · SHA1("abc")/MD5("abc") 与 sha1sum/md5sum 逐字节一致（小写）；
#   · 4 处静态调用点长度为 40/12/40/16，输出与系统 sha1sum/md5sum 一致；
#   · fileMd5 的 200000B 分块增量 == 一次性 hash；result() 可重入（不改状态）。
#
# .h 无条件登记（依赖扫描），内容整体由 QT_VERSION < 0x040100 门控；
# sha1.c 仅在 Qt3 侧编译。
STIKCOMMON_HEADERS += $$STIKCOMMON_DIR/qcryptographichash_shim.h \
                      $$STIKCOMMON_DIR/qbytearrayview_shim.h \
                      $$STIKCOMMON_DIR/sha1.h
isEmpty(QT_VERSION) {
    STIKCOMMON_SOURCES += $$STIKCOMMON_DIR/sha1.c
}

# ── 批次 5：QImageReader / QImage 分版本实现 ──────────────────────────────
# 需求：stickerstore.cpp 的图片管线全线依赖 QImageReader（构造 QIODevice*/QString
# 两型，setAutoTransform/setFormat/canRead/format/size/read/imageCount/
# jumpToNextImage/nextImageDelay/supportsOption(Animation)/error/errorString，
# 加静态 imageFormat/supportedImageFormats），以及 QImage 的
# Format_RGBA8888/convertToFormat/Format_ARGB32_Premultiplied/scaled(带 aspect 与
# transform 模式)/constBits/sizeInBytes/constScanLine。
#
# Qt3 完全没有 QImageReader（连 qimageio.h 都不存在，只有更早的 QImageIOHandler
# 体系），QImage 也没有 Format 概念（qimage.h:74 构造参数是 int depth）。
#
# Qt3 侧用两个垫片：
#   · qimage_shim.h（纯 .h）：qImageRgbaBytes() 把任意 QImage 转成 RGBA 字节序
#     QByteArray，qImageRgba() 转成 32 位带 alpha 图。**关键事实：Qt3 的 32 位
#     QImage 内存是 BGRA**（实测 fill(qRgba(0x11,0x22,0x33,0xff)) 得 row0=
#     33 22 11 ff），Qt6 的 Format_RGBA8888 则是 ff 00 00 ff，故 Qt3 无法造出
#     「内存即 RGBA」的 QImage。给编码器喂字节的地方（buildGifBytes:1122、
#     buildApngFromFrames:1293）必须改吃 qImageRgbaBytes() 而非 constBits()。
#   · qimagereader_shim.h/.cpp：完整 QImageReader + 最小 QImageIOHandler
#     （只需 Animation 枚举）。后端：png/jpeg/bmp/xpm/xbm/ppm/pbm/pgm 走 Qt3
#     原生 loadFromData；gif 走 vendor/libnsgif；apng 走 vendor/uc_apng_loader
#     （自带 stb_image 实现）；webp 走系统 libwebp 的 WebPAnimDecoder。
#
# 实测坑（均为真机 Xvfb 下先隔离验证再改的）：
#   · Qt3 的 <QList> 就是 qptrlist.h（QList=QPtrList，append 收 const T*，迭代器
#     解引用得指针），故 supportedImageFormats() 必须返回 QValueList<QByteArray>，
#     上层 `for (const QByteArray& f : fmts)` 才成立。
#   · Qt3 QByteArray 底层 QMemArray<char>，data() **不以 '\0' 结尾**，不能直接当
#     const char* 用。
#   · QImage::loadFromData 的 format 参数大小写敏感且各 handler 注册名不统一
#     （bmp 只认 "BMP"、jpeg 只认 "JPEG"、png 两者都认），故一律传 0 自动嗅探。
#   · Qt3 QSize 默认值是 (-1,-1) 而 isNull() 判 w==0&&h==0，故 (-1,-1).isNull()
#     为 false，不能拿 isNull() 当「头部尺寸未解析」标志，垫片另设 m_sizeParsed。
#   · libnsgif 的 get_rowspan 必须留 NULL（库内用 info.width×4）；填返回 0 的
#     回调会让所有像素行重叠。像素格式必须 NSGIF_BITMAP_FMT_R8G8B8A8
#     （FMT_RGBA8888 在小端内存是 A,B,G,R）。loop_max==0 是无限循环，必须按
#     info->frame_count 截断。
#   · uc_apng_loader 的 create_memory_loader 收 const char* 且**按值返回**
#     loader<std::istringstream>；静态 PNG 喂给它会抛异常，故须先用 acTL 块
#     扫描分流（pngIsApng）。不能定义 UC_APNG_LOADER_NO_EXCEPTION。
#
# 对拍结论（探针 /tmp/qim/final.cpp vs final6.cpp，样本 ~/ztprobe/img）：
#   · t3.gif / t.gif / d2.gif / big.gif(120 帧) / t.webp
#     逐帧 RGBA 像素 0 差异，逐帧 nextImageDelay() 0 差异，imageCount/size 一致。
#   · t3.apng：Qt3 垫片 3 帧逐帧与 **PIL** 像素 0 差异、延迟 120/80/200ms 与 PIL
#     duration 完全一致；首帧与 Qt6 首帧 0 差异。
#   · ⚠ APNG 平台差异（有意为之）：Qt6 的 PNG 插件把 APNG 拍平，format()="png"、
#     supportsOption(Animation)=false、imageCount()=1、只吐 1 帧（实测 6.x）。
#     垫片刻意沿用同样的 format()/Animation 值以保持控制流一致，但 read() +
#     jumpToNextImage() 能吐全部帧，故 **Qt3 上 APNG 缩放复制是真动画，Qt6 上
#     会回退静态首帧**。这是用户明确要求的方向，不是 bug。
#
# .h 无条件登记（依赖扫描），内容整体由 QT_VERSION < 0x040000 门控；
# .cpp 与 libnsgif 的 C 源、libwebp 链接仅在 Qt3 侧挂载。
STIKCOMMON_HEADERS += $$STIKCOMMON_DIR/qimage_shim.h \
                      $$STIKCOMMON_DIR/qimagereader_shim.h
isEmpty(QT_VERSION) {
    STIKCOMMON_SOURCES += $$STIKCOMMON_DIR/qimagereader_shim.cpp
    # libnsgif 需以 C99 单独编译：其源用 // 注释与 stdint，且 gif.c/lzw.c 互调
    STIKCOMMON_SOURCES += $$STIK_VENDOR_DIR/libnsgif/gif.c \
                          $$STIK_VENDOR_DIR/libnsgif/lzw.c
    STIKCOMMON_INCLUDES += $$STIK_VENDOR_DIR/libnsgif \
                           $$STIK_VENDOR_DIR/uc_apng_loader
    # WebP 动画解码需要 demux；mux 供 WebP 重编码（stickerclipboard.cpp
    # qClipEncodeWebp 的 WebPAnimEncoder）
    LIBS += -lwebp -lwebpdemux -lwebpmux -lm
} else:!lessThan(QT_VERSION, 5.0.0) {
    # Qt5/6：动图 WebP 重编码（stickerclipboard.cpp qClipEncodeWebp）。编 WebP 只
    # 需 mux+encode，解码由 Qt 自带 libwebp 插件负责。⚠ -lwebpmux 与 -lwebp
    # **必须成对**：只给 -lwebpmux 会 `DSO missing from command line`
    # （libwebpmux 依赖 libwebp 的符号，§18.10 第 4 项）。
    LIBS += -lwebpmux -lwebp
}

# davobfus 的 libobfuscate 在 vendor；共享模块已全部物理迁入本目录，不再需要 anystik/src
STIKCOMMON_INCLUDES += $$STIK_VENDOR_DIR/include

STIKCOMMON_CFLAGS = $$MYVENDOR_CFLAGS
