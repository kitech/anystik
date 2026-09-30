// stikcommon/test_qdatetime_shim.cpp —— qdatetime_shim 契约（Qt3 单端，零网络）
//
// 被测对象为 stikcommon/qdatetime_shim.{h,cpp}：
//   * Qt3 无 toString(fmt) / QLocale(English,US).toString(dt,fmt) / toUTC() /
//     toMSecsSinceEpoch() / fromMSecsSinceEpoch() / fromString(Qt::ISODate) /
//     QLocale::toDateTime(s[,fmt])，本垫片以自由函数补齐。
//   * 核心约定：Qt3 的 QDateTime 内部只存墙钟（无 timespec 字段），故本垫片
//     **所有 QDateTime 值一律按 UTC 墙钟存放**。qDateTimeToUtc() 因此是恒等变换。
//     这一点是下面所有绝对毫秒断言成立的前提。
//
// ⚠ 需要链接 qdatetime_shim.cpp（实现全在 .cpp，Qt3 专用写法无法跨版本）。
//
// 期望值来源：epoch 基准用 `date -u -d ... +%s` 取权威值，星期名同样取自 date，
//   不凭记忆写死。epoch 基准点：
//     1970-01-01T00:00:00Z = 0          (Thu)
//     2000-01-01T00:00:00Z = 946684800  (Sat)
//     2026-01-05T09:08:07Z = 1767604087 (Mon)
//     1969-12-31T23:59:59Z = -1         (Wed)

#include <qdatetime.h>
#include <qstring.h>

#include "doctest/doctest.h"

#include "qdatetime_shim.h"

// ── 绝对毫秒：toMsecs / fromMsecs ───────────────────────────────────

TEST_CASE("qDateTimeToMsecs: epoch 基准与无效值")
{
    CHECK_EQ(qDateTimeToMsecs(QDateTime(QDate(1970, 1, 1), QTime(0, 0, 0))), 0);
    CHECK_EQ(qDateTimeToMsecs(QDateTime(QDate(2000, 1, 1), QTime(0, 0, 0))),
             946684800000LL);
    CHECK_EQ(qDateTimeToMsecs(QDateTime(QDate(2026, 1, 5), QTime(9, 8, 7))),
             1767604087000LL);
    // 毫秒分量
    CHECK_EQ(qDateTimeToMsecs(QDateTime(QDate(2026, 1, 5), QTime(9, 8, 7, 123))),
             1767604087000LL + 123);
    // ⚠ 无效 QDateTime 必须得 0：davbisync 正是靠这个探测
    // 「服务器是否支持 getlastmodified」（davbisync.cpp:396-398）
    CHECK_EQ(qDateTimeToMsecs(QDateTime()), 0);
    // 纪元之前（负值），走 buildFromFields 之外的直接换算
    CHECK_EQ(qDateTimeToMsecs(QDateTime(QDate(1969, 12, 31), QTime(23, 59, 59))),
             -1000LL);
}

TEST_CASE("qDateTimeFromMsecs: 含纪元前的负数取整（rem<0 分支）")
{
    const QDateTime epoch = qDateTimeFromMsecs(0);
    CHECK(epoch.isValid());
    CHECK(epoch.date() == QDate(1970, 1, 1));
    CHECK(epoch.time() == QTime(0, 0, 0));

    CHECK(qDateTimeFromMsecs(946684800000LL).date() == QDate(2000, 1, 1));
    // -1 毫秒 → 1969-12-31T23:59:59.999。C++ 的 -1 % 86400000 得负数，
    // 垫片显式把 rem 拉回正区间并把 days 减一（qdatetime_shim.cpp:327-330）
    const QDateTime before = qDateTimeFromMsecs(-1);
    CHECK(before.isValid());
    CHECK(before.date() == QDate(1969, 12, 31));
    CHECK(before.time() == QTime(23, 59, 59, 999));
    // 再退一天
    CHECK(qDateTimeFromMsecs(-86400000LL).date() == QDate(1969, 12, 31));
}

