#include "qwebdavlite.h"

#include <qbuffer.h>
#include <qiodevice.h>

// ═══ 构造/析构 ═══════════════════════════════════════════════════════
QWebdavLite::QWebdavLite(QObject* parent)
    : QNetworkAccessManager(parent)
    , m_connectionType(HTTP)
    , m_port(0)
    , m_rootPath(QString("/"))
    , m_transferTimeoutMsecs(-1)
    , m_lastReply(0)
    , m_authenticator_lastReply(0)
{
    // ── 401 → Basic Auth 回填 ──
    // 垫片侧已实现重发链：收到 401 时回调本槽；槽里把 authenticator 填上，
    // 垫片据此补 Authorization 头并重发一次（m_authRetried 保证只重试一次）。
    // 不接 = WebDAV 一律 401/403，整个模块不可用。
    //
    // `using ::connect;` 只在 Qt3 需要：垫片把 connect 做成自由函数重载
    // （qconnect_slots.h），Qt4+ 的 connect 是 QObject 静态成员，不存在 `::connect`。
    // 槽用**成员函数指针**而非 lambda：qconnect_slots 的这个重载签名是
    // `void (Class::*)(QNetworkReply*, QAuthenticator*)`，lambda 推不出 Class。
#if !defined(QT_VERSION) || QT_VERSION < 0x040000
    using ::connect;
    connect(this, &QNetworkAccessManager::authenticationRequired, this,
            &QWebdavLite::provideAuthentication);
#else
    connect(this, &QNetworkAccessManager::authenticationRequired, this,
            &QWebdavLite::provideAuthentication);
#endif
}

// 认证槽：把 setConnectionSettings 存下的凭据填进 authenticator。
// realm 在 Qt3 垫片里没有服务端下发的值（垫片不发 WWW-Authenticate 头），
// 用主机名兜底；填了不影响服务端判定（Basic 只看 user:pass）。
void QWebdavLite::provideAuthentication(QNetworkReply*, QAuthenticator* auth)
{
    if (auth == 0 || m_username.isEmpty() && m_password.isEmpty()) {
        return;
    }
    auth->setPassword(m_password);
    // Qt6 把 setUserName 改名成 setUser（userName() 同理，Qt6 弃用）。
#if !defined(QT_VERSION) || QT_VERSION < 0x060000
    auth->setUserName(m_username);
#else
    auth->setUser(m_username);
#endif
    if (auth->realm().isEmpty()) {
        auth->setRealm(m_hostname);
    }
}

QWebdavLite::~QWebdavLite() {}

// ── 信号（占位定义：只为取址，实际发射走 emitErrorChanged）──
// 本类刻意不声明 Q_OBJECT、不用 moc（见 qwebdavlite.h 末尾），故「伪信号」
// 必须像 qnam_shim.cpp:220-227 那样手动给一个空定义：qconnect_slots.h 的
// connect 重载只拿成员函数指针来区分同名信号，并不调用它；但 `&QWebdavLite::
// errorChanged` 取址会生成一条**外部未定义符号**的引用。
// 实测漏掉这行时的报错是链接期
//   undefined reference to `QWebdavLite::errorChanged(QString)'
// ——编译能过（模板匹配上了），只有链接才炸，很容易误判成「模板写错了」。
void QWebdavLite::errorChanged(QString) {}

// ═══ 连接配置 ═══════════════════════════════════════════════════════
void QWebdavLite::setConnectionSettings(QWebdavConnectionType connectionType,
                                        const QString& hostname,
                                        const QString& rootPath,
                                        const QString& username,
                                        const QString& password,
                                        int port,
                                        const QString& sslCertDigestMd5,
                                        const QString& sslCertDigestSha1)
{
    m_connectionType = connectionType;
    m_hostname = hostname;
    m_username = username;
    m_password = password;
    m_port = port;
    m_rootPath = rootPath;
    m_sslCertDigestMd5 = sslCertDigestMd5;
    m_sslCertDigestSha1 = sslCertDigestSha1;

    // rootPath 归一：非空、以 '/' 开头、不以 '/' 结尾（对齐 vendor 行为）
    if (m_rootPath.length() == 0) {
        m_rootPath = QString("/");
    }
    if (!m_rootPath.startsWith(QString("/"))) {
        m_rootPath.prepend(QString("/"));
    }
    if (m_rootPath.length() > 1 && m_rootPath.endsWith(QString("/"))) {
        qStringChop(m_rootPath, 1);
    }
}

