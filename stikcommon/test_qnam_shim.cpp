// stikcommon/test_qnam_shim.cpp —— QNAM 垫片契约（Qt3 单端，离线部分）
//
// 被测对象：qnam_shim.h/.cpp —— Qt3 无 QtNetwork，本仓自建 QNetworkRequest /
//   QNetworkReply / QNetworkAccessManager / QSslCertificate 等兼容层。
//
// 覆盖边界（本文件只测**不依赖真实网络/EventPoller**的部分）：
//   · QNetworkRequest 值语义：url / raw header / known header / attribute /
//     timeout / forceOwnTransport
//   · QSslCertificate / QSslError / QAuthenticator 数据载体语义
//   · QNetworkReply::NetworkError 协议枚举值（QWebdav 靠它做映射，值错=误判）
//
// **不覆盖**（需真网络或 EventPoller 泵线程，属 e2e 范畴）：
//   get/post/put/delete 的实际收发、finished/readyRead 投递、TLS 重发链。
//   这些在 qnam_shim.cpp 里与 qldox EventPoller / qwebdavtransport 深度耦合，
//   单元测试无法隔离；请勿在此伪造成功，否则会给「没验证过的东西下结论」。

#include <qstring.h>
#include <qurl.h>
#include <qvariant.h>

#include "doctest/doctest.h"

#include "qglobaltype_shim.h"
#include "qba_shim.h"       // qbaLit（Qt3 QByteArray 无 const char* 构造）
#include "qnam_shim.h"

// ═══════════════════════════════════════════════════════════════════════════
// QNetworkRequest —— URL
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("QNetworkRequest: 构造传入的 URL 可读回，setUrl 可覆盖")
{
    QNetworkRequest r(QUrl(QString::fromLatin1("http://host/a")));
    CHECK_EQ(r.url().toString(), QString::fromLatin1("http://host/a"));

    r.setUrl(QUrl(QString::fromLatin1("http://other/b?x=1")));
    CHECK_EQ(r.url().toString(), QString::fromLatin1("http://other/b?x=1"));
}

// ═══════════════════════════════════════════════════════════════════════════
// QNetworkRequest —— raw headers
// ═══════════════════════════════════════════════════════════════════════════

// ⚠ 核心：rawHeader/hasRawHeader 的查找是**大小写不敏感**的（qnam_shim.cpp:96,107
//   用 strcasecmp）。HTTP header 名本就大小写不敏感，产品按 "Authorization" 设、
//   按 "authorization" 查必须命中。
TEST_CASE("QNetworkRequest::rawHeader: 查找大小写不敏感")
{
    QNetworkRequest r;
    r.setRawHeader("Authorization", "Basic Zm9v");
    CHECK_EQ(r.rawHeader("authorization"), qbaLit("Basic Zm9v"));
    CHECK_EQ(r.rawHeader("AUTHORIZATION"), qbaLit("Basic Zm9v"));
    CHECK(r.hasRawHeader("authorization"));
    CHECK(r.hasRawHeader("AUTHORIZATION"));
}

// ⚠ 未设过的头必须返回**空**，且 hasRawHeader 为 false（不能凭空返回默认值）。
TEST_CASE("QNetworkRequest::rawHeader: 未设过的头返回空且 has 为 false")
{
    QNetworkRequest r;
    CHECK(r.rawHeader("X-Absent").isEmpty());
    CHECK(!r.hasRawHeader("X-Absent"));
}

// ⚠ 重复 set 同名头应覆盖为最后值（若实现成追加，会把旧值漏给服务端）。
TEST_CASE("QNetworkRequest::rawHeader: 同名重复设置取最后值")
{
    QNetworkRequest r;
    r.setRawHeader("X-Tag", "one");
    r.setRawHeader("X-Tag", "two");
    CHECK_EQ(r.rawHeader("X-Tag"), qbaLit("two"));
}

// ⚠ QByteArray 重载必须按 **长度** 取字节（qnam_shim.cpp:88），不能依赖尾 NUL ——
//   否则带内嵌 NUL 的二进制头会被截断。
TEST_CASE("QNetworkRequest::setRawHeader(QByteArray): 保留完整字节（含内嵌 NUL）")
{
    QNetworkRequest r;
    const char raw[] = { 'a', '\0', 'b', '\0' };
    r.setRawHeader(qbaLit("X-Bin"), qbaFromRaw(raw, 4));
    const QByteArray got = r.rawHeader("X-Bin");
    CHECK_EQ(got.size(), 4);
    CHECK_EQ(got[0], 'a');
    CHECK_EQ(got[1], '\0');
    CHECK_EQ(got[2], 'b');
    CHECK_EQ(got[3], '\0');
}

