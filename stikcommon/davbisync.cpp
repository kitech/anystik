#include "davbisync.h"
// 本地贴纸库走 DavLocalSource 窄切面，不再直接依赖 stickerstore.h
// （stickerstore.cpp 4196 行且自身未适配 Qt3，属批次 5；本移植只取 9 个方法）
#include "davlocalsource.h"
#include "qwebdavlite.h"
#include "qwebdavdirparserlite.h"
#include "qwebdavitemlite.h"

#ifdef QT3_BUILD
// Qt 3.5 没有 QCoreApplication（qApp 在 qapplication.h），也没有独立的
// qnetworkrequest.h / qnetworkreply.h —— QNAM 三件套由 qnam_shim.h 提供。
#include "qconnect_slots.h"
#include "qdatetime_shim.h"
#include "qnam_shim.h"
#include "qdebug_shim.h"
#include <qapplication.h>
#include <qdatetime.h>
#include <qtimer.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qdir.h>
#include <qurl.h>
#include <qpair.h>
#include <qobject.h>
#else
#include <QCoreApplication>
#include <QDateTime>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QPair>
#include <QNetworkRequest>
#include <QNetworkReply>
#endif

#include <memory>
#include <vector>

#include <sys/stat.h>
#ifdef QT3_BUILD
#include <utime.h>
#endif

