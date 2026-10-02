// stikcommon/test_qnaturalsort_shim.cpp —— qnaturalsort_shim.h 契约（Qt3 单端）
//
// 被测对象：QNaturalSort::naturalCompare(s1, s2, cs)
//
// 该头是纯 inline，**无需链接任何产品 .cpp**。
//
// 用途：anystik/src/stickerstore.cpp 的 stable_sort 路径用它给贴纸包文件名
// 排序。核心价值是「自然序」—— img2 必须排在 img10 之前，而纯字典序
// 会排反（'1' < '2'）。若哪天垫片退化成 QString::compare，用户看到的
// 贴纸顺序就会变成 1,10,11,2,…，且不会报任何错。
//
// 算法照搬 vendor（anystik/vendor/qwebdav/qnaturalsort.cpp），故断言按
// vendor 语义写：数字段先比长度再逐位、跳过前导 0 与空白、末尾回落整体
// compare。默认 cs=false（不区分大小写），对齐 vendor 的
// Qt::CaseInsensitive 默认值。

#include <qstring.h>
#include <qstringlist.h>

#include <vector>
#include <algorithm>
#include <locale.h>
#include <string>

#include "doctest/doctest.h"

#include "qnaturalsort_shim.h"

namespace {
// ⚠ 用 fromUtf8 而非 fromLatin1：本仓规范要求非 ASCII 一律走 UTF-8
//   （AGENTS.md）。这些比较串全是 ASCII，fromLatin1 对纯 ASCII 等价，
//   但统一走 fromUtf8 可免得日后往里加中文字面量时静默出错。
inline QString S(const char* s) { return QString::fromUtf8(s); }
// 约定：<0 表示 s1 排在前
inline int cmp(const char* a, const char* b, bool cs = false)
{
    return QNaturalSort::naturalCompare(S(a), S(b), cs);
}
// ⚠ naturalCompare 有默认参数，函数指针类型是 3 参；std::sort 只按 2 参调用，
//   故必须包一层。顺带说明为何不用 QStringList::sort：Qt3 的 sort() 无参或收
//   Comparator 枚举，**不收函数指针**（Qt4+ 才有重载），传 naturalCompare
//   会报 "no matching function for call to QStringList::sort"。
bool natLess(const QString& a, const QString& b)
{
    return QNaturalSort::naturalCompare(a, b) < 0;
}
} // namespace

// ── 核心：数字段按数值而非字典序比较 ────────────────────────────────

TEST_CASE("QNaturalSort: 数字段按数值比较（img2 < img10）")
{
    // 这条是整个垫片存在的理由。字典序会给出 img10 < img2（'1'<'2'）。
    CHECK(cmp("img2", "img10") < 0);
    CHECK(cmp("img10", "img2") > 0);
    CHECK_EQ(cmp("img2", "img2"), 0);
}

TEST_CASE("QNaturalSort: 纯数字串按数值比较（含位数差异）")
{
    CHECK(cmp("2", "10") < 0);
    CHECK(cmp("9", "10") < 0);
    CHECK(cmp("100", "99") > 0);
    CHECK(cmp("1", "2") < 0);
    CHECK_EQ(cmp("42", "42"), 0);
    // 位数相同则逐位比
    CHECK(cmp("19", "20") < 0);
    CHECK(cmp("100", "101") < 0);
}

TEST_CASE("QNaturalSort: 整串排序结果符合自然序")
{
    // 把散点串真正排一遍，验证 comparator 满足严格弱序且结果正确。
    std::vector<QString> v;
    v.push_back(S("img10"));
    v.push_back(S("img2"));
    v.push_back(S("img1"));
    v.push_back(S("img20"));
    v.push_back(S("img3"));
    std::sort(v.begin(), v.end(), natLess);
    CHECK_EQ(v.size(), 5u);
    CHECK(v[0] == S("img1"));
    CHECK(v[1] == S("img2"));
    CHECK(v[2] == S("img3"));
    CHECK(v[3] == S("img10"));
    CHECK(v[4] == S("img20"));
}

