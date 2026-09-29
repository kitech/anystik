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
    // manager 伪信号：自挂 SSL pin 裁决 + 认证回填（垫片侧已实现 401 重发链，
    // 这里只需在失败时把内因通过 errorChanged 暴露给 davbisync）
    using ::connect;
    connect(this, &QNetworkAccessManager::finished, this,
            [](QNetworkReply* r) { (void)r; });
}

QWebdavLite::~QWebdavLite() {}

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
    url.setProtocol(isSSL() ? QString("https") : QString("http"));
    url.setHost(m_hostname);
    if (m_port > 0) {
        url.setPort(m_port);
    }
    url.setPath(absolutePath(path));
    if (!m_username.isEmpty()) {
        url.setUser(m_username);
    }
    return url;
}

// ═══ 摘要 hex（"XX:XX:…"，大写，对齐 vendor digestToHex）════════════════
QString QWebdavLite::digestToHex(const QByteArray& input)
{
    // vendor 用 QByteArray::toUpper()，Qt3 无 → qbaToHex 已直接产出大写
    if (input.size() == 0) {
        return QString();
    }
    const QCString hex = qbaToHex(input);
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
    const QByteArray latin = qQStringToBA(s.latin1());
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
    // 注意：本类自己也有 get/put/head/sendCustomRequest（同名的窄切面 API），
    // 它们在类作用域**遮蔽**基类 QNetworkAccessManager 的同名传输方法，故这里
    // 必须显式限定 QNetworkAccessManager::，否则会走进本类那些 QString 版本。
    // verb 用 QString 比：Qt3 的 QByteArray(=QMemArray) 与 const char* 无
    // operator==。
    const QString verb = method;
    if (verb == QString("GET")) {
        reply = QNetworkAccessManager::get(req);
    } else if (verb == QString("HEAD")) {
        reply = QNetworkAccessManager::head(req);
    } else if (verb == QString("PUT")) {
        reply = outgoingData ? QNetworkAccessManager::put(req, outgoingData)
                             : QNetworkAccessManager::put(req, QByteArray());
    } else if (verb == QString("DELETE")) {
        reply = QNetworkAccessManager::deleteResource(req);
    } else if (verb == QString("MKCOL")) {
        reply = QNetworkAccessManager::mkcol(req);
    } else if (verb == QString("PROPFIND")) {
        reply = QNetworkAccessManager::propfind(req, QByteArray());
    } else {
        const QCString v = verb.latin1();
        reply = QNetworkAccessManager::sendCustomRequest(req, v.data());
    }
    if (reply != 0) {
        m_lastReply = reply;
        m_inDataDevices.insert(reply, outgoingData);
        if (m_transferTimeoutMsecs > 0) {
            req.setTransferTimeout(m_transferTimeoutMsecs);
        }
    }
    return reply;
}

QNetworkReply* QWebdavLite::createRequest(const QString& method, QNetworkRequest& req,
                                          const QByteArray& outgoingData)
{
    // Qt3 的 QBuffer 构造/setBuffer 都按**值**收 QByteArray（无指针重载），
    // 只能浅拷贝一份；无害——qnam_shim 的 put() 是**同步** readAll() 取体
    // （qnam_shim.cpp:784），不持有设备指针，故栈上 QBuffer 生命周期安全。
    QBuffer buf(outgoingData);
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
        req.setRawHeader("Date", qFormatDateTime(qDateTimeToUtc(dt),
                                                 "ddd, dd MMM yyyy hh:mm:ss").latin1());
    }
    return createRequest(QString("PUT"), req, data);
}

QNetworkReply* QWebdavLite::put(const QString& path, const QByteArray& data,
                                const QDateTime& dt)
{
    QBuffer buf(data);
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
    req.setRawHeader("Destination", dest.toString().utf8());
    req.setRawHeader("Connection", "close");
    if (overwrite) {
        req.setRawHeader("Overwrite", "T");
    }
    return QNetworkAccessManager::sendCustomRequest(req, "MOVE");
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
    // query 走基类 propfind（自己会补 Depth/Content-Type）
    m_lastReply = QNetworkAccessManager::propfind(req, query);
    return m_lastReply;
}
