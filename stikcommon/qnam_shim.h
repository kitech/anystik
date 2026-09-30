#ifndef QNAM_SHIM_H
#define QNAM_SHIM_H

// QNAM 兼容层（Qt3 专用）：让共享模块的 QNetworkAccessManager / QNetworkReply /
// QNetworkRequest / QNetworkCookie / QNetworkCookieJar / QHttpMultiPart /
// QHttpPart 调用在 Qt3.5 下以原生语义编译并运行。
//
// 唯一后端 = qldox EventPoller（curl_multi 泵线程 + postEvent + CustomEventBase），
// 见 qltox 移植计划 §5。投递范式照 restapi.cpp：回调在泵线程，事件投到 reply
// 所属线程（GUI），槽在本地线程顺序执行。
//
// 启用方式：共享模块在 QT3_BUILD 分支 include 本头；Qt4+ 走原生 <QNetwork*>
// （Qt4.6+ 有 QNetworkAccessManager）。本头不引用任何 Qt5 专属 API。
//
// 跨版本信号：Qt3 无 moc 信号槽。为让 `connect(reply, &QNetworkReply::finished,
// this, lambda)` 能编译，本头把每个"信号"声明为同参不同标签的重载成员，
// 由 qconnect_slots.h 提供模板 connect 把它们路由到内部槽注册表。
//
// 约定：
//   * QNetworkRequest 是值语义（非 QObject），含 headers/attributes/timeout。
//   * QNetworkReply 是 QObject：manager->get/post/... 返回它，EventPoller done
//     回调把结果 postEvent 回本线程，事件处理器发射 finished/readyRead 等。
//   * QNetworkAccessManager 持有 cookie jar 与活动 reply 生命周期表。
//   * 本层所有跨线程交接不触碰 Qt 线程 API，能配 c++14 编过 Qt3。

#include <qobject.h>
#include <qstring.h>
#include <qcstring.h>
#include <qstringlist.h>
#include <qurl.h>
#include <qmap.h>
#include <qfile.h>
#include <qstring.h>     // QByteArray (Qt3 = QMemArray<char>)
#include <qvariant.h>
#include <qdatetime.h>
#include <qiodevice.h>
#include <qevent.h>

#include <string>
#include <map>
#include <vector>
#include <functional>

#include "compatcore34.h"   // CustomEventBase / EventType34 / qFromUtf8 等
#include "qlist_shim.h"    // QList 值容器（vendor TU 也 include 它，类型需一致）
#include "qglobaltype_shim.h"   // Qt3: qint64 / qAbsPath / qMkdir

// Qt3 的 QByteArray(=QMemArray<char>) 无 constData()（只有 data()）、无 '+='
// 与 const char* 构造。这里只补 constData()；'+=/构造' 场景统一走 QCString。
#ifdef QT3_BUILD
inline const char* qbaConstData(const QByteArray& a) { return a.data(); }
#else
inline const char* qbaConstData(const QByteArray& a) { return a.constData(); }
#endif

class QNetworkReplyEvent;
class QNetworkAccessManager;

// ─────────────────────────────────────────────────────────────────────────────
// QNetworkRequest（值语义）
// ─────────────────────────────────────────────────────────────────────────────
class QNetworkRequest
{
public:
    enum KnownHeaders {
        ContentDispositionHeader,
        ContentTypeHeader,
        ContentLengthHeader,
        UserAgentHeader,
        ServerHeader,
        CookieHeader,
        LocationHeader,
    };
    enum Attribute {
        HttpStatusCodeAttribute = 0x01000001,
        HttpReasonPhraseAttribute,
        RedirectionTargetAttribute,
        RedirectPolicyAttribute = 0x01000010,
        ManualRedirectPolicy = 0x01000011,
        NoLessSafeRedirectPolicy = 0x01000013,
        Http2AllowedAttribute  = 0x02000001,
        Http2DirectAttribute   = 0x02000002,
    };

    QNetworkRequest() = default;
    explicit QNetworkRequest(const QUrl& url);
    void setUrl(const QUrl& url) { m_url = url; }
    QUrl url() const { return m_url; }

