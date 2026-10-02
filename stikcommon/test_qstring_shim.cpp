// stikcommon/test_qstring_shim.cpp —— qstring_shim.h 契约（Qt3 单端）
//
// 被测对象：
//   qStringSplitSkipEmpty() —— Qt3 的 QString::split() 是 Qt4.0 才引入的成员，
//                             且无 Qt::SkipEmptyParts；本垫片走静态
//                             QStringList::split(sep, str, allowEmptyEntries=false)。
//   qStringRefAt()          —— Qt3 的 QString::operator[](uint) 返回 QChar（值），
//                             Qt4+ 返回 const QChar&，故写路径必须用 ref(i)。
//   qStringListRemoveAll()  —— Qt3 的 QStringList::remove(const QString&) 删**全部**
//                             匹配项（与 Qt6 removeAll 同义）；而 remove(int)
//                             删的是**下标**，名字相近极易误用。
//
// 该头是纯头，**无需链接任何产品 .cpp**。

#include <qstring.h>
#include <qstringlist.h>
#include <qcstring.h>      // Qt3 的 QByteArray（qToLatin1BA 返回值）
#include <string>          // qToStdString / qFromStdString


#include "doctest/doctest.h"

#include "qstring_shim.h"
#include "qba_shim.h"      // qbaLit：Qt3 的 QByteArray 没有 const char* 构造

// ── qStringSplitSkipEmpty：对齐 Qt6 的 Qt::SkipEmptyParts ────────────────
// 回归点：Qt3 的 QString::split() 在 Qt4.0 才引入，且无 Qt::SkipEmptyParts。
// 本垫片走静态 QStringList::split(sep, str, allowEmptyEntries=false)。
// 「连续分隔符 / 首尾分隔符」这几条是 Qt6 与朴素实现最容易分叉的地方。
TEST_CASE("qStringSplitSkipEmpty: 连续/首尾分隔符产生的空项被跳过")
{
    QStringList l = qStringSplitSkipEmpty(QString("a,,b"), QChar(','));
    CHECK_EQ(l.count(), 2);
    CHECK(l[0] == QString("a"));
    CHECK(l[1] == QString("b"));

    l = qStringSplitSkipEmpty(QString(",a,"), QChar(','));   // 首尾各一个空项
    CHECK_EQ(l.count(), 1);
    CHECK(l[0] == QString("a"));

    l = qStringSplitSkipEmpty(QString("a,,,b"), QChar(','));  // 连续两个分隔符
    CHECK_EQ(l.count(), 2);

    l = qStringSplitSkipEmpty(QString(""), QChar(','));       // 空串 → 空列表
    CHECK_EQ(l.count(), 0);

    l = qStringSplitSkipEmpty(QString("abc"), QChar(','));     // 无分隔符
    CHECK_EQ(l.count(), 1);
    CHECK(l[0] == QString("abc"));

    l = qStringSplitSkipEmpty(QString("a,b"), QChar(','));     // 正常两段
    CHECK_EQ(l.count(), 2);
    CHECK(l[1] == QString("b"));
}

// ── qStringRefAt：Qt3 的 QString::operator[] 返回 const QChar 副本 ────────
// 回归点：Qt3 `QString::operator[](uint) const` 返回 QChar（值），Qt4+ 返回
// const QChar&。写 `s[i] = 'x'` 在 Qt3 下编不过，故写路径必须用 ref(i)。
// stickerstore 逐字符写 QString 的几处依赖它返回**可写引用**。
TEST_CASE("qStringRefAt: 返回可写引用，逐字符改写生效")
{
    QString s = QString("abc");
    qStringRefAt(s, 1) = QChar('X');
    CHECK(s == QString("aXc"));
    // 读完取值也要对
    CHECK(qStringRefAt(s, 0) == QChar('a'));
    CHECK(qStringRefAt(s, 2) == QChar('c'));
}

