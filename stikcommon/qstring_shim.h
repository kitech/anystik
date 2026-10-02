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
#else
// Qt5+ 把 QStringList 并入 QtCore，但 <qstring.h> 不保证拉入完整定义；
// 下面 qStringListRemoveDuplicates 的 Qt5+ 分支按值操作 QStringList，
// 缺了会报 "incomplete type"。故显式包含。此分支对 Qt3 不参与编译。
#include <QStringList>
#endif
#include <string.h>
#include <string>   // qToStdString/qFromStdString（Qt3 无 QString::toStdString）

#if QT_VERSION < 0x050a00
#if QT_VERSION >= 0x040000
#define qUtf8Printable(string) (string).toUtf8().constData()
#else
// Qt3 的 QString::utf8() 返回 QCString（QByteArray 子类），无 constData()
#define qUtf8Printable(string) ((const char*)(string).utf8().data())
#endif
#endif

// qPrintable（Qt4.7 引入）—— 供 qWarning/qDebug 的 printf 风格 %s 实参用。
// Qt6 的定义是 `QtPrivate::asString(s).toLocal8Bit().constData()`（qstring.h:1683）。
// Qt3 侧改用 utf8() 而非 local8Bit()：Qt3 的 local8Bit() 走本地 8 位编码，
// 在 C/POSIX locale 下会把非 ASCII 字符替换成 '?'，日志里路径/中文名全丢。
// utf8() 是保真的那一个，且与本仓 qUtf8Printable 及 AGENTS.md「Qt3 侧非 ASCII
// 一律走 utf8()」一致。实测 C locale 下 QString::fromUtf8("路径/文件-测试.webp")
// 的 utf8() 与 local8Bit() 同为 25 字节原样输出，用 utf8() 不会更差。
// 返回的是临时 QCString 的 data()，其生命周期覆盖整个完整表达式，与 Qt6 同理。
#if QT_VERSION < 0x040700
#define qPrintable(string) ((const char*)(string).utf8().data())
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
// ── QString ↔ std::string 互转（Qt4 才有 toStdString/fromStdString）──────
// Qt3 的 QString 没有 toStdString/fromStdString（Qt 4.0 引入，随
// <string> 支持一起加的），stickerstore.cpp 有 14 处与 std::string 互转
// （StickerRow/StickerPackRow 的 std::string 字段 ↔ QString）。
//
// 编解码：Qt6 的 QString::toStdString() 等价于
//   QString::toUtf8().toStdString()
// 即 **UTF-8** 字节流（qstring.h:1670 注释亦明言 UTF-8）。Qt3 侧必须走
// utf8() 而非 latin1()/local8Bit()：贴纸包 id/路径/中文名会丢高位字节，
// 正是 AGENTS.md 明令禁止的那类转换。故两端统一 UTF-8。
//
// ⚠ 这两个函数必须定义在 Qt3/Qt4+ **两个版本块之外**（实现见整文件结尾），
//   不能落在某一个 `#if QT_VERSION < 0x040000` 内部：
//   stickerstore.cpp 的函数体是 Qt3/Qt6 共用的一份，不按版本分叉，所以
//   Qt6 也需要这两个名字。此前它们被放进 Qt3 块内部，导致 Qt3 侧全绿、
//   Qt6 侧却报 "'qFromStdString' was not declared in this scope; did you
//   mean 'qFromStdU16String'?" —— Qt3 语法门禁看不到这个洞，只有 Qt6 正式
//   构建能暴露。教训：跨版本共用代码所依赖的垫片，定义必须落在版本守卫之外。

