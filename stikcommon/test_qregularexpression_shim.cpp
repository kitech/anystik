// stikcommon/test_qregularexpression_shim.cpp
// —— qregularexpression_shim.h / qstring_shim.h 契约（Qt3 单端，零网络）
//
// 被测对象（#if QT_VERSION < 0x050000 才编译，即本仓 Qt3 专属垫片）：
//   stikcommon/qregularexpression_shim.h —— QRegularExpression（QRegExp 后端）
//   stikcommon/qstring_shim.h           —— qTrimmed / qStringListRemoveDuplicates
//                                          / qFromUtf8BA
// 头文件为纯 static inline，**无需链接任何产品 .cpp**，故本文件零依赖。
//
// ★ 为什么把 sitelistclient / imagesearchclient 的解析逻辑抄进本文件：
//   两个客户端的 parseHtmlImgs / parseBing / parseYandex 都在 .cpp 的匿名
//   namespace 里（有意不导出），外部无法直接调。批次 2 的**唯一**移植风险就在
//   这三段：正则在 Qt3 能否编译、懒惰量词改写后是否仍取到同样的捕获。
//   故这里用**生产同款模式 + 生产同款过滤**跑样本 HTML，把风险钉住。
//   模式串与过滤条件逐字取自 anystik/src/sitelistclient.cpp:88 与
//   imagesearchclient.cpp:145/153/167/175，未凭记忆改写。
//
// 期望值全部来自 /tmp/opencode/probe-b2/ 下隔离探针的实测输出
// （dbg.cpp 打 effectivePattern，reshim.cpp 打匹配结果），不凭记忆写。

#include <qobject.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qcstring.h>

#include "qregularexpression_shim.h"
#include "qstring_shim.h"

#include "doctest/doctest.h"

// QString → 窄串，只给 printf 风格的 CHECK_EQ 失败信息用。
// 刻意不依赖终端转码判断非 ASCII：涉及码点的断言一律比 unicode() 数值。
static QCString cp(const QString& s)
{
    return s.utf8();
}

// ── 1. 懒惰量词改写（qreRewriteLazy 的产出）──────────────────────────
TEST_CASE("qregularexpression: 懒惰量词改写结果与探针一致")
{
    // 两个消费方的真实模式：`m="(.*?)"` / `data-bem="(.*?)"`
    // 关键点：`.*?` 处在**捕获组内部**，改写必须递归进组，否则 Qt3 报
    // bad repetition syntax，整个解析静默失效。
    {
        QRegularExpression re(QString("m=\"(.*?)\""));
        CHECK(re.isValid());
        CHECK_EQ(cp(re.errorString()), QCString("no error occurred"));
        CHECK_EQ(cp(re.effectivePattern()), QCString("m=\"([^\"]*)\""));
    }
    {
        QRegularExpression re(QString("data-bem=\"(.*?)\""));
        CHECK_EQ(cp(re.effectivePattern()), QCString("data-bem=\"([^\"]*)\""));
    }
    // 组外的懒惰点号：后继字面量进否定字符类
    {
        QRegularExpression re(QString("x(.*?)y"));
        CHECK_EQ(cp(re.effectivePattern()), QCString("x([^y]*)y"));
    }
    // +? 保留原子，*? 才丢掉原子（见 shim 里的 qreRewriteLazy 注释）
    {
        QRegularExpression re(QString("a+?b"));
        QRegularExpression re2(QString("a*?b"));
        CHECK_EQ(cp(re.effectivePattern()),  QCString("a[^b]*b"));
        CHECK_EQ(cp(re2.effectivePattern()), QCString("[^b]*b"));
    }
    // 多字符原子（组）不改写：保持原样，避免改出语义不同的模式
    {
        QRegularExpression re(QString("(ab)*?c"));
        CHECK_EQ(cp(re.effectivePattern()), QCString("(ab)*?c"));
    }
    // 无后继字符的懒惰量词：无法构造否定字符类，原样交给 QRegExp
    {
        QRegularExpression re(QString("a*?"));
        CHECK_EQ(cp(re.effectivePattern()), QCString("a*?"));
    }
    // 无懒惰量词的模式必须**一字不改**（改写器不得误伤）
    {
        // sitelistclient 的 <img> 模式
        QRegularExpression img(QString("<img[^>]+src\\s*=\\s*[\"']([^\"'\\s]+)[\"']"),
                               QRegularExpression::CaseInsensitiveOption);
        CHECK(img.isValid());
        CHECK_EQ(cp(img.effectivePattern()),
                 QCString("<img[^>]+src\\s*=\\s*[\"']([^\"'\\s]+)[\"']"));
    }
}