    // Qt3 下 QByteArray 无 (const char*) 构造，setRawHeader 直接收 C 串
    void setRawHeader(const char* name, const char* value);
    void setRawHeader(const QByteArray& name, const QByteArray& value);
    QByteArray rawHeader(const char* name) const;
    bool hasRawHeader(const char* name) const;
    QByteArray rawHeaderList() const;
    void setHeader(KnownHeaders header, const QVariant& value);
    QVariant header(KnownHeaders header) const;
    void setAttribute(Attribute code, const QVariant& value) { m_attrs[code] = value; }
    QVariant attribute(Attribute code, const QVariant& def = QVariant()) const;
    void setTransferTimeout(int timeoutSec = 60000) { m_timeoutSec = timeoutSec; }
    int transferTimeout() const { return m_timeoutSec; }

    const std::map<std::string,std::string>& rawHeaders() const { return m_rawHeaders; }
    std::map<std::string,std::string>& rawHeaders() { return m_rawHeaders; }
    std::map<int,QVariant>& attributes() { return m_attrs; }
    const std::map<int,QVariant>& attributes() const { return m_attrs; }
    std::string bodyData;          // POST/PUT 载荷（multipart 或原始字节）
    std::string contentType;       // 显式 Content-Type（multipart 时填边界码）
    int timeoutSec() const { return m_timeoutSec; }

    // ★ 强制走 stikcommon 自建的 verb-aware 传输（qwebdavtransport），
    //   不进 qldox EventPoller。
    //
    // 为什么需要这个开关（e2e 实测踩出来的，不是推测）：
    //   原本只按「非 GET/POST 才走自建泵」分流。但 `grep -c 401
    //   qldox/eventpoller.cpp` 结果是 **0** —— EventPoller 根本没有
    //   401 → Basic Auth 重发链。而 GET 是 WebDAV 最常用的操作（下载文件），
    //   于是「带凭据的 GET」在 EventPoller 侧永远拿 401，且**不会**触发
    //   authenticationRequired，认证形同虚设。e2e 实测：正确凭据也只发出
    //   1 次请求、无 Authorization 头、服务端回 401。
    //   而 MKCOL/MOVE 等走自建泵的路径反而能正常重发。
    //
    // 所以 WebDAV 侧（QWebdavLite）必须对**所有**动词（含 GET/POST）显式
    // 打这个标记，而不是靠 verb 猜。其余普通 HTTP 调用方不设此标记，
    // 继续走 EventPoller，改动面不外扩。
    void setForceOwnTransport(bool on) { m_forceOwnTransport = on; }
    bool forceOwnTransport() const { return m_forceOwnTransport; }

private:
    QUrl m_url;
    std::map<std::string, std::string> m_rawHeaders;
    std::map<int, QVariant> m_attrs;
    int m_timeoutSec = 60000;
    bool m_forceOwnTransport = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// SSL 类型（Qt3 没有 QtNetwork 的 QSslError/QSslCertificate/QCryptographicHash）
//
// 用途：vendor qwebdav 的 sslErrors/replyError 链路与 acceptSslCertificate 依赖
// QSslError::certificate() 和 QSslCertificate::digest()（qwebdav.cpp:259-262）。
// 这里的 QSslCertificate 是**数据载体**：指纹由 qsslprobe 用 OpenSSL 真实算出，
// 校验本身由 curl/OpenSSL 完成，类型层不做任何桩断言。
//
// 注意：QCryptographicHash 在本垫片里只暴露 digest 计算所需的 Md5/Sha1；
// QWebdavDirParser 用的 Md5 是 QSslCertificate::digest() 的实参。
// ─────────────────────────────────────────────────────────────────────────────
class QCryptographicHash
{
public:
    enum Algorithm { Md5 = 0, Sha1 = 1 };
    // Qt4+ 的 QCryptographicHash 是 QIODevice 子类（hash.addData/result）。
    // QWebdav 只用 digest() 的枚举值，故此处只需枚举可被引用。
    Algorithm algorithm() const { return m_alg; }
    explicit QCryptographicHash(Algorithm a) : m_alg(a) {}

private:
    Algorithm m_alg;
};

class QSslCertificate
{
public:
    QSslCertificate() {}
    // 真实探测结果构造（qsslprobe 提供）
    QSslCertificate(const QByteArray& md5, const QByteArray& sha1, const QString& pem)
        : m_md5(md5), m_sha1(sha1), m_pem(pem) {}

    bool isNull() const { return m_md5.isEmpty() && m_sha1.isEmpty(); }