QString QWebdavLite::absolutePath(const QString& relPath)
{
    QString absolute = m_rootPath;
    if (!relPath.isEmpty()) {
        if (!absolute.endsWith(QString("/"))) {
            absolute.append(QString("/"));
        }
        QString rel = relPath;
        if (rel.startsWith(QString("/"))) {
            rel = rel.mid(1);
        }
        absolute.append(rel);
    }
    return absolute;
}

QString QWebdavLite::buildUrl(const QString& path)
{
    QUrl url;
    // setHost / setPath 在 Qt3 与 Qt4+ 都存在，无需分支。
    // ★ setProtocol 是 Qt3/Qt4 的名字，Qt5 起改名 setScheme。两者语义相同，
    // 这里按版本分支（Qt3 才是本批次 3c-1 的主战场，但 Qt6 门也要能过）。
#if !defined(QT_VERSION) || QT_VERSION < 0x050000
    url.setProtocol(isSSL() ? QString("https") : QString("http"));
#else
    url.setScheme(isSSL() ? QString("https") : QString("http"));
#endif
    url.setHost(m_hostname);
    if (m_port > 0) {
        url.setPort(m_port);
    }
    url.setPath(absolutePath(path));
    // ★ 不要把凭据塞进 URL userinfo。
    // 实测：libcurl 对 `http://user@host/` （有用户名、无密码）会返回
    // 200 但**根本不发请求**——它在等凭据补全；`http://user:pass@host/`
    // 才正常。这是"静默丢请求"最难查的一种形态。
    // 凭据统一走既有的 401 链：authenticationRequired → Basic Auth →
    // 重发（见 qnam_shim.cpp 的 401 段），与 vendor 行为一致。
    return url.toString();
}

// ═══ 摘要 hex（"XX:XX:…"，大写，对齐 vendor digestToHex）════════════════
QString QWebdavLite::digestToHex(const QByteArray& input)
{
    // vendor 用 QByteArray::toUpper()，Qt3 无 → qbaToHex 已直接产出大写
    if (input.size() == 0) {
        return QString();
    }
    const QByteArray hex = qbaToHex(input);
    const char* d = hex.data();
    if (d == 0) {
        return QString();
    }
    QString result;
    // 手工拼：Qt3 的 QByteArray 无 indexOf/left(2) 之类的按字节切片，
    // 逐字节 append 两个 char 组成的 QString。
    for (int i = 0; i < (int)input.size(); ++i) {
        if (i != 0) {
            result.append(QString(":"));
        }
        const int off = i * 2;
        result.append(QChar(d[off]));
        result.append(QChar(d[off + 1]));
    }
    return result;
}

QByteArray QWebdavLite::digestFromHex(const QString& input)
{
    QByteArray raw;
    QString s = input;
    s.replace(QString(":"), QString(""));
    const QByteArray latin = qToLatin1BA(s);
    return qbaFromHex(latin);
}

// ═══ 请求构造 ═══════════════════════════════════════════════════════
QNetworkRequest QWebdavLite::mkcolRequest(const QString& path)
{
    QNetworkRequest req(buildUrl(path));
    req.setRawHeader("Connection", "close");
    req.setRawHeader("User-Agent", "qlstik");
    return req;
}

QNetworkRequest QWebdavLite::proFindRequest(const QString& path, int depth)
{
    QNetworkRequest req(buildUrl(path));
    req.setRawHeader("Connection", "close");
    req.setRawHeader("Depth", depth > 0 ? "1" : "0");
    req.setRawHeader("Content-Type", "text/xml; charset=utf-8");
    return req;
}

