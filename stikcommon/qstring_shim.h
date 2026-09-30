#ifndef QLSTIK_QSTRING_SHIM_H
#define QLSTIK_QSTRING_SHIM_H

// qUtf8Printable 是 Qt 5.10 才有的宏；Qt3/Qt4 用同义实现。
// 安全性：toUtf8()/utf8() 返回的临时对象活到整个函数调用表达式结束，
//        故宏只能用于调用实参处（与 Qt 官方对 qUtf8Printable 的约束一致）。
// Qt5.10+/Qt6 由 #if 自动跳过，用 Qt 自身的宏。
//
// 本头汇总「与 Qt 版本相关的 QString 侧缺口」，每项自带版本守卫：
//   qUtf8Printable  Qt <5.10   宏
//   QStringLiteral Qt <4.1    宏
//   QLatin1Char    Qt <4.0    宏
// 三个都是函数式宏，未带 ( 时不展开，不会误伤同名类型声明。

#ifndef QT_VERSION
#include <qglobal.h>
#endif

#include <qstring.h>
// qcstring.h 是 Qt3/Qt4 独有的（QCString = QByteArray 子类）；Qt5 起已移除，
// 无条件包含会让 Qt6 构建在 myi18n.o 就 fatal error。Qt3 的 qUtf8Printable
// 分支需要它，故按版本守卫。
#if QT_VERSION < 0x050000
#include <qcstring.h>
// QStringList 在 Qt3 是独立类（<qstringlist.h>，QValueList<QString> 的子类），
// qstring.h 里只有前向声明；qStringListRemoveDuplicates 要按值操作它，必须真包含。
#include <qstringlist.h>
#endif
#include <string.h>

#if QT_VERSION < 0x050a00
#if QT_VERSION >= 0x040000
#define qUtf8Printable(string) (string).toUtf8().constData()
#else
// Qt3 的 QString::utf8() 返回 QCString（QByteArray 子类），无 constData()
#define qUtf8Printable(string) ((const char*)(string).utf8().data())
#endif
#endif

// QStringLiteral 是 Qt 5.0 才引入（Qt3/Qt4 都没有，不是 4.1）。
// Qt 官方版把字面量编成 UTF-16 静态数据（零运行时转换）；Qt3/Qt4 只能运行时
// 从 UTF-8 转换。语义等价（返回值都是独立 QString，无临时悬垂），仅慢一点。
#if QT_VERSION < 0x050000
#define QStringLiteral(str) QString::fromUtf8(str)
#endif

// Qt4.0 才引入 QLatin1Char。Qt3 的 QChar 有 char 构造函数，直接替代。
#if QT_VERSION < 0x040000
#define QLatin1Char(c) QChar(c)
#endif

// QLatin1String 是 Qt 4.0 才引入的类；Qt3 无此类，且 Qt3 的 QString 也没有
// startsWith(const QLatin1String&)/operator== 的 QLatin1String 重载。用一个最小
// 类 + 隐式 QString 转换垫上：调用点的 QLatin1String("…") 在需要 const QString&
// 处（startsWith/arg/==）经一次用户转换编译通过，latin1 语义等价。转 const char*
// 就够，但不给（两次用户转换是非法的），只给 operator QString()。
#if QT_VERSION < 0x040000
class QLatin1String
{
public:
    explicit QLatin1String(const char* s) : m_s(s) {}
    operator QString() const { return QString::fromLatin1(m_s); }
private:
    const char* m_s;
};
#endif

// ══ Qt3.5 缺失的 QString 成员（qwebdav 用到）══════════════════════════
// toUtf8/toLocal8Bit/toLatin1：Qt3 分别叫 utf8()/local8Bit()/latin1()，
// 都返回 QCString（与 QByteArray 同为 QMemArray<char>），可直接赋给 QByteArray。
// chop：Qt3 无 QString::chop（裁掉尾部 n 字符）。
// 注：刻意**不**提供 split 垫——Qt3 只有 split(const QString&, Qt::SplitFlags)，
// 语义（是否保留空段）与 Qt4 的 split(QChar) 不同，误垫会静默改变解析结果；
// 需要时用 split(QString(c)) 显式写。
// clear()：Qt3 无（QString 在 Qt3 不可变语义地靠 = QString() 归零）。
#if QT_VERSION < 0x040000
// QCString 与 QByteArray 在 Qt3 是**不同类**（QByteArray=QMemArray<char>），
// 无隐式转换；且 QCString(QByteArray) 构造实测不可靠，故按长度 memcpy。
//
// ★ 必须用 length() 而**不是** size()。Qt3 的 QCString 是 C 字符串：
//   size() 是**含终止 NUL** 的缓冲区大小，length() 才是数据长度。
//   实测（/tmp/opencode/nultest.cpp，QString "davuser:s3cr3t"）：
//       utf8().size() = 15   utf8().length() = 14
//   按 size() 拷会多带一个 0x00。后果不是"多了个看不见的字节"这么轻：
//   Basic Auth 头会变成 `Basic ZGF2dXNlcjpzM2NyM3QA`（末位 A，正确应为 `=`），
//   服务端判定凭据不匹配 → 401 无限重试。这是 PUT 请求体 NUL 坑的同源复发。
//
// ★★ 另一个方向的坑，务必分清：返回的 QByteArray **不保证 NUL 终止**。
//   QCString 在 Qt3 是 C 字符串（data() 末尾必有 '\0'），而 QByteArray 只是
//   QMemArray<char>，data() 后可能是任意字节。故：
//     · 传给 setRawHeader()/qToBase64() 等收 QByteArray（按 size() 读）的接口 → 安全；
//     · 传给 const char* 形参、或用 .data() 当 C 串用 → **越界读**。
//   实测（rawdump.py 抓包）：verb 传 QByteArray 给 const char* 后变成
//   "GETh\xfb\x55"，服务端对未知动词回 501。需要 C 串时用 QCString（Qt3
//   分支）或 std::string 局部量承载，不要用本函数的产物。
inline QByteArray qQStringToBA(const QCString& s)
{
    const int n = s.length();
    if (n <= 0 || s.data() == 0) {
        return QByteArray();
    }
    QByteArray out(n);
    memcpy(out.data(), s.data(), n);
    return out;
}
inline QByteArray qToUtf8BA(const QString& s)   { return qQStringToBA(s.utf8()); }
inline QByteArray qToLatin1BA(const QString& s) { return qQStringToBA(s.latin1()); }
inline QByteArray qToLocal8BA(const QString& s)  { return qQStringToBA(s.local8Bit()); }
// ── QString::chop()（Qt4 才有，Qt3 的 QString 没有）──────────────────────
// ★ 踩过的坑：原实现写成 `if (n > 0) s.left(s.length() - n);` —— QString::left()
//   **返回新串**（Qt4 起是返回引用，Qt3 一律返回新串），结果被丢弃，函数等于
//   什么都没做。两个生产消费方都因此在 Qt3 上失效：
//     · stikcommon/davbisync.cpp:1762 `while (r.endsWith('/')) qStringChop(r, 1);`
//       → **死循环**（条件永真、串永不变）。
//     · stikcommon/qwebdavlite.cpp:94 rootPath 归一化 → 尾斜杠留着，
//       后续拼出的绝对路径全是 `//`。
//   故必须显式回写 s。
// 语义对齐 Qt4+ chop()：「n >= length() 时整串清空」，不做负下标 left()。
inline void qStringChop(QString& s, int n)
{
    if (n <= 0) return;
    if (n >= s.length()) { s = QString(); return; }
    s = s.left(s.length() - n);
}
inline void qStringClear(QString& s)             { s = QString(); }