    // 与 Qt4+ 同语义：返回**原始**摘要字节（Md5 16B / Sha1 20B）。
    // QWebdav::sslErrors 拿它和 hexToDigest() 出来的 pin 直接 == 比较。
    QByteArray digest(QCryptographicHash::Algorithm a) const
    {
        return (a == QCryptographicHash::Md5) ? m_md5 : m_sha1;
    }

    QString toPem() const { return m_pem; }
    QString subjectInfo(QCryptographicHash::Algorithm = QCryptographicHash::Sha1) const
    {
        return QString();   // QWebdav 未使用 subjectInfo/debugCertificate
    }

private:
    QByteArray m_md5;
    QByteArray m_sha1;
    QString m_pem;
};

class QSslError
{
public:
    // Qt4+ QSslError::SslError 枚举取值
    enum SslError {
        NoError = 0,
        UnableToGetIssuerCertificate = 2,
        SelfSignedCertificate = 9,
        CertificateUntrusted = 11,
        HostNameMismatch = 12,
    };

    QSslError() : m_err(NoError) {}
    QSslError(SslError e, const QSslCertificate& cert) : m_err(e), m_cert(cert) {}

    SslError error() const { return m_err; }
    QSslCertificate certificate() const { return m_cert; }
    QString errorString() const { return m_errorString; }

private:
    SslError m_err;
    QSslCertificate m_cert;
    QString m_errorString;
};

// QWebdav::provideAuthenication(QNetworkReply*, QAuthenticator*) 用
class QAuthenticator
{
public:
    QAuthenticator() {}
    QString userName() const { return m_user; }
    void setUserName(const QString& u) { m_user = u; }
    QString password() const { return m_pass; }
    void setPassword(const QString& p) { m_pass = p; }
    QString realm() const { return m_realm; }
    void setRealm(const QString& r) { m_realm = r; }

private:
    QString m_user, m_pass, m_realm;
};

// ─────────────────────────────────────────────────────────────────────────────
// QNetworkReply —— QObject + 槽注册表
// ─────────────────────────────────────────────────────────────────────────────
class QNetworkReply : public QObject
{
public:
    enum NetworkError {
        NoError = 0,
        ConnectionRefusedError = 1,
        RemoteHostClosedError = 3,
        HostNotFoundError = 4,
        TimeoutError = 8,
        OperationCanceledError = 12,
        SslHandshakeFailedError = 15,
        UnknownNetworkError = 99,
        ProtocolFailure = 399,
        UnknownServerError = 199,
        ContentNotFoundError = 203,
        ContentAccessDenied = 201,
        OperationNotSupportedError = 202,
    };

    // 「信号」：声明为同参不同名的成员函数（定义在 .cpp 保证地址唯一），
    // `&QNetworkReply::finished` 等取址编译通过，实际发射经 emitSignal() 路由
    // 到 qconnect_slots.h 注册的槽。非 inline 外部函数地址唯一（C++14），
    // qconnect_slots 据此区分同签名信号。
    void finished();
    void readyRead();
    void uploadProgress(qint64 sent, qint64 total);
    void downloadProgress(qint64 recv, qint64 total);
    void error(QNetworkReply::NetworkError code);
    void redirected(const QUrl& target);
    void metaDataChanged();
    // Qt4+ 的两个信号：QWebdav connect/disconnect 这两个（qwebdav.cpp:180-181,456）
    void errorOccurred(QNetworkReply::NetworkError code);
    void redirectAllowed(const QUrl& target);
    // Qt4+ 的 sslErrors 信号：TLS 校验失败时由垫片在发 finished/error 之前发射，
    // 槽（QWebdav::sslErrors）据 pin 决定 ignoreSslErrors() 还是 abort()。
    void sslErrors(const QList<QSslError>& errors);

    // QNAM 兼容接口
    QUrl url() const { return m_finalUrl; }
    void setUrl(const QUrl& u) { m_finalUrl = u; }
    QByteArray readAll();
    int error() const { return (int)m_error; }
    QString errorString() const { return m_errorString; }
    QVariant attribute(QNetworkRequest::Attribute code,
                       const QVariant& def = QVariant()) const;
    QByteArray rawHeader(const char* name) const;
    bool hasRawHeader(const char* name) const;
    QByteArray rawHeaderList() const;
    void abort();
    void disconnect() { m_slots.clear(); }