// ── 2. 匹配快照回归（历史 bug：Match 延迟读 cap() 导致全读到最后一次）──
TEST_CASE("qregularexpression: globalMatch 每次 Match 持有自己的捕获快照")
{
    // ★ 回归用例。初版 Match 持 const QRegExp* + const_cast 调 cap()，以为
    //   「cap() 只读不推进位置」。实测不成立：迭代器 next() 一 seek，先前
    //   返回的 Match 再读 captured() 就拿到**下一次**的捕获。
    //   当时现象：本用例应得 [aa] [bb]，实际输出 [bb] []。
    QRegularExpression re(QString("m=\"(.*?)\""));
    const QString text = QString::fromUtf8("x m=\"aa\" y m=\"bb\" z");

    QStringList got;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        got << it.next().captured(1);   // 必须立刻取走
    }
    REQUIRE_EQ(got.size(), 2);
    CHECK_EQ(cp(got[0]), QCString("aa"));
    CHECK_EQ(cp(got[1]), QCString("bb"));
}

TEST_CASE("qregularexpression: 延迟读取 Match 仍返回本次匹配的捕获")
{
    // 快照语义的正向证明：把 2 个 Match 攒起来，最后才读 captured()，
    // 结果必须与边取边读一致（若 Match 仍持后端指针，这里会串味）。
    QRegularExpression re(QString("m=\"(.*?)\""));
    auto it = re.globalMatch(QString::fromUtf8("x m=\"aa\" y m=\"bb\" z"));

    QRegularExpression::Match a, b;
    REQUIRE(it.hasNext());
    a = it.next();
    REQUIRE(it.hasNext());
    b = it.next();
    CHECK(!it.hasNext());

    CHECK_EQ(cp(a.captured(1)), QCString("aa"));
    CHECK_EQ(cp(b.captured(1)), QCString("bb"));
    CHECK_EQ(cp(a.captured()),    QCString("m=\"aa\""));
    CHECK_EQ(cp(b.captured()),    QCString("m=\"bb\""));
    CHECK_EQ(a.captureCount(), 1);
}

TEST_CASE("qregularexpression: capturedStart / capturedLength")
{
    QRegularExpression re(QString("m=\"(.*?)\""));
    const QString text = QString::fromUtf8("x m=\"aa\" y m=\"bb\" z");
    auto it = re.globalMatch(text);

    // 第一次：整段在 2..8（"m=\"aa\""），第 1 个捕获在 5..7（"aa"）
    const QRegularExpression::Match a = it.next();
    CHECK_EQ(a.capturedStart(),     2);
    CHECK_EQ(a.capturedLength(),    6);
    CHECK_EQ(a.capturedStart(1),    5);
    CHECK_EQ(a.capturedLength(1),   2);

    // 第二次：整段 11..17，捕获 14..16
    const QRegularExpression::Match b = it.next();
    CHECK_EQ(b.capturedStart(),     11);
    CHECK_EQ(b.capturedLength(),    6);
    CHECK_EQ(b.capturedStart(1),    14);
    CHECK_EQ(b.capturedLength(1),   2);
}

