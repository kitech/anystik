// stikcommon/test_qba_shim.cpp —— qba_shim.h 契约（Qt3 单端）
//
// 被测对象：qbaToLongLong()。
//
// 该头是纯头，**无需链接任何产品 .cpp**。
// ⚠ Qt3 没有 QByteArray::toLongLong()，也没有 qlonglong/qint64 这两个类型名
//   （qglobal.h 里只有 Q_LLONG，见 :727；qint64 是 stikcommon 自己
//   typedef 出来的，见 qglobaltype_shim.h:56）。

#include <qstring.h>
#include <qcstring.h>   // Qt3 的 QByteArray 定义在此（无独立 qbytearray.h）

#include "doctest/doctest.h"

#include "qba_shim.h"

// ── qbaToLongLong：Qt3 返回 Q_LLONG，64 位不能被截断 ─────────────────────
// 回归点：Qt3 的 QByteArray 无 toLongLong()，更没有 qlonglong 类型
// 实测 qglobal.h 里连 qint64 都没有，只有 Q_LLONG（:727）。早期实现误用 int 承接，
// 贴纸包 Content-Length 超过 2GB 就会溢出。Qt3 侧返回 Q_LLONG（qglobal.h），
// Qt4+ 是 qlonglong，故返回类型必须按版本走。
TEST_CASE("qbaToLongLong: 64 位值不被截断，且 ok 标志正确")
{
    bool ok = false;
    // 2^40 + 12345，远超 int 上限
    const QByteArray big = qbaFromRaw("1099511632245", 13);
    const Q_LLONG v = qbaToLongLong(big, &ok);
    CHECK(ok);
    CHECK_EQ(v, 1099511632245LL);
    // 负值
    ok = false;
    CHECK_EQ(qbaToLongLong(qbaFromRaw("-42", 3), &ok), (Q_LLONG)-42);
    CHECK(ok);
}

TEST_CASE("qbaToLongLong: 空串/非数字 → ok=false 且返回 0")
{
    bool ok = true;
    CHECK_EQ(qbaToLongLong(QByteArray(), &ok), (Q_LLONG)0);
    CHECK(!ok);
    ok = true;
    CHECK_EQ(qbaToLongLong(qbaFromRaw("abc", 3), &ok), (Q_LLONG)0);
    CHECK(!ok);
    // 不传 ok 指针也不能崩
    CHECK_EQ(qbaToLongLong(qbaFromRaw("7", 1)), (Q_LLONG)7);
}

