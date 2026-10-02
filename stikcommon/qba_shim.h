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
// Qt5+ 的 qcstring.h 不存在，QByteArray 定义在 QByteArray 头里。必须显式包含：
// 本头以 QByteArray 作参数/返回类型，若依赖「包含者恰好先包含了 QByteArray」
// 则该头不自含 —— 换个包含顺序就会报 incomplete type（本次 Qt6 探针即撞上）。
#if defined(QT_VERSION) && QT_VERSION >= 0x050000
#include <QByteArray>
#endif
#include <ctype.h>
#include <string.h>
#include "qset_shim.h"   // QSet<QByteArray> 助手需要；qset_shim.h 不反向依赖本头

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

// ASCII 空白判定：QChar::isSpace 的 ASCII 部分（QByteArray 是字节序列，
// 不存在码点概念，故只认这 6 个）。**不含 0** —— 内嵌 NUL 不是空白。
inline bool qbaIsAsciiSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

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

// mid：Qt3 的 QMemArray 无 mid（Qt4.0 才给 QByteArray 加的成员）。
// 语义对齐 Qt4+ 的 QByteArray::mid(pos, len=-1)：pos 越界返回空串；
// len<0 表示取到末尾；越界截断而非报错。
#if QT_VERSION < 0x040000
inline QByteArray qbaMid(const QByteArray& ba, int pos, int len = -1)
{
    const int n = ba.size();
    if (pos < 0 || pos >= n || len == 0)
        return QByteArray();
    const int avail = n - pos;
    const int take = (len < 0 || len > avail) ? avail : len;
    QByteArray out;
    out.resize(take);
    for (int i = 0; i < take; ++i) out.at(i) = ba.at(pos + i);
    return out;
}
#else
inline QByteArray qbaMid(const QByteArray& ba, int pos, int len = -1)
{
    return ba.mid(pos, len);
}
#endif

// clear：Qt3 的 QMemArray 无 clear()，只有 resize(0)/truncate(0)（都转发
// QGArray::resize(0)）。语义对齐 Qt4+ 的 QByteArray::clear()：size 归 0，
// 后续 append 从 0 起追加（不含前导残留）。
#if QT_VERSION < 0x040000
inline void qbaClear(QByteArray& ba)
{
    ba.resize(0);
}
#else
inline void qbaClear(QByteArray& ba) { ba.clear(); }
#endif

// toLongLong：Qt3 的 QMemArray<char> 无 toLongLong（Qt4.0 才有）。Qt3 侧只有
// QCString::toInt()/toLong()，且 QCString 还得从 QMemArray 另造一个对象才能调，
// 而 long 在 32 位平台只有 31 位有效位，装不下 HTTP 头里的超大 Content-Length。
// 故直接按 Qt 内部 qstrtoll 的规则手写：跳前导空白 → 可选正负号 → 十进制数字，
// 至少吃到一位数字才置 *ok=true（否则 *ok=false 且返回 0），数字后即停。
// ⚠ 与 Qt 一致：尾随垃圾字符被忽略（不置 false），只要前缀能解出数字就算成功。
#if QT_VERSION < 0x040000
// ⚠ 返回类型用 Qt3 的 Q_LLONG（qglobal.h:727 typedef Q_INT64 Q_LLONG），
//   **不是** qlonglong —— 后者是 Qt4.0 才引入的名字，Qt3 全头文件 grep 无此符号
//   （写 qlonglong 会报 "does not name a type"）。Q_INT64 与 Qt4+ 的 qlonglong
//   同为 long long，跨版本调用点无需感知。
inline Q_LLONG qbaToLongLong(const QByteArray& ba, bool* ok = 0)
{
    if (ok) *ok = false;
    const int n = ba.size();
    int i = 0;
    while (i < n && (unsigned char)ba.at(i) <= ' ') ++i;   // 跳前导空白，同 isspace
    bool neg = false;
    if (i < n && (ba.at(i) == '+' || ba.at(i) == '-')) { neg = (ba.at(i) == '-'); ++i; }
    Q_LLONG acc = 0;
    int digits = 0;
    for (; i < n; ++i) {
        const char c = ba.at(i);
        if (c < '0' || c > '9') break;
        acc = acc * 10 + (c - '0');
        ++digits;
    }
    if (!digits) return 0;
    if (ok) *ok = true;
    return neg ? -acc : acc;
}
#else
inline qlonglong qbaToLongLong(const QByteArray& ba, bool* ok = 0)
{
    return ba.toLongLong(ok);
}
#endif