    // ── SSL（QWebdav 用）──
    // Qt4+ 语义：置位"忽略本次 TLS 错误"。本垫片在 sslErrors 派发返回后据此
    // 把对端证书收进 CA bundle 并重发该请求（见 qnam_shim.cpp 的 retry 段）。
    void ignoreSslErrors() { m_sslIgnore = true; }
    // 本次传输是否因证书问题需要用户裁决
    bool sslErrorsPending() const { return m_sslIgnore || m_sslAbortedByUser; }
    // stikcommon 自建 verb-aware 传输（qwebdavtransport）用的取消标志。
    // EventPoller 无句柄可取消，abort() 只能靠软标志（移植计划 §5.4a）；
    // 自建传输有 easy handle，但为与既有 abort 语义一致，仍用此标志。
    volatile bool m_liteCancel;

    // 探测到的对端证书（TLS 失败时填充，供 accept 后收录）
    const QSslCertificate& probedCertificate() const { return m_sslCert; }
    void setProbedCertificate(const QSslCertificate& c) { m_sslCert = c; }

    // ── QIODevice 侧接口（QWebdavDirParser 轮询用）──
    // Qt4+ QNetworkReply 继承 QIODevice；Qt3 QObject 没有这些，本垫片补最小集。
    // 本垫片不继承 QIODevice（避免动到既有 readAll/缓冲布局），故按普通成员实现。
    bool isFinished() const { return m_done; }
    qint64 bytesAvailable() const { return (qint64)m_body.length(); }
    qint64 bytesToWrite() const { return 0; }   // 上行走 reqData，无背压队列
    void close() { }                            // 结果一次性投递，无需 close
    // deleteLater() 直接继承 QObject 的（Qt3.5 qobject.h:174 已有，
    // 依赖 DeferredDelete 事件；qlstik/src/main.cpp:87 有 app.exec() 事件循环，
    // 故原生实现可用，无需自造）。

    // 内部：EventPoller done 结果落地（泵线程回调里调，仅写共享缓冲）
    void deliverResult(int httpCode, const std::string& curlErr,
                       const std::string& body,
                       const std::map<std::string,std::string>& headers,
                       bool aborted);

    // 事件分发：QNetworkReplyEvent 落到本对象线程时被 QApplication 调用，
    // 我们在这里把泵线程的结果转成信号发射。Qt3 的 QObject::event 是虚函数，
    // 垫片直接 override 即可（不依赖 Q_OBJECT/moc）。
    bool event(QEvent* e) override;

    // curl 错误串 → NetworkError 映射
    NetworkError mapCurlError(const std::string& err) const;

    // TLS 链路内部实现（见 qnam_shim.cpp）
    bool isTlsVerifyFailure(const std::string& err) const;
    bool handleSslErrors();

    bool errorOccurred() const { return m_error != NoError; }

    // 进度回调（EventPoller 泵线程）经 postEvent 投递到本线程
    void deliverProgress(qint64 received, qint64 total);

    // 槽注册表（qconnect_slots.h 使用）
    // map<string, vector<function>>：槽名 → 回调列表
    typedef std::function<void()> VoidSlot;
    typedef std::function<void(QNetworkReply::NetworkError)> ErrorSlot;
    typedef std::function<void(qint64,qint64)> ProgressSlot;
    typedef std::function<void(const QUrl&)> RedirectSlot;
    std::vector<VoidSlot>& slots_finished() { return m_slots["finished"]; }
    std::vector<VoidSlot>& slots_readyRead() { return m_slots["readyRead"]; }
    std::vector<ProgressSlot>& slots_uploadProgress() { return m_up; }
    std::vector<ProgressSlot>& slots_downloadProgress() { return m_dp; }
    std::vector<ErrorSlot>& slots_error() { return m_er; }
    std::vector<RedirectSlot>& slots_redirected() { return m_rd; }
    typedef std::function<void(const QList<QSslError>&)> SslErrorSlot;
    std::vector<SslErrorSlot>& slots_sslErrors() { return m_ssl; }
    // errorOccurred / redirectAllowed 复用既有注册表（同签名），无需新槽位
    void emitErrorOccurred(NetworkError e) { emitError(e); }
    void emitRedirectAllowed(const QUrl& t) { emitRedirected(t); }
    void emitSslErrors(const QList<QSslError>& e) { for (size_t i=0;i<m_ssl.size();++i) m_ssl[i](e); }
    // 由 sslErrors 派发后回调：QWebdav 选择了 ignore 还是 abort
    void markSslAbortedByUser() { m_sslAbortedByUser = true; }
    // TLS 失败后把证书收进 CA bundle 并重发；成功返回 true 表示已重发
    bool acceptSslAndRetry();
    QNetworkAccessManager* sslManager() const { return m_manager; }

