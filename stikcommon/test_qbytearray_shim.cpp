// stikcommon/test_qbytearray_shim.cpp —— qbytearray_shim.h 契约（Qt3 单端）
//
// 被测对象：
//   qByteArrayAppend(QByteArray&, QByteArray / const char*) —— Qt3 的 QByteArray
//       是 typedef QMemArray<char>，无 Qt4 才有的成员 append()。
//   qNumberToByteArray(qint64)      —— 对齐 Qt4 的 QByteArray::number()。
//   qToBase64(const QByteArray&)    —— RFC4648 base64，返回 QCString。
//
// 该头是纯 inline，**无需链接任何产品 .cpp**。
//
// ⚠⚠ [历史，已移除] 本头曾在 Qt3 分支里带一个**无参数函数式宏**：
//      #define trimmed() stripWhiteSpace()
//   它会把本 TU 里**任何** `.trimmed()` 文本替换掉，且对 QByteArray 是错的
//   （QMemArray 连 stripWhiteSpace() 都没有，会展开成不存在的成员）。
//   现已删除，调用点统一用 qTrimmed()，故本文件可以正常使用 trimmed()。
//   删除说明见 qbytearray_shim.h:33-54。
//
// ⚠ 本头用到了 qint64，但 Qt3 根本没有 qint64 —— 那是 qglobaltype_shim.h:56
//   自己写的 typedef。原先本头**不能独立编译**（仅 include 即报
//   "'qint64' was not declared in this scope"），已在头内自带 typedef 修掉。
//   下面「独立编译」那条用例守着这个性质。

#include <qcstring.h>
#include <qstring.h>

#include "doctest/doctest.h"

#include "qbytearray_shim.h"
#include "qba_shim.h"       // qbaFromRaw —— Qt3 的 QByteArray 无 const char* 构造

namespace {

// Qt3 的 QMemArray<char> 构造面很窄：QByteArray(int size) 后逐字节写。
// size() 精确（不像 QCString::length() 含尾 NUL）。
inline QByteArray filled(int n, char c)
{
    QByteArray b(n);
    for (int i = 0; i < n; ++i) b[i] = c;
    return b;
}

} // namespace

// ── qByteArrayAppend ────────────────────────────────────────────────

TEST_CASE("qByteArrayAppend: 追加后内容与长度都对")
{
    QByteArray a = qbaFromRaw("hello");
    const int n0 = a.size();
    qByteArrayAppend(a, qbaFromRaw(" world"));
    CHECK_EQ(a.size(), n0 + 6);
    CHECK(qbaEqLit(a, "hello world"));
}

TEST_CASE("qByteArrayAppend: 追加空串是 no-op")
{
    QByteArray a = qbaFromRaw("keep");
    qByteArrayAppend(a, QByteArray());
    CHECK_EQ(a.size(), 4);
    CHECK(qbaEqLit(a, "keep"));
}

TEST_CASE("qByteArrayAppend: 连续多次追加顺序正确")
{
    QByteArray a = qbaFromRaw("A");
    qByteArrayAppend(a, qbaFromRaw("B"));
    qByteArrayAppend(a, qbaFromRaw("C"));
    qByteArrayAppend(a, qbaFromRaw("D"));
    CHECK(qbaEqLit(a, "ABCD"));
}

TEST_CASE("qByteArrayAppend: 往空数组追加等价于赋值")
{
    QByteArray a;
    CHECK_EQ(a.size(), 0);
    qByteArrayAppend(a, qbaFromRaw("first"));
    CHECK(qbaEqLit(a, "first"));
}

// ⚠ 回归点：**自追加** qByteArrayAppend(a, a)。这看着该是 use-after-free ——
//   shim 是「先 a.resize() 再读 b.data()」，而 Qt3 的 QMemArray::resize 确实
//   会释放旧块（隔离探针 + ASan 实证：resize 后读旧指针报 heap-use-after-free，
//   且 data 指针确实从 0x…dca0 移到 0x…ecd0）。
//   但实测**安全**，原因是形参是**引用**不是拷贝：b 绑定的是 a 本身，
//   resize 改的是同一个对象的内部指针，故 b.data() 读到的是**新**指针；
//   且 memcpy 的源区间 [0,n0) 与目标 [n0,n0+n1) 不重叠。
//   这条用例把该性质钉住 —— 若哪天签名改成按值传（QByteArray a），
//   b 就会是扩容前的快照，立刻变成 UAF，本用例会以内容不符/崩溃暴露。
TEST_CASE("qByteArrayAppend: 自追加 (a, a) 安全且内容为自身两份")
{
    QByteArray a = qbaFromRaw("ABCDEFGH", 8);
    qByteArrayAppend(a, a);
    CHECK_EQ(a.size(), 16);
    QByteArray expect = qbaFromRaw("ABCDEFGHABCDEFGH", 16);
    CHECK(a == expect);
}