namespace {

QString formatBytes(qint64 n)
{
    static const char* units[] = { "B", "KB", "MB", "GB" };
    double v = double(n);
    int u = 0;
    while (v >= 1024.0 && u < 3) {
        v /= 1024.0;
        ++u;
    }
    if (u == 0)
        return QStringLiteral("%1 B").arg(qint64(v));
    return QStringLiteral("%1 %2").arg(v, 0, 'f', 1)
        .arg(QString::fromLatin1(units[u]));
}

QString formatDuration(qint64 msec)
{
    const qint64 s = msec > 0 ? msec / 1000 : 0;
    const qint64 h = s / 3600;
    const qint64 m = (s % 3600) / 60;
    const qint64 ss = s % 60;
    if (h > 0) {
        return QStringLiteral("%1:%2:%3").arg(h)
            .arg(m, 2, 10, QLatin1Char('0')).arg(ss, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(m)
        .arg(ss, 2, 10, QLatin1Char('0'));
}

// Qt 3.5 的 QString 只有 find()/findRev()，没有 indexOf()/lastIndexOf()；
// 也没有 split(QChar)。本移植这几处调用都只用到 QChar 重载，在此收口。
// 语义对齐 Qt4+：indexOf→find、lastIndexOf→findRev。
// Qt 3.5 的 QString 缺 size()/clear()；QStringList 缺 removeFirst()；
// QFileInfo 缺 path()。这些在本移植的调用点语义都唯一确定（QString::size 与
// length() 等价、clear 等于置空、removeFirst 等于 removeAt(0)），故就地收口。
// 刻意不做成 qstring_shim.h 的成员垫片：QString::clear() 若在垫片里以
// 成员形式注入，会和 QString::operator= 的拷贝语义在其它调用点相互干扰，
// 改调用点更可控。
#ifdef QT3_BUILD
inline int strSize(const QString& s) { return static_cast<int>(s.length()); }
inline void strClear(QString& s) { s = QString(); }
#else
inline int strSize(const QString& s) { return static_cast<int>(s.size()); }
inline void strClear(QString& s) { s.clear(); }
#endif

// QFileInfo::path() 在 Qt3 只有 dirPath()（同样含文件名分隔符语义一致）
#ifdef QT3_BUILD
inline QString fileInfoPath(const QFileInfo& fi) { return fi.dirPath(); }
#else
inline QString fileInfoPath(const QFileInfo& fi) { return fi.path(); }
#endif

// 解析 HTTP Last-Modified（RFC 1123 格式）：Qt3 走 qdatetime_shim 的
// qParseRfc1123DateTime；Qt4+ 用原生 QDateTime::fromString(s, Qt::RFC2822Date)
// ——该枚举名虽叫 RFC2822，Qt 实际按 RFC1123 解析（Qt 源码 qdatetime.cpp 的
// rfc2822_parse），与 HTTP 头格式一致。
inline QDateTime parseHttpDate(const QString& s)
{
#ifdef QT3_BUILD
    return qParseRfc1123DateTime(s);
#else
    return QDateTime::fromString(s, Qt::RFC2822Date);
#endif
}

// QByteArray → 整数：Qt3 的 QByteArray 就是 QMemArray<char>，既无 toLongLong
// 也无 toInt。走 QString 转换（Qt3 的 QString(const char*) 按 Latin-1 逐字节，
// 对纯数字头值安全），Qt4+ 直接用原生 toLongLong。
inline qint64 rawHeaderToLongLong(const QByteArray& v)
{
#ifdef QT3_BUILD
    return QString(v).toLongLong();
#else
    return v.toLongLong();
#endif
}

// QIODevice 打开模式：Qt3 叫 IO_ReadOnly / IO_WriteOnly / IO_Truncate
// （qdevice.h 顶层宏，枚举值与 Qt4+ 的 QIODevice::ReadOnly 等一致）。
// 刻意不做成员垫片：3b 的 imagetmpuploader 已用同样的匿名空间常量
// （kImgReadMode）处置同一处，保持全项目一致口径。
#ifdef QT3_BUILD
// 注意 Qt6 的 QIODevice::OpenMode 是 enum class（QIODeviceBase::OpenModeFlag），
// 隐式转 int 会报 invalid conversion from int to OpenModeFlag，故类型随版本走。
typedef int DavOpenMode;
const DavOpenMode qDavReadOnly = IO_ReadOnly;
const DavOpenMode qDavWriteOnly = IO_WriteOnly;
const DavOpenMode qDavTruncate = IO_Truncate;
#else
typedef QIODevice::OpenMode DavOpenMode;
const DavOpenMode qDavReadOnly = QIODevice::ReadOnly;
const DavOpenMode qDavWriteOnly = QIODevice::WriteOnly;
const DavOpenMode qDavTruncate = QIODevice::Truncate;
#endif

// QStringList 删单个元素：Qt3 的 QStringList 派生 QValueList，只有 remove()
// （删全部匹配值），既无 removeFirst() 也无 removeOne()。本移植的调用点
// 语义都是「删掉唯一匹配项」，故统一成删首个匹配项。
inline void stringListRemoveOne(QStringList& l, const QString& v)
{
    // 用迭代器而非下标：Qt3 的 QStringList 就是 QValueList<QString>，**没有**
    // at()（实测 at() 在 Qt3 不存在，qvaluelist.h:553 只有 operator[]），
    // 也没有 removeAt()（qvaluelist.h:512 提供的是 erase(iterator)）。
    // Qt6 的 QList 两者都有。为免两版分叉，统一走迭代器接口。
    for (QStringList::iterator it = l.begin(); it != l.end(); ++it) {
        if (*it == v) {
            l.erase(it);
            return;
        }
    }
}

// QStringList 删首元素：两版都用 erase(begin())，因为 Qt6 的 QList 已无
// remove(const T&)（那是 Qt5 时代接口），而 Qt3 的 remove() 语义是「删全部匹配值」。
inline void stringListRemoveFirst(QStringList& l)
{
    if (!l.isEmpty()) {
        l.erase(l.begin());
    }
}

// qTempPath()：Qt3 的 QDir 只有 static homePath()/rootPath()，**无**
// tempPath()（实测 grep qdir.h 无该声明，Qt 4.2 才加）。按 POSIX 约定取
// 环境变量 TMPDIR，未设则 /tmp —— 与 Qt4+ tempPath() 的取值一致。
inline QString qTempPath()
{
#ifdef QT3_BUILD
    const char* const td = ::getenv("TMPDIR");
    if (td && *td) {
        // Qt3 的 QByteArray 就是 QMemArray<char>，其 (const char*, uint) 构造是
        // protected（实测报 protected within this context），只能经 QCString 隐式转。
        return QString(QCString(td));
    }
    return QString::fromLatin1("/tmp");
#else
    return qTempPath();
#endif
}

// QIODevice::write()：Qt3 只有 writeBlock()（qfile.h:88）。QFile 亦同。
inline qint64 devWrite(QIODevice* dev, const QByteArray& data)
{
#ifdef QT3_BUILD
    return static_cast<qint64>(dev->writeBlock(data));
#else
    return static_cast<qint64>(dev->write(data));
#endif
}

// 设文件 mtime：Qt3 **完全没有** QFileDevice/QFile::setFileTime（Qt 4.1 才加），
// 也没有 QFile::setLastModified —— 实测 grep 整个 Qt3 头目录，setLastModified
// 只出现在 qurlinfo.h（与 QFile 无关的类）。故 Qt3 走 POSIX ::utime()。
// 头是 <utime.h> 而非 <sys/utime.h>：现代 glibc 已移除后者（实测 -fsyntax-only
// 报 fatal error: sys/utime.h: No such file or directory）。
#ifdef QT3_BUILD
inline bool fileSetMsecPosix(const QString& path, qint64 msec)
{
    // Qt3 的 QString 只有 local8Bit()（Qt4 才改名 toLocal8Bit），
    // QByteArray（Qt3 的 QMemArray）也没有 constData()（Qt4 才加），
    // 只能取 operator[] 或经 QCString::data()。
    const QCString cs = path.local8Bit();
    const char* const p8 = cs.data();
    struct stat st;
    if (::stat(p8, &st) != 0) {
        return false;
    }
    struct utimbuf ub;
    ub.actime = st.st_atime;
    ub.modtime = static_cast<time_t>(msec / 1000);
    return ::utime(p8, &ub) == 0;
}
#endif // QT3_BUILD

inline bool fileSetMsec(QFile& f, qint64 msec)
{
#ifdef QT3_BUILD
    return fileSetMsecPosix(f.name(), msec);
#else
    return f.setFileTime(QDateTime::fromMSecsSinceEpoch(msec),
                         QFileDevice::FileModificationTime);
#endif
}

// 按 UTC 解释 msec（下载后把本地 mtime 回写成云端 last-modified）。原码显式带
// Qt::UTC，这个区别不能丢：否则非 UTC 时区下每次同步都会误判「本地变了」而空转重传。
// Qt3 的 ::utime 收 epoch 秒，本身即 UTC 语义，故与 fileSetMsec 同实现。
inline bool fileSetMsecUtc(QFile& f, qint64 msec)
{
#ifdef QT3_BUILD
    return fileSetMsecPosix(f.name(), msec);
#else
    return f.setFileTime(QDateTime::fromMSecsSinceEpoch(msec, Qt::UTC),
                         QFileDevice::FileModificationTime);
#endif
}

// QUrl::setScheme()：Qt3/Qt4 叫 setProtocol()，Qt5 起改名 setScheme。
// 语义相同（qwebdavlite.cpp:118-121 已按版本分支处置同一处）。
inline void urlSetScheme(QUrl& u, const QString& scheme)
{
#ifdef QT3_BUILD
    u.setProtocol(scheme);
#else
    u.setScheme(scheme);
#endif
}

int strIndexOf(const QString& s, const QChar& c)
{
#ifdef QT3_BUILD
    return s.find(c);
#else
    return s.indexOf(c);
#endif
}

int strLastIndexOf(const QString& s, const QChar& c)
{
#ifdef QT3_BUILD
    return s.findRev(c);
#else
    return s.lastIndexOf(c);
#endif
}

// 按 QChar 切分并丢弃尾部空段，等价 Qt4+ 的 split(QChar, SkipEmptyParts)。
// 刻意不写进 qstring_shim.h：Qt3 另有 split(const QString&, flags)，语义
// （是否保留空段）与 Qt4 的 split(QChar) 不同，误垫会静默改变解析结果。
QStringList splitSkipEmpty(const QString& s, const QChar& sep)
{
    QStringList out;
    const int n = static_cast<int>(s.length());
    int start = 0;
    for (int i = 0; i <= n; ++i) {
        if (i == n || s.at(i) == sep) {
            if (i > start) {
                out.append(s.mid(start, i - start));
            }
            start = i + 1;
        }
    }
    return out;
}

// QDateTime 毫秒 epoch：Qt3 的 QDateTime 只有 toString/fromString（无毫秒精度 API），
// 走 qdatetime_shim.h 的 qDateTimeToMsecs/qNowMsecs（内部用 QDate/QTime 拼算）。
// 刻意不做成员垫片：Qt4 起 toMsecsSinceEpoch 是 Qt4.1 才加的，Qt4.0 也没有，
// 成员注入会掩盖「本移植最低支持 Qt4.1」这个前提。
inline qint64 dtMsec(const QDateTime& dt)
{
#ifdef QT3_BUILD
    return qDateTimeToMsecs(dt);
#else
    return dt.toMSecsSinceEpoch();
#endif
}

// 本地文件 mtime → epoch 毫秒。
//
// ⚠ 不能写成 dtMsec(fi.lastModified())：qdatetime_shim 的约定是「所有
//   QDateTime 按 UTC 墙钟存放」，dtMsec() 据此把墙钟直接当 UTC；但 Qt3 的
//   QFileInfo::lastModified() 返回的是**本地墙钟**（实测：mtime=1704067200
//   的文件，lastModified().toString() 是 "Mon Jan 1 08:00:00 2024"，
//   本机 +0800）。两者叠加，本地 mtime 整体偏 +28800 秒，与云端
//   getlastmodified 的真 UTC 永远不相等 —— 于是「size 同且两侧 mtime 亦同
//   → 跳过」永不成立，空基线下被判成「双侧皆变」→ 冲突 → MOVE 改名
//   .conflictN + 重传（实测 e2e 就是这么打出 MOVE 的）。
//
//   toTime_t() 自己按本地墙钟解释并返回 epoch，正好绕开 shim 的 UTC 约定；
//   毫秒用 QTime::msec() 补（HTTP 头只有秒精度，fs 有纳秒但 Qt3 只暴露到秒）。
//   ⚠ Qt5 起 toTime_t() 被弃用、Qt6 已删除，故必须分版本：Qt4+ 的
//   lastModified() 自带 timespec，toMSecsSinceEpoch() 直接就是真 epoch。
inline qint64 localMtimeMsec(const QFileInfo& fi)
{
    const QDateTime dt = fi.lastModified();
    if (!dt.isValid()) {
        return 0;
    }
#ifdef QT3_BUILD
    return (qint64)dt.toTime_t() * 1000LL + (qint64)dt.time().msec();
#else
    return dt.toMSecsSinceEpoch();
#endif
}

inline qint64 nowMsec()
{
#ifdef QT3_BUILD
    return qNowMsecs();
#else
    return QDateTime::currentMSecsSinceEpoch();
#endif
}

// rclone num 风格冲突保留文件判定：file.conflict<digits>
bool isConflictRel(const QString& rel)
{
    const int dot = strLastIndexOf(rel, QLatin1Char('.'));
    if (dot <= 0) {
        return false;
    }
    const QString suff = rel.mid(dot + 1);
    if (!suff.startsWith(QLatin1String("conflict")) || strSize(suff) == 8) {
        return false;
    }
    const QString digits = suff.mid(8);
    if (digits.isEmpty()) {
        return false;
    }
    // Qt3 的 QString 无 begin()/end()，range-for 编不过，改下标遍历
    for (int i = 0; i < static_cast<int>(digits.length()); ++i) {
        if (!digits.at(i).isDigit()) {
            return false;
        }
    }
    return true;
}

} // namespace

SyncEngine::SyncEngine(QObject* parent)
    : QObject(parent)
{
}

SyncEngine::~SyncEngine()
{
    qInfo() << "[sync] ~SyncEngine" << this << "destroyed";
}

// ★ 名字隐藏，不是重载决议：SyncEngine 是 QObject 子类，函数体内的 `connect`
//   先在类作用域找到 QObject::connect **成员**并隐藏掉全局的自由函数模板，
//   qconnect_slots.h 的 PMF 重载就再也进不了候选集（实测报
//   `no matching function for call to 'SyncEngine::connect(QNetworkReply*&,
//    void (QNetworkReply::*)(), ...)'`）。
//   全局 `using ::connect;` **无效**——成员函数体内的名字查找先命中类作用域。
//   必须在每个用到 PMF connect 的函数体**内部**引入（与 qwebdavlite.cpp:289
//   在 attachErrorBridge 体内做的同一处置）。
#ifdef QT3_BUILD
#define DAV_SYNC_JOIN_CONNECT  using ::connect;
#else
#define DAV_SYNC_JOIN_CONNECT
#endif

// 每轮新建 QWebdavLite：上一轮 abort 后 QNAM 的 keep-alive 连接/auth 会话可能已坏死，
// 复用会让本轮首个请求永久挂起；新建 = 与"重启 app"等价的干净连接池。
void SyncEngine::createConnection()
{
    if (m_webdav) {
        disconnect(m_webdav, nullptr, this, nullptr);
        m_webdav->deleteLater();
    }
    m_webdav = new QWebdavLite(this);
    qInfo() << "[sync] QWebdavLite created" << m_webdav;
#ifdef QT3_BUILD
    connect(m_webdav, SIGNAL(destroyed(QObject*)), this, SLOT(onChildDestroyed(QObject*)));
#else
    connect(m_webdav, &QObject::destroyed, this,
            [this](QObject* w) { onChildDestroyed(w); });
#endif
    m_webdav->setConnectionSettings(
        (m_connectionType == 2) ? QWebdavLite::HTTPS : QWebdavLite::HTTP,
        m_host, m_rootPath, m_username, m_password, m_port);
    // 空闲超时：仅在连续无字节传输时计时，且随上传/下载进度重置；大图留余量
    m_webdav->setTransferTimeout(90000);
    // 暴露被吞掉的认证/SSL 等内部原因。
    // QWebdavLite 刻意不 moc（见 qwebdavlite.h 说明），Qt6 下对无 Q_OBJECT 的类
    // 做 PMF connect 会触发 qobject.cpp 的静态断言。Qt3 侧走 qwebdavlite.h
    // 末尾那个 connect 重载（拉姆达进 slots_errorChanged），Qt6 走订阅口；
    // 两者都是**非 moc 伪信号**，故此处不能用 SIGNAL/SLOT 字符串形式。
    m_webdav->subscribeErrorChanged([this](const QString& e) {
        onWebdavError(e);
    });
}

// QWebdavLite 的 errorChanged 落点（两版都经 subscribeErrorChanged 转发到此）
void SyncEngine::onWebdavError(const QString& e)
{
    log(davbisync::Warn, QStringLiteral("webdav"), e);
}

// 子对象析构日志（两版共用）
void SyncEngine::onChildDestroyed(QObject* w)
{
    qInfo() << "[sync] child destroyed" << w;
}

// 每轮新建 parser：旧 parser 的连接/内部 reply 可能残留，拆旧建新彻底隔离
void SyncEngine::createParser()
{
    if (m_parser) {
        disconnect(m_parser, nullptr, this, nullptr);
        m_parser->deleteLater();
    }
    m_parser = new QWebdavDirParserLite(this);
    qInfo() << "[sync] parser created" << m_parser;
    // ★ 包含顺序是硬要求：qmap_shim.h 末尾 #define QMap qlstik_qt3::QMapShim
    //   （文本级替换），必须排在**所有** Qt 头之后。若把 `using ::connect;`
    //   放在 qmap_shim.h 之前，Qt3 的 `signals` 宏展开与宏改写互相干扰。
    //   （也正是本函数上一版的编译错误形态，故显式记下来。）
#ifdef QT3_BUILD
    // Qt 3.5 的 `signals` 展开成 `protected`（qobjectdefs.h:92），
    // 而 QWebdavDirParserLite 的 signals: 段在 moc 生成的元对象里可被外部
    // 取址；字符串形式不经过访问检查，Qt6 则用原生 PMF。
    connect(m_parser, SIGNAL(destroyed(QObject*)), this, SLOT(onChildDestroyed(QObject*)));
    connect(m_parser, SIGNAL(finished()), this, SLOT(onParserFinished()));
    // ⚠ QString 必须写成 `const QString&`，**不能**简写成 `QString`。
    //   Qt 3.5 的 SIGNAL/SLOT 字符串形式拿 moc 存的签名做**逐字符**比对，
    //   不做类型归一化（Qt5+ 才会把 const T& 归一成 T）。实测（qt3sig/t）：
    //   SIGNAL(foo(int,QString)) → "No such signal"，connect 返回 false；
    //   SIGNAL(foo(int,const QString&)) → OK。Qt4+ 两种写法都能过，
    //   但这里必须写全限定式，否则 Qt3 侧静默断线。
    connect(m_parser, SIGNAL(errorChanged(const QString&)), this,
            SLOT(onParserError(const QString&)));
#else
    connect(m_parser, &QObject::destroyed, this,
            [this](QObject* p) { onChildDestroyed(p); });
    connect(m_parser, &QWebdavDirParserLite::finished, this,
            [this]() { onParserFinished(); });
    connect(m_parser, &QWebdavDirParserLite::errorChanged, this,
            [this](const QString& line) { onParserError(line); });
#endif
}

// parser finished 落点：两版都汇到这里，逻辑只此一份
void SyncEngine::onParserFinished()
{
    if (!m_running) return;
    if (m_scanCurrentDir.isEmpty()) return;
    collectCloudItems();
}

// parser errorChanged 落点：只记日志、不中断同步（沿用原语义）
void SyncEngine::onParserError(const QString& line)
{
    // 云端列表解析失败必须**让本次同步失败退出**，不能只 warn。
    //
    // 原因：QWebdavDirParserLite 解析失败时不会给出任何条目，于是
    // m_cloudFiles 为空 → buildOpQueue() 把每个本地文件都判成「云端没有」
    // → 全量重传：已存在的云端文件被无谓覆盖，目录被重新 MKCOL，
    // 冲突判定也全部失效（基线看着是空的，双侧都算「变」）。
    // 在「看不见云端」的前提下做写入是破坏性的，宁可失败退出。
    //
    // 这不是假想：207 曾因 ns0: 前缀不被识别、以及响应体丢掉末字节（根元素
    // 的 '>'）而解析失败，而当时 onParserError 只 warn，现象是引擎安静地
    // 把整个云端当成空目录重传了一遍。
    finishWithError(QStringLiteral("cloud listing failed: %1").arg(line));
}

bool SyncEngine::isCloudAvailable() const
{
    return m_webdav != nullptr && m_parser != nullptr;
}

bool SyncEngine::setConnectionSettings(int connectionType,
                                       const QString& hostname,
                                       const QString& rootPath,
                                       const QString& username,
                                       const QString& password,
                                       int port,
                                       bool useSsl)
{
    m_host = hostname;
    m_rootPath = rootPath;
    m_username = username;
    m_password = password;
    m_port = port;
    m_connectionType = connectionType;
    m_useSsl = useSsl;
    return true;
}

bool SyncEngine::startSync()
{
    if (m_running) {
        return false;
    }
    createConnection();
    createParser();
    if (!m_webdav || !m_parser) {
        return false;
    }
    m_running = true;
    m_finished = false;
    m_percent = 0;
    m_startMsec = nowMsec();
    m_fileStartMsec = m_startMsec;
    m_lastFileMs = 0;

    m_serverProbed = false;
    strClear(m_davCap);
    strClear(m_allowMethods);
    strClear(m_serverName);
    m_cloudMtimeAvail = false;
    m_putMtimeEcho = false;
    m_cloudMtimeDecided = false;
    m_putEchoDecided = false;

    m_cloudReady = false;
    m_cloudFiles.clear();
    m_localFiles.clear();
    m_cloudToLocal.clear();
    m_scannedCloudDirs.clear();
    m_pendingDirs.clear();
    strClear(m_scanCurrentDir);
    m_cloudFileCount = 0;
    m_localFileCount = 0;
    m_localDirPacks.clear();
    m_diagnosis = davbisync::SyncDiagnosis();
    davbisync::baselineLoad(&m_base);
    m_uploadQueue.clear();
    m_downloadQueue.clear();
    m_pendingCloudRenames.clear();
    m_ensuredDirs.clear();
    m_uploadIndex = 0;
    m_uploadDone = 0;
    m_uploadSkipped = 0;
    m_downloadSkipped = 0;
    m_uploadTotal = 0;
    m_downloadDone = 0;
    m_downloadIndex = 0;
    m_downloadFailed = 0;
    m_totalJobs = 0;
    m_jobsDone = 0;
    m_mkdirChain.clear();
    // 清余：断开 + 释放上一会话在途 reply，杜绝其迟到回调污染新一轮
    for (QNetworkReply* r : m_activeReplies) {
        if (r) {
            disconnect(r, nullptr, this, nullptr);
            r->abort();
            r->deleteLater();
        }
    }
    m_activeReplies.clear();
    m_tempFiles.clear();

    emit progressUpdated(0, QStringLiteral("scan"),
                         QStringLiteral("listing local packs"));

    // 阶段 A：本地清单（同步，SQLite + stat）
    buildLocalListing();

    // 阶段 B：云端清单（异步逐目录）
    // 扫描根 = 业务 dbRel 空串（cloudPath 拼出云根 anystik）
    scanCloudDir(QString());
    return true;
}

void SyncEngine::abort()
{
    if (!m_running || m_finished) {
        return;
    }
    m_running = false;
    if (m_parser) {
        m_parser->abort();
    }
    // 取消在途网络请求：断钩防迟到回调，abort + 释放
    for (QNetworkReply* r : m_activeReplies) {
        if (r) {
            disconnect(r, nullptr, this, nullptr);
            r->abort();
            r->deleteLater();
        }
    }
    m_activeReplies.clear();
    for (const QString& t : m_tempFiles) {
        QFile::remove(t);
    }
    m_tempFiles.clear();
    emit progressUpdated(m_percent, QStringLiteral("cancelled"), QString());
    emit finished(davbisync::FinishCancelled, QStringLiteral("cancel requested"));
    m_finished = true;
}

void SyncEngine::setPollingDelay(int ms)
{
    Q_UNUSED(ms);
}

void SyncEngine::setPackPolicies(
    const QMap<QString, davbisync::PackPolicy>& policies)
{
    m_packPolicies = policies;
}

void SyncEngine::setRunning(bool running)
{
    m_running = running;
}

// ═══════════════════════════════════════════════════════════════════
// 阶段 A：本地清单
// ═══════════════════════════════════════════════════════════════════

void SyncEngine::buildLocalListing()
{
    m_localFiles.clear();
    m_cloudToLocal.clear();
    m_localDirPacks.clear();
    m_localFileCount = 0;

    // packs(1, "title ASC") → DavLocalSource 的窄接口已内置 title 升序，
    // 调用点无需再传排序串；list 容器按既定原则用 std::vector（Qt3 的 QList
    // 只是 QPtrList 指针宏，存不了值类型）。
    const auto packs = davLocalSource()->installedPacksByTitleAsc();
    int dirs = 0;
    for (const auto& pack : packs) {
        if (davLocalSource()->isBuiltinSourcePack(pack.id, pack.title)) {
            log(davbisync::Info, QStringLiteral("pack"),
                QStringLiteral("skip builtin-source pack: %1").arg(pack.title));
            continue;
        }
        // M5：包级策略（空 map = 默认全参与）
        const davbisync::PackPolicy policy =
            m_packPolicies.value(pack.id);
        if (m_packPolicies.contains(pack.id) && !policy.participates()) {
            log(davbisync::Info, QStringLiteral("pack"),
                QStringLiteral("skip by policy (pull&push off): %1")
                    .arg(pack.title));
            m_diagnosis.skipped.append(pack.title);
            continue;
        }
        const auto stickers = davLocalSource()->stickersOfPack(pack.id);
        // 云 rel = DB 存储的相对路径直通（本地存储 root ≙ 云根，1:1 透传，
        // 不再以目录末段转译云目录名）：
        //   file_path=pastes/x           → 业务键/云 rel = pastes/x（云上 anystik/pastes/x）
        //   file_path=packs/<T>/x        → 业务键/云 rel = packs/<T>/x（云上 anystik/packs/<T>/x）
        // anystik/ 前缀只在 cloudPath()/cloudUrl()（唯一入云拼根点）临时出现，不落任何持久数据。
        QString relDir;
        // 容器是 std::vector（无 constFirst），取首个元素
        if (!stickers.empty()) {
            // DavLocalSticker.filePath 为 resolve 后的绝对路径 → 归一到 dbRel 父目录
            relDir = fileInfoPath(QFileInfo(davLocalSource()->relativeToBase(
                                   stickers.front().filePath)));
        }
        if (!relDir.isEmpty()) {
            m_localDirPacks.insert(relDir, pack.id);
        }
        if (m_packPolicies.contains(pack.id) && !policy.pushEnabled) {
            continue;    // 不上传（pull 方向仍可）
        }
        log(davbisync::Info, QStringLiteral("diag"),
            QStringLiteral("upload-candidate %1")
                .arg(davLocalSource()->builtinSourceDiag(
                         pack.id, pack.title)));
        qInfo().noquote() << "[sync] diag upload-candidate"
                          << davLocalSource()->builtinSourceDiag(
                                 pack.id, pack.title);
        for (const auto& s : stickers) {
            // 业务键 dbRel = 相对 base 的路径（StickerBrief.filePath 已是绝对路径，
            // 勿再 resolve；用 relativeToBase 归化，与云端 canonicalRel 键对称）
            const QString localAbs = s.filePath;
            const QString rel = davLocalSource()->relativeToBase(localAbs);
            const QFileInfo fi(localAbs);
            if (!fi.exists() || !fi.isFile()) {
                continue;
            }
            davbisync::BaselineEntry e;
            e.size = fi.size();
            e.mtimeMsec = localMtimeMsec(fi);
            m_localFiles.insert(rel, e);
            m_cloudToLocal.insert(rel, localAbs);
        }
        ++dirs;
    }
    m_localFileCount = m_localFiles.size();
    log(davbisync::Info, QStringLiteral("scan"),
        QStringLiteral("local listing: %1 packs, %2 files (builtin-source packs excluded)")
            .arg(dirs).arg(m_localFileCount));
}

// ═══════════════════════════════════════════════════════════════════
// 阶段 B：云端清单（逐目录 listDirectory）
// ═══════════════════════════════════════════════════════════════════

void SyncEngine::scanCloudDir(const QString& dbRelDir)
{
    if (!m_running) {
        return;
    }
    // 入参为业务 dbRel；供目录列表的传输路径 = cloudPath(dbRelDir)（唯一入云拼根点）
    const QString cloudRel = cloudPath(dbRelDir);
    QString dir = cloudRel;
    if (!dir.endsWith(QLatin1Char('/'))) {
        dir += QLatin1Char('/');
    }
    m_scanCurrentDir = dir;
    emit progressUpdated(0, QStringLiteral("scan"),
                         QStringLiteral("listing %1").arg(cloudRel));
    probeServer();   // 阶段 B 起点，轻量 OPTIONS，仅首目录触发一次
    if (!m_parser->listDirectory(m_webdav, dir, false)) {
        finishWithError(QStringLiteral("cloud listing failed to start: %1")
                            .arg(cloudRel));
    }
}

void SyncEngine::collectCloudItems()
{
    const QString dirRel = canonicalRel(m_scanCurrentDir);
    strClear(m_scanCurrentDir);

    // vendor 版 getList() 按值返回 QList<QWebdavItem>；本移植用 std::vector
    // 出参（既定接口），遍历写法不变。
    std::vector<QWebdavItemLite> items;
    m_parser->getList(items);
    for (const auto& item : items) {
        const QString rel = canonicalRel(item.path());
        if (rel.isEmpty()) {
            continue;
        }
        if (item.isDir()) {
            if (rel != dirRel && !m_scannedCloudDirs.contains(rel)) {
                m_pendingDirs.append(QPair<QString, QString>(rel, QString()));
            }
        } else {
            davbisync::BaselineEntry e;
            e.size = qint64(item.size());
            e.mtimeMsec = dtMsec(item.lastModified());
            if (e.mtimeMsec > 0) {
                m_cloudMtimeAvail = true;
            }
            // 一个目录内的重复列出（服务端不一致）以首次为准（insert 不覆盖）
            if (!m_cloudFiles.contains(rel)) {
                m_cloudFiles.insert(rel, e);
                ++m_cloudFileCount;
            }
        }
    }
    m_scannedCloudDirs.insert(dirRel);

    log(davbisync::Info, QStringLiteral("scan"),
        QStringLiteral("cloud dir %1: %2 files").arg(dirRel)
            .arg(m_cloudFiles.size()));
    emit treeScanned(m_localFileCount + m_cloudFileCount,
                     m_scannedCloudDirs.size());

    processPendingCloudDirs();
}

void SyncEngine::processPendingCloudDirs()
{
    if (!m_running) {
        return;
    }
    while (!m_pendingDirs.isEmpty()) {
        const auto d = m_pendingDirs.takeFirst();
        if (m_scannedCloudDirs.contains(d.first)) {
            continue;
        }
        scanCloudDir(d.first);
        return;
    }
    finishCloudScan();
}

void SyncEngine::finishCloudScan()
{
    if (!m_running) {
        return;
    }
    m_cloudReady = true;
    if (m_cloudFileCount > 0) {
        m_cloudMtimeDecided = true;    // 云端有文件 → getlastmodified 结论可定
        updateRemoteFeature();
    }
    log(davbisync::Info, QStringLiteral("scan"),
        QStringLiteral("cloud listing done: %1 files in %2 dirs")
            .arg(m_cloudFiles.size()).arg(m_scannedCloudDirs.size()));

    // 阶段 C：diff + 冲突处置
    buildOpQueue();

    // 阶段 D：应用
    pushNext();
}

// ═══════════════════════════════════════════════════════════════════
// 阶段 C：diff（幂等 put/get；双侧皆变 → 云端旧版改名 .conflictN 保留）
// ═══════════════════════════════════════════════════════════════════

void SyncEngine::buildOpQueue()
{
    m_uploadQueue.clear();
    m_downloadQueue.clear();
    m_conflictRelCloud.clear();
    m_conflictNames.clear();

    // 历史污染自愈：下载曾以临时文件名（anystik_dl_*）落盘/上云，且旧的 (0,0)
    // 伪条目令 localChanged 恒真 → 重跑全量重传。此类键一律不进 diff 基准与
    // 队列（不进基线、不上传播），仅记诊断跳过。
    const auto isLegacyTmpRel = [](const QString& rel) {
        return rel.section(QLatin1Char('/'), -1)
            .startsWith(QStringLiteral("anystik_dl_"));
    };
    for (auto it = m_localFiles.begin(); it != m_localFiles.end();) {
        if (isLegacyTmpRel(it.key())) {
            m_diagnosis.skipped
                .append(QStringLiteral("%1 (历史临时名污染，不再同步)").arg(it.key()));
            it = m_localFiles.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_cloudFiles.begin(); it != m_cloudFiles.end();) {
        if (isLegacyTmpRel(it.key())) {
            m_diagnosis.skipped
                .append(QStringLiteral("%1 (历史临时名污染，不再同步)").arg(it.key()));
            it = m_cloudFiles.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_base.local.begin(); it != m_base.local.end();) {
        if (isLegacyTmpRel(it.key())
                || (it.value().size == 0 && it.value().mtimeMsec == 0)) {
            it = m_base.local.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_base.cloud.begin(); it != m_base.cloud.end();) {
        if (isLegacyTmpRel(it.key())) {
            it = m_base.cloud.erase(it);
        } else {
            ++it;
        }
    }

    // 过滤自身冲突保留文件（.conflict<digits>，rclone num 风格）：
    // 多客户端共享同一 dav 目录时，不互相下载/上传传播，也不写入基线
    //（基线由 m_localFiles/m_cloudFiles 直接导出）。仅记入诊断「跳过」。
    for (auto it = m_localFiles.begin(); it != m_localFiles.end();) {
        if (isConflictRel(it.key())) {
            m_diagnosis.skipped
                .append(QStringLiteral("%1 (conflict 保留文件)").arg(it.key()));
            it = m_localFiles.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_cloudFiles.begin(); it != m_cloudFiles.end();) {
        if (isConflictRel(it.key())) {
            m_diagnosis.skipped
                .append(QStringLiteral("%1 (conflict 保留文件)").arg(it.key()));
            it = m_cloudFiles.erase(it);
        } else {
            ++it;
        }
    }

    // 统一双向 diff：以基线(状态文件)作参照，无状态文件 → 空列表(一切条目均为新增)。
    // 无任何模式/首次分支；空状态下凡「两侧都有且 size 不同」→ 双侧新增且不一致 → 冲突。
    davbisync::SyncBaseline& base = m_base;

    const auto mtimeDiffer = [](qint64 cur, qint64 baseMsec) {
        return cur > 0 && baseMsec > 0 && cur != baseMsec;
    };
    // 判据 = size 不同，或 size 相同但两侧 mtime 均有效且不同（第二判据，防
    // size-only 漏报）。任一侧 mtime 无效（0）→ 仅 size（无 mtime 服务器回退）。
    const auto localChanged = [&base, &mtimeDiffer](
        const QString& rel, qint64 size, qint64 mtimeMsec) {
        const auto it = base.local.constFind(rel);
        if (it == base.local.constEnd()) {
            return true;
        }
        return it.value().size != size
            || mtimeDiffer(mtimeMsec, it.value().mtimeMsec);
    };
    const auto cloudChanged = [&base, &mtimeDiffer](
        const QString& rel, qint64 size, qint64 mtimeMsec) {
        const auto it = base.cloud.constFind(rel);
        if (it == base.cloud.constEnd()) {
            return true;
        }
        return it.value().size != size
            || mtimeDiffer(mtimeMsec, it.value().mtimeMsec);
    };

    for (auto it = m_localFiles.cbegin(); it != m_localFiles.cend(); ++it) {
        const QString rel = it.key();
        const qint64 localSize = it.value().size;
        const auto cit = m_cloudFiles.constFind(rel);
        if (cit == m_cloudFiles.constEnd()) {
            // 云端无此文件 → 上传
            m_uploadQueue.append(QPair<QString, QString>(
                m_cloudToLocal.value(rel), rel));
            continue;
        }
        const qint64 cloudSize = cit.value().size;
        if (cloudSize == localSize) {
            const qint64 cloudMsec = cit.value().mtimeMsec;
            const qint64 localMsec = it.value().mtimeMsec;
            const bool mtimeEqual =
                cloudMsec > 0 && localMsec > 0 && cloudMsec == localMsec;
            if (cloudMsec <= 0 || localMsec <= 0 || mtimeEqual) {
                // 相等预检通过：size 同，且（一侧无 mtime → size-only 一致）
                // 或 mtime 亦同 → 跳过
                ++m_uploadSkipped;
                continue;
            }
            // size 相同但双侧 mtime 有效且不同 -> 落入下方 changed 判卷
            // （双侧皆变且当前不一致 → 冲突双保留）
        }
        const bool lc = localChanged(rel, localSize, it.value().mtimeMsec);
        const bool cc = cloudChanged(rel, cloudSize, cit.value().mtimeMsec);
        if (lc && cc) {
            // 双侧皆变且不一致 → 冲突：云端旧版改名 .conflictN 保留，本地新版本上传覆盖
            m_conflictRelCloud.append(rel);
            const QString dirBase = rel.section(QLatin1Char('/'), 0, -2);
            const QString fileBase = rel.section(QLatin1Char('/'), -1);
            const QString newRel = nextConflictName(dirBase, fileBase);
            m_pendingCloudRenames.append(QPair<QString, QString>(rel, newRel));
            m_uploadQueue.append(QPair<QString, QString>(
                m_cloudToLocal.value(rel), rel));
            m_diagnosis.conflicts
                .append(QStringLiteral("%1 (local %2 B vs cloud %3 B; cloud kept as %4)")
                            .arg(rel).arg(localSize).arg(cloudSize).arg(newRel));
            log(davbisync::Warn, QStringLiteral("conflict"),
                QStringLiteral("%1 changed on both sides; "
                               "keeping cloud version as %2, uploading local")
                    .arg(rel, newRel));
            continue;
        }
        if (lc) {
            // 仅本地变化 → 上传覆盖（云端旧版为基线一致版，rclone ->winner 语义）
            m_uploadQueue.append(QPair<QString, QString>(
                m_cloudToLocal.value(rel), rel));
            continue;
        }
        if (cc) {
            // 仅云端变化 → 下载
            m_downloadQueue.append(QPair<QString, QString>(
                rel, m_cloudToLocal.value(rel)));
            continue;
        }
        ++m_uploadSkipped;   // 理论不可达（size 不同则至少一侧 changed）
    }

    // 云端新增、本地无对应 → 下载（受包级 pullEnabled 控制）
    for (auto cit = m_cloudFiles.cbegin(); cit != m_cloudFiles.cend(); ++cit) {
        const QString rel = cit.key();
        if (m_localFiles.contains(rel)) {
            continue;
        }
        const QString dirRel = rel.section(QLatin1Char('/'), 0, -2);
        const QString packId = m_localDirPacks.value(dirRel);
        if (!packId.isEmpty() && m_packPolicies.contains(packId)) {
            const davbisync::PackPolicy p = m_packPolicies.value(packId);
            if (!p.pullEnabled) {
                log(davbisync::Info, QStringLiteral("pack"),
                    QStringLiteral("skip download by policy: %1").arg(rel));
                ++m_downloadSkipped;
                continue;
            }
        }
        m_downloadQueue.append(QPair<QString, QString>(rel, QString()));
    }

    m_uploadTotal = m_uploadQueue.size();
    m_totalJobs = m_uploadQueue.size()
        + m_downloadQueue.size() + m_pendingCloudRenames.size();
    log(davbisync::Info, QStringLiteral("scan"),
        QStringLiteral("op queue: %1 uploads, %2 downloads, %3 conflicts")
            .arg(m_uploadQueue.size()).arg(m_downloadQueue.size())
            .arg(m_conflictRelCloud.size()));
}

QString SyncEngine::nextConflictName(const QString& dirBase,
                                     const QString& fileBase)
{
    // rclone num 风格：file.conflict1 / file.conflict2 …（时间序自动编号）
    int n = 1;
    QString candidate = dirBase + QLatin1Char('/') + fileBase
                        + QStringLiteral(".conflict%1").arg(n);
    while (m_conflictNames.contains(candidate)
           || m_cloudFiles.contains(candidate)) {
        ++n;
        candidate = dirBase + QLatin1Char('/') + fileBase
                    + QStringLiteral(".conflict%1").arg(n);
    }
    m_conflictNames.append(candidate);
    return candidate;
}

// ═══════════════════════════════════════════════════════════════════
// 阶段 D：应用
// ═══════════════════════════════════════════════════════════════════

void SyncEngine::pushNext()
{
    DAV_SYNC_JOIN_CONNECT
    if (!m_running) {
        return;
    }

    // 1) 冲突改名（先于上传，避免云端旧版被新上传覆盖后改名丢失）
    if (!m_pendingCloudRenames.isEmpty()) {
        const auto item = m_pendingCloudRenames.takeFirst();
        const QString from = item.first;
        const QString to = item.second;
        QNetworkReply* reply = m_webdav->move(cloudPath(from), cloudPath(to));
        m_activeReplies.append(reply);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, from, to]() {
                    removeActiveReply(reply);
                    if (!m_running) {
                        return;
                    }
                    if (reply->error() == QNetworkReply::NoError) {
                        m_cloudFiles.insert(to, m_cloudFiles.value(from));
                        m_cloudFiles.remove(from);
                        persistBaseline();
                        log(davbisync::Info, QStringLiteral("conflict"),
                            QStringLiteral("cloud rename %1 → %2")
                                .arg(from, to));
                    } else {
                        log(davbisync::Warn, QStringLiteral("conflict"),
                            QStringLiteral("cloud rename failed %1: %2")
                                .arg(from, reply->errorString()));
                    }
                    ++m_jobsDone;
                    emitProgress();
                    pushNext();
                });
        return;
    }

    // 2) 上传（目录链未就位时先逐层 mkdir）
    if (m_uploadIndex < m_uploadQueue.size()) {
        const auto& item = m_uploadQueue.at(m_uploadIndex);
        const QString localPath = item.first;
        const QString cloudRel = item.second;
        const QString cloudDir = cloudRel.section(QLatin1Char('/'), 0, -2);
        if (m_mkdirChain.isEmpty() && !m_ensuredDirs.contains(cloudDir)) {
            // 构建「根目录 → cloudDir」的完整待建链，顶层在前
            m_mkdirChain.clear();
            QString cur;
            const QStringList parts = splitSkipEmpty(cloudDir, QLatin1Char('/'));
            for (const QString& seg : parts) {
                if (seg.isEmpty()) {
                    continue;
                }
                cur = (cur.isEmpty() ? seg : cur + QLatin1Char('/') + seg);
                if (!m_ensuredDirs.contains(cur)) {
                    m_mkdirChain.append(cur);
                }
            }
        }
        if (!m_mkdirChain.isEmpty()) {
            mkdirNextChain();
            return;
        }
        checkAndUpload(localPath, cloudRel);
        return;
    }

    // 3) 下载
    if (m_downloadIndex < m_downloadQueue.size()) {
        const auto& item = m_downloadQueue.at(m_downloadIndex);
        downloadFile(item.first, QString());
        return;
    }

    finishOk(QStringLiteral("uploaded %1, skipped %2, downloaded %3")
             .arg(m_uploadDone)
             .arg(m_uploadSkipped + m_downloadSkipped)
             .arg(m_downloadDone));
}

void SyncEngine::mkdirNextChain()
{
    DAV_SYNC_JOIN_CONNECT
    if (m_mkdirChain.isEmpty()) {
        pushNext();
        return;
    }
    const QString dir = m_mkdirChain.front();   // QList(value shim) 无 constFirst
    QNetworkReply* reply = m_webdav->mkdir(cloudPath(dir));
    m_activeReplies.append(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, dir]() {
                removeActiveReply(reply);
                if (!m_running) {
                    return;
                }
                if (reply->error() != QNetworkReply::NoError) {
                    log(davbisync::Warn, QStringLiteral("mkdir"),
                        QStringLiteral("%1: %2").arg(dir, reply->errorString()));
                }
                // 无论成败都标记 ensured（继续建子层；put 前真正缺失会以 HTTP 错误上报）
                m_ensuredDirs.append(dir);
                stringListRemoveFirst(m_mkdirChain);
                pushNext();
            });
}

void SyncEngine::checkAndUpload(const QString& localPath, const QString& cloudRel)
{
    DAV_SYNC_JOIN_CONNECT
    // QWebdavLite 的 13 个方法一律吃云端**路径**（vendor 版 head 吃
    // QNetworkRequest）；拼根的唯一入点是 cloudPath()，故这里传它而非 URL。
    QNetworkReply* reply = m_webdav->head(cloudPath(cloudRel));
    m_activeReplies.append(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, localPath, cloudRel]() {
                removeActiveReply(reply);
                if (!m_running) {
                    return;
                }
                if (reply->error() == QNetworkReply::ContentNotFoundError) {
                    uploadFile(localPath, cloudRel);      // 云端不存在 → 直接上传
                    return;
                }
                if (reply->error() != QNetworkReply::NoError) {
                    finishWithError(QStringLiteral("webdav check failed: %1 (%2)")
                                        .arg(cloudRel, reply->errorString()));
                    return;
                }
                const qint64 remoteSize =
                    rawHeaderToLongLong(reply->rawHeader("Content-Length"));
                const QFileInfo fi(localPath);
                // HEAD 幂等判定读 size + mtime 双值（与 buildOpQueue 同口径）：
                // skip 当且仅当「大小与两侧 mtime 均相对基线未变」。两侧各自与基线比
                //（云 mtime 不与本地 mtime 直接比——上传后云 last-modified 为服务器
                // 时刻，互比会每轮空转重传）。
                bool cloudUnchanged = false;
                const qint64 headMsec = [reply]() -> qint64 {
                    const QByteArray lm = reply->rawHeader("Last-Modified");
                    if (lm.isEmpty()) {
                        return 0;
                    }
                    const QDateTime t = parseHttpDate(QString::fromLatin1(lm));
                    return t.isValid() ? dtMsec(t) : 0;
                }();
                const qint64 baseCloudMsec =
                    m_cloudFiles.value(cloudRel).mtimeMsec;
                if (headMsec > 0) {
                    // 服务器回 mtime：与基线一致才视为云端未变；
                    // run 中途被第三方改动（mtime 漂移）→ 需重传
                    cloudUnchanged = (headMsec == baseCloudMsec);
                } else if (baseCloudMsec <= 0) {
                    // 纯 size-only 型服务器（两侧均无 mtime）→ 大小一致即视为一致
                    cloudUnchanged = true;
                } else {
                    // 扫描能拿到 mtime 但 HEAD 不回 → 无法确认，保守视为已变
                    cloudUnchanged = false;
                }
                bool localUnchanged = false;
                const qint64 localMsec = localMtimeMsec(fi);
                const auto bit = m_base.local.constFind(cloudRel);
                if (!localMsec) {
                    localUnchanged = true;      // 本地 mtime 不可用 → size-only 语义
                } else if (bit == m_base.local.constEnd()) {
                    localUnchanged = false;     // 无本地基线 → 视为已变（dst 为准）
                } else {
                    localUnchanged = (localMsec == bit.value().mtimeMsec);
                }
                if (remoteSize == fi.size() && cloudUnchanged && localUnchanged) {
                    ++m_uploadSkipped;
                    ++m_uploadIndex;
                    m_fileStartMsec = nowMsec(); // 跳过不算文件用时
                    ++m_jobsDone;
                    emitProgress();
                    pushNext();
                    return;
                }
                uploadFile(localPath, cloudRel);   // 缺失/大小不同/任一侧相对基线已变 → 上传覆盖
            });
}

void SyncEngine::uploadFile(const QString& localPath, const QString& cloudRel)
{
    DAV_SYNC_JOIN_CONNECT
    m_fileStartMsec = nowMsec();
    const QFileInfo finfo(localPath);
    log(davbisync::Info, QStringLiteral("upload"),
        QStringLiteral("上传 %1 · %2 · 第 %3/%4")
            .arg(finfo.fileName())
            .arg(formatBytes(finfo.size()))
            .arg(m_uploadIndex + 1)
            .arg(m_uploadTotal));

// ★ 为何是 shared_ptr 而非原码的 unique_ptr：Qt3 侧的信号投递走
//   qconnect_slots.h → std::function<void()>，而 std::function 要求**可拷贝构造**
//   （实测报 static assertion failed: std::function target must be
//   copy-constructible，指向这个 lambda）。捕获 unique_ptr 的闭包是 move-only，
//   进不了 std::function。shared_ptr 拷贝廉价，语义（回调期间文件句柄保持打开、
//   回调结束自动关闭）与 unique_ptr 完全一致。
    auto file = std::make_shared<QFile>(localPath);
    if (!file->open(DavOpenMode(qDavReadOnly))) {
        finishWithError(QStringLiteral("cannot open local file: %1").arg(localPath));
        return;
    }
    // 上传不 set Date 头（远端 last-modified 只读不改写；dt 缺省为无效
    // QDateTime，qwebdav 内部 dt.isValid() 分支跳过），云 mtime 交服务器生成
    QNetworkReply* reply = m_webdav->put(cloudPath(cloudRel),
                                         file.get());
    m_activeReplies.append(reply);
    connect(reply, &QNetworkReply::uploadProgress, this,
            [this, reply](qint64 done, qint64 total) {
                Q_UNUSED(reply)
                if (m_running && total > 0) {
                    setProgress(QStringLiteral("upload"), done, total);
                }
            });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, file = std::move(file), localPath, cloudRel]() {
                removeActiveReply(reply);
                if (!m_running) {
                    return;
                }
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (reply->error() != QNetworkReply::NoError
                        || (status >= 400)) {
                    const qint64 elapsed =
                        nowMsec() - m_fileStartMsec;
                    if (reply->error() == QNetworkReply::OperationCanceledError) {
                        log(davbisync::Warn, QStringLiteral("upload"),
                            QStringLiteral("canceled after %1 ms · timeout=90000 · https=%2 · host=%3")
                                .arg(elapsed).arg(m_connectionType == 2).arg(m_host));
                    }
                    finishWithError(QStringLiteral("upload failed: %1 (%2 status %3, %4 ms)")
                                        .arg(localPath, reply->errorString())
                                        .arg(status).arg(elapsed));
                    return;
                }
                m_lastFileMs = nowMsec() - m_fileStartMsec;
                log(davbisync::Info, QStringLiteral("upload"),
                    QStringLiteral("完成 %1 · 用时 %2 s")
                        .arg(QFileInfo(localPath).fileName())
                        .arg(m_lastFileMs / 1000.0, 0, 'f', 1));
                // 更新云清单，保证 finishOk 的基线反映真实云端
                // 写后回读：PUT 响应若带 Last-Modified 头直接回填真实云 mtime
                // （远端时间戳只读不改写；解析失败则置 0 等下一轮扫描回填）
                davbisync::BaselineEntry e;
                e.size = QFileInfo(localPath).size();
                e.mtimeMsec = 0;
                const QByteArray lm = reply->rawHeader("Last-Modified");
                if (!lm.isEmpty()) {
                    const QDateTime t =
                        parseHttpDate(QString::fromLatin1(lm));
                    if (t.isValid()) {
                        e.mtimeMsec = dtMsec(t);
                        m_putMtimeEcho = true;
                    }
                }
                if (!m_putEchoDecided) {
                    m_putEchoDecided = true;
                    updateRemoteFeature();
                }
                m_cloudFiles.insert(cloudRel, e);
                persistBaseline();
                ++m_uploadDone;
                ++m_uploadIndex;
                ++m_jobsDone;
                emitProgress();
                pushNext();
            });
}