// ⚠ 回归点：算法里有两段「跳过前导 0」的 while
//   （`while (c1.digitValue() == 0) c1 = nextChar(...)`）。但**实测结果与
//   vendor 源码里那句注释 "The two strings are the same (02 == 2)" 不符**：
//   隔离探针（Qt3.5 /opt/qt338sh）实测
//     cmp("02","2")   = -2
//     cmp("002","2")  = -2
//     cmp("a02","a2") = -2
//   原因：数字段比较后 currentReturnValue 仍为 0，但逐字符循环此时已走到
//   末尾，于是落到函数最后一行 `return QString::compare(s1, s2);` —— 而
//   **Qt3 的 QString::compare 不归一化到 -1/0/1**，它返回首个差异字符的
//   码位差（'0'=0x30 vs '2'=0x32 → -2）。
//   即：数字段比较认为二者相等，最终定序却回落到字典序，于是 "02" < "2"。
//   这是 vendor 原件行为（垫片逐行照搬，不引入偏差），对调用方无影响 ——
//   stickerstore 把它交给 std::stable_sort，而排序只关心 <0/==0/>0，
//   -2 与 -1 等价。故此处断言实际行为，并记录该反直觉点。
//   ⚠ 另：返回值**可能超出 [-1,1]**，故本文件所有断言只用符号比较
//     (<0 / >0 / ==0)，不用 CHECK_EQ(cmp(...), -1) 这类写法。
TEST_CASE("QNaturalSort: 数字段跳过前导 0 后仍回落到字典序（02 < 2）")
{
    // 前导 0 被跳过参与数字段比较，但最终定序由 QString::compare 决定 → 02 排前
    CHECK(cmp("02", "2") < 0);
    CHECK(cmp("2", "02") > 0);
    CHECK(cmp("002", "2") < 0);
    CHECK(cmp("a02", "a2") < 0);
    CHECK(cmp("img02", "img2") < 0);
    // 无前导 0 时数字段相等 → 0
    CHECK_EQ(cmp("2", "2"), 0);
    CHECK_EQ(cmp("img2", "img2"), 0);
    // 前导 0 不影响数字段的**大小**判断：02→2 仍小于 10
    CHECK(cmp("02", "10") < 0);
}

// ⚠ 回归点：循环内有 `while (c1.isSpace()) c1 = nextChar(...)`，即空白
//   **不参与**循环内的逐字符比较。但函数末尾回落是
//   `return QString::compare(s1, s2)` —— 用的是**原始字符串**，空白原样
//   参与字典序比较。所以「循环内逻辑相等」≠「整体比较相等」。
//   隔离探针实测：cmp("a b","ab") = -66（' '=0x20 vs 'a'=0x61 的码位差）。
//   这是 vendor 原件行为（垫片逐行照搬），对调用方无影响：文件名里极少有
//   前后空格，且排序只关心符号。此处锁定实际行为。
TEST_CASE("QNaturalSort: 空白在末尾回落时仍参与字典序（非 0）")
{
    // ⚠ 不要断言 == 0：末尾 QString::compare 用原始串，空白没被剔除
    CHECK(cmp("a b", "ab") < 0);
    CHECK(cmp("  lead", "lead") < 0);
    CHECK(cmp("trail  ", "trail") > 0);
    // 内部空白的位置也影响定序（"a b" vs "a  b"）
    CHECK(cmp("a b", "a  b") > 0);
    // 完全无空白时才可能为 0
    CHECK_EQ(cmp("ab", "ab"), 0);
}

// ── 大小写：默认不区分，cs=true 才区分 ──────────────────────────────
// 回归点：Qt3 没有 Qt::CaseSensitivity，也没有 QChar::isLower()/toLower()
// （只有 lower()，qstring.h:190），大小写标志用 bool 承载。

TEST_CASE("QNaturalSort: 默认不区分大小写")
{
    CHECK_EQ(cmp("ABC", "abc"), 0);
    CHECK_EQ(cmp("AbC", "aBc"), 0);
    CHECK_EQ(QNaturalSort::naturalCompare(S("Pack01"), S("pack01")), 0);
    // ⚠ 逐字符位置才走到末尾的，靠末尾回落 → 用原始串比，大小写又生效了。
    //   "ABC" vs "abd" 在 l=2 处 C vs d 已不等，循环内即 return -1
    //   （'c' vs 'd' 经 lower 后比较），**不是** 0。
    CHECK(cmp("ABC", "abd") < 0);
    CHECK(cmp("abd", "ABC") > 0);
}

TEST_CASE("QNaturalSort: cs=true 时区分大小写")
{
    // ⚠ 这里只断言**locale 无关**的不变量，不写 cmp("ABC","abc",true) < 0。
    //   原因：早先版本写了这条，它只在 C locale 下成立。QApplication 构造会
    //   setlocale(LC_ALL, "")，locale 变成 en_US.UTF-8 后 glibc collation
    //   认为小写在前（localeAwareCompare('A','a') 由 -32 变 +5），
    //   于是同一条断言在「跑过 clipboard 测试（建了 QApplication）」的
    //   进程里失败、单独跑却通过 —— 顺序依赖型 flaky。
    //   本垫片已改为码位比较（见 qnaturalsort_shim.h
    //   qNatSortCompareChars 上方注释），结果与 locale 无关，
    //   'A'(0x41) < 'a'(0x61) 现在两种 locale 下都是大写在前。
    CHECK(cmp("ABC", "abc", true) < 0);
    CHECK(cmp("abc", "ABC", true) > 0);
    // 反对称性：任意 locale 下都必须成立
    CHECK_EQ(cmp("Pack01", "pack01", true), -cmp("pack01", "Pack01", true));
    CHECK(cmp("ABC", "abc", true) != 0);
    // 完全相同仍为 0
    CHECK_EQ(cmp("abc", "abc", true), 0);
    // 数字段相同、大小写不同时才体现差别
    CHECK(cmp("Pack02", "pack02", true) != 0);
}