// ═══ 统一出口 ═════════════════════════════════════════════════════════
QNetworkReply* QWebdavLite::createRequest(const QString& method, QNetworkRequest& req,
                                          QIODevice* outgoingData)
{
    m_lastReply = 0;
    QNetworkReply* reply = 0;
    // 请求体：必须在**这里**当场从 QIODevice 读出来。
    //   1. 垫片的 sendCustomRequest 收不到 QIODevice（只有 QByteArray /
    //      std::string 两种形参），原来只把设备登记进 m_inDataDevices 却从不
    //      读取 → PUT/POST 的 body 永远是 0 字节（e2e 实测 PUT len=0）。
    //   2. 必须在 createRequest 内读：调用方 put() 里的 QBuffer 是**栈上局部
    //      对象**，put() 一返回就析构，事后任何解引用都是 use-after-free。
    //      m_inDataDevices 存的正是这个裸指针，属悬垂指针且只写不读，已删。
    //   3. 必须先 open：本套头是 Qt1/2 时代的 QIODevice API 面
    //      （open(int mode) 纯虚、isOpen() 判 state()==IO_Open）。Qt3 的
    //      QBuffer(QByteArray) 构造后**并未**打开，直接 readAll() 一律返回空
    //      （实测 body=0）。故未打开时补一次只读打开。
    //      不做 seek(0)：调用方若已把设备定位到中途，那是它的语义，应保留。
    std::string body;
    if (outgoingData != 0) {
        if (!outgoingData->isOpen()) {
#if !defined(QT_VERSION) || QT_VERSION < 0x040000
            outgoingData->open(IO_ReadOnly);
#else
            outgoingData->open(QIODevice::ReadOnly);
#endif
        }
        const QByteArray raw = outgoingData->readAll();
        if (raw.size() > 0) {
            // QByteArray 在 Qt3 就是 QMemArray<char>，data()+size() 无损，
            // 可承载含 NUL 的请求体（QCString 做不到，见 qnam_shim.h 注释）。
            body.assign(raw.data(), raw.size());
        }
    }
    // 注意：本类自己也有 get/put/head/sendCustomRequest（同名的窄切面 API），
    // 它们在类作用域**遮蔽**基类 QNetworkAccessManager 的同名传输方法，故这里
    // 必须显式限定 QNetworkAccessManager::，否则会走进本类那些 QString 版本。
    //
    // ★ 统一走 sendCustomRequest，不再按 verb 分派到 mkcol/propfind/deleteResource。
    // 理由有二，都是实测/查证结论，不是风格偏好：
    //   1. 垫片里 mkcol() 与 propfind() 的实现本身就是
    //      `return sendCustomRequest(req, "MKCOL")` 的薄包装，分派纯属冗余；
    //      而 Qt5+ 的 QNetworkAccessManager **根本没有** mkcol/propfind 成员，
    //      保留分派会直接让 Qt6 编不过。
    //   2. 垫片 propfind() 会硬写 `Depth: 1`，把调用方 proFindRequest() 里
    //      按 depth 参数设的头**覆盖掉**（depth=0 被静默改成 1，语义错误）。
    //      绕过它之后 Depth 才能真正生效。
    // GET/POST 也照样走这里：全部交给 QWebdavTransport 泵。
    // 显式限定基类：本类有个同名 QString 版 sendCustomRequest 会遮蔽基类。
    // QString→QByteArray 无隐式转换（Qt3 下 QMemArray 更没有），故手工转。
    //
    // ★ timeout 必须在 sendCustomRequest **之前**设，两个分支都适用。
    //   Qt3：垫片在 sendCustomRequest 内部就把 req.timeoutSec() 拷进
    //        QNetworkReply::reqTimeoutSec（qnam_shim.cpp:851），再由 issue()
    //        传给 QWebdavTransport::send（qnam_shim.cpp:993）。
    //   Qt4+：原生 QNetworkAccessManager 同样在发包时快照 timeout。
    //   请求一旦发出，之后再改这份 req 就是死代码。实测：原先把这段放在调用
    //   之后，超时功能**完全不生效**（探针 /slow 挂到 15s 兜底才返回，
    //   而非 2s 限额）。
    if (m_transferTimeoutMsecs > 0) {
        req.setTransferTimeout(m_transferTimeoutMsecs);
    }
#if !defined(QT_VERSION) || QT_VERSION < 0x040000
    // Qt3 垫片：打标记，要求 issue() 把这个请求交给自建泵。**所有**动词都打，
    // 因为 qldox EventPoller 没有 401 → Basic Auth 重发链（grep -c 401 = 0），
    // GET 走那边就永远拿 401。详见 QNetworkRequest::setForceOwnTransport 注释。
    req.setForceOwnTransport(true);
    // ★ 这里必须用 QCString，不能用 qToLatin1BA()。
    //   基类 sendCustomRequest 的 verb 形参是 `const char*`，而 Qt3 的
    //   QByteArray 就是 QMemArray<char>，其 data() **不保证** NUL 终止
    //   （与 QCString 相反）。把 QByteArray 隐式转成 const char* 后，
    //   std::string(verb) 会一路读过缓冲末尾。
    //   实测（/tmp/opencode/rawdump.py 抓包）：verb 变成 "GETh\xfb\x55"，
    //   服务端按未知动词回 501，客户端遂报 status=501 / err=399。
    //   QCString 在 Qt3 是 C 字符串类型，data() 保证以 '\0' 结尾。
    const QCString verb(method.latin1());
    reply = QNetworkAccessManager::sendCustomRequest(req, verb.data(), body);
#else
    // Qt4+：原生 QNetworkAccessManager 自带完整 HTTP 能力（动词靠
    // sendCustomRequest 传递、401 由原生信号重发），无需标记，直接走原生。
    reply = QNetworkAccessManager::sendCustomRequest(req, qToLatin1BA(method),
                                                    QByteArray(body.data(), body.size()));
#endif
    if (reply != 0) {
        m_lastReply = reply;
        attachErrorBridge(reply);
    }
    return reply;
}

