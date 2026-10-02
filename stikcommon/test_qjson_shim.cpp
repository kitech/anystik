// stikcommon/test_qjson_shim.cpp —— qjson_shim.h 契约（Qt3 单端）
//
// 被测对象：
//   qJsonParseObject()      —— 唯一的两版本共用入口（Qt3 走 cJSON 版
//                             QJsonDocument，Qt4.5+ 走原生 + QJsonParseError）。
//   cJSON 版 QJsonObject    —— keys/contains/value/size/constBegin 迭代。
//   cJSON 版 QJsonValue     —— isXxx/toXxx 的类型判定与「不跨类型强转」。
//
// ⚠ 本文件需链接 stikcommon/qjson_shim.cpp + qldox/cJSON.c（已在
//   build_tests.sh 的 PRODUCTS 里），不是纯头。
//
// ⚠ Qt3 侧没有 QJsonParseError（Qt4.5 才有），故失败判定只能是
//   「ok=false / isEmpty()」。这正是 qJsonParseObject 抽出来统一的目的 ——
//   调用点不必再关心两侧的失败表现差异。
//
// ⚠ 非 ASCII 一律走 utf8()（AGENTS.md）：本 shim 内部用
//   QString::fromUtf8(cJSON 的 char*)，故喂进去的 JSON 字节必须是 UTF-8。
//   本仓不使用 latin1/ascii 系列做文本转换 —— 它们对非 ASCII 是「1 字节 →
//   1 码点」的硬映射，UTF-8 多字节序列会被逐字节拆成乱码，且**不报错**，
//   只能靠码点断言发现。故含中文的断言用 CHECK(a == b) 而非 CHECK_EQ ——
//   后者要打印 QString，在 Qt3 + 管道环境下转码不可靠（曾因此把「数据坏了」
//   误判成「打印坏了」）。

#include <qstring.h>
#include <qcstring.h>   // Qt3 的 QByteArray 定义在此（无独立 qbytearray.h）
#include <qstringlist.h>

#include "doctest/doctest.h"

#include "qjson_shim.h"

namespace {

// 用 UTF-8 源字面量构造 JSON 字节（Qt3 的 utf8() 是规范做法）
inline QByteArray json(const char* utf8Src) { return QString::fromUtf8(utf8Src).utf8(); }

} // namespace

// ── qJsonParseObject：成功路径 ────────────────────────────────────────

TEST_CASE("qJsonParseObject: 解析合法 object，ok=true 且键值可取")
{
    bool ok = false;
    const QJsonObject o = qJsonParseObject(json("{\"name\":\"alpha\",\"n\":42}"), &ok);
    CHECK(ok);
    CHECK_EQ(o.size(), 2);
    CHECK(o.contains(QString::fromLatin1("name")));
    CHECK(o.value(QString::fromLatin1("name")).isString());
    CHECK(o.value(QString::fromLatin1("name")).toString() == QString::fromLatin1("alpha"));
    CHECK(o.value(QString::fromLatin1("n")).isDouble());
    CHECK_EQ(o.value(QString::fromLatin1("n")).toInt(), 42);
}

// ⚠ 回归点：cJSON 只存 double，int 是**按值存 double** 取回。故
//   toInt() 在大 int 上必须仍然精确 —— 贴纸元数据里有 id/张数等字段，
//   一旦被截成 float 就会出现「张数对不上」。2^30 远小于 2^53，能被
//   double 精确表示，是这条契约的判据。
TEST_CASE("qJsonParseObject: int 经 double 存储后 toInt 仍精确（2^30）")
{
    bool ok = false;
    // 1073741824 == 2^30
    const QJsonObject o = qJsonParseObject(json("{\"big\":1073741824}"), &ok);
    CHECK(ok);
    const QJsonValue v = o.value(QString::fromLatin1("big"));
    CHECK(v.isDouble());          // Qt 原生也把 JSON number 视作 double
    CHECK_EQ(v.toInt(), 1073741824);
    CHECK_EQ((int)v.toDouble(), 1073741824);
}

// ── qJsonParseObject：失败路径 ────────────────────────────────────────

TEST_CASE("qJsonParseObject: 非法 JSON → ok=false 且返回空 object")
{
    bool ok = true;
    const QJsonObject o = qJsonParseObject(json("{\"a\":}"), &ok);
    CHECK(!ok);
    CHECK(o.isEmpty());
    CHECK_EQ(o.size(), 0);
}

TEST_CASE("qJsonParseObject: 顶层非 object 一律失败（数组/标量/空）")
{
    bool ok = true;
    // 顶层是数组 —— 返回的是 QJsonObject，isObject() 为假
    CHECK(!qJsonParseObject(json("[1,2,3]"), &ok).size());
    CHECK(!ok);

    ok = true;
    CHECK(!qJsonParseObject(json("42"), &ok).size());
    CHECK(!ok);

    ok = true;
    CHECK(!qJsonParseObject(json("\"str\""), &ok).size());
    CHECK(!ok);

    ok = true;
    CHECK(!qJsonParseObject(json(""), &ok).size());       // 空输入
    CHECK(!ok);

    ok = true;
    CHECK(!qJsonParseObject(QByteArray(), &ok).size());   // 空 QByteArray
    CHECK(!ok);
}