void SyncEngine::downloadFile(const QString& cloudRel, const QString& _localAbs)
{
    DAV_SYNC_JOIN_CONNECT
    Q_UNUSED(_localAbs)
    m_fileStartMsec = nowMsec();
    const QString baseName = cloudRel.section(QLatin1Char('/'), -1);

    // 云端目录（StickerPacks/<title>）→ 本地包标题
    const QString relDir = cloudRel.section(QLatin1Char('/'), 0, -2);
    const QString title = relDir.section(QLatin1Char('/'), -1);
    // B：内置源排除不区分上传/下载侧，按云目录名对称判定（上传侧整包不列出、
    // 下载侧对属于内置源包的云目录整体跳过），在 ensurePack 前拦截，不为其建包
    if (davLocalSource()->builtinSourceCloudDirs().contains(title)) {
        log(davbisync::Info, QStringLiteral("pack"),
            QStringLiteral("skip download into builtin-source pack: %1")
                .arg(cloudRel));
        m_diagnosis.skipped.append(cloudRel);
        ++m_downloadSkipped;
        ++m_downloadIndex;
        ++m_jobsDone;
        emitProgress();
        pushNext();
        return;
    }
    // 云端目录 → 本地包：优先用本机已有云目录映射（确保剪贴板 pastes 归原包），
    // 新机器无映射时以云目录段为标题建包（目录名两端保持一致）
    QString packId = m_localDirPacks.value(relDir);
    if (packId.isEmpty()) {
        const QString title = relDir.section(QLatin1Char('/'), -1);
        packId = davLocalSource()->ensurePack(title);
    }
    if (packId.isEmpty()) {
        finishWithError(QStringLiteral("ensurePack failed for cloud dir: %1")
                            .arg(relDir));
        return;
    }

    // 临时文件，下载完成后 importStickerFile 按业务 rel(cloudRel) 移入
    //   对应目录：pastes/* → base/pastes 顶层；packs/<T>/* → base/packs/<T>
    const QString tmp = qTempPath() + QLatin1Char('/')
                        + QStringLiteral("anystik_dl_")
                        + QString::number(m_downloadIndex)
                        + QLatin1Char('_') + baseName;
    // 同上：shared_ptr 而非 unique_ptr（std::function 需可拷贝）
    auto out = std::make_shared<QFile>(tmp);
    if (!out->open(DavOpenMode(qDavWriteOnly | qDavTruncate))) {
        finishWithError(QStringLiteral("cannot create temp file: %1").arg(tmp));
        return;
    }
    m_tempFiles.append(tmp);

    log(davbisync::Info, QStringLiteral("download"),
        QStringLiteral("下载 %1 · 第 %2/%3")
            .arg(baseName).arg(m_downloadIndex + 1)
            .arg(m_downloadQueue.size()));

    QNetworkReply* reply = m_webdav->get(cloudPath(cloudRel));
    m_activeReplies.append(reply);
    connect(reply, &QNetworkReply::readyRead, this,
            [this, reply, out = out.get()]() {
                if (m_running && out && out->isOpen())
                    devWrite(out, reply->readAll());
            });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, reply](qint64 done, qint64 total) {
                Q_UNUSED(reply)
                if (m_running && total > 0) {
                    setProgress(QStringLiteral("download"), done, total);
                }
            });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, out = std::move(out), tmp, packId, cloudRel]() {
                removeActiveReply(reply);
                if (out->isOpen()) {
                    out->flush();
                }
                if (!m_running) {
                    return;
                }
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (reply->error() != QNetworkReply::NoError
                        || (status >= 400)) {
                    stringListRemoveOne(m_tempFiles, tmp);
                    QFile::remove(tmp);
                    const qint64 elapsed =
                        nowMsec() - m_fileStartMsec;
                    if (reply->error() == QNetworkReply::OperationCanceledError) {
                        log(davbisync::Warn, QStringLiteral("download"),
                            QStringLiteral("canceled after %1 ms · timeout=90000 · https=%2 · host=%3")
                                .arg(elapsed).arg(m_connectionType == 2).arg(m_host));
                    }
                    finishWithError(QStringLiteral("download failed: %1 (%2 status %3, %4 ms)")
                                        .arg(cloudRel, reply->errorString())
                                        .arg(status).arg(elapsed));
                    return;
                }
                QString err;
                const QString dlName = cloudRel.section(QLatin1Char('/'), -1);
                if (!davLocalSource()->importStickerFile(
                        packId, tmp, &err, dlName, cloudRel)) {
                    stringListRemoveOne(m_tempFiles, tmp);
                    QFile::remove(tmp);
                    // 单文件内容失败（如图片解码失败）不全盘中止：
                    // 跳过该文件，继续后续队列（网络/HTTP 错误才 finishWithError）
                    m_diagnosis.skipped
                        .append(QStringLiteral("%1 (%2)").arg(cloudRel, err));
                    ++m_downloadFailed;
                    ++m_downloadIndex;
                    ++m_jobsDone;
                    emitProgress();
                    pushNext();
                    return;
                }
                stringListRemoveOne(m_tempFiles, tmp);
                QFile::remove(tmp);
                // 更新本地清单以便 finishOk 基线（落盘路径与业务 rel 一致：
                // 剪贴板 base/pastes/<f>、普通包 base/packs/<T>/<f>）
                const QString localAbs =
                    davLocalSource()->resolveStickerPath(cloudRel);
                // 防御：import 已成功则 dst 必存在；避免以 (size0,mtime0)
                // 伪条目污染基线（其曾导致重跑被全判为「本地变化」而全量重传）
                if (!QFileInfo(localAbs).isFile()) {
                    finishWithError(
                        QStringLiteral("imported file missing on disk: %1")
                            .arg(localAbs));
                    return;
                }
                // 远端 last-modified 只读：把本地副本 mtime 对齐为云端值（只改本地），
                // 下一轮不会因下载改了本地时间戳而误判为空转上传
                const qint64 cloudMsec =
                    m_cloudFiles.value(cloudRel).mtimeMsec;
                if (cloudMsec > 0) {
                    QFile f(localAbs);
                    fileSetMsecUtc(f, cloudMsec);
                }
                m_lastFileMs = nowMsec() - m_fileStartMsec;
                log(davbisync::Info, QStringLiteral("download"),
                    QStringLiteral("完成 %1 · 用时 %2 s")
                        .arg(cloudRel.section(QLatin1Char('/'), -1))
                        .arg(m_lastFileMs / 1000.0, 0, 'f', 1));
                QFileInfo fi(localAbs);
                davbisync::BaselineEntry e;
                e.size = fi.size();
                e.mtimeMsec = localMtimeMsec(fi);
                m_localFiles.insert(cloudRel, e);
                m_cloudToLocal.insert(cloudRel, localAbs);
                persistBaseline();
                ++m_downloadDone;
                ++m_downloadIndex;
                ++m_jobsDone;
                emitProgress();
                pushNext();
            });
}

