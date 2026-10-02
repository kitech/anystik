#ifndef QLSTIK_QBYTEARRAY_SHIM_H
#define QLSTIK_QBYTEARRAY_SHIM_H

// Qt3 的 QByteArray 是 typedef QMemArray<char>（qcstring.h:105），无法用垫片
// 补它在 Qt4 才有的成员方法（toBase64 等）——静态模板方法加不进 typedef 类。
// 本头提供等价自由函数 qToBase64()，供共享模块在 QT3_BUILD 分支收口调用；
// Qt4+ 走原生 QByteArray::toBase64()，本头定义不参与编译。

#if QT_VERSION < 0x040000
#include <qglobal.h>
// qcstring.h 是 Qt3/Qt4 独有的（QCString = QByteArray 子类），Qt5 起已移除。
// 无条件包含会让 Qt6 构建 fatal error。Qt3 专用的 qbaToHex/qbaFromHex 等
// 辅助自带 #if QT_VERSION < 0x040000 守卫，故此处也按版本守卫。
#if !defined(QT_VERSION) || QT_VERSION < 0x050000
#include <qcstring.h>
#endif
#include <string>
#include <string.h>

// ⚠ 本头用到了 qint64（qNumberToByteArray 的形参），但 **Qt3 根本没有
//   qint64** —— 那个 typedef 是 stikcommon/qglobaltype_shim.h:56 自己写的。
//   于是本头**不能独立编译**：只要调用方没恰好先 include qglobaltype_shim.h，
//   就在 qNumberToByteArray 处报 "'qint64' was not declared in this scope"。
//   （隔离探针实证：仅 include 本头即失败。）qdatetime_shim.h 有同样的隐式
//   依赖，属同一类问题。
//   这里自带 typedef 修掉。C++ 允许重复的**相同** typedef，故与
//   qglobaltype_shim.h:56 并存无害（两者都是 long long）。
#if QT_VERSION < 0x040000
typedef long long qint64;
typedef unsigned long long quint64;
#endif

// ── 已移除：#define trimmed() stripWhiteSpace() ──────────────────────
//
// 原先这里有个函数式宏，把所有 `.trimmed()` 一律改写成 `.stripWhiteSpace()`。
// 现已删除，调用点改用 qTrimmed()（qstring_shim.h 的 QString 重载 +
// qba_shim.h 的 QByteArray 重载，构成重载对，调用点不必判断类型）。
//
// 为什么必须删掉它：
//   1) 它对 QByteArray 是**错的**。Qt3 的 QByteArray 就是
//      `typedef QMemArray<char> QByteArray`（qcstring.h:105），而
//      QMemArray 既无 trimmed() 也无 stripWhiteSpace()。宏会把
//      `someQByteArray.trimmed()` 展开成 `someQByteArray.stripWhiteSpace()`
//      → 编译期 "'QByteArray' {aka 'class QMemArray<char>'} has no member
//      named 'stripWhiteSpace'"。隔离探针实证过。
//   2) 它污染整个 TU：任何第三方/产品代码里的 `.trimmed()` 都会被无声改写，
//      包括 Qt6 分支（若该头在 Qt6 下也被包含，Qt6 原生 trimmed() 会被
//      改成不存在的 stripWhiteSpace()）。
//   3) 无版本守卫，两种编译都生效，风险面比它省下的事大。
//
// 参考正确范式：anystik/src/sitelistclient.cpp:61 的
//   #define QLSTIK_TRIMMED(s)  qTrimmed(s)      ← QT3_BUILD 分支
//   #define QLSTIK_TRIMMED(s)  ((s).trimmed())  ← Qt6 分支

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

// ── Qt4+ 同名薄封装 ─────────────────────────────────────────────────────
// 放在上面的 #endif **之外**：那整段被 `#if QT_VERSION < 0x040000` 包着，
// 若把 Qt4+ 分支写在里面，Qt6 下外层直接为假，分支永远进不去（死代码）。
// 返回 QByteArray（值）而非 QByteArray&，与 Qt3 分支签名保持一致。
#if QT_VERSION >= 0x040000
inline QByteArray qByteArrayAppend(QByteArray& a, const QByteArray& b)
{
    a += b;
    return a;
}

inline QByteArray qByteArrayAppend(QByteArray& a, const char* s)
{
    a += QByteArray(s ? s : "");
    return a;
}

inline QByteArray qNumberToByteArray(qint64 v)
{
    return QByteArray::number(v);
}
#endif

#endif // QLSTIK_QBYTEARRAY_SHIM_H