    // Qt4+ 里 manager 的 finished(reply) 与 reply 的 finished() 同轮发射；
    // 统一在这里补 manager 通知，保证各条错误路径都覆盖到（QWebdav::replyFinished
    // 正是挂在 manager::finished 上收尾的）。实体在 .cpp：此处 manager 仅前置声明。
    void emitFinished();
    void emitReadyRead() { runVoid("readyRead"); }
    void emitUpload(qint64 s, qint64 t);
    void emitDownload(qint64 r, qint64 t);
    void emitError(NetworkError e) { m_error = e; for (size_t i=0;i<m_er.size();++i) m_er[i](e); }
    void emitRedirected(const QUrl& t) { for (size_t i=0;i<m_rd.size();++i) m_rd[i](t); }

private:
    void runVoid(const std::string& name);
    QCString m_body;                                   // Qt3: QCString 可 +=
    std::map<std::string,std::vector<VoidSlot>> m_slots;   // finished/readyRead
    std::vector<ProgressSlot> m_up;
    std::vector<ProgressSlot> m_dp;
    std::vector<ErrorSlot> m_er;
    std::vector<RedirectSlot> m_rd;
    std::vector<SslErrorSlot> m_ssl;
    bool m_sslIgnore = false;          // QWebdav 调了 ignoreSslErrors()
    bool m_sslAbortedByUser = false;   // QWebdav emit checkSslCertifcate 后 abort()
    bool m_sslRetried = false;         // 收录后已重发过一次，防无限重试
    bool m_inSslDispatch = false;      // 正在派发 sslErrors（窗口内 abort 视为用户拒绝）
    bool m_authRetried = false;        // 401 补 Authorization 后已重发过一次
    QSslCertificate m_sslCert;         // 探测到的对端证书（accept 时收录）
    QNetworkAccessManager* m_manager = nullptr;   // retry 时回投给 manager
    std::map<std::string, std::string> m_rawHdrs;
    std::map<int, QVariant> m_attrs;      // 含 HttpStatusCode / RedirectionTarget
    NetworkError m_error = NoError;
    QString m_errorString;
    QUrl m_finalUrl;
    bool m_aborted = false;
    bool m_done = false;
    class QNetworkCookieJar* m_jar = nullptr;    // 创建时从 manager 挂入，仅 GUI 线程用

    friend class QNetworkAccessManager;
public:
    QNetworkReply(QObject* parent = nullptr);
    ~QNetworkReply() override;
    // Qt3 QObject 无 setParent/reparent：multi 等"父子所有权"由 reply 显式托管。
    // public：QHttpMultiPart::setParent 生命周期垫会用（qnam_shim.cpp）。
    void qnamAddOwned(QObject* o) { if (o) m_qnamOwned.push_back(o); }
    // 请求数据（EventPoller 建立句柄需要）
    QUrl requestedUrl;
    std::string method;
    std::string reqData;
    std::string reqContentType;
    std::map<std::string,std::string> reqExtraHeaders;
    int reqTimeoutSec = 60000;
    bool reqAbort = false;
    // 来自 QNetworkRequest::forceOwnTransport()：见该字段处注释。WebDAV 全动词
    // 都靠它绕开没有 401 链的 EventPoller。
    bool reqForceOwnTransport = false;
    std::vector<QObject*> m_qnamOwned;   // qt3 setParent 模拟：reply 析构时连带删除
};

// ─────────────────────────────────────────────────────────────────────────────
// QNetworkAccessManager
// ─────────────────────────────────────────────────────────────────────────────
class QNetworkCookie;
class QNetworkCookieJar;

class QNetworkAccessManager : public QObject
{
public:
    explicit QNetworkAccessManager(QObject* parent = nullptr);
    ~QNetworkAccessManager() override;