// ── qStringListRemoveAll：Qt3 的 remove() 删全部 ─────────────────────────
// 回归点：Qt3 QStringList::remove(const QString&) 删**所有**匹配项；
// Qt6 的 removeAll() 同样删全部；而 Qt3 的 remove(int) 删的是**下标**。
// 名字对不上容易误用 remove(int)，故单测把「按值删全部」这条钉住。
TEST_CASE("qStringListRemoveAll: 按值删除所有匹配项（非按下标）")
{
    QStringList l;
    l << QString("a") << QString("b") << QString("a") << QString("c") << QString("a");
    qStringListRemoveAll(l, QString("a"));
    CHECK_EQ(l.count(), 2);
    CHECK(l[0] == QString("b"));
    CHECK(l[1] == QString("c"));
    // 删不存在的值不应改变列表
    qStringListRemoveAll(l, QString("zzz"));
    CHECK_EQ(l.count(), 2);
    // 删空列表上的值不崩
    QStringList empty;
    qStringListRemoveAll(empty, QString("a"));
    CHECK_EQ(empty.count(), 0);
}

// ── qToLower / qToUpper：跨版本统一入口 ─────────────────────────────────
// 回归点：Qt3 是 lower()/upper()，Qt4+ 是 toLower()/toUpper()。调用点不该写
// 版本分支，故本垫片必须两端同名同语义。
// ⚠ 只测 ASCII：qstring_shim.h:408-410 明确写了折叠等价性**仅限 ASCII**，
//   非 ASCII 的折叠两版本会分叉，调用点的字面量必须全 ASCII。
TEST_CASE("qToLower/qToUpper: ASCII 折叠，且不改原串")
{
    const QString s("AbC-Def_123");
    CHECK_EQ(qToLower(s), QString("abc-def_123"));
    CHECK_EQ(qToUpper(s), QString("ABC-DEF_123"));
    CHECK_EQ(s, QString("AbC-Def_123"));       // 入参是 const&，不得被就地改
    CHECK_EQ(qToLower(QString("")), QString(""));   // 空串边界
}

// ── qStringEqualsNoCase / qStringContainsNoCase ─────────────────────────
// 回归点：Qt3 的 compare() 没有大小写重载、连 Qt::CaseInsensitive 符号都不存在
// （qstring_shim.h:392-395），故 Qt3 侧靠折叠实现，行为必须与 Qt6 的
// compare(CaseInsensitive) / contains(CaseInsensitive) 一致。
TEST_CASE("qStringEqualsNoCase: 大小写无关的相等判断")
{
    CHECK(qStringEqualsNoCase(QString("AbC"), QString("aBc")));
    CHECK(qStringEqualsNoCase(QString(""), QString("")));
    CHECK(!qStringEqualsNoCase(QString("abc"), QString("abd")));
    CHECK(!qStringEqualsNoCase(QString("abc"), QString("ab")));   // 长度不同
}

TEST_CASE("qStringContainsNoCase: 大小写无关的子串判断（含未命中分支）")
{
    CHECK(qStringContainsNoCase(QString("Hello World"), QString("WORLD")));
    CHECK(qStringContainsNoCase(QString("Hello World"), QString("hello")));
    CHECK(qStringContainsNoCase(QString("Hello"), QString("Hello")));
    CHECK(!qStringContainsNoCase(QString("Hello"), QString("xyz")));
    CHECK(!qStringContainsNoCase(QString(""), QString("a")));
}

// ── qToLatin1BA / qToStdString / qFromStdString ─────────────────────────
// ⚠ 这三个是本仓**踩过事故**的函数（AGENTS.md 立规处）：latin1() 丢高位字节、
//   Qt3 的 toLatin1() 传 const char* 会越界读。故这里把 ASCII 通路和「非 ASCII
//   必须显式知道是有损的」都钉住。

TEST_CASE("qToLatin1BA: ASCII 原样字节")
{
    const QByteArray b = qToLatin1BA(QString("abc-123"));
    CHECK_EQ(b.size(), 7);
    CHECK_EQ(b, qbaLit("abc-123"));
}

// ⚠ 非 ASCII 走 latin1 是**有损**的（每 QChar 映射为单字节）。本用例只断言
//   「结果全是 ASCII 字节」这一必然性质，不去猜具体替换字符是 '?' 还是别的
//   —— 替换字符由 Qt3 的 latin1() 决定，凭记忆写死会变成假契约。
//   真需要非 ASCII 时应走 qToStdString（UTF-8），见下一条。
TEST_CASE("qToLatin1BA: 非 ASCII 有损，结果必为 ASCII 字节")
{
    const QString cjk = QString::fromUtf8("中文");
    const QByteArray b = qToLatin1BA(cjk);
    CHECK(b.size() > 0);
    bool allAscii = true;
    for (int i = 0; i < b.size(); ++i)
        if ((unsigned char)b[i] >= 0x80) allAscii = false;
    CHECK(allAscii);
}