// ⚠ rawHeaderList 用 "name: value\r\n" 拼接（qnam_shim.cpp:114-125）。产品把它
//   直接塞进请求；若少了 "\r\n" 服务端会把后续行当同一头的值。
TEST_CASE("QNetworkRequest::rawHeaderList: 以 CRLF 结尾的 name: value 行")
{
    QNetworkRequest r;
    r.setRawHeader("A", "1");
    r.setRawHeader("B", "2");
    const QByteArray list = r.rawHeaderList();
    const QString s = QString::fromLatin1(list.data(), list.size());
    CHECK(s.contains(QString::fromLatin1("A: 1\r\n")));
    CHECK(s.contains(QString::fromLatin1("B: 2\r\n")));
    CHECK(s.endsWith(QString::fromLatin1("\r\n")));
}

// ═══════════════════════════════════════════════════════════════════════════
// QNetworkRequest —— known headers
// ═══════════════════════════════════════════════════════════════════════════

// ⚠ setHeader(ContentTypeHeader) 走**规范化**键 "Content-Type"，且 header() 返回
//   QString（qnam_shim.cpp:130-133,171-173），保证 QWebdav PUT 带对 Content-Type。
TEST_CASE("QNetworkRequest::setHeader: Content-Type 往返为 QString")
{
    QNetworkRequest r;
    r.setHeader(QNetworkRequest::ContentTypeHeader,
                QVariant(QString::fromLatin1("text/xml")));
    CHECK_EQ(r.header(QNetworkRequest::ContentTypeHeader),
             QVariant(QString::fromLatin1("text/xml")));
}

// ⚠ Content-Length 特殊：header() 会把值解析成 qint64（qnam_shim.cpp:175-177）。
//   产品 POST 依赖它；若解析失败会退成字符串，下游 toInt 取不到值。
TEST_CASE("QNetworkRequest::setHeader: Content-Length 解析为整数")
{
    QNetworkRequest r;
    r.setHeader(QNetworkRequest::ContentLengthHeader,
                QVariant(QString::fromLatin1("12345")));
    const QVariant v = r.header(QNetworkRequest::ContentLengthHeader);
    CHECK_EQ(v.toLongLong(), (qint64)12345);
}

// ⚠ Content-Length 非数字时不得崩溃，应原样以字符串返回（qnam_shim.cpp:177）。
TEST_CASE("QNetworkRequest::setHeader: Content-Length 非数字退化为字符串")
{
    QNetworkRequest r;
    r.setHeader(QNetworkRequest::ContentLengthHeader,
                QVariant(QString::fromLatin1("abc")));
    CHECK_EQ(r.header(QNetworkRequest::ContentLengthHeader).toString(),
             QString::fromLatin1("abc"));
}

// ⚠ Server/Location/ContentDisposition 是显式 no-op（qnam_shim.cpp:134-136,148-150）。
//   请求侧本不该设响应头；钉住它们不会污染 rawHeaders。
TEST_CASE("QNetworkRequest::setHeader: Server/Location/ContentDisposition 为 no-op")
{
    QNetworkRequest r;
    r.setHeader(QNetworkRequest::ServerHeader, QVariant(QString::fromLatin1("nginx")));
    r.setHeader(QNetworkRequest::LocationHeader, QVariant(QString::fromLatin1("/x")));
    r.setHeader(QNetworkRequest::ContentDispositionHeader,
                QVariant(QString::fromLatin1("attachment")));
    CHECK(r.rawHeaders().empty());
    // ⚠ 不能写 CHECK_EQ(r.header(...), QVariant())：Qt3 的 QVariant::operator==
    //   对**两个都无效**的 QVariant 返回 **false**（实测 isValid=0/0、type=0/0
    //   而 a==b 为 0，/tmp/opencode/probe/zk7.cpp），Qt6 则返回 true。
    //   故按契约「返回无效 QVariant」用 isValid() 判定，两版本都成立。
    CHECK(!r.header(QNetworkRequest::ServerHeader).isValid());
    CHECK(!r.header(QNetworkRequest::LocationHeader).isValid());
    CHECK(!r.header(QNetworkRequest::ContentDispositionHeader).isValid());
}

// ⚠ header() 对未知/未设已知头返回**空 QVariant**，不能是空字符串（类型不同）。
TEST_CASE("QNetworkRequest::header: 未设置的已知头返回空 QVariant")
{
    QNetworkRequest r;
    CHECK(r.header(QNetworkRequest::UserAgentHeader).isNull());
}

// ═══════════════════════════════════════════════════════════════════════════
// QNetworkRequest —— attributes / timeout / transport
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("QNetworkRequest::attribute: 取默认值 / 设后可读回")
{
    QNetworkRequest r;
    CHECK_EQ(r.attribute(QNetworkRequest::HttpStatusCodeAttribute, QVariant(0)),
             QVariant(0));

    r.setAttribute(QNetworkRequest::HttpStatusCodeAttribute, QVariant(200));
    CHECK_EQ(r.attribute(QNetworkRequest::HttpStatusCodeAttribute), QVariant(200));
}

