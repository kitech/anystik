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


#include "doctest/doctest.h"

#include "qstring_shim.h"

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
