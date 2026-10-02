// stikcommon/test_qtrimmed_shim.cpp —— qTrimmed() 契约（Qt3 单端）
//
// 被测对象：
//   qstring_shim.h:qTrimmed(const QString&)
//   qba_shim.h:qTrimmed(const QByteArray&)   ← 与上者构成同名重载对
//   （两个函数分处两个头，本文件同时 include 以验证重载解析与行为）
//
// 纯头文件，无需链接产品 .cpp。
//
// ── 这组测试的期望值从哪来 ──────────────────────────────────────────────
// 不是照抄 Qt 文档，而是差分探针实测：/tmp/opencode/probe_trimdiff.cpp 对
// 0x0000-0xFFFF 全部 65536 个码位逐个构造 `<cp>A B` 放进首部，看是否被裁，
// 在 Qt3(/opt/qt338sh) 与 Qt6(6.7.3) 下各跑一遍再取差集。实测结论：
//
//   QString 裁剪集合（Qt3 stripWhiteSpace / Qt6 trimmed 各 25 个，二者仅差 2 个）：
//     09 0A 0B 0C 0D 20  00A0 1680 2000..200A 2028 2029 202F 205F 3000
//     └─ Qt6 另加 0085(NEL)；Qt3 另加 200B(ZWSP)
//
//   QByteArray 裁剪集合（两版本完全一致，只有 6 个 ASCII）：
//     09 0A 0B 0C 0D 20   ← 注意 00 **不在**内：内嵌 NUL 不算空白，保留
//
// ⚠ 故本文件**不**断言「三版本语义一致」——实测证明不成立，见
//   qstring_shim.h:qTrimmed 处注释。断言分两种：跨版本一致的写死；只对
//   当版本成立地用 QT_VERSION 分支写，并注明另一个版本的行为。
//
// ── 本文件刻意不测的东西 ────────────────────────────────────────────────
// 不测「qTrimmed 与原生函数逐位相同」这个全码位性质：那是探针的活（跑一次
// 几十秒），塞进单测每次都跑不划算。这里只钉住几个代表性码位 + 两个差异点，
// 够挡住「有人把 stripWhiteSpace 写成 trim、或误用 QByteArray 重载」这类改动。
//
// ── Qt3 构造陷阱（本文件为此不用 QString(int,QChar) / QByteArray operator+）──
//   * QString(int, QChar) **Qt3 不存在**（qstring.h 只有 private 的
//     QString(int, bool)），传字面量 1 会撞上那个 private 构造 → 一律用 +=。
//   * QByteArray(=QMemArray<char>) **没有 operator+**（也没 operator+=，
//     后者由 qbytearray_shim.h:525 补上），故只用 += / qByteArrayAppend。
//   * QByteArray 也**没有 const char* 构造**，字面量一律过 qbaFromRaw()。

#include "qstring_shim.h"
#include "qba_shim.h"

#include <doctest.h>

using namespace doctest;

// 造「n 个 cp」的 QString。Qt3/Qt6 都只有 += 可用。
static QString mkQ(ushort cp, int n = 1)
{
    QString s;
    for (int i = 0; i < n; ++i) s += QChar(cp);
    return s;
}

// 造「n 个 c」的 QByteArray。
static QByteArray mkB(char c, int n = 1)
{
    QByteArray b;
    for (int i = 0; i < n; ++i) b += c;
    return b;
}

// mkQ(cp) + mkQ(qp) + mkQ(qt)
static QString mkQ3(ushort cp, ushort mid, ushort tp)
{
    QString s;
    s += QChar(cp);
    s += QChar(mid);
    s += QChar(tp);
    return s;
}

static QByteArray mkB3(char c0, char c1, char c2)
{
    QByteArray b;
    b += c0;
    b += c1;
    b += c2;
    return b;
}

// ── QString：ASCII 空白六码位 ───────────────────────────────────────────
// 09 \t / 0A \n / 0B \v / 0C \f / 0D \r / 20 space
TEST_CASE("qTrimmed(QString) 去掉首尾 ASCII 空白")
{
    const char ws[] = { '\t', '\n', '\v', '\f', '\r', ' ' };
    for (int i = 0; i < 6; ++i) {
        CAPTURE(i);
        CHECK(qTrimmed(mkQ3((ushort)ws[i], 0x0061, 0x0062))
                  == QString("ab"));                    // 只在首部
        CHECK(qTrimmed(mkQ3(0x0061, 0x0062, (ushort)ws[i]))
                  == QString("ab"));                    // 只在尾部
        CHECK(qTrimmed(mkQ3((ushort)ws[i], 0x0061, (ushort)ws[i]))
                  == QString("a"));                     // 首尾各一，内部 a 留下
    }
}