// ⚠ 默认 60s（qnam_shim.h:131）。产品不显式设时超时语义不能变。
TEST_CASE("QNetworkRequest::transferTimeout: 默认 60000，可覆盖")
{
    QNetworkRequest r;
    CHECK_EQ(r.transferTimeout(), 60000);
    r.setTransferTimeout(5000);
    CHECK_EQ(r.transferTimeout(), 5000);
    CHECK_EQ(r.timeoutSec(), 5000);            // 两个访问器同源
}

// ⚠ forceOwnTransport 默认 false（普通 HTTP 走 EventPoller）；QWebdavLite 才置 true
//   以强制走自建 verb-aware 传输（qnam_shim.h:109-125）。默认值错会让所有普通
//   请求被劫持到自建泵。
TEST_CASE("QNetworkRequest::forceOwnTransport: 默认 false")
{
    QNetworkRequest r;
    CHECK(!r.forceOwnTransport());
    r.setForceOwnTransport(true);
    CHECK(r.forceOwnTransport());
}

// ═══════════════════════════════════════════════════════════════════════════
// SSL / 认证 数据载体
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("QSslCertificate: 默认构造为 null，带摘要构造非 null")
{
    CHECK(QSslCertificate().isNull());
    const QSslCertificate c(qbaLit("M"), qbaLit("S"), QString::fromLatin1("PEM"));
    CHECK(!c.isNull());
}

// ⚠ digest() 必须按算法返回**原始字节**（Md5 16B / Sha1 20B），QWebdav 拿它和
//   hexToDigest() 的 pin 做 == 比较（qnam_shim.h:161-166）。返回 hex 或搞反算法
//   都会让 pin 校验恒失败 → 用户被莫名要求信任证书。
TEST_CASE("QSslCertificate::digest: Md5/Sha1 各返回对应原始字节")
{
    QByteArray md5 = qbaLit("mmmmmmmmmmmmmmmm");       // 16B
    QByteArray sha1 = qbaLit("ssssssssssssssssssss");   // 20B
    const QSslCertificate c(md5, sha1, QString::fromLatin1("PEM"));
    CHECK_EQ(c.digest(QCryptographicHash::Md5), md5);
    CHECK_EQ(c.digest(QCryptographicHash::Sha1), sha1);
    CHECK_EQ(c.digest(QCryptographicHash::Md5).size(), 16);
    CHECK_EQ(c.digest(QCryptographicHash::Sha1).size(), 20);
    CHECK_EQ(c.toPem(), QString::fromLatin1("PEM"));
    CHECK(c.subjectInfo().isEmpty());          // 明确未实现
}

// ⚠ QSslError 单纯数据载体：error()/certificate() 往返；errorString() 当前恒空
//   （构造时 m_errorString 未赋值），属已知限制，钉住以防有人误以为有文案。
TEST_CASE("QSslError: error/certificate 往返，errorString 为空")
{
    const QSslCertificate c(qbaLit("m"), qbaLit("s"), QString());
    const QSslError e(QSslError::SelfSignedCertificate, c);
    CHECK_EQ(e.error(), QSslError::SelfSignedCertificate);
    CHECK_EQ(e.certificate().digest(QCryptographicHash::Md5), qbaLit("m"));
    CHECK(e.errorString().isEmpty());
}

TEST_CASE("QAuthenticator: user/password/realm 往返")
{
    QAuthenticator a;
    CHECK(a.userName().isEmpty());
    a.setUserName(QString::fromLatin1("u"));
    a.setPassword(QString::fromLatin1("p"));
    a.setRealm(QString::fromLatin1("r"));
    CHECK_EQ(a.userName(), QString::fromLatin1("u"));
    CHECK_EQ(a.password(), QString::fromLatin1("p"));
    CHECK_EQ(a.realm(), QString::fromLatin1("r"));
}

// ⚠⚠ 协议枚举值必须与 Qt 官方一致：QWebdav 用这些值做错误映射（如把
//   ContentNotFoundError 当 404）。值错会把「文件不存在」误判成别的错误。
TEST_CASE("QNetworkReply::NetworkError: 协议枚举值锁定")
{
    CHECK_EQ((int)QNetworkReply::NoError, 0);
    CHECK_EQ((int)QNetworkReply::ConnectionRefusedError, 1);
    CHECK_EQ((int)QNetworkReply::RemoteHostClosedError, 3);
    CHECK_EQ((int)QNetworkReply::HostNotFoundError, 4);
    CHECK_EQ((int)QNetworkReply::TimeoutError, 8);
    CHECK_EQ((int)QNetworkReply::OperationCanceledError, 12);
    CHECK_EQ((int)QNetworkReply::SslHandshakeFailedError, 15);
    CHECK_EQ((int)QNetworkReply::UnknownNetworkError, 99);
    CHECK_EQ((int)QNetworkReply::ContentAccessDenied, 201);
    CHECK_EQ((int)QNetworkReply::OperationNotSupportedError, 202);
    CHECK_EQ((int)QNetworkReply::ContentNotFoundError, 203);
    CHECK_EQ((int)QNetworkReply::UnknownServerError, 199);
    CHECK_EQ((int)QNetworkReply::ProtocolFailure, 399);
}