TEST_CASE("qDateTimeToMsecs/FromMsecs 往返一致")
{
    const QDateTime a(QDate(2026, 1, 5), QTime(9, 8, 7, 456));
    CHECK(qDateTimeFromMsecs(qDateTimeToMsecs(a)) == a);
    const QDateTime b(QDate(1999, 12, 31), QTime(23, 59, 59, 999));
    CHECK(qDateTimeFromMsecs(qDateTimeToMsecs(b)) == b);
    const QDateTime c(QDate(1970, 1, 1), QTime(0, 0, 0));
    CHECK(qDateTimeFromMsecs(qDateTimeToMsecs(c)) == c);
}

TEST_CASE("qDateTimeToUtc: 恒等变换（值本就按 UTC 墙钟存）")
{
    const QDateTime a(QDate(2026, 1, 5), QTime(9, 8, 7));
    CHECK(qDateTimeToUtc(a) == a);
    // 绝对时刻不变
    CHECK_EQ(qDateTimeToMsecs(qDateTimeToUtc(a)), qDateTimeToMsecs(a));
    // 无效值传入返回无效（不崩）
    CHECK(qDateTimeToUtc(QDateTime()).isValid() == false);
}

TEST_CASE("qNowMsecs: 合理量级且为当前 UTC")
{
    const qint64 now = qNowMsecs();
    // 必须晚于 2026-01-05 的基准，且不早于 2000 年
    CHECK(now > 946684800000LL);
    CHECK(now < 4000000000000LL);   // 远未到 2096，够挡住「返回 0 / 秒当毫秒」
}

// ── 格式化 ──────────────────────────────────────────────────────────

TEST_CASE("qFormatDateTime: 数字 token 补零宽度")
{
    const QDateTime dt(QDate(2026, 1, 5), QTime(9, 8, 7));
    CHECK(qFormatDateTime(dt, "yyyy-MM-dd hh:mm:ss") == QString("2026-01-05 09:08:07"));
    CHECK(qFormatDateTime(dt, "yy") == QString("26"));
    CHECK(qFormatDateTime(dt, "yyyy") == QString("2026"));
    CHECK(qFormatDateTime(dt, "M") == QString("1"));
    CHECK(qFormatDateTime(dt, "MM") == QString("01"));
    CHECK(qFormatDateTime(dt, "d") == QString("5"));
    CHECK(qFormatDateTime(dt, "dd") == QString("05"));
    CHECK(qFormatDateTime(dt, "h") == QString("9"));
    CHECK(qFormatDateTime(dt, "hh") == QString("09"));
    CHECK(qFormatDateTime(dt, "m") == QString("8"));
    CHECK(qFormatDateTime(dt, "mm") == QString("08"));
    CHECK(qFormatDateTime(dt, "s") == QString("7"));
    CHECK(qFormatDateTime(dt, "ss") == QString("07"));
    // 无效值与空格式返回空串
    CHECK(qFormatDateTime(QDateTime(), "yyyy").isEmpty());
    CHECK(qFormatDateTime(dt, "").isEmpty());
}

TEST_CASE("qFormatDateTime: 月份与星期名")
{
    const QDateTime dt(QDate(2026, 1, 5), QTime(9, 8, 7));
    CHECK(qFormatDateTime(dt, "MMM") == QString("Jan"));
    CHECK(qFormatDateTime(dt, "MMMM") == QString("January"));
    // 2026-01-05 是周一（取自 date -u -d "2026-01-05" +%a）
    CHECK(qFormatDateTime(dt, "ddd") == QString("Mon"));
    CHECK(qFormatDateTime(dt, "dddd") == QString("Monday"));
}

