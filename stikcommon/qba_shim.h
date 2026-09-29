#ifndef QLSTIK_QBA_SHIM_H
#define QLSTIK_QBA_SHIM_H

// QByteArrayLiteral 是 Qt 5.4 才引入的宏（Qt3/Qt4 没有）。
// Qt3 的 QByteArray 是 typedef QMemArray<char>（无 const char* 构造、无静态 number、
// 无 operator+），这里把字面量落到 QCString（Qt3 特有，public 继承自 QByteArray，
// 自带 operator+ / setNum）。语义等价：返回的都是「UTF-8 字节串」对象，仅类型名不同。
// 注意 QCString 含尾 NUL —— 需要精确长度用 length()/data() 自行处理（toCleanQBA 惯例）。
#ifndef QT_VERSION
#include <qglobal.h>
#endif
#if QT_VERSION >= 0x040000 && QT_VERSION < 0x050400
#define QByteArrayLiteral(s) QByteArray((s))
#endif
#if QT_VERSION < 0x040000
#define QByteArrayLiteral(s) QCString((s))
#endif

#include <qcstring.h>
#include <string.h>

#if QT_VERSION < 0x040000
// ══ QByteArray::toHex() / QByteArray::fromHex()（Qt4 起）════════════════
// Qt3 的 QByteArray(=QMemArray<char>) 无 hex 成员。QWebdav 用它把 MD5/SHA1
// 原始字节转成大写 hex 串存 pin 比对，故输出**必须是大写**（Qt4 toHex 默认
// 大写；Qt3 无对应 API，此处显式大写以对齐）。
inline QCString qbaToHex(const QByteArray& in)
{
    static const char* kHex = "0123456789ABCDEF";
    QCString out;
    out.resize(in.size() * 2 + 1);
    char* d = out.data();
    for (int i = 0; i < (int)in.size(); ++i) {
        const unsigned char b = (unsigned char)in.at(i);
        d[i * 2]     = kHex[b >> 4];
        d[i * 2 + 1] = kHex[b & 0x0F];
    }
    d[in.size() * 2] = '\0';
    return out;
}

// fromHex：Qt4 容忍奇数长度与非法字符（非法字符按 0 处理）。Qt3 侧同语义。
inline QByteArray qbaFromHex(const QByteArray& in)
{
    static int kVal[256];
    static bool kInit = false;
    if (!kInit) {
        for (int i = 0; i < 256; ++i) { kVal[i] = 0; }
        for (int i = 0; i < 10; ++i)  { kVal[(unsigned char)('0' + i)] = i; }
        for (int i = 0; i < 6; ++i)  {
            kVal[(unsigned char)('a' + i)] = 10 + i;
            kVal[(unsigned char)('A' + i)] = 10 + i;
        }
        kInit = true;
    }
    const int n = (int)in.size();
    QByteArray out;
    out.resize(n / 2);
    for (int i = 0; i + 1 < n; i += 2) {
        const int hi = kVal[(unsigned char)in.at(i)];
        const int lo = kVal[(unsigned char)in.at(i + 1)];
        out[i / 2] = (char)((hi << 4) | lo);
    }
    return out;
}
#endif

#endif // QLSTIK_QBA_SHIM_H