// ── QString::trimmed() ──────────────────────────────────────────────────
// Qt3 的 QString 无 trimmed()，只有 stripWhiteSpace()；Qt4+ 有 trimmed()。
// 二者都只去首尾空白、不动内部空白，语义一致，故三版本等价。
// ⚠ qTrimmed(const QString&) 的**唯一权威定义**在本文件末尾（带完整取舍
//   说明的那处）。此处不再重复定义 —— 本文件历史上曾有两份同名定义
//   （一处在版本守卫内、一处在 Qt6 分支尾部），加上末尾那份共三份，
//   导致任何同时经过两处的 TU 直接编译失败：
//     "error: redefinition of 'QString qTrimmed(const QString&)'"
//   触发者：qdebug_shim.h、qtemporaryfile_shim.h 等包含了本文件两次以上
//   版本分支路径的头。新增重载函数时务必全文搜索确认只有一处定义。

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
// ⚠ qFromUtf8BA 的**唯一权威定义**在上面的版本守卫内（带长度安全说明的
//   那处），Qt6 分支此处不再重复 —— 否则同 TU 经过两分支时直接
//   "redefinition of 'QString qFromUtf8BA(const QByteArray&)'"。
//   与 qTrimmed 同一类问题，本文件历史上多处如此，新增前务必全文搜索。
inline void qStringListRemoveDuplicates(QStringList& list)
{
    list.removeDuplicates();
}
#endif


// ── QString::toLower()/toUpper()/indexOf()/lastIndexOf()（Qt3 命名不同）──────
// Qt3 的 QString 里（/opt/qt338sh/include/qstring.h 实测）：
//   toLower()   无；有 QString lower() const          → :522，返回**新串**
//   indexOf()   无；有 int find(QChar, int, bool)     → :469
//   lastIndexOf() 无；有 int findRev(QChar, int, bool) → :478
//   toUpper()   无；有 QString upper() const          → :523，返回**新串**
// 四者语义与 Qt4+ 对应成员一致，故 Qt3 分支直接转调 Qt3 原语，Qt4+ 转调原生。
//
// ⚠ lower()/upper() 的等价性**仅限 ASCII**。实测（码点探针，同一输入
//   "Ünïcødé-X"）：
//     Qt6 toLower → 00FC 006E 00EF 0063 00F8 0064 00E9   （9 字符，按码点映射）
//     Qt3 lower() → 00E3 009C 006E 00E3 00AF ...          （13 字符，按 UTF-8
//                  **字节**逐个映射：ü 的 0xC3 0xBC 两字节各自被当作独立字符）
//   纯 ASCII 输入（"MiXeD.TgS"/"ABC"/""）两版输出逐字节相同。
//   故调用点若对含非 ASCII 的串做大小写折叠，两版本行为不同，不能用本函数。
//   本仓当前全部调用点输入均为 ASCII 后缀/格式名（png/jpg/tgs/svg/apng…），
//   非 ASCII 不可达，此前提成立。
//
// 放在「Qt3 大块」之外：本组是跨版本统一入口，Qt4+ 也走本函数，这样调用点
// 不用写版本分支。块内只有 Qt3 才编译，Qt6 侧会缺这几个名字。
#if QT_VERSION < 0x040000
inline QString qToLower(const QString& s) { return s.lower(); }
inline QString qToUpper(const QString& s) { return s.upper(); }
inline int     qIndexOf(const QString& s, QChar c, int from = 0)
{
    return s.find(c, from);
}
inline int     qIndexOf(const QString& s, const QString& sub, int from = 0)
{
    return s.find(sub, from);
}
inline int     qLastIndexOf(const QString& s, QChar c)
{
    return s.findRev(c);
}
inline int     qLastIndexOf(const QString& s, const QString& sub)
{
    return s.findRev(sub);
}
#else
inline QString qToLower(const QString& s) { return s.toLower(); }
inline QString qToUpper(const QString& s) { return s.toUpper(); }
inline int     qIndexOf(const QString& s, QChar c, int from = 0)
{
    return s.indexOf(c, from);
}
inline int     qIndexOf(const QString& s, const QString& sub, int from = 0)
{
    return s.indexOf(sub, from);
}
inline int     qLastIndexOf(const QString& s, QChar c)
{
    return s.lastIndexOf(c);
}
inline int     qLastIndexOf(const QString& s, const QString& sub)
{
    return s.lastIndexOf(sub);
}
#endif

