#ifndef QLSTIK_QCABUNDLE_H
#define QLSTIK_QCABUNDLE_H

// 已接受证书的 CA bundle 落盘与激活（Qt3 专用）。
//
// 背景：qltox 的 EventPoller（只读，不在本项目范围内）用 libcurl 跑 curl_multi，
// 没有任何注入 CAINFO / SSL_CTX_FUNCTION / VERIFYPEER 的口子。libcurl 的 OpenSSL
// 后端在**首次**建立 SSL 连接时读取 CURL_CA_BUNDLE（见 curl 手册 CAINFO 说明），
// 因此只要在第一次 HTTPS 请求之前把环境变量指到一个含系统根证书 + 用户已接受
// 自签证书的 PEM 文件，后续所有请求（含 EventPoller 内部发起的）就都能通过校验。
//
// 已知取舍（用户已确认接受）：CURL_CA_BUNDLE 是进程级环境变量，会影响本进程内
// 其它所有 TLS 客户端。qlstik 单进程单 GUI，不存在第二个 TLS 客户端冲突。
//
// 文件格式：PEM 证书块顺序拼接（OpenSSL 的 SSL_CTX_load_verify_locations 能
// 直接读多个 BEGIN CERTIFICATE 块）。首次写入时把系统根证书全量复制进去，
// 之后每接受一个新证书就追加该证书（不重复写已存在的内容）。
//
// 只在 QT3_BUILD 编译单元 include；Qt4+ 走原生 QSslConfiguration，无此文件。

#include <qstring.h>

// bundle 文件路径：$XDG_CONFIG_HOME/qlstik/dav-ca.pem（缺省 ~/.config/qlstik/）
QString qCaBundlePath();

// 把 certPem（单张 PEM 证书全文）追加进 bundle，并确保文件已含系统根证书。
// 幂等：已存在同一张证书时不重复追加。失败返回 false。
bool qCaBundleTrust(const QString& certPem);

// 确保 bundle 文件存在（必要时从系统 CA 初始化），然后
// setenv("CURL_CA_BUNDLE", path)。
// 幂等：可重复调用；已在激活且文件未变时直接返回 true。
// 返回 false 表示无法写文件（此时上层应保持原有校验行为，不放行自签证书）。
bool qCaBundleActivate();

// 当前进程是否已激活过（供诊断/测试用）。
bool qCaBundleIsActive();

#endif // QLSTIK_QCABUNDLE_H