QUrl SyncEngine::cloudUrl(const QString& dbRel) const
{
    QUrl u;
    urlSetScheme(u, m_webdav->isSSL() ? QStringLiteral("https") : QStringLiteral("http"));
    u.setHost(m_webdav->hostname());
    u.setPort(m_webdav->port());
    QString p = m_webdav->rootPath();
    if (p.isEmpty()) {
        p = QStringLiteral("/");
    }
    if (!p.endsWith(QLatin1Char('/'))) {
        p += QLatin1Char('/');
    }
    p += cloudPath(dbRel);
    u.setPath(p);
    return u;
}

QString SyncEngine::cloudPath(const QString& dbRel) const
{
    // 唯一入云拼接点：业务 dbRel → 传q参数（传出 传输路径）。
    // 规律：业务域（DB/基线/内存/文件系统）一律无云根，仅此处临时添 m_cloudRoot。
    if (dbRel.isEmpty()) {
        return m_cloudRoot;
    }
    return m_cloudRoot + QLatin1Char('/') + dbRel;
}

void SyncEngine::emitProgress()
{
    setProgress(QStringLiteral("sync"), 0, 0);
}

void SyncEngine::setProgress(const QString& stage, qint64 fileDone,
                             qint64 fileTotal)
{
    // 统一连续标尺：已完成件数为基，当前件内 fileDone/fileTotal 折算一格，
    // m_jobsDone 只增 → 阶段切换（改名→上传→下载）不回落、不重置
    const double total = double(qMax(1, m_totalJobs));
    const double inFile = (fileTotal > 0)
        ? double(fileDone) / double(fileTotal) : 0.0;
    m_percent = int(100.0 * (double(m_jobsDone) + inFile) / total);
    m_percent = qBound(0, m_percent, 100);
    if (stage == QStringLiteral("upload")
            || stage == QStringLiteral("download")) {
        const qint64 now = nowMsec();
        if (m_speedStage != stage) {
            m_speedStage = stage;
            m_speedLastMsec = 0;
            m_speedBps = 0;
        }
        if (m_speedLastMsec > 0) {
            const qint64 dMs = now - m_speedLastMsec;
            const qint64 dB = fileDone - m_speedLastBytes;
            if (dMs >= 300 && dB >= 0) {
                const qreal inst = qreal(dB) * 1000.0 / qreal(dMs);
                m_speedBps = m_speedBps > 0 ? m_speedBps * 0.6 + inst * 0.4
                                            : inst;
            }
        }
        m_speedLastMsec = now;
        m_speedLastBytes = fileDone;
    } else {
        strClear(m_speedStage);
    }
    emit progressUpdated(m_percent, stage, statDetail());
}