TEST_CASE("qregularexpression: 越界 nth 与未参与捕获的返回约定")
{
    // 越界 nth → -1（不给 0，避免与「位置 0」混淆）
    QRegularExpression re(QString("\"murl\"\\s*:\\s*\"([^\"]+)\""));
    const QRegularExpression::Match m = re.match(QString::fromUtf8("{\"murl\":\"http://x/y.jpg\"}"));
    REQUIRE(m.hasMatch());
    CHECK_EQ(cp(m.captured(1)),        QCString("http://x/y.jpg"));
    CHECK_EQ(m.capturedStart(9),      -1);
    CHECK_EQ(m.capturedLength(9),     -1);
    CHECK_EQ(m.captured(9).isEmpty(), true);
    CHECK_EQ(m.captureCount(),        1);

    // ⚠ 未参与匹配的捕获：Qt6 返回 -1，Qt3 的 QRegExp::pos() 返回 0。
    //   QRegExp 无从区分「未参与」与「参与了但匹配到空串」，shim 照实返回
    //   Qt3 的值（0/0），判断请用 captured(n).isEmpty()。
    QRegularExpression alt(QString("(a)|(b)"));
    const QRegularExpression::Match b = alt.match(QString("b"));
    REQUIRE(b.hasMatch());
    CHECK(b.captured(1).isEmpty());
    CHECK_EQ(cp(b.captured(2)), QCString("b"));
    CHECK_EQ(b.capturedStart(2),    0);   // 与 Qt6 一致
    CHECK_EQ(b.capturedLength(2),   1);
    CHECK_EQ(b.capturedStart(1),    0);   // ← Qt6 此处为 -1，已知差异
    CHECK_EQ(b.capturedLength(1),   0);

    // 未命中的 Match：hasMatch()=false，captured() 空，captureCount()=0
    const QRegularExpression::Match miss = re.match(QString("nope"));
    CHECK(!miss.hasMatch());
    CHECK(miss.captured(1).isEmpty());
    CHECK(miss.captured().isEmpty());
    CHECK_EQ(miss.captureCount(), 0);
    CHECK_EQ(miss.capturedStart(),  -1);
    CHECK_EQ(miss.capturedLength(), -1);
}

TEST_CASE("qregularexpression: CaseInsensitiveOption 生效与否")
{
    QRegularExpression ci(QString("ABC"), QRegularExpression::CaseInsensitiveOption);
    QRegularExpression cs(QString("ABC"));
    CHECK(ci.match(QString("abc")).hasMatch());
    CHECK(!cs.match(QString("abc")).hasMatch());
    CHECK(cs.match(QString("ABC")).hasMatch());
}

// ── 3. 生产解析逻辑（模式与过滤条件逐字取自两个客户端）─────────────────
TEST_CASE("sitelistclient parseHtmlImgs: 生产模式在 Qt3 下取到同样的 <img src>")
{
    // 逐字照搬 sitelistclient.cpp:88 的模式 + 93 行的 trimmed/data: 过滤。
    // 期望值取自 Qt6.7.3 实测（/tmp/opencode/probe-b2/img6.cpp 同一模式同一
    // 样本 HTML，打出 count=4: a.png / /b/c.png / d.png / e.png）。
    QStringList out;
    const QByteArray html = qToUtf8BA(QString::fromUtf8(
        "<IMG SRC='a.png' alt=x>"                       // 大写标签，验 CaseInsensitive
        "<img src=\"/b/c.png\" title=t>"                 // 绝对路径
        "<img data-src='d.png'>"                         // 见下方说明
        "<img src='e.png'/>"                             // 自闭合
        "<img src='data:image/png;base64,AAAA'>"));      // data: 内联，必须跳过
    const QString text = qFromUtf8BA(html);

    QRegularExpression re(QString("<img[^>]+src\\s*=\\s*[\"']([^\"'\\s]+)[\"']"),
                          QRegularExpression::CaseInsensitiveOption);
    REQUIRE(re.isValid());
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const QString src = qTrimmed(it.next().captured(1));
        if (src.isEmpty() || src.startsWith(QString("data:"))) continue;
        if (out.contains(src)) continue;             // 生产里比的是绝对 URL
        out << src;
    }
    // ⚠ `data-src='d.png'` **会被抓进来**：`[^>]+` 可以匹配到 ` data-`，
    //   剩下 `src='d.png'` 正好接上。这是生产正则自身的行为，Qt5/6/PCRE
    //   同样如此（Qt6 实测也是 4 条，见 img6.cpp），**不是 Qt3 移植引入的**。
    //   故此处按实测钉 4；「lazy-load 的 data-src 被当成真 src」是 anystik
    //   既有问题，修它要改生产正则（`(?!data-)` 之类），超出本垫片范围。
    REQUIRE_EQ(out.size(), 4);
    CHECK_EQ(cp(out[0]), QCString("a.png"));
    CHECK_EQ(cp(out[1]), QCString("/b/c.png"));
    CHECK_EQ(cp(out[2]), QCString("d.png"));
    CHECK_EQ(cp(out[3]), QCString("e.png"));
}