// ── QString::fromUtf16() / fromStdU16String()（Qt4.1+ 才有）─────────────────
// EIF 贴纸的表名/分组名在 CFB 里是 UTF-16LE，需从 char16_t 序列构造 QString。
// Qt3 的 QString 内部**本来就是 UTF-16**（QChar = ushort），故不需要任何转码：
// 直接走 Qt3 的深拷贝构造 QString(const QChar*, uint)（qstring.h:408）即可，
// 零转码即语义正确（这正是 QString::fromUtf16 在 Qt4 的做法）。
#if QT_VERSION < 0x040000
inline QString qFromUtf16(const char16_t* chars, int n)
{
    // char16_t 与 Qt3 的 QChar 同为 16 位无符号，static_cast 零成本且无值变化。
    // n 传 0 时 Qt3 会构造空串；QString::fromUtf16 同样允许 size=0。
    return QString(reinterpret_cast<const QChar*>(chars), (uint)n);
}
#else
inline QString qFromUtf16(const char16_t* chars, int n)
{
    return QString::fromUtf16(reinterpret_cast<const ushort*>(chars), n);
}
#endif

// std::u16string 与 char16_t 序列布局一致（Qt4 的 fromStdU16String 也只做
// reinterpret_cast + 长度）。char16_t 属 C++11，需在 Qt3 下确认编译器支持。
inline QString qFromStdU16String(const std::u16string& s)
{
#if QT_VERSION < 0x040000
    return qFromUtf16(s.data(), (int)s.size());
#else
    return QString::fromStdU16String(s);
#endif
}

// ── QString::split()（Qt4 才有；Qt3 的 QString 与 QRegExp 都没有 split）─────
// EIF 解析用 body.split(QLatin1Char(':')) / split(QLatin1Char('\\'))，
// 只用到「按单个字符切、保留空段」这一种形态。Qt3 无 split，且 Qt3 的
// QStringList 是 QValueList<QString>（无 += / << 的某些重载差异），故手写。
// 语义对齐 Qt4 QString::split(QChar)：
//   * 默认 Qt::KeepEmptyParts，段数 = 分隔符个数 + 1（连续分隔符产生空段）
//   * 空输入返回含一个空串的列表
#if QT_VERSION < 0x040000
inline QStringList qSplit(const QString& s, QChar sep)
{
    QStringList out;
    int start = 0;
    for (;;) {
        const int ix = s.find(sep, start);
        if (ix < 0) { out << s.mid(start); break; }
        out << s.mid(start, ix - start);
        start = ix + 1;
    }
    return out;
}
#else
inline QStringList qSplit(const QString& s, QChar sep)
{
    // Qt4+ 的 split 默认 Qt::KeepEmptyParts，与上面 Qt3 实现的空段行为一致
    // （Qt4 默认就是 KeepEmptyParts，只有 Qt::SkipEmptyParts 才丢空段）。
    return s.split(sep);
}
#endif

// ── QString ↔ std::string 互转：定义在**所有版本守卫之外** ────────────────
// 见上方 162 行起的说明：stickerstore.cpp 的函数体 Qt3/Qt6 共用，故本组
// 函数必须在两个版本块之外定义，否则 Qt3 侧编译通过而 Qt6 侧未声明。
// Qt4+ 直接转调原生 toStdString/fromStdString（Qt6 实测走 UTF-8）。
#if QT_VERSION < 0x040000
inline std::string qToStdString(const QString& s)
{
    const QCString u = s.utf8();
    return std::string(u.data(), (size_t)u.length());
}

inline QString qFromStdString(const std::string& s)
{
    return QString::fromUtf8(s.data(), (int)s.size());
}
#else
inline std::string qToStdString(const QString& s) { return s.toStdString(); }
inline QString qFromStdString(const std::string& s) { return QString::fromStdString(s); }
#endif