QString SyncEngine::statDetail() const
{
    QStringList parts;
    if (m_uploadTotal > 0) {
        parts << QStringLiteral("上传 在传%1/要传%2/总%3")
                     .arg(m_uploadIndex + 1).arg(m_uploadTotal)
                     .arg(m_localFiles.size());
    }
    if (!m_downloadQueue.isEmpty()) {
        parts << QStringLiteral("下载 在传%1/要下%2/总%3")
                     .arg(m_downloadIndex + 1).arg(m_downloadQueue.size())
                     .arg(m_cloudFileCount);
    }
    if (!m_conflictRelCloud.isEmpty()) {
        parts << QStringLiteral("冲突 %1").arg(m_conflictRelCloud.size());
    }

    QString curText;
    QString curSizeText;
    if (!m_pendingCloudRenames.isEmpty()) {
        curText = QStringLiteral("重命名 %1").arg(m_pendingCloudRenames.size());
    } else if (m_uploadIndex < m_uploadQueue.size()) {
        const auto& item = m_uploadQueue.at(m_uploadIndex);
        curText = QStringLiteral("上传 %1").arg(
            davLocalSource()->relativeToBase(item.first));
        curSizeText = formatBytes(QFileInfo(item.first).size());
    } else if (m_downloadIndex < m_downloadQueue.size()) {
        const QString rel = m_downloadQueue.at(m_downloadIndex).first;
        const QString dlAbs = m_cloudToLocal.value(rel);
        curText = QStringLiteral("下载 %1").arg(
            dlAbs.isEmpty()
                ? rel.section(QLatin1Char('/'), -1)
                : davLocalSource()->relativeToBase(dlAbs));
        const auto cit = m_cloudFiles.constFind(rel);
        if (cit != m_cloudFiles.constEnd()) {
            curSizeText = formatBytes(cit.value().size);
        }
    } else {
        curText = QStringLiteral("收尾");
    }

    const int total = qMax<int>(1, m_uploadTotal + m_downloadQueue.size()
                              + m_pendingCloudRenames.size());
    const int done = m_uploadIndex + m_downloadDone;
    const qint64 now = nowMsec();
    const double elapsedSec = qMax(1.0, double(now - m_startMsec) / 1000.0);

    // Qt3 的 QStringList 无 operator<<(QStringList)，且 line 末尾要 join(" · ")，
    // 故 parts 逐项 append（语义与原 `line << parts` 的批量追加一致）。
    QStringList line;
    line << curText;
    if (!curSizeText.isEmpty()) {
        line << curSizeText;
    }
    for (int i = 0; i < parts.size(); ++i) {
        line << parts[i];   // Qt3 QValueList 无 at()，用 operator[]
    }
    if (m_speedBps > 0) {
        line << QStringLiteral("速度 %1/s")
                 .arg(formatBytes(qint64(m_speedBps)));
    }
    if (m_lastFileMs > 0) {
        line << QStringLiteral("本次 %1").arg(formatDuration(m_lastFileMs));
    }
    line << QStringLiteral("已用 %1").arg(formatDuration(m_startMsec > 0
        ? now - m_startMsec : 0));
    if (done > 0 && total > done) {
        line << QStringLiteral("ETA %1").arg(formatDuration(
            qint64(elapsedSec * double(total - done) / double(done)) * 1000));
    }
    return line.join(QStringLiteral(" · "));
}