TEST_CASE("qToStdString/qFromStdString: ASCII 与 UTF-8 往返一致")
{
    CHECK_EQ(qToStdString(QString("abc")), std::string("abc"));
    CHECK_EQ(qFromStdString(std::string("abc")), QString("abc"));

    // ⚠ 非 ASCII 必须走 UTF-8（AGENTS.md：非 ASCII 一律 utf8/fromUtf8）。
    //   往返成立即证明两端编码一致，不靠终端显示判断。
    const QString cjk = QString::fromUtf8("中文");
    const std::string u8 = qToStdString(cjk);
    CHECK_EQ(u8.size(), 6);                     // 2 个汉字 = 6 字节 UTF-8
    CHECK_EQ(qFromStdString(u8), cjk);          // 按码点质量往返，非显示
}

// ⚠ qFromStdString 走 fromUtf8(s.data(), s.size())。
//   ⚠ 此前这里有条用例断言「保留内嵌 NUL，length()==3」—— 那是**没实测过**的
//     想当然：Qt3 的 QString::fromUtf8(const char*, int) 遇到 NUL 就停，给的
//     len 不起作用（实测 fromUtf8("a\0b", 3).length()==1，Qt6 为 3，
//     /tmp/opencode/probe/zk8.cpp）。原断言从未跑到过（更早的用例先 SIGSEGV）。
//     文本往返由上一条用例覆盖，这里只把限制钉成真值。
// 【已知限制】Qt3 上内嵌 NUL 会被截断。产品侧调用点全是文本字段
//   （stickerstore.cpp 的 row.id / cover_path / file_path / pack_id），故不需要
//   修；详见 qstring_shim.h 的 qFromStdString 注释。
TEST_CASE("qFromStdString: 【已知限制】Qt3 上内嵌 NUL 被截断而非保留")
{
    const std::string withNul("a\0b", 3);
    const QString got = qFromStdString(withNul);
    CHECK_EQ(got.length(), 1);                       // Qt6 上这里会是 3
    CHECK_EQ(got, QString("a"));
}

// ⚠ 边界：空串往返，且空串是「长度 0」而非异常。
TEST_CASE("qToStdString/qFromStdString: 空串边界")
{
    CHECK_EQ(qToStdString(QString("")), std::string(""));
    CHECK_EQ(qFromStdString(std::string("")).length(), 0);
}

// ── qSplit：对齐 Qt4 QString::split(QChar) 的 KeepEmptyParts 语义 ────────
// 回归点（qstring_shim.h:320-322）：默认 KeepEmptyParts，段数 = 分隔符数 + 1，
// 连续分隔符产生空段；空输入返回**含一个空串**的列表。这几条正是朴素实现
// （顺手 SkipEmpty）最容易分叉的地方。
TEST_CASE("qSplit: 连续/尾部/无分隔符的段数都按 KeepEmptyParts 计")
{
    const QStringList a = qSplit(QString("a,,b,"), QChar(','));
    CHECK_EQ(a.count(), 4);                     // "a" "" "b" ""
    CHECK_EQ(a[0], QString("a"));
    CHECK_EQ(a[1], QString(""));
    CHECK_EQ(a[2], QString("b"));
    CHECK_EQ(a[3], QString(""));
    // ⚠ 空段必须**非 null**：Qt3 的 mid() 在起点==长度时给 null，而 Qt6 给
    //   非 null 空串。不钉这条，`part == QString("")` 在两版本上结论相反
    //   （详见 qstring_shim.h 的 qSplit 注释）。第 4 段正是尾部分隔符产物。
    CHECK(!a[1].isNull());
    CHECK(!a[3].isNull());
    CHECK(a[1].isEmpty());
    CHECK(a[3].isEmpty());

    const QStringList b = qSplit(QString("abc"), QChar(','));   // 无分隔符
    CHECK_EQ(b.count(), 1);
    CHECK_EQ(b[0], QString("abc"));
}

TEST_CASE("qSplit: 空输入返回含一个空串的列表（不是空列表）")
{
    const QStringList e = qSplit(QString(""), QChar(','));
    CHECK_EQ(e.count(), 1);                     // 关键：1 而不是 0
    CHECK_EQ(e[0], QString(""));
    CHECK(!e[0].isNull());                      // 同上：非 null 空串，不是 null
}

