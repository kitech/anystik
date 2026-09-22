#ifndef HTTPUA_H
#define HTTPUA_H

#include <QByteArray>

// 在线表情页全网请求统一 UA（真实常用：Windows 桌面 Chrome 当前代）
inline QByteArray kHttpUserAgent() {
    return QByteArrayLiteral(
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
        " (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36");
}

#endif // HTTPUA_H