// trimmed：Qt3 的 QMemArray 无 trimmed，手工去 ASCII 空白（' ' \t \n \v \f \r）。
// 注意 QByteArray::trimmed 只去 ASCII 空白，与 QString::trimmed 的 Unicode
// 空白集不是一回事，Qt3 实现刻意与之对齐。
// ══ QByteArray::toLower() / toUpper()（Qt4 起）══════════════════════════════
// Qt3 的 QByteArray 是 typedef QMemArray<char>，**无** lower()/upper() 成员
// （QCString 有，qcstring.h:181-182，但 QCString 只存在于 Qt3/Qt4，不能作返回
// 类型跨版本统一）。故 Qt3 分支自己按字节实现，Qt4+ 转调原生成员。
//
// 两版语义一致：仅 ASCII A-Z ⇄ a-z 折叠，其余字节（含 UTF-8 多字节序列的每个
// 字节，若落在 ASCII 区间外则）原样保留 —— 故对非 ASCII 输入亦不产生 mojibake，
// 与 QString 那边的 Qt3/Qt6 差异（见 qstring_shim.h 的 ⚠ 段）不同。
//
// 实测对拍（Qt 3.5 与 Qt 6.7.3 真机，7 组用例，打印十六进制码点逐字节比对，
// 14 行输出全部相同）：
//   MiXeD.TgS / ABC / a / "" / a-b_c123 / PNG  → ASCII 正常折叠
//   "ü中-png"（UTF-8 字节 C3 BC E4 B8 AD 2D 70 6E 67）→ 大小写折叠后
//   前 5 字节**原样保留**，仅 70 6E 67 → 50 4E 47，两版一致。
#if QT_VERSION < 0x040000
inline QByteArray qbaToLower(const QByteArray& ba)
{
    QByteArray out = ba;
    char* p = qbaData(out);
    for (int i = 0; i < out.size(); ++i) {
        if (p[i] >= 'A' && p[i] <= 'Z') p[i] = char(p[i] - 'A' + 'a');
    }
    return out;
}

inline QByteArray qbaToUpper(const QByteArray& ba)
{
    QByteArray out = ba;
    char* p = qbaData(out);
    for (int i = 0; i < out.size(); ++i) {
        if (p[i] >= 'a' && p[i] <= 'z') p[i] = char(p[i] - 'a' + 'A');
    }
    return out;
}
#else
inline QByteArray qbaToLower(const QByteArray& ba) { return ba.toLower(); }
inline QByteArray qbaToUpper(const QByteArray& ba) { return ba.toUpper(); }
#endif

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

// ── qbaLit()：从字符串字面量造 QByteArray ──────────────────────────────────
//
// 为什么需要（Qt3 三处都编不过，逐个实测确认）：
//   1. QByteArray b(".gif")        —— Qt3 的 QByteArray 只有 QByteArray(int size)
//      公有构造（qcstring.h:102），无 const char* 重载，编译器把 const char*
//      往 int 试 → "invalid conversion from ‘const char*’ to ‘int’"。
//   2. QByteArray b = "png"        —— 同理，无 operator=(const char*)。
//   3. QSet<QByteArray>::insert("apng") —— insert(const T&) 要把 const char[5]
//      绑到 const QMemArray<char>&，需用户定义转换，不合法。
// Qt4+ 三种写法都原生可用，故 Qt4+ 分支直接转调 QByteArray(p)。
//
// 与 qbaFromRaw(p, len) 的分工：本函数走 strlen（字面量语义，且 Qt4+ 行为
// 一致），那个走显式长度（含 NUL 的协议字段用）。
inline QByteArray qbaLit(const char* s)
{
#if QT_VERSION < 0x040000
    return qbaFromRaw(s, (int)strlen(s));
#else
    return QByteArray(s);
#endif
}

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