TEST_CASE("qJsonParseObject: ok 传 0（不取标志）不崩")
{
    const QJsonObject o = qJsonParseObject(json("{\"k\":1}"), 0);
    CHECK_EQ(o.size(), 1);
    // 失败路径同样不能崩
    const QJsonObject bad = qJsonParseObject(json("nope"), 0);
    CHECK(bad.isEmpty());
}

// ── UTF-8 往返（AGENTS.md 关注的非 ASCII 通道）────────────────────────
// 回归点：cJSON 存的是 char*（UTF-8 字节），shim 用 QString::fromUtf8
// 还原。若哪一层误用 latin1()，中文会变成乱码且**不报错** —— 只能靠
// 码点断言发现，故这里直接比 QString 相等。

TEST_CASE("qJsonParseObject: 中文 key 与 value 原样往返（UTF-8 不被破坏）")
{
    bool ok = false;
    const QJsonObject o =
        qJsonParseObject(json("{\"标题\":\"贴纸包\",\"数量\":3}"), &ok);
    CHECK(ok);
    CHECK_EQ(o.size(), 2);
    CHECK(o.contains(QString::fromUtf8("标题")));
    const QString title = o.value(QString::fromUtf8("标题")).toString();
    CHECK(title == QString::fromUtf8("贴纸包"));
    // 码点级校验：'贴' 应是 U+8D34。
    // ⚠ Qt3 的 QString::at() 返回 **QChar 值**（qstring.h:646），而
    //   QString::unicode() 是 QChar 的成员不是 QString 的成员（QString 在
    //   372 行有个 `QChar *unicode` 成员，同名易混）—— 故用 at(0)。
    CHECK_EQ(title.at(0).unicode(), 0x8D34u);
    CHECK_EQ(o.value(QString::fromUtf8("数量")).toInt(), 3);
}

// ── 嵌套结构 ────────────────────────────────────────────────────────

TEST_CASE("qJsonParseObject: 嵌套 object/array 可下钻")
{
    bool ok = false;
    const QJsonObject o = qJsonParseObject(
        json("{\"list\":[{\"id\":1},{\"id\":2}],\"meta\":{\"v\":9}}"), &ok);
    CHECK(ok);

    const QJsonArray arr = o.value(QString::fromLatin1("list")).toArray();
    CHECK_EQ(arr.size(), 2);
    CHECK_EQ(arr.at(0).toObject().value(QString::fromLatin1("id")).toInt(), 1);
    CHECK_EQ(arr.at(1).toObject().value(QString::fromLatin1("id")).toInt(), 2);

    CHECK_EQ(o.value(QString::fromLatin1("meta")).toObject()
                 .value(QString::fromLatin1("v")).toInt(), 9);
}

// ── 缺失键与类型不匹配：不跨类型强转（与 Qt 原生一致）───────────────

TEST_CASE("qJsonParseObject: 缺失键 → Undefined，取值回落到默认值")
{
    bool ok = false;
    const QJsonObject o = qJsonParseObject(json("{\"a\":1}"), &ok);
    CHECK(ok);
    const QJsonValue miss = o.value(QString::fromLatin1("nope"));
    CHECK(miss.isUndefined());
    // 各 toXxx 都应回落到 def，而不是从别的键/类型里硬转
    CHECK_EQ(miss.toInt(-7), -7);
    CHECK(miss.toString(QString::fromLatin1("fallback")) == QString::fromLatin1("fallback"));
    CHECK_EQ(miss.toBool(true), true);
}

TEST_CASE("qJsonParseObject: 类型不匹配不跨类型强转（string 取 int 回落默认）")
{
    bool ok = false;
    const QJsonObject o = qJsonParseObject(json("{\"s\":\"notanumber\"}"), &ok);
    CHECK(ok);
    const QJsonValue v = o.value(QString::fromLatin1("s"));
    CHECK(v.isString());
    CHECK(!v.isDouble());
    CHECK_EQ(v.toInt(99), 99);     // 不把 "notanumber" 硬转成 0
}

// ── QJsonObject 迭代面 ───────────────────────────────────────────────

TEST_CASE("qJsonObject: keys()/constBegin 迭代能取全键值")
{
    bool ok = false;
    const QJsonObject o = qJsonParseObject(json("{\"a\":1,\"b\":2}"), &ok);
    CHECK(ok);

    QStringList ks = o.keys();
    ks.sort();
    CHECK_EQ(ks.count(), 2);
    CHECK(ks[0] == QString::fromLatin1("a"));
    CHECK(ks[1] == QString::fromLatin1("b"));

    int n = 0;
    for (QJsonObject::const_iterator it = o.constBegin(); it != o.constEnd(); ++it) {
        CHECK(it.key().isEmpty() == false);
        CHECK(it.value().isDouble());
        ++n;
    }
    CHECK_EQ(n, 2);
}