// ── 字符串切分 / 大小写无关比较 ────────────────────────────────────────
// ⚠ Qt3 的 split 有**好几个**可用实现，选错就出隐性语义差，故逐一核过：
//   1. QStringList::split(...) 静态成员（qstringlist.h:76-79，QString/QChar/
//      QRegExp 三个重载，最后一个参数 bool allowEmptyEntries）。本垫片用这个。
//   2. QString::section(QChar sep, int start, int end, int flags)（qstring.h:505）
//      配 SectionSkipEmpty（:500）。Qt3 无 SectionAll 常量，"取到末尾"只能写
//      0xffffffff 默认值，要靠循环试探越界才能判定终止，比方案 1 脆得多。
//   3. Qt3 无 QDir::splitPath（qdir.h 内 grep 无此符号），此路不通。
// 关键差异：Qt3 **没有** QString::split 成员函数；Qt4+ 的
// QString::split(QChar, Qt::SplitFlags) 在 Qt3 无对应物，Qt 4.0 才引入。
// allowEmptyEntries=FALSE 即"丢弃空段"，对齐 Qt4+ 的 Qt::SkipEmptyParts ——
// 已实测对拍（Qt3 QStringList::split vs Qt6 split(..., SkipEmptyParts)，
// 4 组输入结果逐项相同）：
//   "/a//b/" → [a, b]     "a" → [a]
//   "//"    → []          "/a/b/" → [a, b]
#if QT_VERSION < 0x040000
inline QStringList qStringSplitSkipEmpty(const QString& s, QChar sep)
{
    return QStringList::split(sep, s, false);
}
#else
inline QStringList qStringSplitSkipEmpty(const QString& s, QChar sep)
{
    return s.split(sep, Qt::SkipEmptyParts);
}
#endif

// Qt3 的 QString 各查找/比较方法用 bool cs 参数，Qt4.0 起才改成
// Qt::CaseSensitivity 枚举 —— Qt3 里连 Qt::CaseInsensitive 这个符号都不存在
// （实测 /opt/qt338sh/include 全目录 grep 无该符号，只有无关的
//  SectionCaseInsensitiveSeps）。
#if QT_VERSION < 0x040000
inline bool qStringContainsNoCase(const QString& hay, const QString& needle)
{
    return hay.contains(needle, false) != 0;
}
#else
inline bool qStringContainsNoCase(const QString& hay, const QString& needle)
{
    return hay.contains(needle, Qt::CaseInsensitive);
}
#endif

// Qt3 的 compare() 只有大小写敏感一个重载（qstring.h:681），没有 cs 参数，
// 只能折叠后比。⚠ 折叠等价性仅限 ASCII —— 见本文件 qToLower() 上方注记，
// 调用点的比较字面量必须全是 ASCII，否则 Qt3 会按 UTF-8 字节错误折叠。
#if QT_VERSION < 0x040000
inline bool qStringEqualsNoCase(const QString& a, const QString& b)
{
    return a.lower() == b.lower();
}
#else
inline bool qStringEqualsNoCase(const QString& a, const QString& b)
{
    return a.compare(b, Qt::CaseInsensitive) == 0;
}
#endif

// ── qStringListAt()：按索引取值 ────────────────────────────────────────
// ⚠ Qt3 的 QStringList 派生自 QValueList<QString>，而 QValueList::at(size_type)
// 返回的是**迭代器**（qvaluelist.h:555-556），不是元素引用 —— QStringList
// 自己没覆盖它。故 Qt3 侧写 parts.at(i) 会拿到 const_iterator，赋给 QString
// 即报 "no match for operator= (QString and QValueListConstIterator<QString>)"。
// 同一类的 operator[] 才返回 const T&（qvaluelist.h:554）。Qt4+ 的
// QList::at() 返回 const T&，与 Qt3 的 operator[] 语义一致，故直接转调。
#if QT_VERSION < 0x040000
inline QString qStringListAt(const QStringList& l, int i)
{
    return l[i];
}
#else
inline QString qStringListAt(const QStringList& l, int i)
{
    return l.at(i);
}
#endif