TEST_CASE("qFormatDateTime: h 单独出现时是 24 小时制（Qt 约定）")
{
    // ⚠ 关键约定：qdatetime_shim.cpp:418 是 `padStr(ampm ? hour12 : t.hour(), len)`，
    //   只有当**同一格式串里出现 AM/PM 标记**（t / a / A）时，h 才切到 12 小时制。
    //   这与 Qt 一致，不是缺陷。下面的 ap 用例另有一处真缺陷。
    const QDateTime am(QDate(2026, 1, 5), QTime(9, 8, 7));
    const QDateTime pm(QDate(2026, 1, 5), QTime(13, 8, 7));
    // 无 AM/PM 标记 → 24 小时制
    CHECK(qFormatDateTime(am, "h") == QString("9"));
    CHECK(qFormatDateTime(am, "hh") == QString("09"));
    CHECK(qFormatDateTime(pm, "h") == QString("13"));
    CHECK(qFormatDateTime(pm, "hh") == QString("13"));
    // 显式 24 小时制用大写 H
    CHECK(qFormatDateTime(pm, "H") == QString("13"));
    CHECK(qFormatDateTime(pm, "HH") == QString("13"));
    // 配了 AM/PM 标记才切 12 小时制
    CHECK(qFormatDateTime(pm, "h t") == QString("1 PM"));
    CHECK(qFormatDateTime(am, "h t") == QString("9 AM"));
    // 午夜 0 点在 12 小时制里是 12
    const QDateTime mid(QDate(2026, 1, 5), QTime(0, 30, 0));
    CHECK(qFormatDateTime(mid, "h t") == QString("12 AM"));
    // 正午
    const QDateTime noon(QDate(2026, 1, 5), QTime(12, 0, 0));
    CHECK(qFormatDateTime(noon, "h t") == QString("12 PM"));
}

TEST_CASE("qFormatDateTime: AM/PM 标记只有 t/a 可用，ap/AP 已损坏")
{
    const QDateTime am(QDate(2026, 1, 5), QTime(9, 8, 7));
    const QDateTime pm(QDate(2026, 1, 5), QTime(13, 8, 7));
    // 可用的：t / tt 出大写，a / A 出小写（qdatetime_shim.cpp:431-435）
    CHECK(qFormatDateTime(am, "t") == QString("AM"));
    CHECK(qFormatDateTime(pm, "t") == QString("PM"));
    CHECK(qFormatDateTime(am, "tt") == QString("AM"));   // 同字符成组，len=2 仍走 case 't'
    CHECK(qFormatDateTime(pm, "tt") == QString("PM"));
    CHECK(qFormatDateTime(am, "a") == QString("am"));
    CHECK(qFormatDateTime(pm, "a") == QString("pm"));
    CHECK(qFormatDateTime(am, "A") == QString("am"));
    // ⚠⚠ 缺陷：ap / AP 必然输出垃圾。
    //   qdatetime_shim.cpp:388-391 按**连续相同字符**给 token 分组，而 ap / AP 是
    //   由两个**不同**字符构成的单个 token。于是 'a' 命中 case 'a' 输出 "am"，
    //   随后的 'p' 落到 default 被原样抄出，拼成 "amp"；AP 同理得 "amP"/"pmP"。
    //   头文件 qdatetime_shim.h:39 却把 "AP ap" 列为受支持 token —— 文档与实现不符。
    //   生产影响：无。唯一调用方 qwebdavlite.cpp:409 的格式串是
    //   "ddd, dd MMM yyyy hh:mm:ss"，不含这些 token，DAV Date 头不受影响。
    //   修复需在分组之前显式识别 "ap"/"AP"（会动到 RFC1123 走的同一循环），
    //   故此处只锁定实测行为并记录，不擅自改。
    CHECK(qFormatDateTime(am, "ap") == QString("amp"));
    CHECK(qFormatDateTime(am, "AP") == QString("amP"));
    CHECK(qFormatDateTime(pm, "AP") == QString("pmP"));
}

