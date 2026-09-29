#ifndef QLSTIK_QSSLPROBE_H
#define QLSTIK_QSSLPROBE_H

// 对端 TLS 证书探测（Qt3 专用）。
//
// 背景：qltox 的 EventPoller（只读）用 libcurl 跑 curl_multi，没有暴露
// CURLOPT_SSL_CTX_FUNCTION / CAINFO / VERIFYPEER 的注入点，也没有 Qt4+ 的
// QSslConfiguration::peerVerifyMode + QSslCertificate::digest 链路。要在 Qt3 下
// 复刻 vendor qwebdav 的 setSslPin(md5, sha1) 与 sslErrors/acceptSslCertificate
// 语义，必须自己单独做一次 TLS 握手把对端证书取出来。
//
// 做法：**自己用 OpenSSL 做一次裸 TLS 握手**（不借道 libcurl）。踩过的坑：
//   * CURLOPT_SSL_CTX_FUNCTION 拿到的 SSL_CTX 只能 new 出没有握手的 SSL*，
//     SSL_get_peer_certificate() 必返 NULL；
//   * 装在 SSL_CTX 上的 verify / cert_verify 回调都会被 libcurl 事后自己那次
//     SSL_CTX_set_verify(...) 冲掉（实测 hits=0）；
//   * 传输后用 CURLINFO_TLS_SSL_PTR 取 SSL*，OpenSSL 3.6 下
//     SSL_get_peer_certificate() 同样返 NULL（verifyresult 却有值）。
// 所以直接 socket + SSL_connect：verify=false 时关校验只为取到（自签）证书的
// 指纹，verify=true 时走系统默认根证书 + X509_VERIFY_PARAM 主机名校验。
// 指纹用 X509_digest 算 MD5 / SHA-1，与 Qt4+ QSslCertificate::digest() 同一语义
// （vendor qwebdav.cpp:261 正是拿它和 m_sslCertDigestMd5/Sha1 比对）。
// SNI 必带（SSL_set_tlsext_host_name），否则虚拟主机场景取到错的证书。
//
// 调用时机：只在请求**已经因 TLS 校验失败**后才探测（见 qnam_shim 的 sslErrors
// 分发）。正常路径（证书能被系统根证书验证）完全不经过这里，所以这次阻塞式
// 握手不会出现在常规请求的关键路径上。
//
// 只在 QT3_BUILD 编译单元 include；Qt4+ 走原生 QSslConfiguration，无此文件。

#include <qstring.h>
#include <qcstring.h>   // Qt3 的 QByteArray 在 qcstring.h 里（typedef QMemArray<char>）
#include <qurl.h>

// 探测结果。
struct QSslProbeResult {
    bool ok;             // 是否成功取到对端证书（md5/sha1/pem 齐全）
    bool tlsEstablished; // TLS 握手是否完成（不看 body 结果）
    QString errorText;   // curl_easy_strerror 文本
    QByteArray md5;      // 原始 16 字节（与 QSslCertificate::digest(Md5) 同形）
    QByteArray sha1;     // 原始 20 字节
    QString certPem;     // 对端证书 PEM 全文，供 qCaBundleTrust() 收录

    QSslProbeResult() : ok(false), tlsEstablished(false) {}
};

// 阻塞式探测。url 必须是非空的 https URL。
// 返回 true 表示取到了证书（ok=true）；失败时 out 里带 errorText。
bool qSslProbeSync(const QUrl& url, QSslProbeResult& out);

// 探一次"按系统根证书校验能不能过"，用于区分"证书不可信"与"连不上/TLS 层就坏了"。
// true 表示默认校验可通过（无需任何 sslErrors 处理）。
bool qSslVerifyWithSystemCa(const QUrl& url, QString& errorText);

#endif // QLSTIK_QSSLPROBE_H