void SyncEngine::probeServer()
{
    DAV_SYNC_JOIN_CONNECT
    if (m_serverProbed || !m_webdav) {
        return;
    }
    m_serverProbed = true;
    QNetworkReply* reply = m_webdav->options(m_cloudRoot);
    if (!reply) {
        return;
    }
    m_activeReplies.append(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        removeActiveReply(reply);
        m_serverName = QString::fromLatin1(reply->rawHeader("Server"));
        m_davCap = QString::fromLatin1(reply->rawHeader("DAV"));
        m_allowMethods = QString::fromLatin1(reply->rawHeader("Allow"));
        if (reply->error() == QNetworkReply::NoError
                && !m_davCap.contains(QLatin1Char('1'))) {
            log(davbisync::Warn, QStringLiteral("probe"),
                QStringLiteral("非标准 WebDAV（DAV=%1），部分功能可能失败")
                    .arg(m_davCap));
        }
        updateRemoteFeature();
    });
}

void SyncEngine::updateRemoteFeature()
{
    QStringList lines;
    if (m_serverProbed) {
const QString davNames = [this]() {
        QStringList parts;
        if (m_davCap.contains(QLatin1Char('1'))) {
            parts << QStringLiteral("1(文件读写)");
        }
        if (m_davCap.contains(QLatin1Char('2'))) {
            parts << QStringLiteral("2(文件锁)");
        }
        return parts.isEmpty()
            ? (m_davCap.isEmpty() ? QStringLiteral("?") : m_davCap)
            : parts.join(QStringLiteral("+"));
    }();
    lines << QStringLiteral("服务器软件: %1 · WebDAV 等级: %2 · 可用方法: %3")
                  .arg(m_serverName.isEmpty() ? QStringLiteral("?") : m_serverName)
                  .arg(davNames)
                  .arg(m_allowMethods.isEmpty() ? QStringLiteral("?") : m_allowMethods);
    }
    if (m_cloudMtimeDecided || m_putEchoDecided) {
lines << QStringLiteral("文件时间: %1 · 上传回读: %2 · 判定: %3")
                  .arg(m_cloudMtimeAvail ? QStringLiteral("支持✓")
                                         : QStringLiteral("不支持✗"))
                  .arg(m_putMtimeEcho ? QStringLiteral("✓")
                                      : QStringLiteral("✗(下轮回填)"))
                  .arg(m_cloudMtimeAvail ? QStringLiteral("大小＋时间")
                                         : QStringLiteral("仅大小"));
    }
    const QString text = lines.isEmpty()
        ? QStringLiteral("远程特征: 检测中...")
        : QStringLiteral("远程特征: ") + lines.join(QStringLiteral(" | "));
    emit remoteFeature(text);
    log(davbisync::Info, QStringLiteral("probe"), text);
}