TEST_CASE("imagesearchclient parseBing: m=\"(.*?)\" + \"murl\" 两级捕获")
{
    // 逐字照搬 imagesearchclient.cpp:145/153 的两级模式与 150-152 行的
    // &quot; / &amp; 反转义。样本是 Bing 真实结构里最常见的一行一图形态。
    QStringList out;
    const QString text = qFromUtf8BA(qToUtf8BA(QString::fromUtf8(
        "a {m=\"{&quot;murl&quot;:&quot;http://a/1.jpg&quot;}\"}"
        "b {m=\"{&quot;murl&quot;:&quot;http://b/2.jpg&quot;}\"}"
        "c {m=\"{&quot;murl&quot;:&quot;http://a/1.jpg&quot;}\"}")));  // 3rd 与 1st 重复

    QRegularExpression re(QString("m=\"(.*?)\""),
                          QRegularExpression::DotMatchesEverythingOption);
    REQUIRE(re.isValid());
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        QString json = it.next().captured(1);
        json.replace(QString("&quot;"), QString("\""));
        json.replace(QString("&amp;"), QString("&"));
        QRegularExpression murlRe(QString("\"murl\"\\s*:\\s*\"([^\"]+)\""));
        const auto m = murlRe.match(json);
        if (m.hasMatch()) out << m.captured(1);
    }
    qStringListRemoveDuplicates(out);
    REQUIRE_EQ(out.size(), 2);
    CHECK_EQ(cp(out[0]), QCString("http://a/1.jpg"));
    CHECK_EQ(cp(out[1]), QCString("http://b/2.jpg"));
}

TEST_CASE("imagesearchclient parseYandex: data-bem=\"(.*?)\" + \"img_href\"")
{
    QStringList out;
    const QString text = qFromUtf8BA(qToUtf8BA(QString::fromUtf8(
        "x data-bem=\"{&quot;img_href&quot;:&quot;http://y/1.png&quot;}\" y"
        "z data-bem=\"{&quot;img_href&quot;:&quot;http://y/2.png&quot;}\" w")));

    QRegularExpression re(QString("data-bem=\"(.*?)\""),
                          QRegularExpression::DotMatchesEverythingOption);
    REQUIRE(re.isValid());
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        QString json = it.next().captured(1);
        json.replace(QString("&quot;"), QString("\""));
        json.replace(QString("&amp;"), QString("&"));
        QRegularExpression hrefRe(QString("\"img_href\"\\s*:\\s*\"([^\"]+)\""));
        const auto m = hrefRe.match(json);
        if (m.hasMatch()) out << m.captured(1);
    }
    qStringListRemoveDuplicates(out);
    REQUIRE_EQ(out.size(), 2);
    CHECK_EQ(cp(out[0]), QCString("http://y/1.png"));
    CHECK_EQ(cp(out[1]), QCString("http://y/2.png"));
}

// ── 4. qstring_shim 契约 ─────────────────────────────────────────────
TEST_CASE("qFromUtf8BA: 显式长度解码，不依赖 NUL 终止")
{
    // ★ 为何不能直接 QString::fromUtf8(ba)：Qt3 的 fromUtf8 只有
    //   fromUtf8(const char*, int len = -1)，而 QMemArray<char> 有隐式
    //   operator const char*()，于是 ba 被当 C 串、len 取 -1 → strlen 语义。
    //   qnam_shim 的 readAll() 产物不保证 NUL 终止，直接 fromUtf8 会越界读。
    const QString s = QString::fromUtf8("站点加载");  // 站点加载
    const QByteArray ba = qToUtf8BA(s);
    CHECK_EQ(ba.size(), 12);                       // 4 个汉字 × 3 字节，无尾 NUL
    const QString back = qFromUtf8BA(ba);
    REQUIRE_EQ(back.length(), 4);
    // 比码点数值，不靠终端转码判断对错
    CHECK_EQ(back[0].unicode(), 0x7AD9u);   // 站
    CHECK_EQ(back[1].unicode(), 0x70B9u);   // 点
    CHECK_EQ(back[2].unicode(), 0x52A0u);   // 加
    CHECK_EQ(back[3].unicode(), 0x8F7Du);   // 载

    // 非法/截断 UTF-8 不得崩：解码多少算多少
    QByteArray bad(3);
    bad[0] = (char)0xE7; bad[1] = (char)0x8C; bad[2] = (char)0xAB;
    const QString partial = qFromUtf8BA(bad);
    CHECK(partial.length() >= 1);
    // 空数组
    CHECK(qFromUtf8BA(QByteArray()).isEmpty());
}

