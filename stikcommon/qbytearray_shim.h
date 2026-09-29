#ifndef QLSTIK_QBYTEARRAY_SHIM_H
#define QLSTIK_QBYTEARRAY_SHIM_H

// Qt3 的 QByteArray 是 typedef QMemArray<char>（qcstring.h:105），无法用垫片
// 补它在 Qt4 才有的成员方法（toBase64 等）——静态模板方法加不进 typedef 类。
// 本头提供等价自由函数 qToBase64()，供共享模块在 QT3_BUILD 分支收口调用；
// Qt4+ 走原生 QByteArray::toBase64()，本头定义不参与编译。

#if QT_VERSION < 0x040000
#include <qglobal.h>
#include <qcstring.h>
#include <string>
#include <string.h>

// Qt3 无 QString::trimmed() / 无 QByteArray 成员（简单 typedef）——Qt3 提供
// stripWhiteSpace()，语义等同（去首尾空白）。函数式宏把 .trimmed() 一次映射，
// 避免 QString 与 QByteArray 两套调用点逐处收口。仅 QT3_BUILD 编译单元生效，
// Qt 原生头在宏定义前已完成预处理，不受影响。
#define trimmed() stripWhiteSpace()

// ══ QByteArray::append（Qt4 起）═══════════════════════════════════════
// Qt3 的 QByteArray 是 QMemArray<char>，只有 resize()/data()，无 append。
// 注意 Qt3 的 resize() 不填充，故必须手工写字节；返回值语义对齐 Qt4 的
// QByteArray&（Qt3 无法返回引用，故返回 QByteArray 值，调用点只当语句用）。
#if QT_VERSION < 0x040000
inline QByteArray qByteArrayAppend(QByteArray& a, const QByteArray& b)
{
    const uint n0 = a.size();
    const uint n1 = b.size();
    if (n1 == 0) {
        return a;
    }
    a.resize(n0 + n1);
    if (b.data() != 0) {
        memcpy(a.data() + n0, b.data(), n1);
    }
    return a;
}
// QByteArray::number（Qt4 成员，Qt3 无）：整数 → 十进制 ASCII 字节
inline QByteArray qNumberToByteArray(qint64 v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)v);
    QByteArray out;
    const int n = (int)strlen(buf);
    out.resize(n);
    memcpy(out.data(), buf, n);
    return out;
}

inline QByteArray qByteArrayAppend(QByteArray& a, const char* s)
{
    QByteArray t;
    if (s == 0) {
        return a;
    }
    const uint n = (uint)strlen(s);
    t.resize(n);
    memcpy(t.data(), s, n);
    return qByteArrayAppend(a, t);
}
#define qByteArrayAppendStr(a, s) qByteArrayAppend((a), QCString(s))
#endif

// 标准 RFC4648 base64（无内嵌换行），与 Qt4+ 的 QByteArray::toBase64() 默认行为对齐。
// 返回 QCString：其 length()/data() 语义可靠（QMemArray 的 size() 含尾 NUL，不可用于
// 定长判断）；调用方 imageaiutil 用 QString::fromLatin1(qToBase64(...)) 取串。
static inline QCString qToBase64(const QByteArray& src)
{
    static const char* TBL =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const char* data = src.data();
    const uint n = src.size();   // 源为 QFile::readAll()/resize() 结果：size() 精确
    if (!data) {
        return QCString();       // 空 QByteArray：data() 为 NULL
    }
    std::string out;
    out.reserve(((n + 2) / 3) * 4);
    uint i = 0;
    while (i + 2 < n) {
        // 必须转 unsigned char：data[i] 是 signed char，0xFF 会符号扩展成巨索引
        const unsigned a = (unsigned char)data[i];
        const unsigned b = (unsigned char)data[i + 1];
        const unsigned c = (unsigned char)data[i + 2];
        out += TBL[a >> 2];
        out += TBL[((a & 0x03) << 4) | (b >> 4)];
        out += TBL[((b & 0x0F) << 2) | (c >> 6)];
        out += TBL[c & 0x3F];
        i += 3;
    }
    if (i + 1 == n) {
        const unsigned a = (unsigned char)data[i];
        out += TBL[a >> 2];
        out += TBL[(a & 0x03) << 4];
        out += "==";
    } else if (i + 2 == n) {
        const unsigned a = (unsigned char)data[i];
        const unsigned b = (unsigned char)data[i + 1];
        out += TBL[a >> 2];
        out += TBL[((a & 0x03) << 4) | (b >> 4)];
        out += TBL[(b & 0x0F) << 2];
        out += '=';
    }
    // QCString(const char*, uint) 是 qstrncpy 语义（len 含终止 NUL，会少 1 字符）；
    // base64 输出无内嵌 NUL，用 QCString(const char*) 的 strlen 构造。
    return QCString(out.c_str());
}

#endif // QT_VERSION < 0x040000

#endif // QLSTIK_QBYTEARRAY_SHIM_H