TEST_CASE("qFormatDateTime: 格式串里的 'T' 是字面量，不得被当成 AM/PM 标记")
{
    // 垫片 qdatetime_shim.cpp:369-373 专门点明：'T' 不是格式字符。
    // 若 'T' 误触发 ampm 判定，'h' 就会切成 12 小时制。
    const QDateTime dt(QDate(2026, 1, 5), QTime(9, 8, 7));
    CHECK(qFormatDateTime(dt, "yyyy-MM-ddThh:mm:ss") == QString("2026-01-05T09:08:07"));
    // 13 点时若被误判成 12 小时制，这里会输出 01
    const QDateTime pm(QDate(2026, 1, 5), QTime(13, 8, 7));
    CHECK(qFormatDateTime(pm, "yyyy-MM-ddThh:mm:ss") == QString("2026-01-05T13:08:07"));
    // ⚠ 单引号不是转义引号：Qt 的日期格式串没有引号转义机制，未识别的字符
    //   一律原样输出（qdatetime_shim.cpp:436-440 的 default 分支），故 'T' 的
    //   两个引号会被保留下来。
    CHECK(qFormatDateTime(pm, "yyyy-MM-dd'T'hh:mm:ss") ==
          QString("2026-01-05'T'13:08:07"));
}

TEST_CASE("qFormatRfc1123DateTime: 固定 +GMT 后缀（QWebdavLite::put 发的 Date 头）")
{
    const QDateTime dt(QDate(2026, 1, 5), QTime(9, 8, 7));
    CHECK(qFormatRfc1123DateTime(dt) == QString("Mon, 05 Jan 2026 09:08:07 GMT"));
    CHECK(qFormatRfc1123DateTime(QDateTime()).isEmpty());
}

// ── 解析 ────────────────────────────────────────────────────────────

TEST_CASE("qParseDateTime/qParseDate/qParseTime: 显式格式")
{
    const QDateTime dt = qParseDateTime(QString("2026-01-05 09:08:07"),
                                         "yyyy-MM-dd hh:mm:ss");
    CHECK(dt.isValid());
    CHECK(dt.date() == QDate(2026, 1, 5));
    CHECK(dt.time() == QTime(9, 8, 7));
    // 只取日期
    const QDate d = qParseDate(QString("05/01/2026"), "dd/MM/yyyy");
    CHECK(d.isValid());
    CHECK(d == QDate(2026, 1, 5));
    // 只取时间
    const QTime t = qParseTime(QString("09:08:07"), "hh:mm:ss");
    CHECK(t.isValid());
    CHECK(t == QTime(9, 8, 7));
    // 格式不匹配 → 无效
    CHECK(qParseDate(QString("2026-01-05"), "dd/MM/yyyy").isValid() == false);
    // 非法日历日 → 无效
    CHECK(qParseDate(QString("2026-02-30"), "yyyy-MM-dd").isValid() == false);
    // 非法时刻 → 无效（QTime 构造越界值会得到 isValid()==false）
    CHECK(qParseTime(QString("25:00:00"), "hh:mm:ss").isValid() == false);

    // ⚠ qParseTime 无法用 isValid() 表达「解析失败」：Qt3 的 QTime() 就是 00:00:00
    //   且 isValid() 恒为 true（实测 QTime() isValid=1），所以喂完全对不上的串
    //   也会得到一个"有效"的午夜。qdatetime_shim.cpp:477 的注释说
    //   「调用方用 isValid() 判定」，在 QTime 上做不到。
    //   本仓无任何生产调用方（已 grep 确认 stikcommon/qlstik/anystik/src 全无），
    //   故为潜伏问题；此处锁定实测行为并记录，调用时必须另想办法判成败
    //   （例如先判非空、或改用 qParseDateTime）。
    CHECK(qParseTime(QString("nope"), "hh:mm:ss").isValid());   // 仍是"有效"的午夜
    CHECK(qParseTime(QString("nope"), "hh:mm:ss") == QTime(0, 0, 0));
}

TEST_CASE("qParseRfc1123DateTime: 与格式化互逆")
{
    const QDateTime dt(QDate(2026, 1, 5), QTime(9, 8, 7));
    const QString s = qFormatRfc1123DateTime(dt);
    const QDateTime back = qParseRfc1123DateTime(s);
    CHECK(back.isValid());
    CHECK(back == dt);
    // 真实服务器头的样子
    const QDateTime r = qParseRfc1123DateTime(QString("Wed, 30 Sep 2026 09:36:47 GMT"));
    CHECK(r.isValid());
    CHECK(r.date() == QDate(2026, 9, 30));
    CHECK(r.time() == QTime(9, 36, 47));
    CHECK_EQ(qDateTimeToMsecs(r), 1790761007000LL);
    // 垃圾输入
    CHECK(qParseRfc1123DateTime(QString("not a date")).isValid() == false);
    CHECK(qParseRfc1123DateTime(QString("")).isValid() == false);
}