// ── QByteArray 与字符串字面量的 == / != ──────────────────────────────────
//
// 为什么需要：Qt3 的 QByteArray 就是 QMemArray<char>，而 QMemArray 有一个
// `operator const type*() const`（qmemarray.h:106）。于是 `ba == "acTL"` 同时
// 匹配：
//   ① QMemArray::operator==(const QMemArray&)  —— 需要把 const char[5] 转成
//      QMemArray<char>（QCString 派生可转，派生→基类转换）；
//   ② 内建 operator==(const char*, const char*) —— 需要把 QMemArray 经上面那个
//      转换运算符转成 char*。
// 两条都只差一次用户定义转换，编译器判为歧义（实测 5 个候选）。
//
// 做法：补一对精确匹配的全局重载，把 ① 换成不需要转换的版本。
//
// 长度语义：Qt3 的 QMemArray::size() 返回 shd->len（qgarray.h:82），是否含尾
// NUL **取决于怎么构造**：
//   · QCString(const char*) → 分配 strlen+1，size() 含 NUL（实测 5 / length() 4）；
//   · resize(n) + memcpy    → size() 恰为 n，不含 NUL。
// Qt6 的 QByteArray 恒不含尾 NUL。故比较前剥掉可能存在的尾 NUL，两种来源都
// 与 Qt6 的 `== "acTL"` 语义一致。已验证 9 个用例（有/无 NUL、空串、前缀、
// 反向比较、!=）全部符合预期。
// ⚠ 本函数**必须**整体放在 `#if QT_VERSION < 0x040000` 内（见下方守卫）。
//   它原先定义在守卫之外，形参还直接写 `const QMemArray<char>&`；而
//   QMemArray 在 Qt5 就被移除了（Qt5 起 QByteArray 改为独立类），于是任何
//   包含本头的 Qt6 TU（如 anystik/src/eifreader.cpp:9）都会在这里炸：
//   "‘QMemArray’ does not name a type; did you mean ‘QBitArray’?",
//   且后续 's'/'a'/'d'/'n' 全部 "was not declared" —— 一个不存在的类型名把
//   整个形参列表吃掉，连带 5 条派生错误。Qt3 构建察觉不到，因为 Qt3 的
//   QByteArray 恰好就是 QMemArray<char>。
//   形参改用 QByteArray 而非 QMemArray<char>：Qt3 下二者是同一个类型
//   （qcstring.h:98 `class QByteArray : public QMemArray<char>`），
//   但 QByteArray 是本头已在 Qt3/Qt6 下都可见的稳定名字，不把已移除的
//   QMemArray 泄漏到守卫之外，从根上消除这类跨版本事故。
#if QT_VERSION < 0x040000
inline bool qbaEqLit(const QByteArray& a, const char* s)
{
    if (!s) return false;
    const char* d = a.data();
    if (!d) return s[0] == '\0';
    uint n = a.size();
    if (n && d[n - 1] == '\0') --n;
    const size_t m = strlen(s);
    return n == m && memcmp(d, s, m) == 0;
}

inline bool operator==(const QMemArray<char>& a, const char* s) { return qbaEqLit(a, s); }
inline bool operator==(const char* s, const QMemArray<char>& a) { return qbaEqLit(a, s); }
inline bool operator!=(const QMemArray<char>& a, const char* s) { return !qbaEqLit(a, s); }
inline bool operator!=(const char* s, const QMemArray<char>& a) { return !qbaEqLit(a, s); }