// ── QByteArray → QString（UTF-8），显式长度，绝不依赖 NUL 终止 ───────────
// ★ 为什么不能直接写 QString::fromUtf8(ba)：Qt3 的 QString::fromUtf8 只有
//   `fromUtf8(const char*, int len = -1)` 一个重载，而 QMemArray<char> 有隐式
//   `operator const char*()`，于是 ba 被当 C 串传、len 取 -1 → strlen 语义。
//   本文件上方的注释已写明 qQStringToBA 的产物**不保证 NUL 终止**（qnam_shim 的
//   readAll() 就是 `QByteArray out(n)` 逐字节 memcpy），于是 fromUtf8 越界读。
//   后果不是"多读几个字节"这么轻：HTML/JSON 响应体尾部垃圾会被当正文解析，
//   匹配到最后一处未闭合的 <img>/"murl" 片段，解析结果随堆内容漂移。
//   Qt4+ 侧 fromUtf8 有 (const QByteArray&) 重载，本身长度安全，薄封装即可。
inline QString qFromUtf8BA(const QByteArray& ba)
{
    return QString::fromUtf8(ba.data(), (int)ba.size());
}

// 注意 qQStringToBA(const QCString&) **不**在这里提供 —— QCString 在 Qt5+ 根本
// 不存在，加了等于在 Qt6 下再炸一次。Qt4+ 侧直接用原生 toUtf8/toLatin1。
//
// ── QString::trimmed()（Qt4 才有，Qt3 只有 stripWhiteSpace()）────────────
// sitelistclient.cpp 解析 <img src=" ..."> 时要 .trimmed()。Qt3 的
// stripWhiteSpace() 与 Qt4+ 的 trimmed() 都只去首尾空白，内部空白不动，语义一致。
#if QT_VERSION < 0x040000
inline QString qTrimmed(const QString& s) { return s.stripWhiteSpace(); }
#else
inline QString qTrimmed(const QString& s) { return s.trimmed(); }
#endif

// ── QStringList::removeDuplicates()（Qt4.1+ 才有）────────────────────────
// imagesearchclient.cpp 的 parseBing/parseYandex 在收集完原图 URL 后去重。
// Qt3 的 QStringList 是 QValueList<QString>，无此成员；按「保留首次出现」的
// 顺序原地压缩（与 Qt4.1+ 实现一致：稳定去重，不排序、不改相对顺序）。
inline void qStringListRemoveDuplicates(QStringList& list)
{
    QStringList out;
    for (uint i = 0; i < (uint)list.size(); ++i) {
        // Qt3 的 at(i) 返回**迭代器**而非引用，故用 operator[]；size() 也返回
        // size_type(uint) 而非 int。
        const QString s = list[(int)i];
        // Qt3 的 QValueList::contains() 返回**出现次数**(size_type)，不是 bool。
        if (out.contains(s) == 0) out << s;
    }
    list = out;
}

#else
inline QByteArray qToUtf8BA(const QString& s)   { return s.toUtf8(); }
inline QByteArray qToLatin1BA(const QString& s) { return s.toLatin1(); }
inline QByteArray qToLocal8BA(const QString& s)  { return s.toLocal8Bit(); }
inline void qStringChop(QString& s, int n)       { if (n > 0) s.chop(n); }
inline void qStringClear(QString& s)             { s.clear(); }
inline QString qFromUtf8BA(const QByteArray& ba) { return QString::fromUtf8(ba); }
inline QString qTrimmed(const QString& s) { return s.trimmed(); }
inline void qStringListRemoveDuplicates(QStringList& list)
{
    list.removeDuplicates();
}
#endif

#endif // QLSTIK_QSTRING_SHIM_H