// ── qStringListRemoveAll() ───────────────────────────────────────────
// Qt3 的 QStringList 派生自 QValueList<QString>，没有 Qt4.1 才加的
// removeAll(const QString&)。但 QValueList 已有 remove(const T&)（qvaluelist.h:244），
// 语义恰好就是「删掉所有等于 x 的元素、返回删除个数」，与 Qt6 removeAll 一致 ——
// 已实测对拍（Qt3）"a b a c".remove("a") → removed=2, size=2, [b,c]；
// 删不存在的值 → removed=0 且 size 不变。
// ⚠ 别改用 QValueList 的另一个 remove(Iterator)（qvaluelist.h:239）：那个按下标，
//   而 Qt3 **没有** remove(int) 重载，传 int 会去匹配 Iterator 指针重载。
#if QT_VERSION < 0x040000
inline void qStringListRemoveAll(QStringList& list, const QString& v)
{
    list.remove(v);
}
#else
inline void qStringListRemoveAll(QStringList& list, const QString& v)
{
    list.removeAll(v);
}
#endif

// ── qStringRefAt()：按索引取**可写**的 QChar 引用 ───────────────────────
// ⚠ Qt3 的 QString::operator[] 与 at() 都按**值**返回 QChar（qstring.h:646-648：
//   `QChar at(uint) const` / `QChar operator[](int) const`），拿不到可写引用，
//   `out[i] = '_'` 会被丢弃。真正可写的只有 `QChar& ref(uint)`（qstring.h:654）。
//   Qt4+ 的 QString::operator[] 返回 QCharRef，可直接读写。
// 用于「原地逐字符替换」这类按位改写。
#if QT_VERSION < 0x040000
inline QChar& qStringRefAt(QString& s, int i) { return s.ref((uint)i); }
#else
inline QChar& qStringRefAt(QString& s, int i) { return s[i]; }
#endif

// ── qStringListBuild(...)：Qt3 下的「初始化列表」替代 ───────────────────
// Qt3 的 QStringList（QValueList<QString> 子类）没有 initializer_list 构造，
// 写 `const QStringList l = {"a", "b"};` 会报：
//   could not convert ‘{QString::fromUtf8("packs"), ...}’ from
//   ‘<brace-enclosed initializer list>’ to ‘const QStringList’
// 用法：static const QStringList kX = qStringListBuild(a, b, c);
// 局部 static 由函数返回值初始化 → C++11 magic static，线程安全地只建一次；
// 不用 initializer_list，Qt3 侧照样能编。
inline QStringList qStringListBuild0() { return QStringList(); }

template <class A1>
QStringList qStringListBuild(const A1& a1)
{
    QStringList l; l << a1; return l;
}

template <class A1, class A2>
QStringList qStringListBuild(const A1& a1, const A2& a2)
{
    QStringList l; l << a1 << a2; return l;
}

template <class A1, class A2, class A3>
QStringList qStringListBuild(const A1& a1, const A2& a2, const A3& a3)
{
    QStringList l; l << a1 << a2 << a3; return l;
}

template <class A1, class A2, class A3, class A4>
QStringList qStringListBuild(const A1& a1, const A2& a2, const A3& a3, const A4& a4)
{
    QStringList l; l << a1 << a2 << a3 << a4; return l;
}

template <class A1, class A2, class A3, class A4, class A5>
QStringList qStringListBuild(const A1& a1, const A2& a2, const A3& a3, const A4& a4, const A5& a5)
{
    QStringList l; l << a1 << a2 << a3 << a4 << a5; return l;
}

template <class A1, class A2, class A3, class A4, class A5, class A6>
QStringList qStringListBuild(const A1& a1, const A2& a2, const A3& a3, const A4& a4, const A5& a5, const A6& a6)
{
    QStringList l; l << a1 << a2 << a3 << a4 << a5 << a6; return l;
}