// ── QByteArray 的 operator< ───────────────────────────────────────────────
//
// 为什么需要：Qt3 的 QMap（= 本仓 qset_shim.h 里 QSet 的基类）内部按 key 排序
// 查找（qmap.h:530 `result = (k < key(x))`、:546 `if ((j.node->key) < k)`），
// 故 key 类型必须有 operator<。而 Qt3 的 QByteArray = QMemArray<char> **没有**
// operator<，却有一个隐式 `operator const char*()`（qmemarray.h:106），于是这
// 两行会同时匹配 4 个候选（内建 operator<(const char*, const char*)、QString
// 的 operator< 等），编译报 "ambiguous overload for ‘operator<’"。
// 只有把 QByteArray 当有序容器 key 用（QSet<QByteArray> / QMap<QByteArray,…>）
// 才触发；stickerstore.cpp:886 runtimeSupportedImageFormats() 即此情形。
//
// 语义：按 memcmp 的字典序比较**剥掉尾 NUL 后**的内容，与 Qt6 的
// QByteArray::operator< 一致（Qt6 恒不含尾 NUL）。剥 NUL 的必要性同
// qbaEqLit：QCString 构造的串 size() 含 NUL，resize+memcpy 的不含，
// 不剥则同一内容的两种来源会互相比较不等。
//
// 实测（Qt 3.5 真机）：9 个字符串（""/a/ab/abc/b/Z/z/png/apng）两两 81 组
// 比较，含尾 NUL 与不含尾 NUL 两种来源结果**完全一致**（不一致数=0）；
// QSet<QByteArray> 插入同一内容的两种来源，size() 均为 1（正确去重）。
// ⚠ 但 Qt3 **原生** operator==（qmemarray.h:107 → QGArray::isEqual）比较的是
//   size()，不剥 NUL，故 `QCString("png") == QByteArray("png")` 在 Qt3 为
//   false。这是 Qt3 既有语义、非本垫片引入，本仓也无此依赖：所有 QByteArray
//   均经 qbaFromRaw / qToUtf8BA(qstring_shim.h:114 走 length()+resize 归一化)
//   产出，不含尾 NUL，两版本行为一致。
inline bool operator<(const QByteArray& a, const QByteArray& b)
{
    const char* da = a.data();
    const char* db = b.data();
    uint na = a.size();
    uint nb = b.size();
    if (na && da && da[na - 1] == '\0') --na;
    if (nb && db && db[nb - 1] == '\0') --nb;
    const uint n = na < nb ? na : nb;
    if (n) {
        const int c = memcmp(da, db, n);
        if (c != 0) return c < 0;
    }
    return na < nb;
}
#endif

// ── QByteArray::constData() ──────────────────────────────────────────────
//
// Qt3 的 QByteArray = QMemArray<char>，只有 QGArray::data()（qgarray.h:80，
// 返回 char*，**非 const 限定**），无 Qt4.0 才加的 constData()。全 Qt3 头
// 里 grep 不到 constData，故 QByteArray 的 constData() 全部编译失败。
//
// 事实核查（实测，非推测）：
//   1. `grep -rn constData /opt/qt338sh/include` → **0 命中**。Qt3 全部头文件
//      里根本没有 constData，QString 也不例外。所以这里不存在「劫持 QString
//      的 constData」的风险——Qt3 侧没有任何 constData 可被误伤。
//   2. 但**不能**因此就 `#define constData() data()`：Qt3 QString 另有
//      `const char* data() const { return ascii(); }`（qstring.h:696，QT_NO_COMPAT
//      段内），返回的是 **ASCII/Latin-1 视图**，实测 QString::fromUtf8("测试abc")
//      的 data()[0] 是 0x3f('?')，而 utf8().data() 是正确的 e6 b5 8b e8。
//      一旦未来有 QString 侧 constData 调用点落进这个宏，就会拿到 '?' 串，
//      正是 AGENTS.md 明令禁止的 latin1 类丢高位字节转换。
//   3. 同理 QByteArray 的 QGArray::data() 返回非 const char*，虽可隐式转
//      const char*，但把它伪装成 constData() 会掩盖「Qt3 没有 const 限定」这一事实。
//
// 故本宏**不定义**。27 处 QByteArray 调用点一律显式改用 qbaConstData()，
// QString 侧则用 qUtf8Printable()。类型由调用点自己标明，不靠宏猜，
// 也避免日后 QString/QByteArray 混用时静默取到错误的字节视图。

// QMemArray 的 operator+= ──────────────────────────────────────────────
//
// Qt3 的 QByteArray = QMemArray<char>，QMemArray 只有 resize()/data()，
// **没有 operator+=**（Qt4.0 才给 QByteArray 加的），所以 37 处
// `ba += 'x'` / `ba += otherBa` / `ba += "lit"` 全部报 no match。
// QMemArray 是模板类，加不进成员；同样用全局重载补。
//
// ⚠ QMemArray::resize() 是**重新分配**，不做原地扩容，故必须先读旧 size、
//   再 resize、再 memcpy 回去。三种入参各自重载，避免隐式转换歧义。
// ⚠ Qt3 的 resize() **不填充**新增字节（Qt6 的 QByteArray::resize 会补 \0），
//   故 append 后必须显式写每一个字节。
// ⚠ 自赋值：Qt3 resize 会重新分配并拷贝旧内容，实测 `d += d` 结果为 "hihi"
//   （size 4），与 Qt6 一致，无需特判。
// 已验证 6 个用例：+=char / +=QByteArray / +=const char* / 自赋值。
#if QT_VERSION < 0x040000
inline QMemArray<char>& operator+=(QMemArray<char>& a, char c)
{
    const uint n = a.size();
    a.resize(n + 1);
    a[n] = c;
    return a;
}