void SyncEngine::removeActiveReply(QNetworkReply* reply)
{
    if (reply) {
        m_activeReplies.removeOne(reply);
        reply->deleteLater();
    }
}

// 成功操作后增量持久化基线：中断（abort/崩溃/杀进程）后重启，已完成部分
// 不会因旧基线缺失而被 diff 判为「双侧皆变」重传/误改 .conflictN。
// 原子写（QSaveFile tmp+rename），每文件一次，JSON 极小可接受。
void SyncEngine::persistBaseline()
{
    davbisync::SyncBaseline bl;
    bl.lastMode = QStringLiteral("incremental");
    bl.lastRunMsec = nowMsec();
    bl.local = m_localFiles;
    bl.cloud = m_cloudFiles;
    davbisync::baselineSave(bl);
}

void SyncEngine::finishOk(const QString& summary)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    m_running = false;
    m_percent = 100;

    // 成功后固化基线（原子写，中断不毁旧基线）
    // 统一双向同步：状态文件固定以 incremental 记录（无模式区分，字段仅作兼容占位）
    persistBaseline();

    QString s = summary;
    if (m_downloadFailed > 0) {
        s += QStringLiteral(", failed %1").arg(m_downloadFailed);
    }
    if (!m_diagnosis.conflicts.isEmpty()) {
        s += QStringLiteral(", conflicts %1").arg(m_diagnosis.conflicts.size());
    }
    for (QNetworkReply* r : m_activeReplies) {
        if (r) {
            disconnect(r, nullptr, this, nullptr);
            r->abort();
            r->deleteLater();
        }
    }
    m_activeReplies.clear();
    log(davbisync::Info, QStringLiteral("sync"), s);
    emit progressUpdated(100, QStringLiteral("done"), s);
    emit finished(davbisync::FinishOk, s);
}