// ── 错误桥接 ─────────────────────────────────────────────────────────────
// 为什么需要：davbisync 连的是 errorChanged(QString)，它要的是**人可读的失败
// 原因**；而传输层只会给一个 QNetworkReply::NetworkError 枚举码。不桥接的话，
// 认证失败/证书被拒/主机不通在 davbisync 侧全塌缩成同一个「失败」。
void QWebdavLite::attachErrorBridge(QNetworkReply* reply)
{
#if !defined(QT_VERSION) || QT_VERSION < 0x040000
    using ::connect;
    connect(reply, &QNetworkReply::error, this,
            [this, reply](QNetworkReply::NetworkError) {
                // 只在真的有错时上报；NoError 不该触发 errorChanged。
                if (reply->error() != QNetworkReply::NoError) {
                    emitErrorChanged(describeError(reply));
                }
            });
#else
    connect(reply, &QNetworkReply::errorOccurred, this,
            [this, reply](QNetworkReply::NetworkError) {
                if (reply->error() != QNetworkReply::NoError) {
                    emitErrorChanged(describeError(reply));
                }
            });
#endif
}

QString QWebdavLite::describeError(QNetworkReply* reply) const
{
    if (reply == 0) {
        return QString::fromUtf8("WebDAV: 无效 reply");
    }
    // 优先用垫片/原生给的文字串（curl 错误原文），它比枚举码可读得多。
    const QString detail = reply->errorString();
    // ★ 本函数所有非 ASCII 字面量必须走 QString::fromUtf8()，**不能**用
    //   QString("中文")。Qt3 的 QString(const char*) 按 Latin-1 逐字节解释，
    //   源文件里的 UTF-8 字节会被拆成一串 U+00xx 假字符；再经 qToUtf8BA
    //   重新编码就成了双重编码乱码，davbisync 日志里全是「èµæºä¸å¬å¨」。
    //   与 207 解析那个坑同源（见 移植计划.md §6.3e-1 第五阶段根因 2）。
    // 注：ContentAccessDenied 在 Qt3 垫片与 Qt6.7 原生里**同名**（都无 Error
    // 后缀），不需要版本分支；早期 Qt5 才短暂有过 ContentAccessDeniedError。
    switch (reply->error()) {
    case QNetworkReply::ContentAccessDenied:
        return QString::fromUtf8("WebDAV: 认证/访问被拒（401/403）") +
               (detail.isEmpty() ? QString() : QString::fromUtf8(" - ") + detail);
    case QNetworkReply::ContentNotFoundError:
        return QString::fromUtf8("WebDAV: 资源不存在（404）") +
               (detail.isEmpty() ? QString() : QString::fromUtf8(" - ") + detail);
    case QNetworkReply::ConnectionRefusedError:
        return QString::fromUtf8("WebDAV: 连接被拒") +
               (detail.isEmpty() ? QString() : QString::fromUtf8(" - ") + detail);
    case QNetworkReply::HostNotFoundError:
        return QString::fromUtf8("WebDAV: 主机名解析失败") +
               (detail.isEmpty() ? QString() : QString::fromUtf8(" - ") + detail);
    case QNetworkReply::TimeoutError:
        return QString::fromUtf8("WebDAV: 超时") +
               (detail.isEmpty() ? QString() : QString::fromUtf8(" - ") + detail);
    case QNetworkReply::SslHandshakeFailedError:
        return QString::fromUtf8("WebDAV: TLS 握手/证书校验失败") +
               (detail.isEmpty() ? QString() : QString::fromUtf8(" - ") + detail);
    default:
        break;
    }
    return detail.isEmpty() ? QString::fromUtf8("WebDAV: 请求失败") : detail;
}

