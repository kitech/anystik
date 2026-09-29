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
#include "qglobaltype_shim.h"   // Qt3: qint64 / qAbsPath / qMkdir

// Qt3 的 QByteArray(=QMemArray<char>) 无 constData()（只有 data()）、无 '+='
// 与 const char* 构造。这里只补 constData()；'+=/构造' 场景统一走 QCString。
#ifdef QT3_BUILD
inline const char* qbaConstData(const QByteArray& a) { return a.data(); }
#else
inline const char* qbaConstData(const QByteArray& a) { return a.constData(); }
#endif

class QNetworkReplyEvent;

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
    std::string contentType;       // 显式 Content-Type（multipart 时填边界头）
    int timeoutSec() const { return m_timeoutSec; }

private:
    QUrl m_url;
    std::map<std::string, std::string> m_rawHeaders;
    std::map<int, QVariant> m_attrs;
    int m_timeoutSec = 60000;
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

    void emitFinished() { runVoid("finished"); }
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
    // 请求数据（EventPoller 建立句柄需要）
    QUrl requestedUrl;
    std::string method;
    std::string reqData;
    std::string reqContentType;
    std::map<std::string,std::string> reqExtraHeaders;
    int reqTimeoutSec = 60000;
    bool reqAbort = false;
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
    QNetworkReply* sendCustomRequest(const QNetworkRequest& req,
                                     const char* verb,
                                     const QByteArray& data = QByteArray());

    QNetworkCookieJar* cookieJar() const { return m_cookieJar; }
    void setCookieJar(QNetworkCookieJar* jar);
    void setTransferTimeout(int timeoutSec = 60000) { m_timeoutSec = timeoutSec; }
    int transferTimeout() const { return m_timeoutSec; }

private:
    QNetworkReply* createReply(const QNetworkRequest& req, const std::string& verb);
    void issue(QNetworkReply* r);
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
    void setBody(const char* body) { m_body = body; }
    void setBodyDevice(QIODevice* device);
    const char* body() const { return m_body.data(); }
    int bodySize() const { return m_body.size(); }
    const char* header() const { return m_header.data(); }
private:
    QCString m_header;
    QCString m_body;
};

class QHttpMultiPart : public QObject
{
public:
    enum ContentType { MixedType, DigestType, AlternativeType, RelatedType,
                       FormDataType };
    explicit QHttpMultiPart(ContentType contentType = MixedType,
                            QObject* parent = nullptr)
        : QObject(parent), m_type(contentType) {}
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