TEST_CASE("qByteArrayAppend: const char* 重载，空指针是 no-op")
{
    QByteArray a = qbaFromRaw("x");
    qByteArrayAppend(a, "yz");
    CHECK(qbaEqLit(a, "xyz"));
    qByteArrayAppend(a, (const char*)0);      // 不崩，长度不变
    CHECK_EQ(a.size(), 3);
    CHECK(qbaEqLit(a, "xyz"));
    // 返回值即追加后的对象（Qt3 无法返回引用，改为返回值）
    CHECK(qByteArrayAppend(a, "!").size() == 4);
}

TEST_CASE("qByteArrayAppend: 追加含内嵌 0 的二进制不被 strlen 截断")
{
    QByteArray a = qbaFromRaw("A\0B", 3);
    CHECK_EQ(a.size(), 3);
    QByteArray b = qbaFromRaw("C\0D", 3);
    qByteArrayAppend(a, b);
    CHECK_EQ(a.size(), 6);
    QByteArray expect = qbaFromRaw("A\0BC\0D", 6);
    CHECK(a == expect);
}

// ── qNumberToByteArray ──────────────────────────────────────────────

TEST_CASE("qNumberToByteArray: 十进制 ASCII，含 0 与负数")
{
    CHECK(qbaEqLit(qNumberToByteArray(0), "0"));
    CHECK(qbaEqLit(qNumberToByteArray(42), "42"));
    CHECK(qbaEqLit(qNumberToByteArray(-7), "-7"));
    CHECK(qbaEqLit(qNumberToByteArray(1234567890LL), "1234567890"));
}

TEST_CASE("qNumberToByteArray: 64 位极值不截断（INT64_MIN/MAX）")
{
    // ⚠ 回归点：形参是 qint64。若实现误用 int 承接，INT64_MAX 会被截成
    //   -1 之类，贴纸包的字节数就会变成垃圾值。
    const long long big = 9223372036854775807LL;   // INT64_MAX
    QByteArray s = qNumberToByteArray(big);
    CHECK(qbaEqLit(s, "9223372036854775807"));
    // 手工回读校验（不依赖 QByteArray::toLongLong —— Qt3 无此成员）
    long long back = 0;
    for (int i = 0; i < s.size(); ++i) {
        const char c = s.at(i);
        const bool isDigit = (c >= '0' && c <= '9');   // 先求值：CHECK 里的 &&
        CHECK(isDigit);                                       // 会走 doctest 的分解模板
        back = back * 10 + (c - '0');
    }
    CHECK_EQ(back, big);

    const long long neg = -9223372036854775807LL - 1;  // INT64_MIN
    QByteArray ns = qNumberToByteArray(neg);
    CHECK(qbaEqLit(ns, "-9223372036854775808"));
}

// ── qToBase64 ──────────────────────────────────────────────────────
// RFC 4648 §10 的标准测试向量。这几个是规范里写死的，错了就是实现错。
// ⚠ 关键坑：Qt3 的 QByteArray 是 QMemArray<char>，data() 返回 signed char*。
//   0xFF 会符号扩展成 -1，若不转 unsigned char 就会用 -1>>2 = -1 去索引
//   TBL[] → 越界读。头里那句「必须转 unsigned char」正是防这个，故用
//   0xFF/0xFE/0x80 三组高位字节专门验。
//
// ⚠ 断言一律写 `qToBase64(x) == "..."`，**不**走 `.latin1()` /
//   QString::fromLatin1() 中转。qToBase64 返回 QCString，而 Qt3 的
//   QString::fromUtf8() 只收 const char*，从 QCString 拿 const char* 的
//   出口只有 latin1()/ascii()/operator const char*（qcstring.h:137 的
//   构造与 qstring.h:657/659/667 的取值）—— 全是 latin1/ascii 一路。
//   QCString 自带 operator==(const QCString&, const char*)（qcstring.h:294），
//   直接比较即可，语义也更直白，不必把 base64 结果先转成 QString 再转回来。
//   本仓规范：非 ASCII 一律走 utf8()（AGENTS.md）；此文件不碰任何
//   非 ASCII 文本转换。