// ── locale 无关性（本组用例是该垫片的**核心稳定性契约**）───────────
//
// ⚠ 背景：早先版本用 QString::localeAwareCompare() 逐字符比，而它查当前
//   LC_COLLATE。QApplication 构造会 setlocale(LC_ALL, "")，所以同一批文件名
//   在 GUI 进程里和在命令行/测试进程里排出来的顺序**不同**。实测：
//     localeAwareCompare(QChar('A'), QChar('a'))
//       "C" locale     → -32
//       "en_US.UTF-8"  → +5      ← glibc collation 小写在前
//   表现就是 flaky：单跑某个文件绿、全量跑红。
//   现在垫片按码位比较，与 locale 无关。本组用例主动切 locale 验证这一点。

static int natCmp(const char* a, const char* b, bool cs)
{
    return QNaturalSort::naturalCompare(QString::fromUtf8(a),
                                        QString::fromUtf8(b), cs);
}

TEST_CASE("QNaturalSort: 结果与 locale 无关（切 C / en_US.UTF-8 不变）")
{
    // ⚠ POSIX 规定 setlocale() 返回的字符串会被后续 setlocale() 失效，
    //   所以必须拷贝一份再还原（不能直接拿返回指针当参数传回去）。
    const char* loc0 = setlocale(LC_ALL, 0);
    const std::string saved = loc0 ? std::string(loc0) : std::string();

    struct Probe { const char* a; const char* b; bool cs; };
    const Probe probes[] = {
        { "ABC", "abc", true  },
        { "abc", "ABC", true  },
        { "ABC", "abd", false },
        { "Pack02", "pack02", true },
        { "Zebra", "apple", false },
        { "img2", "img10", false },
        { "贴", "a", false },                     // 贴 vs a（跨 ASCII/非 ASCII）
    };
    const int n = (int)(sizeof(probes)/sizeof(probes[0]));

    int base[16];
    REQUIRE(n <= 16);

    // 先在当前 locale 下取基准（不切 locale，避免依赖机器默认设置）
    for (int i = 0; i < n; ++i) base[i] = natCmp(probes[i].a, probes[i].b, probes[i].cs);

    // 依次切 locale 重测，结果必须逐一相同
    const char* locales[] = { "C", "en_US.UTF-8", "POSIX" };
    for (int li = 0; li < 3; ++li) {
        if (!setlocale(LC_ALL, locales[li])) continue;   // 该 locale 不可用则跳过
        for (int i = 0; i < n; ++i) {
            CHECK_MESSAGE(natCmp(probes[i].a, probes[i].b, probes[i].cs) == base[i],
                          "locale=", locales[li], " probe=", i);
        }
    }
    if (!saved.empty()) setlocale(LC_ALL, saved.c_str());   // 还原，别影响后续用例
}

// ── 边界：空串、纯符号、超长数字 ───────────────────────────────────
TEST_CASE("QNaturalSort: 空串与相等串")
{
    CHECK_EQ(cmp("", ""), 0);
    CHECK(cmp("a", "") != 0);
    CHECK(cmp("", "a") != 0);
    CHECK(cmp("", "0") != 0);
}

TEST_CASE("QNaturalSort: 数字位数悬殊时不溢出/不误判")
{
    // 20 位 vs 1 位：若实现用 int 累加会溢出，这里靠长度比较规避
    CHECK(cmp("100000000000000000000", "9") > 0);
    CHECK(cmp("9", "100000000000000000000") < 0);
    // ⚠ 前导 0 极多也不能死循环（while 跳过前导 0 依赖 nextChar 越界返回
    //   空 QChar）。但结果**不是 0** —— 数字段认为相等后落到末尾
    //   QString::compare，'0'=0x30 vs '1'=0x31 → -1。
    CHECK(cmp("000000000000000000001", "1") < 0);
}

TEST_CASE("QNaturalSort: 一个是另一个的前缀时按长度定序")
{
    CHECK(cmp("img", "img1") < 0);
    CHECK(cmp("img1", "img") > 0);
    CHECK(cmp("img1", "img2") < 0);
}