TEST_CASE("qTrimmed(QString) 不动内部空白")
{
    // "  a b\tc  " → "a b\tc"
    QString in;
    in += QChar(0x0020);
    in += QChar(0x0020);
    in += QChar(0x0061);
    in += QChar(0x0020);
    in += QChar(0x0062);
    in += QChar(0x0009);
    in += QChar(0x0063);
    in += QChar(0x0020);
    in += QChar(0x0020);

    QString out = qTrimmed(in);
    CHECK_EQ(out.length(), 5);
    CHECK(out.at(0) == QChar(0x0061));
    CHECK(out.at(1) == QChar(0x0020));              // 内部空格保留
    CHECK(out.at(2) == QChar(0x0062));
    CHECK(out.at(3) == QChar(0x0009));              // 内部 \t 保留
    CHECK(out.at(4) == QChar(0x0063));
}

TEST_CASE("qTrimmed(QString) 边界：空串 / 全空白 / 无需裁剪")
{
    CHECK_EQ(qTrimmed(QString()).length(), 0);

    // 全空白 → 空串（不是留下一串空白）。含表意空格 U+3000。
    CHECK_EQ(qTrimmed(mkQ(0x0020) + mkQ(0x0009) + mkQ(0x3000)).length(), 0);

    // 无需裁剪：内容原样返回
    QString plain = QString("abc");
    CHECK(qTrimmed(plain) == plain);
}

// ── QString：非 ASCII 空白（产品里 URL/标题可能出现）────────────────────
TEST_CASE("qTrimmed(QString) 去非 ASCII 空白，两版本一致")
{
    // 这五个在 Qt3 与 Qt6 的裁剪集合里都存在（探针实测），故可写死
    const ushort codes[] = { 0x00A0,   // NO-BREAK SPACE
                             0x1680,   // OGHAM SPACE MARK
                             0x2028,   // LINE SEPARATOR
                             0x205F,   // MEDIUM MATHEMATICAL SPACE
                             0x3000 }; // IDEOGRAPHIC SPACE
    for (int i = 0; i < 5; ++i) {
        CAPTURE((int)codes[i]);
        // 被裁 → 首尾空白消失留下 "xx"；未被裁 → 长度 3，两者可区分
        CHECK(qTrimmed(mkQ3(codes[i], 0x0078, 0x0078)) == QString("xx"));   // 首部
        CHECK(qTrimmed(mkQ3(0x0078, 0x0078, codes[i])) == QString("xx"));   // 尾部
    }
}

// ── 两版本差异已钉住 ────────────────────────────────────────────────────
// 探针实测 Qt3 与 Qt6 的 QString 裁剪集合只差这两个码位。这里按版本断言，
// 等于把「差异存在」写进测试，免得后人看到注释里的警告却以为是笔误、
// 或反过来拿它当等价证明。
TEST_CASE("qTrimmed(QString) 两版本差异码位 U+0085 / U+200B")
{
    // 探针的构造法：候选码位放**首部**，后面跟单个哨兵 'x'。
    // 于是「被裁」→ 长度 1，「未被裁」→ 长度 2。
    QString s85 = mkQ(0x0085) + QString("x");       // "\u0085x"
    QString s2b = mkQ(0x200B) + QString("x");       // "\u200Bx"

#if QT_VERSION < 0x040000
    // Qt3 stripWhiteSpace()：保留 0085（未裁），裁掉 200B
    CHECK_EQ(qTrimmed(s85).length(), 2);
    CHECK_EQ(qTrimmed(s2b).length(), 1);
#else
    // Qt4+ trimmed()：裁掉 0085，保留 200B
    CHECK_EQ(qTrimmed(s85).length(), 1);
    CHECK_EQ(qTrimmed(s2b).length(), 2);
#endif
}

// ── QString：非 ASCII 内容不被破坏（回归护栏）──────────────────────────
// 产品里 qTrimmed 多用于「先裁剪再转 UTF-8 字节」，若实现里误用 Latin-1
// 或逐字节处理，中文会变乱码。这里用码点断言，不靠终端显示。
TEST_CASE("qTrimmed(QString) 保住非 ASCII 内容的码点")
{
    // 「纸」= U+7EB8，「优」= U+4F18。预期值必须用 fromUtf8，见 AGENTS.md。
    QString cjk = QString::fromUtf8("\xe7\xba\xb8\xe4\xbc\x98");
    CHECK_EQ(cjk.length(), 2);
    CHECK(cjk.at(0).unicode() == 0x7EB8);
    CHECK(cjk.at(1).unicode() == 0x4F18);

    // 首尾各一个空格，裁掉后应仍是那两个码点
    QString padded;
    padded += QChar(0x0020);
    padded += cjk;
    padded += QChar(0x0020);

    QString out = qTrimmed(padded);
    CHECK_EQ(out.length(), 2);
    CHECK(out.at(0).unicode() == 0x7EB8);
    CHECK(out.at(1).unicode() == 0x4F18);

    // 字节层面也确认一次：裁剪没有吞掉 UTF-8 的任何字节。
    // 直接比字节而不是比 qbaToHex 的十六进制串，省得把「hex 大写还是小写」
    // 这种书写约定也变成测试的一部分。
    // ⚠ Qt3 的 QString::utf8() 返回 QCString，其 size() **含结尾 NUL**
    //   （qstring_shim.h:41 记的就是这个怪癖），故只取前 6 字节，
    //   不能拿 size() 当字节数用。
    const char want[] = { (char)0xE7, (char)0xBA, (char)0xB8,
                          (char)0xE4, (char)0xBC, (char)0x98 };
    CHECK(qbaMid(out.utf8(), 0, 6) == qbaFromRaw(want, 6));
}

