#include "qsslprobe.h"

#include "qurl_shim.h"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

#include <cstdio>
#include <cstring>

namespace {

const int kIoTimeoutSeconds = 10;

void setSocketTimeout(int fd)
{
    struct timeval tv;
    tv.tv_sec = kIoTimeoutSeconds;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

QByteArray digestOf(X509* cert, const EVP_MD* md)
{
    unsigned char buf[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    if (X509_digest(cert, md, buf, &len) != 1 || len == 0) {
        return QByteArray();
    }
    // Qt3 的 QByteArray(=QMemArray<char>) 无 (ptr,len) 构造，只能 resize + memcpy。
    QByteArray out;
    out.resize(len);
    if (out.data() != 0) {
        std::memcpy(out.data(), buf, len);
    }
    return out;
}

void fillFromCert(X509* cert, QSslProbeResult& out)
{
    out.md5 = digestOf(cert, EVP_md5());
    out.sha1 = digestOf(cert, EVP_sha1());

    BIO* bio = BIO_new(BIO_s_mem());
    if (bio != 0 && PEM_write_bio_X509(bio, cert) == 1) {
        char* mem = 0;
        const long len = BIO_get_mem_data(bio, &mem);
        if (mem != 0 && len > 0) {
            out.certPem = QString::fromLatin1(mem, (uint)len);
        }
    }
    if (bio != 0) {
        BIO_free(bio);
    }
    ERR_clear_error();
    out.ok = !out.md5.isEmpty() && !out.sha1.isEmpty() && !out.certPem.isEmpty();
}

// 用 OpenSSL 错误栈取第一段可读文本。
QString opensslError()
{
    const unsigned long e = ERR_get_error();
    if (e == 0) {
        return QString();
    }
    char buf[256];
    ERR_error_string_n(e, buf, sizeof(buf));
    return QString(buf);
}

// 直接做一次 TLS 握手，把对端证书取出来。
// verify=true 时用系统默认根证书 + 主机名校验（等价 Qt 的 peer verify）。
bool tlsHandshake(const QString& host, int port, bool verify, QSslProbeResult& out)
{
    // Qt3 的 QByteArray(=QMemArray<char>) 连 (const char*) 构造都没有，直接用
    // QString::latin1() 拿 const char*（QString 生命周期覆盖整个函数）。
    const char* hostC = host.latin1();

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portStr[16];
    std::snprintf(portStr, sizeof(portStr), "%d", port);

    struct addrinfo* res = 0;
    if (::getaddrinfo(hostC, portStr, &hints, &res) != 0 || res == 0) {
        out.errorText = "getaddrinfo failed: " + host;
        return false;
    }

    int fd = -1;
    for (struct addrinfo* ai = res; ai != 0; ai = ai->ai_next) {
        fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            break;
        }
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);
    if (fd < 0) {
        out.errorText = "connect failed: " + host;
        return false;
    }
    setSocketTimeout(fd);

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (ctx == 0) {
        ::close(fd);
        out.errorText = "SSL_CTX_new failed";
        return false;
    }
    // 只做证书探测，不做协议版本/密码套件收紧，行为贴近 libcurl 默认。
    SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv3);

    if (verify) {
        SSL_CTX_set_default_verify_paths(ctx);
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, 0);
    } else {
        // 关校验：自签/过期证书也要能取到指纹
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, 0);
    }

    SSL* ssl = SSL_new(ctx);
    if (ssl == 0) {
        SSL_CTX_free(ctx);
        ::close(fd);
        out.errorText = "SSL_new failed";
        return false;
    }
    // SNI：虚拟主机（同一 IP 多域名）必须带，否则拿到错的证书
    SSL_set_tlsext_host_name(ssl, hostC);
    if (verify) {
        X509_VERIFY_PARAM* param = SSL_get0_param(ssl);
        X509_VERIFY_PARAM_set_hostflags(param, 0);
        X509_VERIFY_PARAM_set1_host(param, hostC, 0);
    }
    SSL_set_fd(ssl, fd);

    bool result = false;
    if (SSL_connect(ssl) == 1) {
        out.tlsEstablished = true;
        if (verify) {
            const long vr = SSL_get_verify_result(ssl);
            if (vr == X509_V_OK) {
                result = true;
            } else {
                out.errorText = QString("certificate verify failed: %s")
                                    .arg(ERR_reason_error_string(vr));
            }
        }
        if (verify && !result) {
            // 校验没过也把证书取出来，交给上层做指纹比对（Qt 的 sslErrors 语义）
            X509* cert = SSL_get_peer_certificate(ssl);
            if (cert != 0) {
                fillFromCert(cert, out);
                X509_free(cert);
            }
        } else {
            X509* cert = SSL_get_peer_certificate(ssl);
            if (cert != 0) {
                fillFromCert(cert, out);
                X509_free(cert);
                result = out.ok;
                if (!result && out.errorText.isEmpty()) {
                    out.errorText = "no peer certificate";
                }
            } else if (out.errorText.isEmpty()) {
                out.errorText = "no peer certificate";
            }
        }
    } else {
        out.errorText = "TLS handshake failed: " + opensslError();
    }

    SSL_free(ssl);
    SSL_CTX_free(ctx);
    ::close(fd);
    ERR_clear_error();
    return result;
}

bool validHttps(const QUrl& url, int& port, QString& errorText)
{
    if (qUrlScheme(url) != "https" || url.host().isEmpty()) {
        errorText = "not an https url";
        return false;
    }
    // Qt3 QUrl::port() 无默认参数：未显式指定时返回 -1
    port = url.port();
    if (port <= 0) {
        port = 443;
    }
    return true;
}

} // namespace

bool qSslProbeSync(const QUrl& url, QSslProbeResult& out)
{
    int port = 443;
    if (!validHttps(url, port, out.errorText)) {
        return false;
    }
    return tlsHandshake(url.host(), port, false, out);
}

bool qSslVerifyWithSystemCa(const QUrl& url, QString& errorText)
{
    int port = 443;
    if (!validHttps(url, port, errorText)) {
        return false;
    }
    QSslProbeResult r;
    if (tlsHandshake(url.host(), port, true, r)) {
        return true;
    }
    errorText = r.errorText;
    return false;
}