    QNetworkReply* get(const QNetworkRequest& req);
    QNetworkReply* head(const QNetworkRequest& req);
    QNetworkReply* post(const QNetworkRequest& req, const QByteArray& data);
    QNetworkReply* post(const QNetworkRequest& req, class QHttpMultiPart* multi);
    QNetworkReply* put(const QNetworkRequest& req, const QByteArray& data);
    QNetworkReply* put(const QNetworkRequest& req, QIODevice* data);
    QNetworkReply* deleteResource(const QNetworkRequest& req);
    // WebDAV 动词（窄切面 QWebdavLite 需要；vendor qwebdav 也用）
    QNetworkReply* mkcol(const QNetworkRequest& req);
    QNetworkReply* propfind(const QNetworkRequest& req, const QByteArray& query);
    QNetworkReply* sendCustomRequest(const QNetworkRequest& req,
                                     const char* verb,
                                     const QByteArray& data = QByteArray());
    // 请求体的无损通道。Qt3 的 QByteArray(=QMemArray<char>) 与 QCString 都无法
    // 无损表达任意二进制：QCString(str, maxlen) 的 maxlen 是**含 NUL 的上限**
    // （实测 QCString(buf,9).length()==8），QByteArray 则没有 (const char*, int)
    // 公开构造。故请求体改由 std::string 承载，见 .cpp 中 put(QIODevice*) 注释。
    QNetworkReply* sendCustomRequest(const QNetworkRequest& req,
                                     const char* verb,
                                     const std::string& body);

    QNetworkCookieJar* cookieJar() const { return m_cookieJar; }
    void setCookieJar(QNetworkCookieJar* jar);
    void setTransferTimeout(int timeoutSec = 60000) { m_timeoutSec = timeoutSec; }
    int transferTimeout() const { return m_timeoutSec; }

    // ── 「信号」：与 Qt4+ QNetworkAccessManager 同签名 ──
    // QWebdav 继承本类（qwebdav.cpp:55），构造时连的是
    //   connect(this, &QWebdav::finished, this, &QWebdav::replyFinished)
    //   connect(this, &QWebdav::authenticationRequired, this, &QWebdav::provideAuthenication)
    // 取址拿到的是本类这两个成员，故需走 qconnect_slots 的槽注册表（不 moc）。
    void finished(QNetworkReply* reply);
    void authenticationRequired(QNetworkReply* reply, QAuthenticator* authenticator);

    typedef std::function<void(QNetworkReply*)> ReplySlot;
    typedef std::function<void(QNetworkReply*, QAuthenticator*)> AuthSlot;
    std::vector<ReplySlot>& slots_finished() { return m_finished; }
    std::vector<AuthSlot>& slots_authRequired() { return m_authReq; }
    void emitFinished(QNetworkReply* r)
    {
        for (size_t i = 0; i < m_finished.size(); ++i) m_finished[i](r);
    }
    // 401 时派发：槽（QWebdav::provideAuthenication）填好 authenticator 后，
    // 垫片据此加 Authorization 头并重发（见 qnam_shim.cpp）。
    void emitAuthenticationRequired(QNetworkReply* r, QAuthenticator* a)
    {
        for (size_t i = 0; i < m_authReq.size(); ++i) m_authReq[i](r, a);
    }
    // reply 侧回调 manager 的内部入口
    void notifyReplyFinished(QNetworkReply* r) { emitFinished(r); }
    void notifyAuthRequired(QNetworkReply* r, QAuthenticator* a)
    {
        emitAuthenticationRequired(r, a);
    }
    // 401 重发（垫片内部用，避免 reply 直接依赖 issue）
    void reissue(QNetworkReply* r);

private:
    QNetworkReply* createReply(const QNetworkRequest& req, const std::string& verb);
    void issue(QNetworkReply* r);
    std::vector<ReplySlot> m_finished;
    std::vector<AuthSlot> m_authReq;
    QNetworkCookieJar* m_cookieJar = nullptr;
    bool m_ownsJar = false;
    int m_timeoutSec = 60000;
};

// ─────────────────────────────────────────────────────────────────────────────
// QNetworkCookie / QNetworkCookieJar
// ─────────────────────────────────────────────────────────────────────────────
class QNetworkCookie
{
public:
    QNetworkCookie() = default;
    QNetworkCookie(const char* name, const char* value)
        : m_name(name), m_value(value) {}
    QCString name() const { return m_name; }
    QCString value() const { return m_value; }
    QString domain() const { return m_domain; }
    void setDomain(const QString& d) { m_domain = d; }
    QString path() const { return m_path; }
    void setPath(const QString& p) { m_path = p; }
    bool isSecure() const { return m_secure; }
    void setSecure(bool s) { m_secure = s; }
    QDateTime expirationDate() const { return m_expiry; }
    void setExpirationDate(const QDateTime& d) { m_expiry = d; }
    bool isHttpOnly() const { return m_httpOnly; }
    void setHttpOnly(bool h) { m_httpOnly = h; }