// ── QByteArray 重载：集合两版本完全一致，可写死 ─────────────────────────
TEST_CASE("qTrimmed(QByteArray) 只去首尾 ASCII 空白，两版本一致")
{
    const char ws[] = { '\t', '\n', '\v', '\f', '\r', ' ' };
    for (int i = 0; i < 6; ++i) {
        CAPTURE(i);
        CHECK(qTrimmed(mkB3(ws[i], 'a', 'b')) == qbaFromRaw("ab", 2));
        CHECK(qTrimmed(mkB3('a', 'b', ws[i])) == qbaFromRaw("ab", 2));
        CHECK(qTrimmed(mkB3(ws[i], 'a', ws[i])) == qbaFromRaw("a", 1));
    }
}

TEST_CASE("qTrimmed(QByteArray) 内嵌 NUL 与高位字节都保留")
{
    // Qt6 原生 QByteArray::trimmed() 不裁 0x00（探针：裁剪集合无 00），
    // Qt3 的 qTrimmed 同样按 qbaIsAsciiSpace 只认 6 个 ASCII。
    const char withNul[] = { ' ', 'a', '\0', 'b', ' ' };
    QByteArray out = qTrimmed(qbaFromRaw(withNul, 5));
    CHECK_EQ(out.size(), 3);
    CHECK(out.at(0) == 'a');
    CHECK(out.at(1) == '\0');                       // NUL 仍在中间
    CHECK(out.at(2) == 'b');

    // 高位字节（>=0x80）一律不当作空白，即使在首尾
    const char high[] = { (char)0x80, (char)0xA0, (char)0xFF };
    for (int i = 0; i < 3; ++i) {
        CAPTURE(i);
        QByteArray s;
        s += high[i];
        s += 'a';
        QByteArray r = qTrimmed(s);
        CHECK_EQ(r.size(), 2);                      // 未被裁
        CHECK((unsigned char)r.at(0) == (unsigned char)high[i]);
    }
}

TEST_CASE("qTrimmed(QByteArray) 边界：空 / 全空白 / 内部字节不动")
{
    CHECK_EQ(qTrimmed(QByteArray()).size(), 0);

    QByteArray allWs;
    allWs += ' ';
    allWs += '\t';
    allWs += '\n';
    CHECK_EQ(qTrimmed(allWs).size(), 0);

    // " a b " → "a b"（长度 3：内部空格必须留着）
    QByteArray inner;
    inner += ' ';
    inner += 'a';
    inner += ' ';
    inner += 'b';
    inner += ' ';
    QByteArray o = qTrimmed(inner);
    CHECK_EQ(o.size(), 3);
    CHECK(o.at(0) == 'a');
    CHECK(o.at(1) == ' ');                          // 内部空格保留
    CHECK(o.at(2) == 'b');
}

// ── QByteArray 的 UTF-8 内容不被破坏（回归护栏）─────────────────────────
// 与 QString 侧同理：产品里多是「裁完再当 UTF-8 用」，实现若按字节误判
// 多字节序列的中间字节为空白，汉字就会被截断。
TEST_CASE("qTrimmed(QByteArray) 保住 UTF-8 多字节序列")
{
    // 「纸」的尾字节是 0xB8，首字节 0xE7 —— 若实现把续字节当空白就会截断
    const char cjk[] = { ' ', (char)0xE7, (char)0xBA, (char)0xB8,
                         (char)0xE4, (char)0xBC, (char)0x98, ' ' };
    QByteArray out = qTrimmed(qbaFromRaw(cjk, 8));
    CHECK_EQ(out.size(), 6);                        // 两个汉字各 3 字节
    const char want[] = { (char)0xE7, (char)0xBA, (char)0xB8,
                          (char)0xE4, (char)0xBC, (char)0x98 };
    CHECK(out == qbaFromRaw(want, 6));
}

// ── 同名重载能按形参类型正确分派 ───────────────────────────────────────
// 两个 qTrimmed 分处不同头、签名只差形参类型。编译期就分派，这里保证
// 传 QString 变量与 QByteArray 变量各自命中对应重载。
// ⚠ 刻意**不**传 const char* 字面量：Qt3 的 QString 有隐式 const char* 构造
//   （按 Latin-1），而 QByteArray 没有，字面量会静默走 QString 重载 —— 那测的
//   是隐式转换不是重载分派，且正好撞上 AGENTS.md 禁用的那条路。
TEST_CASE("qTrimmed 同名重载按类型分派")
{
    QString qs = mkQ(0x0020) + mkQ(0x0073) + mkQ(0x0020);   // " s "
    QByteArray bs;
    bs += ' ';
    bs += 'b';
    bs += ' ';

    QString rq = qTrimmed(qs);                  // → QString 重载
    QByteArray rb = qTrimmed(bs);               // → QByteArray 重载

    CHECK(rq == QString("s"));
    CHECK_EQ(rb.size(), 1);
    CHECK(rb.at(0) == 'b');
}