template <class A1, class A2, class A3, class A4, class A5, class A6, class A7>
QStringList qStringListBuild(const A1& a1, const A2& a2, const A3& a3, const A4& a4, const A5& a5, const A6& a6, const A7& a7)
{
    QStringList l; l << a1 << a2 << a3 << a4 << a5 << a6 << a7; return l;
}

template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8>
QStringList qStringListBuild(const A1& a1, const A2& a2, const A3& a3, const A4& a4, const A5& a5, const A6& a6, const A7& a7, const A8& a8)
{
    QStringList l; l << a1 << a2 << a3 << a4 << a5 << a6 << a7 << a8; return l;
}

// 长列表（贴纸格式白名单有 18 项）走「字符指针数组 + 个数」，避免堆 18 个重载。
// 元素按 const char* 原样进 QString —— ⚠ 走的是 Latin1 语义，故只可用于纯
// ASCII 字面量（含格式名、URL 这类），不能放非 ASCII 文本。
//
// 用 fromLatin1 而非 fromAscii：后者在 Qt6 已被移除（Qt5 起弃用），
// 而 fromLatin1 在 Qt3（qstring.h:660）与 Qt6 都在；对纯 ASCII 输入两者
// 等价，故一处写法通吃两版本。
inline QStringList qStringListBuildAscii(const char* const* items, int count)
{
    QStringList l;
    for (int i = 0; i < count; ++i) l << QString::fromLatin1(items[i]);
    return l;
}

// ── qTrimmed(const QString&) ───────────────────────────────────────────
// 取代原先 qbytearray_shim.h 里的 `#define trimmed() stripWhiteSpace()`。
//
// 为什么不用宏：那个无参函数式宏会把 include 它的**整个 TU** 里所有
// `.trimmed()` 文本替换掉 —— 不分 QString 还是 QByteArray、不分是不是
// 第三方头里的 token，也无法在局部关掉。实测它还会让
// stikcommon/qba_shim.h 与 qlcomp/limelog.h 里为说明用途而写的
// 「.trimmed()」字样被一并展开。宏改写调用点语法而非补 API，与本仓
// 既定范式相反（见 qdir_shim.h:13-14：「不用同名宏 …… 宏替换反而会
// 误伤无关文本；自由函数让调用点自己标明意图」）。
//
// 为什么用重载而不是 qStringTrimmed/qbaTrimmed 两个名字：调用点是
// 36 处 QString/QByteArray 混用，同名重载让调用方不必先判断类型，
// 也不必记住该用哪个前缀。两侧同名 qTrimmed，靠形参类型解析。
//
// 语义：Qt3 无 trimmed()，只有 stripWhiteSpace()（qstring.h）。两者都是去首尾
// 空白、**不动内部空白**，qTrimmed 只是忠实代理，不引入任何行为改变（探针实证：
// 0x0000-0xFFFF 全码位扫描，qTrimmed 与当版本原生函数裁剪集合逐位相同）。
//
// ⚠ 但**两个 Qt 版本之间并不等价**，实测差两个码位（/tmp 差分探针，见
//   test_qtrimmed_shim.cpp 的「两版本差异已钉住」用例）：
//     U+0085 NEL  —— Qt6 trimmed() 裁掉，Qt3 stripWhiteSpace() 保留
//     U+200B ZWSP —— Qt3 stripWhiteSpace() 裁掉，Qt6 trimmed() 保留
//   故写文档/写注释时**不能**声称「三版本语义一致」。绝大多数场景（URL、标题、
//   提示词）碰不到这两个码位，故不在 shim 里强行统一成某一版 —— 那等于把 Qt6
//   的原生行为也改掉，风险更大。若某天确实需要跨版本位级一致，得显式实现固定
//   空白集，并在本文件记录该决定。
#if QT_VERSION < 0x040000
inline QString qTrimmed(const QString& s) { return s.stripWhiteSpace(); }
#else
inline QString qTrimmed(const QString& s) { return s.trimmed(); }
#endif

#endif // QLSTIK_QSTRING_SHIM_H