// ── qStringListAt：仅界内 ───────────────────────────────────────────────
// ⚠⚠ Qt3 的 QStringList 是 QValueList<QString>，**越界下标会 ASSERT 掉整个
//   进程**（实测：qStringListAt(l,-1) 触发 qvaluelist.h:381
//   ASSERT "i <= nodes"）。故：
//     1) 本用例**只能**覆盖界内取值，否则全量测试会直接 abort；
//     2) 契约是「调用方必须自行判界」—— 这不是缺陷，是 Qt3 的既有行为。
//   历史动机见 qstring_shim.h:423-429：Qt3 的 .at() 返回**迭代器**而非元素，
//   故垫片转调 operator[]；跨版本调用点一律走 qStringListAt。
TEST_CASE("qStringListAt: 界内取值为元素本身，跨版本语义一致")
{
    QStringList l;
    l << "first" << "second" << "third";
    CHECK_EQ(qStringListAt(l, 0), QString("first"));
    CHECK_EQ(qStringListAt(l, 2), QString("third"));
    // ⚠ 不要在这里加 l[-1] / l[3]：Qt3 会 ASSERT 整进程退出。
}

// ── qStringListBuild：变参构造（1..8 个） ────────────────────────────────
// 回归点：stickerstore.cpp 的函数体 Qt3/Qt6 共用，不能写 initializer_list
// （Qt3 侧编不过），故用 1..8 个重载。这里抽查 0/1/3/8 个，确保每个元数都建对。
TEST_CASE("qStringListBuild: 0/1/3/8 个元素的变参构造")
{
    CHECK_EQ(qStringListBuild0().count(), 0);

    const QStringList one = qStringListBuild(QString("a"));
    CHECK_EQ(one.count(), 1);
    CHECK_EQ(one[0], QString("a"));

    const QStringList three = qStringListBuild(QString("a"), QString("b"), QString("c"));
    CHECK_EQ(three.count(), 3);
    CHECK_EQ(three[0], QString("a"));
    CHECK_EQ(three[1], QString("b"));
    CHECK_EQ(three[2], QString("c"));

    const QStringList eight = qStringListBuild(
        QString("1"), QString("2"), QString("3"), QString("4"),
        QString("5"), QString("6"), QString("7"), QString("8"));
    CHECK_EQ(eight.count(), 8);
    CHECK_EQ(eight[0], QString("1"));
    CHECK_EQ(eight[7], QString("8"));
}

// ⚠ 混合类型（const char* 与 QString）也要能编 —— 调用点常写
//   qStringListBuild("literal", someQString)，重载分辨率必须吃得下。
TEST_CASE("qStringListBuild: 混入 const char* 字面量仍正确")
{
    const QStringList l = qStringListBuild("lit", QString("qs"), "lit2");
    CHECK_EQ(l.count(), 3);
    CHECK_EQ(l[0], QString("lit"));
    CHECK_EQ(l[1], QString("qs"));
    CHECK_EQ(l[2], QString("lit2"));
}

// ── qStringClear ────────────────────────────────────────────────────────
// 回归点：Qt3 无 clear()，只能靠赋值归零；Qt4+ 是 s.clear()。
// ⚠ 两版本在此**故意不完全一致**，取舍见下面注释：
//   Qt6 实测 clear() 给的是 **null**（isNull=1，/tmp/opencode/probe/zk9.cpp），
//   而 Qt3 若写 `s = QString()` 则 `s == QString("")` 为 false、Qt6 为 true ——
//   「清空后与空串比较」在两版本上结论相反，是会静默分叉的那类分歧。
//   产品侧 isNull() 只用在 QImage 上（grep 全仓无 QString 的 isNull 调用），
//   故选择让 Qt3 给**非 null 空串**：牺牲 isNull()，保住 == "" 与 isEmpty()。
TEST_CASE("qStringClear: 清空为长度 0 的非 null 空串")
{
    QString s("non empty");
    qStringClear(s);
    CHECK_EQ(s.length(), 0);
    CHECK(s.isEmpty());
    CHECK(!s.isNull());                            // 见上：有意与 Qt6 的 clear() 不同
    CHECK_EQ(s, QString(""));
}