    static std::vector<QNetworkCookie> parseCookies(const char* raw);
    QByteArray toRawForm() const;

private:
    QCString m_name, m_value;
    QString m_domain, m_path;
    QDateTime m_expiry;
    bool m_secure = false;
    bool m_httpOnly = false;
};

class QNetworkCookieJar : public QObject
{
public:
    explicit QNetworkCookieJar(QObject* parent = nullptr) : QObject(parent) {}
    ~QNetworkCookieJar() override = default;

    virtual std::vector<QNetworkCookie> cookiesForUrl(const QUrl& url) const;
    virtual bool setCookiesFromUrl(const std::vector<QNetworkCookie>& cookieList,
                                   const QUrl& url);

protected:
    std::vector<QNetworkCookie> allCookies() const;
    void setAllCookies(const std::vector<QNetworkCookie>& cookies);
private:
    std::vector<QNetworkCookie> m_cookies;
};

// ─────────────────────────────────────────────────────────────────────────────
// QHttpMultiPart / QHttpPart —— multipart/form-data 拼装（§5.4-c）
// ─────────────────────────────────────────────────────────────────────────────
class QHttpPart
{
public:
    void setHeader(QNetworkRequest::KnownHeaders header, const QVariant& value)
        { m_header = value.toString().utf8(); }
    void setBody(const char* body) { m_body = body ? body : ""; }
    void setBody(const QByteArray& body)
        { m_body.assign(body.data(), body.size()); }
    void setBody(const QCString& body)
        { m_body.assign(body.data(), body.length()); }
    void setBodyDevice(QIODevice* device);
    const char* body() const { return m_body.data(); }
    int bodySize() const { return (int)m_body.size(); }
    const char* header() const { return m_header.data(); }
private:
    QCString m_header;        // Content-Disposition 的值（发射时前缀"Content-Disposition: "）
    std::string m_body;       // 精确长度字节（图片/文本都无尾 NUL，bodySize() 即内容长）
};

class QHttpMultiPart : public QObject
{
public:
    enum ContentType { MixedType, DigestType, AlternativeType, RelatedType,
                       FormDataType };
    explicit QHttpMultiPart(ContentType contentType = MixedType,
                            QObject* parent = nullptr)
        : QObject(parent), m_type(contentType) {}
    // Qt4/6 原生语义：multi 由 reply 父子链持有；Qt3 QObject 无 setParent，
    // 该垫片把所有权登记进 reply 的 m_qnamOwned，reply 析构时连带释放。
    void setParent(QObject* p);
    void append(const QHttpPart& part) { m_parts.push_back(part); }
    const std::vector<QHttpPart>& parts() const { return m_parts; }
    int boundaryString() const { return m_boundary; }
    QByteArray contentTypeHeader() const;
private:
    ContentType m_type;
    std::vector<QHttpPart> m_parts;
    mutable int m_boundary = 0;
};

// 事件：EventPoller done → 本线程 QNetworkReply 处理
class QNetworkReplyEvent : public CustomEventBase
{
public:
    QNetworkReplyEvent(QNetworkReply* reply,
                       int httpCode, const std::string& curlErr,
                       const std::string& body,
                       const std::map<std::string,std::string>& headers,
                       bool aborted);
    QNetworkReply* m_reply;
    int m_httpCode;
    std::string m_curlErr;
    std::string m_body;
    std::map<std::string,std::string> m_headers;
    bool m_aborted;
};

// 进度事件：泵线程 progress → 本线程
class QNetworkProgressEvent : public CustomEventBase
{
public:
    QNetworkProgressEvent(QNetworkReply* reply, qint64 received, qint64 total);
    QNetworkReply* m_reply;
    qint64 m_received;
    qint64 m_total;
};

#endif // QNAM_SHIM_H