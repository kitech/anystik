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

// qcstring.h 是 Qt3/Qt4 独有的（QCString = QByteArray 子类），Qt5 起已移除。
// 无条件包含会让 Qt6 构建 fatal error。Qt3 专用的 qbaToHex/qbaFromHex 等
// 辅助自带 #if QT_VERSION < 0x040000 守卫，故此处也按版本守卫。
#if !defined(QT_VERSION) || QT_VERSION < 0x050000
#include <qcstring.h>
#endif
#include <ctype.h>
#include <string.h>

// ══ QByteArray::toHex() / QByteArray::fromHex() ════════════════════════════
// 统一返回 **QByteArray** 而非 QCString：QCString 只存在于 Qt3/Qt4，返回类型
// 必须跨版本一致，否则调用点在 Qt5+ 下无法编译。
#if QT_VERSION >= 0x040000
// Qt4+ 原生就有这两个成员，薄封装使调用点无需版本分支。
// toHex() 默认大写，与下方 Qt3 实现一致（QWebdav 拿它存 pin 比对）。
inline QByteArray qbaToHex(const QByteArray& in)
{
    return in.toHex();
}

inline QByteArray qbaFromHex(const QByteArray& in)
{
    return QByteArray::fromHex(in);
}

#else
// Qt3 的 QByteArray(=QMemArray<char>) 无 hex 成员。QWebdav 用它把 MD5/SHA1
// 原始字节转成大写 hex 串存 pin 比对，故输出**必须是大写**（Qt4 toHex 默认
// 大写；Qt3 无对应 API，此处显式大写以对齐）。
inline QByteArray qbaToHex(const QByteArray& in)
{
    static const char* kHex = "0123456789ABCDEF";
    // 构 QByteArray(n) 而非 QCString(n)：后者在 Qt5+ 不存在。
    // 注意 n 取数据长度而非「含 NUL」——QCString 路径的 NUL 坑见
    // 移植计划.md 3c-1 踩坑记录第 4 条。
    QByteArray out((int)in.size() * 2);
    char* d = out.data();
    for (int i = 0; i < (int)in.size(); ++i) {
        const unsigned char b = (unsigned char)in.at(i);
        d[i * 2]     = kHex[b >> 4];
        d[i * 2 + 1] = kHex[b & 0x0F];
    }
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

// ══ QByteArray 通用垫片（跨 Qt3/Qt4+/Qt5/Qt6；Qt4+ 全部转调原生成员）══════
// Qt3 的 QByteArray 是 QMemArray<char>，**不能给已有类加成员函数**（也不能靠
// 继承：QCString 已 public 继承它，再包一层会让调用点的类型不匹配），故一律
// 自由函数 + 调用点替换，范式同 qstring_shim.h 的 qTrimmed/qStringChop。
// 这批函数定义在文件末尾、跨版本生效：Qt3 分支用 Qt3 原语实现，Qt4+ 分支
// 转调同名原生成员（见各自的 #if QT_VERSION < 0x040000 / #else）。
//
// Qt3 QMemArray 实测接口（/opt/qt338sh/include/qmemarray.h）与其缺口：
//   有：data() size() resize() find() contains() bsearch() sort() truncate()
//       fill() isEmpty() isNull() sizeof()
//   无：constData() indexOf() append() reserve() trimmed()

// Qt3 的 QMemArray::data() 非 const 版本返回 char*，const 版本返回 const char*。
#if QT_VERSION < 0x040000
inline const char* qbaConstData(const QByteArray& ba) { return ba.data(); }
#else
inline const char* qbaConstData(const QByteArray& ba) { return ba.constData(); }
#endif
// 非 const 版取可写指针。Qt3/Qt4+ 的 QMemArray/QByteArray 都有 char* data()。
inline char*       qbaData(QByteArray& ba)             { return ba.data(); }

// indexOf：Qt3 用 QMemArray::find(const T&, uint)——返回索引、找不到 -1，
// 与 Qt4+ QByteArray::indexOf 语义一致。
#if QT_VERSION < 0x040000
inline int qbaIndexOf(const QByteArray& ba, char c, int from = 0)
{
    return ba.find(c, (uint)from);
}

// QMemArray::find 也接受 const T&，即整个 QByteArray 序列（Qt4+ 的
// indexOf(const QByteArray&) 同形）。eifreader 用它找 Face.dat 里的定长标记。
inline int qbaIndexOf(const QByteArray& ba, const QByteArray& sub, int from = 0)
{
    const int m = sub.size();
    if (m == 0) return (from <= ba.size()) ? from : -1;
    for (int i = from; i + m <= ba.size(); ++i) {
        int k = 0;
        while (k < m && ba.at(i + k) == sub.at(k)) ++k;
        if (k == m) return i;
    }
    return -1;
}

inline int qbaLastIndexOf(const QByteArray& ba, char c)
{
    for (int i = ba.size() - 1; i >= 0; --i)
        if (ba.at(i) == c) return i;
    return -1;
}
#else
inline int qbaIndexOf(const QByteArray& ba, char c, int from = 0)
{
    return ba.indexOf(c, from);
}

inline int qbaIndexOf(const QByteArray& ba, const QByteArray& sub, int from = 0)
{
    return ba.indexOf(sub, from);
}

inline int qbaLastIndexOf(const QByteArray& ba, char c)
{
    return ba.lastIndexOf(c);
}
#endif

// reserve：Qt3 的 QMemArray 无 capacity 概念，也没有「只扩容量不改 size」的 API。
// Qt4 的 reserve(n) 的**可观察**语义是「size 不变、容量变大」；Qt3 只能 no-op
// —— 绝不能用 resize(n) 代替：那会把 size 真的变成 n，之后 append() 从 n 起追加，
// 结果全错（decoded.reserve(N) 后 decoded.append(...) 会得到 N 个前导空字节）。
// 代价只是失去一次预分配（贴纸名几十字节，realloc 可忽略），但 size 不变这一
// 关键可观察行为必须严格保持。
inline void qbaReserve(QByteArray& ba, int n)
{
#if QT_VERSION < 0x040000
    (void)ba; (void)n;      // Qt3：no-op，保持 size 不变
#else
    ba.reserve(n);
#endif
}

// append：Qt3 的 QMemArray 无 append，用 resize + 末位赋值实现；Qt4+ 转调原生。
// 三种重载都与 Qt4+ 的同名成员同形（Qt4 append 均返回 QByteArray&）。
#if QT_VERSION < 0x040000
inline QByteArray& qbaAppend(QByteArray& ba, char c)
{
    const int n = ba.size();
    ba.resize(n + 1);
    ba[n] = c;
    return ba;
}

inline QByteArray& qbaAppend(QByteArray& ba, const QByteArray& tail)
{
    const int n = ba.size();
    const int m = tail.size();
    ba.resize(n + m);
    for (int i = 0; i < m; ++i) ba[n + i] = tail.at(i);
    return ba;
}

inline QByteArray& qbaAppend(QByteArray& ba, const char* s, int len)
{
    const int n = ba.size();
    ba.resize(n + len);
    for (int i = 0; i < len; ++i) ba[n + i] = s[i];
    return ba;
}
#else
inline QByteArray& qbaAppend(QByteArray& ba, char c) { return ba.append(c); }
inline QByteArray& qbaAppend(QByteArray& ba, const QByteArray& tail) { return ba.append(tail); }
inline QByteArray& qbaAppend(QByteArray& ba, const char* s, int len) { return ba.append(s, len); }
#endif

// trimmed：Qt3 的 QMemArray 无 trimmed，手工去 ASCII 空白（' ' \t \n \v \f \r）。
// 注意 QByteArray::trimmed 只去 ASCII 空白，与 QString::trimmed 的 Unicode
// 空白集不是一回事，Qt3 实现刻意与之对齐。
inline QByteArray qbaTrimmed(const QByteArray& ba)
{
#if QT_VERSION < 0x040000
    int b = 0, e = ba.size();
    while (b < e && isspace((unsigned char)ba.at(b))) ++b;
    while (e > b && isspace((unsigned char)ba.at(e - 1))) --e;
    QByteArray out(e - b);
    for (int i = b; i < e; ++i) out[i - b] = ba.at(i);
    return out;
#else
    return ba.trimmed();
#endif
}

// 从裸指针造 QByteArray：Qt3 无 QByteArray(const char*, uint) 构造
// （QMemArray(int,int) 是 protected，见 qmemarray.h），且 QByteArray(int) 给的
// 是「长度 + 未初始化内容」。故按精确长度逐字节复制。Qt4+ 有原生构造。
inline QByteArray qbaFromRaw(const char* p, int len)
{
#if QT_VERSION < 0x040000
    QByteArray out(len);
    for (int i = 0; i < len; ++i) out[i] = p[i];
    return out;
#else
    return QByteArray(p, len);
#endif
}

// 单参版按 strlen 长度（Qt4+ 的 QByteArray(const char*) 同样遇 NUL 截断）。
inline QByteArray qbaFromRaw(const char* p) { return qbaFromRaw(p, (int)strlen(p)); }

// uninitialized 缓冲：Qt3 无 Qt::Uninitialized 枚举，但 QByteArray(int) 本身
// 就是「长度 + 未初始化」，语义等价；Qt6+ 用原生带 Qt::Uninitialized 的构造。
inline QByteArray qbaUninit(int size)
{
#if QT_VERSION < 0x050000
    return QByteArray(size);
#else
    return QByteArray(size, Qt::Uninitialized);
#endif
}

#endif // QLSTIK_QBA_SHIM_H