TEST_CASE("qTrimmed: 只去首尾空白，内部空白保留")
{
    CHECK_EQ(cp(qTrimmed(QString("  a b  "))), QCString("a b"));
    CHECK_EQ(cp(qTrimmed(QString("\t\nx\r "))), QCString("x"));
    CHECK_EQ(cp(qTrimmed(QString("abc"))), QCString("abc"));
    CHECK(qTrimmed(QString("   ")).isEmpty());
    CHECK(qTrimmed(QString("")).isEmpty());
}

TEST_CASE("qStringListRemoveDuplicates: 稳定去重，保留首次出现的相对顺序")
{
    // 与 Qt4.1+ removeDuplicates() 同语义：不排序、不改相对顺序。
    QStringList l;
    l << QString("b") << QString("a") << QString("b") << QString("c")
      << QString("a") << QString("b");
    qStringListRemoveDuplicates(l);
    REQUIRE_EQ(l.size(), 3);
    CHECK_EQ(cp(l[0]), QCString("b"));
    CHECK_EQ(cp(l[1]), QCString("a"));
    CHECK_EQ(cp(l[2]), QCString("c"));

    // 空表 / 单元素
    QStringList empty;
    qStringListRemoveDuplicates(empty);
    CHECK_EQ(empty.size(), 0);
    QStringList one;
    one << QString("x");
    qStringListRemoveDuplicates(one);
    REQUIRE_EQ(one.size(), 1);
    CHECK_EQ(cp(one[0]), QCString("x"));

    // 空串参与去重（只留一份）
    QStringList withEmpty;
    withEmpty << QString("") << QString("a") << QString("");
    qStringListRemoveDuplicates(withEmpty);
    REQUIRE_EQ(withEmpty.size(), 2);
    CHECK(withEmpty[0].isEmpty());
    CHECK_EQ(cp(withEmpty[1]), QCString("a"));
}

TEST_CASE("qStringChop: 真删尾字符（Qt3 的 left() 返回值必须回写）")
{
    // ★ 回归用例。原实现 `if (n > 0) s.left(s.length() - n);` 因 left() 返回
    //   新串而整条丢弃 → 空操作。生产后果：davbisync.cpp:1762 的
    //   `while (r.endsWith('/')) qStringChop(r, 1);` **死循环**。
    QString s = QString("abc/");
    qStringChop(s, 1);
    CHECK_EQ(cp(s), QCString("abc"));

    s = QString("/");
    qStringChop(s, 1);
    CHECK(s.isEmpty());

    // n <= 0 为空操作
    s = QString("ab");
    qStringChop(s, 0);
    CHECK_EQ(cp(s), QCString("ab"));
    qStringChop(s, -1);
    CHECK_EQ(cp(s), QCString("ab"));

    // n >= length() 整串清空（对齐 Qt4+ chop），不做负下标 left()
    // ⚠ 清空后必须是**非 null** 空串：Qt6 实测 chop(越界) 给 isNull=0
    //   （/tmp/opencode/probe/zk9.cpp），且 Qt3 下 null != QString("")。
    //   此前这里断言 s.isNull()，钉的是 Qt3 旧实现 `s = QString()` 的分叉行为。
    s = QString("ab");
    qStringChop(s, 5);
    CHECK(s.isEmpty());
    CHECK(!s.isNull());
    CHECK_EQ(s, QString(""));

    // davbisync canonicalRel 的收敛形态：必须终止
    QString r = QString("pastes/x/");
    int guard = 0;
    while (r.endsWith(QString("/"))) {
        qStringChop(r, 1);
        REQUIRE(++guard < 10);          // 超过即死循环，用例失败而非挂住
    }
    CHECK_EQ(cp(r), QCString("pastes/x"));
}