TEST_CASE("qToBase64: RFC4648 标准向量")
{
    // ⚠ 空输入不能用 `== ""` 判等：Qt3 空 QCString 的 data() 是 NULL，而
    //   qstrcmp(NULL, "") 显式返回 -1（qcstring.h:70-72 把 NULL 与非 NULL
    //   判为不等），故 `QCString() == ""` 是 **false**。改判 length()。
    CHECK_EQ(qToBase64(qbaFromRaw("")).length(), 0);
    { QCString e = qToBase64(qbaFromRaw("f")); const bool eq = (e == "Zg=="); CHECK(eq); }
    { QCString e = qToBase64(qbaFromRaw("fo")); const bool eq = (e == "Zm8="); CHECK(eq); }
    { QCString e = qToBase64(qbaFromRaw("foo")); const bool eq = (e == "Zm9v"); CHECK(eq); }
    { QCString e = qToBase64(qbaFromRaw("foob")); const bool eq = (e == "Zm9vYg=="); CHECK(eq); }
    { QCString e = qToBase64(qbaFromRaw("fooba")); const bool eq = (e == "Zm9vYmE="); CHECK(eq); }
    { QCString e = qToBase64(qbaFromRaw("foobar")); const bool eq = (e == "Zm9vYmFy"); CHECK(eq); }
}

TEST_CASE("qToBase64: 高位字节（0xFF/0xFE/0x80）不越界、不乱码")
{
    // 0xFF 0xFF 0xFF → 24 个 1 bit → "////"
    QByteArray b(3);
    b[0] = (char)0xFF; b[1] = (char)0xFF; b[2] = (char)0xFF;
    { QCString e = qToBase64(b); const bool eq = (e == "////"); CHECK(eq); }

    // 单个 0xFF → 11111111 → 111111 11(0000) → "////==" 应为 "/w=="
    QByteArray one(1);
    one[0] = (char)0xFF;
    { QCString e = qToBase64(one); const bool eq = (e == "/w=="); CHECK(eq); }

    // 0x80 0x00 0x00 → 10000000 00000000 00000000 → "gAAA"
    QByteArray hi(3);
    hi[0] = (char)0x80; hi[1] = 0; hi[2] = 0;
    { QCString e = qToBase64(hi); const bool eq = (e == "gAAA"); CHECK(eq); }

    // 混合：0x00 0xFF 0x7F
    //   比特 00000000 11111111 01111111 → 6 位分组 000000|001111|111101|111111
    //   → 索引 0/15/61/63 → 'A'/'P'/'9'/'/'  = "AP9/"
    //   （61 = '9'：TBL 里 0-25 是 A-Z、26-51 是 a-z、52-61 是 0-9）
    QByteArray mix(3);
    mix[0] = 0; mix[1] = (char)0xFF; mix[2] = 0x7F;
    { QCString e = qToBase64(mix); const bool eq = (e == "AP9/"); CHECK(eq); }
}

TEST_CASE("qToBase64: 输出无内嵌换行，长度符合 4 的倍数")
{
    QByteArray big = filled(300, 'Z');
    const QCString enc = qToBase64(big);
    // 300 字节 → ceil(300/3)*4 = 100*4 = 400
    CHECK_EQ(enc.length(), 400);
    // Qt3 的 QCString::length() 含尾 NUL，故内容长度是 399
    QCString noNul(enc);
    for (int i = 0; i < noNul.length(); ++i) {
        const char c = noNul.at(i);
        const bool isEol = (c == '\n' || c == '\r');
        CHECK(!isEol);
    }
}

TEST_CASE("qToBase64: 各长度余数（n%3 == 0/1/2）的 padding 正确")
{
    // 直接对照「无 padding 的字符数」：n%3==1 → 2 个 '='，n%3==2 → 1 个 '='
    struct { int n; int pad; } cases[] = { {1,2}, {2,1}, {3,0}, {4,2}, {5,1}, {6,0} };
    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        const int n = cases[i].n;
        QCString enc = qToBase64(filled(n, 'a'));
        // ⚠ QCString::length() 是 strlen 语义，**不含**尾 NUL（实测
        //   qToBase64("f") 得 "Zg=="，length() 为 4 而非 5）。QMemArray 的
        //   size() 才含尾 NUL —— 两者别混。隔离探针实测确认。
        const int chars = enc.length();
        const int expected = ((n + 2) / 3) * 4;
        CHECK_EQ(chars, expected);
        int padCount = 0;
        for (int i2 = chars - cases[i].pad; i2 < chars; ++i2)
            if (enc.at(i2) == '=') ++padCount;
        CHECK_EQ(padCount, cases[i].pad);
    }
}
