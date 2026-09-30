#ifndef HTTPUA_H
#define HTTPUA_H

#ifdef QT3_BUILD
// Qt3 没有 <qbytearray.h>：QByteArray(=QMemArray<char>) 与 QCString 都在
// <qcstring.h> 里。
#include <qcstring.h>
#include <string.h>
#else
#include <QByteArray>
#endif

// UA 文本本体。Qt3 与 Qt4+ 两条路径共用同一份串，避免两处各写一遍后走样。
#define QLSTIK_HTTP_UA \
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36" \
    " (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"

// 在线表情页全网请求统一 UA（真实常用：Windows 桌面 Chrome 当前代）
#ifdef QT3_BUILD
// Qt3 侧不能用 QByteArrayLiteral：qba_shim.h 把该宏展开成 QCString(s)，而
// QCString 继承 QMemArray<char>，其 size() **含终止 NUL**。qnam_shim 的
// setRawHeader(const QByteArray&, const QByteArray&) 用 data()/size() 无损取串，
// 于是 User-Agent 头会带上一个 0x00（实测 501/头部解析异常的常见诱因）。
// 故按精确长度重建：QByteArray(int) 给的是「长度 + 未初始化内容」，无隐含 NUL。
inline QByteArray kHttpUserAgent() {
    QByteArray out((int)sizeof(QLSTIK_HTTP_UA) - 1);
    memcpy(out.data(), QLSTIK_HTTP_UA, sizeof(QLSTIK_HTTP_UA) - 1);
    return out;
}
#else
inline QByteArray kHttpUserAgent() {
    return QByteArrayLiteral(QLSTIK_HTTP_UA);
}
#endif

#endif // HTTPUA_H