void SyncEngine::finishWithError(const QString& msg)
{
    if (m_finished || !m_running) {
        return;
    }
    m_finished = true;
    m_running = false;
    log(davbisync::Error, QStringLiteral("sync"), msg);
    log(davbisync::Error, QStringLiteral("sync"),
        QStringLiteral("aborted; completed transfers kept, rerun will re-check by size"));
    for (QNetworkReply* r : m_activeReplies) {
        if (r) {
            disconnect(r, nullptr, this, nullptr);
            r->abort();
            r->deleteLater();
        }
    }
    m_activeReplies.clear();
    emit progressUpdated(m_percent, QStringLiteral("aborted"), msg);
    emit finished(davbisync::FinishError, msg);
}

void SyncEngine::log(davbisync::LogLevel level, const QString& tag, const QString& line)
{
    emit syncLog(int(level), tag, line);
}

QString SyncEngine::canonicalRel(const QString& raw)
{
    // 出云唯一剥离点：清前后斜杠后，剥掉首个目录段（=云根）。
    // ⇒ 远端返回路径归化为业务域 dbRel（anystik/pastes/x → pastes/x）。
    QString r = raw;
    while (r.startsWith(QLatin1Char('/'))) {
        r.remove(0, 1);
    }
    while (r.endsWith(QLatin1Char('/'))) {
        qStringChop(r, 1);            // Qt3 QString 无 chop()
    }
    const int slash = strIndexOf(r, QLatin1Char('/'));
    if (slash < 0) {
        strClear(r);                  // Qt3 QString 无 clear()
    } else {
        r = r.mid(slash + 1);
    }
    return r;
}