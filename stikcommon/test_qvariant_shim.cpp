// stikcommon/test_qvariant_shim.cpp —— qvariant_shim.h 契约（Qt3 单端）
//
// 被测对象：qVariantMapValue()（单参/双参）。
//
// 该头是纯头，**无需链接任何产品 .cpp**。

#include <qstring.h>
#include <qvariant.h>

#include "doctest/doctest.h"

#include "qvariant_shim.h"

// ── QVariantMap 垫片 ──────────────────────────────────────────────────
// 回归点：Qt3 的 QMap 没有 Qt4.0 才加的 value(key)/value(key,def)
// （qvariant.h:87 的 QVariantMap 就是 QMap<QString,QVariant>），Qt6 上自然的
// `m.value(k, def).toXxx()` 在 Qt3 下报 "has no member named 'value'"。
//
// ⚠ 关键契约：双参版缺失时必须返回 **def 本身**（类型即 def 的类型），
//   而不是「返回 default-constructed QVariant 再让调用点 toXxx()」——
//   后者在 def 是整数 -1 时会得到 invalid QVariant，toLongLong() 变 0，
//   与 Qt6 行为不一致（Qt6 的 value(key,def) 保留 def 类型）。
//   stickerstore 的下载大小提示就靠这个区分 -1（未知）与 0。

TEST_CASE("qVariantMapValue: 单参版命中取真值，缺失返回 invalid QVariant")
{
    QVariantMap m;
    m.insert(QString("name"), QString("hello"));
    CHECK(qVariantMapValue(m, QString("name")).isValid());
    CHECK(qVariantMapValue(m, QString("name")).toString() == QString("hello"));
    CHECK(!qVariantMapValue(m, QString("missing")).isValid());
}

TEST_CASE("qVariantMapValue: 双参版缺失时返回 def 且保留 def 的类型")
{
    QVariantMap m;
    m.insert(QString("n"), 7);
    CHECK_EQ(qVariantMapValue(m, QString("n"), -1).toLongLong(), 7);
    // 缺失 → def 必须是 **valid** 且能 toLongLong 出 -1，
    // 而不是 invalid QVariant（那样 toLongLong() 会给 0）
    const QVariant miss = qVariantMapValue(m, QString("nope"), -1);
    CHECK(miss.isValid());
    CHECK_EQ(miss.toLongLong(), -1);
    CHECK_EQ(miss.type(), QVariant::Int);
}

TEST_CASE("qVariantMapValue: 空 map 上双参版同样返回 def")
{
    QVariantMap empty;
    const QVariant v = qVariantMapValue(empty, QString("any"), -1);
    CHECK(v.isValid());
    CHECK_EQ(v.toLongLong(), -1);
}