QNetworkReply* QWebdavLite::createRequest(const QString& method, QNetworkRequest& req,
                                          const QByteArray& outgoingData)
{
    // Qt3 的 QBuffer 构造/setBuffer 都按**值**收 QByteArray（无指针重载），
    // 只能浅拷贝一份；无害——qnam_shim 的 put() 是**同步** readAll() 取体
    // （qnam_shim.cpp:784），不持有设备指针，故栈上 QBuffer 生命周期安全。
    // Qt4+ 的 QBuffer 构造收 `QByteArray*`（指向的对象被 take 走），且
    // setBuffer 在 Qt5.15+ 已移除，故这里按版本分支。
#if !defined(QT_VERSION) || QT_VERSION < 0x040000
    QBuffer buf(outgoingData);
#else
    QBuffer buf(const_cast<QByteArray*>(&outgoingData));
#endif
    return createRequest(method, req, static_cast<QIODevice*>(&buf));
}

QNetworkReply* QWebdavLite::sendCustomRequest(const QString& method,
                                              const QByteArray& data,
                                              QNetworkRequest& req,
                                              QIODevice* outgoingData)
{
    Q_UNUSED(method);
    Q_UNUSED(data);
    return createRequest(QString("PROPFIND"), req, outgoingData);
}

// ═══ 10 个传输方法 ════════════════════════════════════════════════════
QNetworkReply* QWebdavLite::get(const QString& path)
{
    QNetworkRequest req(buildUrl(path));
    req.setRawHeader("Connection", "close");
    return createRequest(QString("GET"), req, static_cast<QIODevice*>(0));
}

QNetworkReply* QWebdavLite::get(const QString& path, QIODevice* data)
{
    return get(path, data, 0);
}

QNetworkReply* QWebdavLite::get(const QString& path, QIODevice* data,
                                quint64 fromRangeInBytes)
{
    QNetworkRequest req(buildUrl(path));
    req.setRawHeader("Connection", "close");
    if (fromRangeInBytes != 0) {
        QByteArray fromRange;
        qByteArrayAppend(fromRange, "bytes=");
        qByteArrayAppend(fromRange, qNumberToByteArray((qint64)fromRangeInBytes));
        qByteArrayAppend(fromRange, "-");
        req.setRawHeader("Range", fromRange);
    }
    return createRequest(QString("GET"), req, data);
}

QNetworkReply* QWebdavLite::put(const QString& path, QIODevice* data,
                                const QDateTime& dt)
{
    QNetworkRequest req(buildUrl(path));
    req.setRawHeader("Connection", "close");
    if (dt.isValid()) {
        // RFC1123（QLocale 英文 US 的 "ddd, dd MMM yyyy hh:mm:ss"），走 qdatetime
        // 垫：Qt3 的 QLocale::toString 无 (QDateTime, fmt) 重载。
        req.setRawHeader("Date", qToLatin1BA(
                                 qFormatDateTime(qDateTimeToUtc(dt),
                                                 "ddd, dd MMM yyyy hh:mm:ss")));
    }
    return createRequest(QString("PUT"), req, data);
}

