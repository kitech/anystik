#include "qnam_shim.h"
#include "qwebdavtransport.h"

#include <qapplication.h>
#include <qcstring.h>
#include <qstringlist.h>
#include <qmap.h>
#include <qfile.h>

#include "eventpoller.h"
#include "compatcore34.h"   // qFromUtf8 / qToUtf8 / CustomEventBase
#include "qsslprobe.h"      // 真实探测对端证书（OpenSSL 裸握手）
#include "qcabundle.h"      // 已接受证书收录 + CURL_CA_BUNDLE 激活
#include "qbytearray_shim.h"  // qToBase64（Qt3 Basic 认证）
#include "qstring_shim.h"     // qToUtf8BA：QCString→QByteArray 按 length() 拷

#include <cstdlib>
#include <cstring>

// 对外统一把 QCString（含尾 NUL）转成 size() 语义与原生 QByteArray 一致的
// QByteArray：去掉尾 NUL。原生 Qt 的 QByteArray::size() 不含 NUL，而 Qt3
// QCString 继承的 QMemArray 若在 QCString 上 size() 含 NUL——直接返回会让
// 依赖 size() 的 fromJson/trimmed/写入错位 1 字节。这里用 QMemArray 手工拷贝
// content 部分（size()=内容长，无自动 NUL）。
static QByteArray toCleanQBA(const QCString& s)
{
    int n = s.size();
    if (n > 0 && s.data()[n - 1] == '\0') {
        --n;
    }
    if (n <= 0) {
        return QByteArray();
    }
    QByteArray out(n);
    memcpy(out.data(), s.data(), n);
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// 事件类型
// ─────────────────────────────────────────────────────────────────────────────
static EventType34 qnamReplyEventType()
{
    static EventType34 t = toEventType34(QEvent::User + 120);
    return t;
}


static EventType34 qnamProgressEventType()
{
    static EventType34 t = toEventType34(QEvent::User + 121);
    return t;
}

QNetworkReplyEvent::QNetworkReplyEvent(QNetworkReply* reply,
                                       int httpCode, const std::string& curlErr,
                                       const std::string& body,
                                       const std::map<std::string,std::string>& headers,
                                       bool aborted)
    : CustomEventBase(qnamReplyEventType())
    , m_reply(reply), m_httpCode(httpCode), m_curlErr(curlErr),
      m_body(body), m_headers(headers), m_aborted(aborted)
{
}

QNetworkProgressEvent::QNetworkProgressEvent(QNetworkReply* reply,
                                             qint64 received, qint64 total)
    : CustomEventBase(qnamProgressEventType())
    , m_reply(reply), m_received(received), m_total(total)
{
}

// ─────────────────────────────────────────────────────────────────────────────
// QNetworkRequest
// ─────────────────────────────────────────────────────────────────────────────
QNetworkRequest::QNetworkRequest(const QUrl& url)
    : m_url(url)
{
}

void QNetworkRequest::setRawHeader(const char* name, const char* value)
{
    m_rawHeaders[std::string(name)] = std::string(value);
}

void QNetworkRequest::setRawHeader(const QByteArray& name, const QByteArray& value)
{
    m_rawHeaders[std::string(name.data(), name.size())] =
        std::string(value.data(), value.size());
}

QByteArray QNetworkRequest::rawHeader(const char* name) const
{
    for (std::map<std::string,std::string>::const_iterator it =
             m_rawHeaders.begin(); it != m_rawHeaders.end(); ++it) {
        if (strcasecmp(it->first.c_str(), name) == 0) {
            return toCleanQBA(QCString(it->second.c_str()));
        }
    }
    return QCString();
}

bool QNetworkRequest::hasRawHeader(const char* name) const
{
    for (std::map<std::string,std::string>::const_iterator it =
             m_rawHeaders.begin(); it != m_rawHeaders.end(); ++it) {
        if (strcasecmp(it->first.c_str(), name) == 0) {
            return true;
        }
    }
    return false;
}

QByteArray QNetworkRequest::rawHeaderList() const
{
    QCString out;
    for (std::map<std::string,std::string>::const_iterator it = m_rawHeaders.begin();
         it != m_rawHeaders.end(); ++it) {
        out += it->first.c_str();
        out += ": ";
        out += it->second.c_str();
        out += "\r\n";
    }
    return toCleanQBA(out);
}

void QNetworkRequest::setHeader(KnownHeaders header, const QVariant& value)
{
    switch (header) {
    case ContentTypeHeader:
        m_rawHeaders["Content-Type"] =
            std::string(value.toString().utf8().data());
        break;
    case ContentDispositionHeader:
        break;
    case ContentLengthHeader:
        m_rawHeaders["Content-Length"] =
            std::string(value.toString().utf8().data());
        break;
    case UserAgentHeader:
        m_rawHeaders["User-Agent"] =
            std::string(value.toString().utf8().data());
        break;
    case CookieHeader:
        m_rawHeaders["Cookie"] =
            std::string(value.toString().utf8().data());
        break;
    case ServerHeader:
    case LocationHeader:
        break;
    }
}

QVariant QNetworkRequest::header(KnownHeaders header) const
{
    const char* name = nullptr;
    switch (header) {
    case ContentTypeHeader: name = "Content-Type"; break;
    case ContentLengthHeader: name = "Content-Length"; break;
    case UserAgentHeader: name = "User-Agent"; break;
    case CookieHeader: name = "Cookie"; break;
    default: return QVariant();
    }
    std::map<std::string,std::string>::const_iterator it =
        m_rawHeaders.find(name);
    if (it == m_rawHeaders.end()) {
        return QVariant();
    }
    std::string v = it->second;
    QString s = QString::fromUtf8(v.data(), v.size());
    if (header == ContentTypeHeader || header == UserAgentHeader ||
        header == CookieHeader) {
        return QVariant(s);
    }
    bool ok = false;
    qint64 n = s.toLongLong(&ok);
    return ok ? QVariant(n) : QVariant(s);
}

QVariant QNetworkRequest::attribute(Attribute code, const QVariant& def) const
{
    std::map<int,QVariant>::const_iterator it = m_attrs.find((int)code);
    return it == m_attrs.end() ? def : it->second;
}

// ─────────────────────────────────────────────────────────────────────────────
// QNetworkReply —— 事件分发 + 信号发射
// ─────────────────────────────────────────────────────────────────────────────
namespace {

std::string findHeaderCaseInsensitive(
    const std::map<std::string,std::string>& hdrs, const char* want)
{
    for (std::map<std::string,std::string>::const_iterator it = hdrs.begin();
         it != hdrs.end(); ++it) {
        if (strcasecmp(it->first.c_str(), want) == 0) {
            return it->second;
        }
    }
    return std::string();
}

} // namespace

QNetworkReply::QNetworkReply(QObject* parent)
    : QObject(parent)
    , m_liteCancel(false)
{
}

QNetworkReply::~QNetworkReply()
{
    // Qt3 setParent 模拟：析构时连带删除登记的多部分对象（模拟原生父子所有权）。
    for (size_t i = 0; i < m_qnamOwned.size(); ++i) {
        QObject* o = m_qnamOwned[i];
        if (o) delete o;
    }
}

// ── 信号（占位定义：只为取址，实际发射走 emit* 系列）──
void QNetworkReply::finished() {}
void QNetworkReply::readyRead() {}
void QNetworkReply::uploadProgress(qint64, qint64) {}
void QNetworkReply::downloadProgress(qint64, qint64) {}
void QNetworkReply::error(QNetworkReply::NetworkError) {}
void QNetworkReply::redirected(const QUrl&) {}
void QNetworkReply::metaDataChanged() {}

bool QNetworkReply::event(QEvent* e)
{
    if (e->type() == (int)qnamReplyEventType()) {
        QNetworkReplyEvent* re = static_cast<QNetworkReplyEvent*>(e);
        deliverResult(re->m_httpCode, re->m_curlErr, re->m_body,
                      re->m_headers, re->m_aborted);
        return true;
    }
    if (e->type() == (int)qnamProgressEventType()) {
        QNetworkProgressEvent* pe = static_cast<QNetworkProgressEvent*>(e);
        deliverProgress(pe->m_received, pe->m_total);
        return true;
    }
    return QObject::event(e);
}

void QNetworkReply::deliverProgress(qint64 received, qint64 total)
{
    emitDownload(received, total);
    if (received > 0) {
        emitUpload(received, total);
    }
}

void QNetworkReply::deliverResult(int httpCode, const std::string& curlErr,
                                  const std::string& body,
                                  const std::map<std::string,std::string>& headers,
                                  bool aborted)
{
    m_rawHdrs = headers;
    m_attrs[(int)QNetworkRequest::HttpStatusCodeAttribute] = QVariant((qint64)httpCode);
    std::string loc = findHeaderCaseInsensitive(headers, "location");
    if (!loc.empty()) {
        QString l = QString::fromUtf8(loc.data(), loc.size());
        m_attrs[(int)QNetworkRequest::RedirectionTargetAttribute] = QVariant(QUrl(l));
    }

    if (aborted) {
        m_aborted = true;
        m_error = OperationCanceledError;
        m_errorString = QString::fromUtf8("Request aborted");
        emitError(OperationCanceledError);
        emitFinished();
        return;
    }

    m_done = true;
    if (httpCode >= 400) {
        // 401：先给 QWebdav::provideAuthenication 一次填 authenticator 的机会，
        // 填了就补 Authorization 头重发（Qt 原生行为），不当作错误上报。
        if (httpCode == 401 && !m_authRetried && m_manager) {
            QAuthenticator auth;
            m_manager->notifyAuthRequired(this, &auth);
            if (!auth.userName().isEmpty() || !auth.password().isEmpty()) {
                m_authRetried = true;
                m_done = false;
                const QString plain = auth.userName() + ":" + auth.password();
                // ★ 用 qToUtf8BA()，不要手写 `QCString::size()` 拷贝。
                // Qt3 的 QCString 是 C 字符串，size() **含终止 NUL**；手写 size()
                // 拷贝会让 user 变成 "user:pass\0"（多 1 字节），base64 末位
                // 于是从 '=' 变成 'A'，服务端判定凭据不匹配 → 401 无限重试。
                // 实测（nultest）：utf8().size()=15 / length()=14。
                // qToUtf8BA 内部已按 length() 拷，两个版本语义一致。
                const QByteArray user = qToUtf8BA(plain);
#ifdef QT3_BUILD
                const QCString b64 = qToBase64(user);
                reqExtraHeaders["Authorization"] =
                    std::string("Basic ") + std::string(b64.data() ? b64.data() : "");
#else
                reqExtraHeaders["Authorization"] =
                    std::string("Basic ") +
                    std::string(user.toBase64().constData());
#endif
                m_manager->reissue(this);
                return;
            }
        }
        if (httpCode == 404) {
            m_error = ContentNotFoundError;
        } else if (httpCode == 401 || httpCode == 403) {
            m_error = ContentAccessDenied;
        } else {
            m_error = ProtocolFailure;
        }
        m_errorString = QString::fromUtf8("HTTP %1").arg(httpCode);
        emitError(m_error);
        emitFinished();
        return;
    }
    if (!curlErr.empty()) {
        // TLS 校验失败：真实探测对端证书 → 派发 sslErrors → 按 QWebdav 的裁决
        // （ignoreSslErrors 收录进 CA bundle 重发 / abort）。
        if (isTlsVerifyFailure(curlErr) && handleSslErrors()) {
            return;   // 已重发，等新结果
        }
        m_error = mapCurlError(curlErr);
        m_errorString = QString::fromUtf8(curlErr.data(), curlErr.size());
        emitError(m_error);
        emitFinished();
        return;
    }

    m_body = QCString(body.data(), (int)body.size());

    // 响应头 Set-Cookie → cookie jar（跨请求会话；EventPoller 后端）
    if (m_jar) {
        std::string sc = findHeaderCaseInsensitive(headers, "set-cookie");
        if (!sc.empty()) {
            std::vector<QNetworkCookie> cs =
                QNetworkCookie::parseCookies(sc.c_str());
            if (!cs.empty()) {
                m_jar->setCookiesFromUrl(cs, m_finalUrl.isValid() ? m_finalUrl
                                                                  : requestedUrl);
            }
        }
    }

    emitReadyRead();
    emitFinished();
}

QNetworkReply::NetworkError QNetworkReply::mapCurlError(const std::string& err) const
{
    if (err.find("Could not resolve") != std::string::npos ||
        err.find("couldn't resolve") != std::string::npos) {
        return HostNotFoundError;
    }
    if (err.find("timed out") != std::string::npos ||
        err.find("Timeout") != std::string::npos) {
        return TimeoutError;
    }
    if (err.find("Connection refused") != std::string::npos) {
        return ConnectionRefusedError;
    }
    if (err.find("failed to connect") != std::string::npos) {
        return ConnectionRefusedError;
    }
    if (err.find("SSL") != std::string::npos ||
        err.find("TLS") != std::string::npos) {
        return SslHandshakeFailedError;
    }
    return UnknownNetworkError;
}

void QNetworkReply::emitFinished()
{
    // manager 先于 reply 自己收到 finished（Qt 原生同轮），QWebdav::replyFinished
    // 挂在 manager::finished 上收尾读数据
    if (m_manager) {
        m_manager->notifyReplyFinished(this);
    }
    runVoid("finished");
}

// ── TLS 证书链路（对齐 Qt4+ QNetworkReply::sslErrors / ignoreSslErrors）──

// EventPoller 传上来的是 curl_easy_strerror 文本，据此识别"证书不可信"类失败。
// CURLE_PEER_FAILED_VERIFICATION(60)/CURLE_SSL_CACERT(77) 的文本含
// "certificate"；CURLE_SSL_CONNECT_ERROR(35) 可能只是协议错，交给 mapCurlError
// 报 SslHandshakeFailedError，不进 sslErrors 裁决链（避免把纯 TLS 故障
// 误当成"证书可接受"）。
bool QNetworkReply::isTlsVerifyFailure(const std::string& err) const
{
    if (err.find("certificate") == std::string::npos) {
        return false;
    }
    return true;
}

// 返回 true 表示已收录证书并重发，调用方直接 return（不再报错）。
bool QNetworkReply::handleSslErrors()
{
    if (m_manager == nullptr || m_sslRetried) {
        return false;
    }
    // 真实探测对端证书（OpenSSL 裸握手，见 qsslprobe）
    QSslProbeResult probe;
    if (!qSslProbeSync(requestedUrl, probe)) {
        return false;   // 连不上/非 TLS：不是"证书不可信"，按普通错误处理
    }
    const QSslCertificate cert(probe.md5, probe.sha1, probe.certPem);
    m_sslCert = cert;

    QList<QSslError> errs;
    errs.append(QSslError(QSslError::SelfSignedCertificate, cert));

    // 派发。QWebdav::sslErrors 内部二选一：
    //   pin 匹配 → reply->ignoreSslErrors()
    //   否则     → emit checkSslCertifcate(errors) + reply->abort()
    m_inSslDispatch = true;
    emitSslErrors(errs);
    m_inSslDispatch = false;

    if (m_sslIgnore && !m_sslAbortedByUser) {
        if (qCaBundleTrust(probe.certPem) && qCaBundleActivate()) {
            m_sslRetried = true;
            m_done = false;
            m_manager->reissue(this);
            return true;
        }
    }
    return false;   // 用户 abort 或收录失败：交回调用方走 error/finished
}

bool QNetworkReply::acceptSslAndRetry()
{
    if (m_sslIgnore && !m_sslAbortedByUser && !m_sslRetried && m_manager != nullptr &&
        !m_sslCert.isNull() && qCaBundleTrust(m_sslCert.toPem()) && qCaBundleActivate()) {
        m_sslRetried = true;
        m_done = false;
        m_manager->reissue(this);
        return true;
    }
    return false;
}

QByteArray QNetworkReply::readAll()
{
    // 实测（Qt 3.5 /opt/qt338sh）：QMemArray<char>::size() 就是数据长度，
    // **不含**尾 NUL —— 此前「size() 含尾 NUL、要用 length()」是错的判断
    // （QByteArray 根本没有 length()），已按实测纠正。
    // 仍需逐字节拷：QMemArray 赋值是**浅拷贝共享**底层数组，
    // 先赋值再清 m_body 会连带把返回值一起清掉。
    const int n = (int)m_body.size();
    if (n <= 0) {
        m_body = QCString();
        return QByteArray();
    }
    QByteArray out(n);
    memcpy(out.data(), m_body.data(), n);
    m_body = QCString();
    return out;
}

QVariant QNetworkReply::attribute(QNetworkRequest::Attribute code,
                                 const QVariant& def) const
{
    std::map<int,QVariant>::const_iterator it = m_attrs.find((int)code);
    return it == m_attrs.end() ? def : it->second;
}

QByteArray QNetworkReply::rawHeader(const char* name) const
{
    std::string key(name);
    for (std::map<std::string,std::string>::const_iterator it = m_rawHdrs.begin();
         it != m_rawHdrs.end(); ++it) {
        if (strcasecmp(it->first.c_str(), key.c_str()) == 0) {
            return toCleanQBA(QCString(it->second.c_str()));
        }
    }
    return QCString();
}

bool QNetworkReply::hasRawHeader(const char* name) const
{
    std::string key(name);
    for (std::map<std::string,std::string>::const_iterator it = m_rawHdrs.begin();
         it != m_rawHdrs.end(); ++it) {
        if (strcasecmp(it->first.c_str(), key.c_str()) == 0) {
            return true;
        }
    }
    return false;
}

QByteArray QNetworkReply::rawHeaderList() const
{
    QCString out;
    for (std::map<std::string,std::string>::const_iterator it = m_rawHdrs.begin();
         it != m_rawHdrs.end(); ++it) {
        out += it->first.c_str();
        out += ": ";
        out += it->second.c_str();
        out += "\r\n";
    }
    return toCleanQBA(out);
}

void QNetworkReply::abort()
{
    // 处于 sslErrors 派发窗口时的 abort = QWebdav 走 checkSslCertifcate 后拒绝
    if (m_inSslDispatch) {
        m_sslAbortedByUser = true;
    }
    m_aborted = true;
    m_error = OperationCanceledError;
    m_errorString = QString::fromUtf8("Request aborted");
}

void QNetworkReply::emitUpload(qint64 s, qint64 t)
{
    for (size_t i = 0; i < m_up.size(); ++i) {
        m_up[i](s, t);
    }
}

void QNetworkReply::emitDownload(qint64 r, qint64 t)
{
    for (size_t i = 0; i < m_dp.size(); ++i) {
        m_dp[i](r, t);
    }
}

void QNetworkReply::runVoid(const std::string& name)
{
    std::map<std::string, std::vector<VoidSlot> >::iterator it = m_slots.find(name);
    if (it == m_slots.end()) {
        return;
    }
    std::vector<VoidSlot> copy = it->second;
    for (size_t i = 0; i < copy.size(); ++i) {
        copy[i]();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// QNetworkCookie / QNetworkCookieJar
// ─────────────────────────────────────────────────────────────────────────────
std::vector<QNetworkCookie> QNetworkCookie::parseCookies(const char* raw)
{
    std::vector<QNetworkCookie> out;
    if (!raw) {
        return out;
    }
    QString s = QString::fromUtf8(raw);
    QStringList pairs = QStringList::split(';', s);
    if (pairs.empty()) {
        return out;
    }
    QString kv = pairs.first().simplifyWhiteSpace();
    int eq = kv.find('=');
    if (eq <= 0) {
        return out;
    }
    // 与原生 QNetworkCookie::parseCookies 一致：只解析首个 name=value，
    // 其余分号视为 cookie 属性（max-age/expires/domain/path/secure…）不进入新 cookie。
    QNetworkCookie c(kv.left(eq).utf8().data(), kv.mid(eq + 1).utf8().data());
    out.push_back(c);
    return out;
}

QByteArray QNetworkCookie::toRawForm() const
{
    QCString out(qbaConstData(m_name), m_name.size());
    out += "=";
    out += qbaConstData(m_value);
    if (!m_domain.isEmpty()) {
        out += "; domain=";
        out += m_domain.utf8();
    }
    if (!m_path.isEmpty()) {
        out += "; path=";
        out += m_path.utf8();
    }
    if (m_secure) {
        out += "; secure";
    }
    return toCleanQBA(out);
}

std::vector<QNetworkCookie> QNetworkCookieJar::allCookies() const
{
    return m_cookies;
}

void QNetworkCookieJar::setAllCookies(const std::vector<QNetworkCookie>& cookies)
{
    m_cookies = cookies;
}

std::vector<QNetworkCookie> QNetworkCookieJar::cookiesForUrl(const QUrl& url) const
{
    std::vector<QNetworkCookie> out;
    QString host = url.host();
    for (std::vector<QNetworkCookie>::const_iterator it = m_cookies.begin();
         it != m_cookies.end(); ++it) {
        const QString& d = it->domain();
        if (!d.isEmpty() && host != d && !host.endsWith("." + d)) {
            continue;
        }
        const QString& p = it->path();
        if (!p.isEmpty() && !url.path().startsWith(p)) {
            continue;
        }
        out.push_back(*it);
    }
    return out;
}

bool QNetworkCookieJar::setCookiesFromUrl(
    const std::vector<QNetworkCookie>& cookieList, const QUrl& url)
{
    QString host = url.host();
    for (std::vector<QNetworkCookie>::const_iterator it = cookieList.begin();
         it != cookieList.end(); ++it) {
        QNetworkCookie c = *it;
        if (c.domain().isEmpty()) {
            c.setDomain(host);
        }
        if (c.path().isEmpty()) {
            c.setPath(QString("/"));
        }
        if (c.expirationDate().isValid() &&
            c.expirationDate() < QDateTime::currentDateTime()) {
            continue;
        }
        m_cookies.push_back(c);
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// QHttpMultiPart
// ─────────────────────────────────────────────────────────────────────────────
void QHttpPart::setBodyDevice(QIODevice* device)
{
    m_body.clear();
    if (device && device->open(IO_ReadOnly)) {
        QByteArray d = device->readAll();
        m_body.assign(d.data(), d.size());
        device->close();
    }
}

QByteArray QHttpMultiPart::contentTypeHeader() const
{
    if (m_boundary == 0) {
        m_boundary = rand();
    }
    QCString ct = "multipart/form-data; boundary=";
    ct += QCString().setNum(m_boundary);
    return toCleanQBA(ct);
}

void QHttpMultiPart::setParent(QObject* p)
{
    // Qt4/6 原生：multi 挂为 reply 子对象，reply 析构连带删除。Qt3 QObject
    // 无 reparent/子对象协议（仅构造期定父），此处把所有权登记进 reply 的
    // m_qnamOwned，由 QNetworkReply 析构统一释放（qnam_shim.cpp）。
    if (QNetworkReply* r = dynamic_cast<QNetworkReply*>(p)) {
        r->qnamAddOwned(this);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// QNetworkAccessManager
// ─────────────────────────────────────────────────────────────────────────────
// 「信号」实体：非 inline 外部函数，保证 &QNetworkAccessManager::finished 与
// &QNetworkAccessManager::authenticationRequired 的地址唯一（qconnect_slots 靠取址区分）。
void QNetworkAccessManager::finished(QNetworkReply*)
{
}

void QNetworkAccessManager::authenticationRequired(QNetworkReply*, QAuthenticator*)
{
}

QNetworkAccessManager::QNetworkAccessManager(QObject* parent)
    : QObject(parent)
{
}

QNetworkAccessManager::~QNetworkAccessManager()
{
    if (m_ownsJar) {
        delete m_cookieJar;
    }
}

void QNetworkAccessManager::setCookieJar(QNetworkCookieJar* jar)
{
    if (m_ownsJar) {
        delete m_cookieJar;
    }
    m_cookieJar = jar;
    m_ownsJar = false;
}

QNetworkReply* QNetworkAccessManager::get(const QNetworkRequest& req)
{
    return sendCustomRequest(req, "GET");
}

QNetworkReply* QNetworkAccessManager::head(const QNetworkRequest& req)
{
    return sendCustomRequest(req, "HEAD");
}

QNetworkReply* QNetworkAccessManager::mkcol(const QNetworkRequest& req)
{
    return sendCustomRequest(req, "MKCOL");
}

QNetworkReply* QNetworkAccessManager::propfind(const QNetworkRequest& req,
                                               const QByteArray& query)
{
    // req 是 const&，改不了头：先按值拷一份再补 Depth/Content-Type
    QNetworkRequest r(req);
    r.setRawHeader("Depth", "1");
    r.setRawHeader("Content-Type", "text/xml; charset=utf-8");
    return sendCustomRequest(r, "PROPFIND", query);
}

QNetworkReply* QNetworkAccessManager::post(const QNetworkRequest& req,
                                           const QByteArray& data)
{
    return sendCustomRequest(req, "POST", data);
}

QNetworkReply* QNetworkAccessManager::post(const QNetworkRequest& req,
                                           QHttpMultiPart* multi)
{
    QNetworkRequest r = req;
    QByteArray ct = multi->contentTypeHeader();
    r.contentType = std::string(ct.data(), ct.size());
    // multipart 用 std::string 精确长度拼装：part 文件体可含 NUL 字节
    // （图片），必须按 bodySize() 追加，不能走 C 字符串 strlen。
    std::string body;
    QCString boundaryStr;
    boundaryStr.setNum(multi->boundaryString());
    for (size_t i = 0; i < multi->parts().size(); ++i) {
        const QHttpPart& p = multi->parts().at(i);
        body += "--";
        body += boundaryStr.data();
        body += "\r\n";
        if (p.header() && p.header()[0]) {
            body += "Content-Disposition: ";
            body += p.header();
            body += "\r\n";
        }
        body += "\r\n";
        body.append(p.body(), p.bodySize());
        body += "\r\n";
    }
    body += "--";
    body += boundaryStr.data();
    body += "--\r\n";
    r.bodyData = body;
    return sendCustomRequest(r, "POST");
}

QNetworkReply* QNetworkAccessManager::put(const QNetworkRequest& req,
                                          const QByteArray& data)
{
    return sendCustomRequest(req, "PUT", data);
}

QNetworkReply* QNetworkAccessManager::put(const QNetworkRequest& req,
                                          QIODevice* device)
{
    // 请求体全程用 std::string 承载，**不经过任何 Qt3 字符串容器**。
    //
    // 原因（Qt 3.5 /opt/qt338sh/include/qcstring.h 实测 + qcstring.cpp 语义）：
    //   QCString(const char*, uint maxlen) 的 maxlen 是**含 NUL 的上限**，
    //   不是数据长度。实测三组：
    //     QCString(buf)        size=10 length=9   ← 正确
    //     QCString(buf, 9)     size=9  length=8   ← 少 1 字节
    //     QCString(buf, 20)    size=10 length=9   ← 正确
    //   故「QCString(ptr, size) + 取 size()」会多带一个 '\0'，
    //   「+ size()-1」又会少一字节（e2e 实证过这两种错法）。
    // 另外 Qt3 的 QByteArray == QMemArray<char>，**没有** (const char*, int)
    // 公开构造（会落到 protected 的 QMemArray(int,int)），也没 length()。
    // 唯一无损路径：QByteArray 用 data()+size()（size() 确为数据长度），
    // QCString 用 length()。
    std::string data;
    if (device) {
        const bool wasOpen = device->isOpen();
        if (!wasOpen) {
            device->open(IO_ReadOnly);
        }
        QByteArray d = device->readAll();
        if (d.size() > 0) {
            data.assign(d.data(), d.size());
        }
        if (!wasOpen) {
            device->close();
        }
    }
    return sendCustomRequest(req, "PUT", data);
}

QNetworkReply* QNetworkAccessManager::deleteResource(const QNetworkRequest& req)
{
    return sendCustomRequest(req, "DELETE");
}

QNetworkReply* QNetworkAccessManager::sendCustomRequest(
    const QNetworkRequest& req, const char* verb, const QByteArray& data)
{
    // QByteArray 在 Qt3 就是 QMemArray<char>，data()+size() 无损
    std::string body;
    if (data.size() > 0) {
        body.assign(data.data(), data.size());
    }
    return sendCustomRequest(req, verb, body);
}

QNetworkReply* QNetworkAccessManager::sendCustomRequest(
    const QNetworkRequest& req, const char* verb, const std::string& body)
{
    QNetworkReply* r = createReply(req, std::string(verb));
    if (!body.empty()) {
        r->reqData = body;
    }
    if (!req.bodyData.empty()) {
        r->reqData = req.bodyData;
    }
    if (!req.contentType.empty()) {
        r->reqContentType = req.contentType;
    }
    issue(r);
    return r;
}

QNetworkReply* QNetworkAccessManager::createReply(const QNetworkRequest& req,
                                                  const std::string& verb)
{
    QNetworkReply* r = new QNetworkReply(this);
    r->requestedUrl  = req.url();
    r->m_finalUrl    = req.url();
    r->method        = verb;
    r->reqExtraHeaders = req.rawHeaders();
    r->reqTimeoutSec = req.timeoutSec() > 0 ? req.timeoutSec() : m_timeoutSec;
    r->reqContentType = req.contentType;
    r->reqForceOwnTransport = req.forceOwnTransport();
    r->m_jar        = m_cookieJar;
    r->m_manager    = this;

    const std::map<int,QVariant>& a = req.attributes();
    for (std::map<int,QVariant>::const_iterator it = a.begin(); it != a.end(); ++it) {
        r->m_attrs[it->first] = it->second;
    }

    if (m_cookieJar) {
        std::vector<QNetworkCookie> cs = m_cookieJar->cookiesForUrl(req.url());
        if (!cs.empty()) {
            QCString cookieVal;
            for (size_t i = 0; i < cs.size(); ++i) {
                if (i) {
                    cookieVal += "; ";
                }
                const QNetworkCookie& c = cs[i];
                cookieVal += qbaConstData(c.name());
                cookieVal += "=";
                cookieVal += qbaConstData(c.value());
            }
            r->reqExtraHeaders["Cookie"] =
                std::string(cookieVal.data(), cookieVal.size());
        }
    }
    return r;
}

namespace {
struct ReplyCtx {
    QNetworkReply* reply;
    QNetworkCookieJar* jar;
};

// stikcommon 自建 verb-aware 传输（qwebdavtransport）的完成回调：契约与
// qnamDoneCb 相同，只是响应数据不走 HttpResponse 而是裸参数。投递同一个
// QNetworkReplyEvent，故 QNetworkReply::event() 无需任何改动。
void qwebdavDoneCb(void* udata,
                    int httpCode,
                    const std::string& curlErr,
                    const std::string& body,
                    const std::map<std::string, std::string>& headers,
                    bool aborted)
{
    ReplyCtx* ctx = static_cast<ReplyCtx*>(udata);
    if (!ctx || !ctx->reply) {
        delete ctx;
        return;
    }
    QNetworkReply* r = ctx->reply;
    QNetworkReplyEvent* ev = new QNetworkReplyEvent(
        r, httpCode, curlErr, body, headers, aborted);
    QApplication::postEvent(r, ev);
    delete ctx;
}

void qnamDoneCb(const HttpResponse& resp, void* udata)
{
    ReplyCtx* ctx = static_cast<ReplyCtx*>(udata);
    if (!ctx || !ctx->reply) {
        delete ctx;
        return;
    }
    QNetworkReply* r = ctx->reply;
    // 响应头里的 Set-Cookie（若开 cookie jar）由 reply 线程统一写回——
    // 这里只转发原始 headers，写回逻辑放 deliverResult 之后由 GUI 线程做。
    QNetworkReplyEvent* ev = new QNetworkReplyEvent(
        r, resp.httpCode, resp.curlErrStr, resp.body, resp.headers, false);
    QApplication::postEvent(r, ev);
    delete ctx;
}

void qnamProgressCb(long long received, long long total,
                    long long speed, void* udata)
{
    (void)speed;
    ReplyCtx* ctx = static_cast<ReplyCtx*>(udata);
    if (!ctx || !ctx->reply) {
        return;
    }
    QNetworkReply* r = ctx->reply;
    QNetworkProgressEvent* ev =
        new QNetworkProgressEvent(r, (qint64)received, (qint64)total);
    QApplication::postEvent(r, ev);
}
} // namespace

void QNetworkAccessManager::issue(QNetworkReply* r)
{
    HttpRequest hr;
    hr.url = r->requestedUrl.toString().utf8().data();
    hr.method = r->method;
    hr.data = r->reqData;
    hr.timeoutSec = r->reqTimeoutSec;

    for (std::map<std::string,std::string>::const_iterator it =
             r->reqExtraHeaders.begin(); it != r->reqExtraHeaders.end(); ++it) {
        hr.extraHeaders[it->first] = it->second;
    }
    if (!r->reqContentType.empty() &&
        hr.extraHeaders.find("Content-Type") == hr.extraHeaders.end()) {
        hr.extraHeaders["Content-Type"] = r->reqContentType;
    }

    hr.progress = &qnamProgressCb;

    ReplyCtx* ctx = new ReplyCtx;
    ctx->reply = r;
    ctx->jar = m_cookieJar;

    // ── 分流：自建 verb-aware 传输 vs qldox EventPoller ──
    //
    // 两条理由，都来自只读依赖 qldox/eventpoller.cpp 的**实测**缺陷：
    //   1. 全文没有 CURLOPT_CUSTOMREQUEST，只在 `if (req.method == "POST")`
    //      分支设 POSTFIELDS。故非 GET/POST 动词（MKCOL/MOVE/DELETE/OPTIONS/
    //      HEAD/PROPFIND…）经它发出会退回 curl 默认行为 → 一律 GET 且 body 被丢弃。
    //      改造前 e2e 实测：9 个请求服务端**全部**收到 GET，PUT 的 9 字节 body 变 0。
    //   2. `grep -c 401 eventpoller.cpp` = **0**。EventPoller 根本没有
    //      401 → Basic Auth 重发链，所以「带凭据的 GET」永远拿 401，也不会触发
    //      authenticationRequired。而 GET 是 WebDAV 最常用的操作。
    //
    // 因此：QWebdavLite 对**所有**动词打 forceOwnTransport 标记走这里；
    // 没打标记的非 GET/POST 动词也走这里（兜底，防漏标）；其余仍走 EventPoller。
    const bool own = r->reqForceOwnTransport ||
                     (r->method != "GET" && r->method != "POST");
    if (own) {
        std::map<std::string, std::string> hdrs;
        for (std::map<std::string, std::string>::const_iterator it =
                 r->reqExtraHeaders.begin(); it != r->reqExtraHeaders.end(); ++it) {
            hdrs[it->first] = it->second;
        }
        if (!r->reqContentType.empty() &&
            hdrs.find("Content-Type") == hdrs.end()) {
            hdrs["Content-Type"] = r->reqContentType;
        }
        QWebdavTransport::send(r->method,
                               r->requestedUrl.toString().utf8().data(),
                               hdrs,
                               r->reqData,
                               r->reqTimeoutSec,
                               qwebdavDoneCb,
                               ctx,
                               &r->m_liteCancel);
        return;
    }

    EventPoller::addRequest(hr, qnamDoneCb, ctx);
}

void QNetworkAccessManager::reissue(QNetworkReply* r)
{
    issue(r);
}