TEST_CASE("qParseIsoDateTime: Z 与 ±hh:mm 偏移都归一到 UTC 墙钟")
{
    const QDateTime z = qParseIsoDateTime(QString("2026-01-05T09:08:07Z"));
    CHECK(z.isValid());
    CHECK_EQ(qDateTimeToMsecs(z), 1767604087000LL);
    // +02:00：墙钟比 UTC 快 2 小时，故 UTC = 墙钟 - 2h
    // （qdatetime_shim.cpp:309 是 wallToSecs - tzOffset）
    const QDateTime plus = qParseIsoDateTime(QString("2026-01-05T09:08:07+02:00"));
    CHECK(plus.isValid());
    CHECK_EQ(qDateTimeToMsecs(plus), 1767604087000LL - 7200000LL);
    // -05:00：UTC = 墙钟 + 5h
    const QDateTime minus = qParseIsoDateTime(QString("2026-01-05T09:08:07-05:00"));
    CHECK(minus.isValid());
    CHECK_EQ(qDateTimeToMsecs(minus), 1767604087000LL + 18000000LL);
    // 两者代表同一绝对时刻，只是墙钟不同
    const QDateTime same = qParseIsoDateTime(QString("2026-01-05T07:08:07Z"));
    CHECK(qDateTimeToMsecs(same) == qDateTimeToMsecs(plus));
    // ISO 8601 允许 date-only，故无时刻部分也接受（补 00:00:00）
    const QDateTime dateOnly = qParseIsoDateTime(QString("2026-01-05"));
    CHECK(dateOnly.isValid());
    CHECK(dateOnly.date() == QDate(2026, 1, 5));
    CHECK(dateOnly.time() == QTime(0, 0, 0));
    // 完全不成 ISO 的
    CHECK(qParseIsoDateTime(QString("garbage")).isValid() == false);
    CHECK(qParseIsoDateTime(QString("")).isValid() == false);
}

TEST_CASE("qParseDateTimeAuto: 依次尝试多种格式（顺序见 qdatetime_shim.cpp:588-611）")
{
    // ISO
    CHECK(qParseDateTimeAuto(QString("2026-01-05T09:08:07Z")).isValid());
    // RFC1123
    const QDateTime rfc = qParseDateTimeAuto(QString("Mon, 05 Jan 2026 09:08:07 GMT"));
    CHECK(rfc.isValid());
    CHECK(rfc.date() == QDate(2026, 1, 5));
    // asctime 风格
    CHECK(qParseDateTimeAuto(QString("Sun Nov  6 08:49:37 1994")).isValid());
    // "d MMM yyyy hh:mm:ss"
    const QDateTime d1 = qParseDateTimeAuto(QString("6 Nov 1994 08:49:37"));
    CHECK(d1.isValid());
    CHECK(d1.date() == QDate(1994, 11, 6));
    // "yyyy-MM-dd hh:mm:ss"
    const QDateTime d2 = qParseDateTimeAuto(QString("2026-01-05 09:08:07"));
    CHECK(d2.isValid());
    CHECK(d2.time() == QTime(9, 8, 7));
    // 只有日期
    const QDateTime d3 = qParseDateTimeAuto(QString("2026-01-05"));
    CHECK(d3.isValid());
    CHECK(d3.date() == QDate(2026, 1, 5));
    // 首尾空白被剥掉
    CHECK(qParseDateTimeAuto(QString("  2026-01-05 09:08:07  ")).isValid());
    // 全不匹配
    CHECK(qParseDateTimeAuto(QString("garbage")).isValid() == false);
    CHECK(qParseDateTimeAuto(QString("")).isValid() == false);
    CHECK(qParseDateTimeAuto(QString("   ")).isValid() == false);
}