QNetworkReply* QWebdavLite::put(const QString& path, const QByteArray& data,
                                const QDateTime& dt)
{
    // QBuffer 构造收值(Qt3/Qt4) 还是指针(Qt5+)，与 createRequest 里的分支同因。
#if !defined(QT_VERSION) || QT_VERSION < 0x040000
    QBuffer buf(data);
#else
    QBuffer buf(const_cast<QByteArray*>(&data));
#endif
    return put(path, static_cast<QIODevice*>(&buf), dt);
}

QNetworkReply* QWebdavLite::mkdir(const QString& dir)
{
    QNetworkRequest req = mkcolRequest(dir);
    return createRequest(QString("MKCOL"), req, static_cast<QIODevice*>(0));
}

QNetworkReply* QWebdavLite::move(const QString& pathFrom, const QString& pathTo,
                                 bool overwrite)
{
    QNetworkRequest req(buildUrl(pathFrom));
    QUrl dest(buildUrl(pathTo));
    // toString().utf8() 是 Qt3 写法；qToUtf8BA 两侧都成立（Qt3 走 QCString 中转）。
    req.setRawHeader("Destination", qToUtf8BA(dest.toString()));
    req.setRawHeader("Connection", "close");
    if (overwrite) {
        req.setRawHeader("Overwrite", "T");
    }
    // 必须走 createRequest，不能直调基类 sendCustomRequest：
    // 直调会跳过 createRequest 里的 forceOwnTransport 标记、错误桥 attachErrorBridge
    // 和 transferTimeout 注入。缺标记时 MOVE 会落回 qldox EventPoller，而那边没有
    // 401 → Basic Auth 重发链（grep -c 401 = 0），带认证的 MOVE 就永远拿 401；
    // 缺错误桥/超时则 MOVE 的失败原因在 davbisync 侧塌缩成同一个「失败」。
    return createRequest(QString("MOVE"), req, static_cast<QIODevice*>(0));
}

QNetworkReply* QWebdavLite::remove(const QString& path)
{
    QNetworkRequest req(buildUrl(path));
    req.setRawHeader("Connection", "close");
    return createRequest(QString("DELETE"), req, static_cast<QIODevice*>(0));
}

QNetworkReply* QWebdavLite::options(const QString& path)
{
    QNetworkRequest req(buildUrl(path));
    req.setRawHeader("Connection", "close");
    return createRequest(QString("OPTIONS"), req, static_cast<QIODevice*>(0));
}

QNetworkReply* QWebdavLite::head(const QString& path)
{
    QNetworkRequest req(buildUrl(path));
    req.setRawHeader("Connection", "close");
    return createRequest(QString("HEAD"), req, static_cast<QIODevice*>(0));
}

QNetworkReply* QWebdavLite::propfind(const QString& path, int depth)
{
    QNetworkRequest req = proFindRequest(path, depth);
    QByteArray query;
    qByteArrayAppend(query, "<?xml version=\"1.0\"?>\r\n");
    qByteArrayAppend(query, "<propfind xmlns=\"DAV:\"><allprop/></propfind>");
    // 走 createRequest 而非基类 propfind()。两条理由：
    //   1. 基类 propfind() 只有垫片里有；Qt5+ 的 QNetworkAccessManager 根本没这个
    //      成员（WebDAV 动词一律用 sendCustomRequest 发），走它 Qt6 编不过。
    //   2. 垫片的 propfind() 会硬写 `Depth: 1`，把 proFindRequest() 按 depth
    //      参数设的头覆盖掉——depth=0 被静默改成 1，语义错误。绕开它 Depth 才生效。
    //    （query 作为请求体传出，与垫片 propfind 的行为一致。）
    return createRequest(QString("PROPFIND"), req, query);
}