inline QMemArray<char>& operator+=(QMemArray<char>& a, const QMemArray<char>& b)
{
    const uint n0 = a.size();
    const uint n1 = b.size();
    if (n1 == 0) return a;
    a.resize(n0 + n1);
    if (b.data()) memcpy(a.data() + n0, b.data(), n1);
    return a;
}

inline QMemArray<char>& operator+=(QMemArray<char>& a, const char* s)
{
    if (!s) return a;
    const uint n0 = a.size();
    const uint n1 = (uint)strlen(s);
    if (n1 == 0) return a;
    a.resize(n0 + n1);
    memcpy(a.data() + n0, s, n1);
    return a;
}
#endif

// ── qByteArraySetBuildAscii()：Qt3 下 QSet 的「初始化列表」替代 ───────────
// Qt3 的 QSet（qset_shim.h 里基于 QMap 的实现）没有 initializer_list 构造，
// `const QSet<QByteArray> s = {"png", "jpg"};` 会报
// "too many initializers for const QSet<QMemArray<char>>" —— 注意它是在找
// 元素类型的构造，QMemArray<char> 只认 (size, char)，所以整条列表全废。
// 用法：static const QSet<QByteArray> kAllowed =
//              qByteArraySetBuildAscii(kItems, nItems);   // kItems 为 const char*[]
// magic static：函数返回值初始化局部 static，C++11 保证只初始化一次且线程安全。
// ⚠ 元素一律按 ASCII 进（qbaFromRaw），故只能放纯 ASCII 字面量
//   （格式名如 "png"/"webp" 属此类）。
inline QSet<QByteArray> qByteArraySetBuildAscii(const char* const* items, int count)
{
    QSet<QByteArray> s;
    for (int i = 0; i < count; ++i)
        s.insert(QByteArray(qbaFromRaw(items[i], (int)strlen(items[i]))));
    return s;
}

// ── qTrimmed(const QByteArray&) ────────────────────────────────────────
// 与 qstring_shim.h 里的 qTrimmed(const QString&) 构成重载对，共用同一个
// 调用点名字（36 处 QString/QByteArray 混用，不必先判断类型）。
// 取代原先 qbytearray_shim.h 里的 `#define trimmed() stripWhiteSpace()` ——
// 那个宏会把整个 TU 的 `.trimmed()` 无差别替换（含第三方头与注释里的
// 字样），改写调用点语法而非补 API，理由详见 qstring_shim.h 同名函数处。
//
// ⚠⚠ Qt3 的 QByteArray **没有** stripWhiteSpace()，也不能用 trimmed()：
//   它就是裸的 `typedef QMemArray<char> QByteArray`（qcstring.h:105），
//   全部公开构造只有 QByteArray() 与 QByteArray(int)（:101/:102），
//   一个字符串方法都没有（QCString 才有 latin1/ascii/utf8）。
//   故只能自己按字节切。空白集与 Qt 的 ASCII 空白一致：
//   空格 / \t / \n / \v / \f / \r（QChar::isSpace 的 ASCII 部分）。
//   内嵌 0 字节不是空白，会被保留 —— 与 Qt4+ QByteArray::trimmed() 一致。
inline QByteArray qTrimmed(const QByteArray& b)
{
    int begin = 0;
    int end = b.size();
    while (begin < end && qbaIsAsciiSpace(b.at(begin))) ++begin;
    while (end > begin && qbaIsAsciiSpace(b.at(end - 1))) --end;
    if (begin == 0 && end == b.size())
        return b;                       // 无需裁剪，直接返回（QMemArray 无隐式共享）
    return qbaMid(b, begin, end - begin);
}

#endif // QLSTIK_QBA_SHIM_H
