// stikcommon/test_qurl_shim.cpp —— qurl_shim.h 契约（Qt3 单端，零网络）
//
// 被测对象为 stikcommon/qurl_shim.h（#if QT_VERSION < 0x040000 才编译，
// 即本仓 Qt3 专属垫片；Qt4/5/6 分支走 Qt 自带 QUrl，本文件不适用）。
//
// 头文件为纯 static inline，**无需链接任何产品 .cpp**，故本用例零依赖。
//
// 断言依据：逐条对照 qurl_shim.h 的实现与其注释里写明的契约，不凭记忆写期望值。
// 凡 Qt3 与 Qt4/5 语义不同处，注释均指出对照的实测结论。

#include <qstring.h>
#include <qurl.h>
#include "qurl_shim.h"

#include "doctest/doctest.h"

// qUrlRawString 取 QUrl 的"原始串"。Qt3 对无 scheme 输入会把 protocol 默认成
// file，toString() 被污染成 "file:xxx"，垫片用 path() 还原，故比较结果时
// 一律先过 qUrlRawString，避免拿 Qt3 的污染串去比。
static QString raw(const QUrl& u)
{
    return qUrlRawString(u);
}

TEST_CASE("qUrlHexVal: 十六进制取值，非法返回 -1")
{
    CHECK_EQ(qUrlHexVal(QChar('0')), 0);
    CHECK_EQ(qUrlHexVal(QChar('9')), 9);
    CHECK_EQ(qUrlHexVal(QChar('A')), 10);
    CHECK_EQ(qUrlHexVal(QChar('F')), 15);
    CHECK_EQ(qUrlHexVal(QChar('a')), 10);
    CHECK_EQ(qUrlHexVal(QChar('f')), 15);
    // 小写与大小写等价（同一 hex 位的两种写法）
    CHECK_EQ(qUrlHexVal(QChar('c')), qUrlHexVal(QChar('C')));
    // 非法字符必须返回 -1，否则会被当成合法 hex 位算出错误字节
    CHECK_EQ(qUrlHexVal(QChar('G')), -1);
    CHECK_EQ(qUrlHexVal(QChar('g')), -1);
    CHECK_EQ(qUrlHexVal(QChar('/')), -1);
    CHECK_EQ(qUrlHexVal(QChar(' ')), -1);
    CHECK_EQ(qUrlHexVal(QChar(':')), -1);
}

TEST_CASE("qUrlQueryDecode: 仅 %XX 解码，'+' 保留，非 ASCII 替换为 '?'")
{
    // 基本 %XX
    CHECK(qUrlQueryDecode(QString("a%20b")) == QString("a b"));
    CHECK(qUrlQueryDecode(QString("%41%42%43")) == QString("ABC"));
    // '+' 不解码：头文件注释记明「Qt 的 QUrlQuery 不做
    // application/x-www-form-urlencoded 的空格转换，实测 queryItemValue("q")
    // 对 "q=cat+red" 返回 "cat+red"」，故 '+' 必须原样保留。
    CHECK(qUrlQueryDecode(QString("cat+red")) == QString("cat+red"));
    CHECK(qUrlQueryDecode(QString("a+b%20c")) == QString("a+b c"));
    // %XX 逐字节收集后按 UTF-8 解释：%E4%B8%AD = 「中」(U+4E2D, 3 字节)
    CHECK(qUrlQueryDecode(QString("%E4%B8%AD")) == QString::fromUtf8("\xe4\xb8\xad"));
    // 非 ASCII 的**输入**字符不在常态范围，契约是替换为 '?'
    CHECK(qUrlQueryDecode(QString::fromUtf8("\xe4\xb8\xad")) == QString("?"));
    // 非法 hex：% 保留、只前进 1 字节，后续字符按普通字符处理
    CHECK(qUrlQueryDecode(QString("a%ZZb")) == QString("a%ZZb"));
    CHECK(qUrlQueryDecode(QString("a%2")) == QString("a%2"));
    // 恰好落在串尾的 %XX 仍能解码（i+2 < n 保证两字符都在界内）
    CHECK(qUrlQueryDecode(QString("%41")) == QString("A"));
    // 空串与无 % 的直通
    CHECK(qUrlQueryDecode(QString("")).isEmpty());
    CHECK(qUrlQueryDecode(QString("plain")) == QString("plain"));
}

TEST_CASE("QUrlQuery: 按 key 取 query 值，缺失/无值返回空串")
{
    const QUrl u("http://h/p?a=1&b=hello%20world&c&d=");
    const QUrlQuery q(u);
    CHECK(q.queryItemValue(QString("a")) == QString("1"));
    CHECK(q.queryItemValue(QString("b")) == QString("hello world"));
    // 有 key 无 '=' → 空串（契约：eq == -1 ? QString() : ...）
    CHECK(q.queryItemValue(QString("c")).isEmpty());
    // 有 key 有 '=' 但值为空 → 也是空串
    CHECK(q.queryItemValue(QString("d")).isEmpty());
    // 缺失的 key
    CHECK(q.queryItemValue(QString("zz")).isEmpty());
    // 不得误匹配子串前缀：查 "a" 不能命中 "aa"
    const QUrl u2("http://h/p?aa=2&a=1");
    const QUrlQuery q2(u2);
    CHECK(q2.queryItemValue(QString("a")) == QString("1"));
    // 值为空串时不得返回后续段的串
    const QUrl u3("http://h/p?a=&b=2");
    const QUrlQuery q3(u3);
    CHECK(q3.queryItemValue(QString("a")).isEmpty());
    // URL 完全无 query
    const QUrl u4("http://h/p");
    const QUrlQuery q4(u4);
    CHECK(q4.queryItemValue(QString("a")).isEmpty());
}

// ⚠ 必须先把返回值落进局部变量再比，不能写 CHECK(qToPercentEncoding(x) == y)：
//   doctest 的 CHECK 会把 == 的左操作数包成 Expression_lhs<T>，包装后到
//   QMemArray<char> 的 operator==（qmemarray.h:107 只接受 const QMemArray&）没有
//   可用重载 → 编译报 no match for operator==。
//   （QString 那几组能写，是因为 QString 另有 const char* / QString 重载兜住。）
// ⚠ 局部变量类型必须是 QByteArray 而不是 QCString：qToPercentEncoding 返回
//   QByteArray，而 Qt3 里 QCString 是从 QMemArray<char> **派生**的，反向不可隐式。
// ⚠ 不能用 QByteArray(want) 构造期望值：Qt3 的 QMemArray<char> 没有 const char*
//   构造函数（qmemarray.h:62 只有 QMemArray(int size)），会报 invalid conversion
//   from 'const char*' to 'int'；若开了 -fpermissive 更会被当成「按指针值定长的
//   数组」而静默错分配。改用 QCString 作比较右操作数。
//
// 主契约按 C 字符串取内容：QCString(data, size) 是 qstrncpy 语义（长度含终止 NUL，
// 会少一字符），正好把返回值末尾那个 NUL 去掉，得到逻辑字符串。
static void checkEnc(const QString& in, const char* want)
{
    const QByteArray got = qToPercentEncoding(in);
    const QCString gotStr(got.data(), got.size());
    CHECK(gotStr == QCString(want));
}

TEST_CASE("qToPercentEncoding: 保留 -_.~ 与 alnum，其余按 UTF-8 转 %XX")
{
    checkEnc(QString("abcXYZ019"), "abcXYZ019");
    // - _ . ~ 为 RFC 3986 unreserved，必须保留
    checkEnc(QString("-_.~"), "-_.~");
    // 空格与保留字符要转
    checkEnc(QString("a b"), "a%20b");
    checkEnc(QString("/"), "%2F");
    checkEnc(QString("?#&=+"), "%3F%23%26%3D%2B");
    // 十六进制为大写（头文件里的 HEX 表是大写）
    checkEnc(QString(" "), "%20");
    // 非 ASCII 按 UTF-8 逐字节编码：「中」= E4 B8 AD
    checkEnc(QString::fromUtf8("\xe4\xb8\xad"), "%E4%B8%AD");
    // 空串
    const QByteArray empty = qToPercentEncoding(QString(""));
    CHECK(QString::fromLatin1(QCString(empty.data(), empty.size())).isEmpty());
}

TEST_CASE("qToPercentEncoding 已知缺陷: size() 比逻辑长度大 1（切片带走终止 NUL）")
{
    // 根因：函数声明返回 QByteArray，实现却是 `return QCString(out.c_str())`。
    // QCString 按 NUL 终止分配（qcstring.h:135「allocate size incl. \0」），
    // 派生→基类切片拷贝时把那个 NUL 也复制进 QMemArray<char>，故 size() 多 1。
    // 头文件注释只防了 QCString(const char*, uint) 的 qstrncpy 坑，漏了切片这条。
    //
    // 为何一直没炸：唯一生产调用方 anystik/src/imageaiutil.cpp:406 把它交给
    // QString::fromLatin1，Qt3 按 QCString::length()（即 strlen）取长度，
    // 末尾 NUL 不可见 → 实测 "a b" 得到恰为 a%20b 的 5 字符，结果正确。
    // 只有把返回值当**定长缓冲**用 .size() 时才会踩到；全仓无此用法。
    //
    // 本条把缺陷显式钉住而非掩盖：修 shim 后此用例会失败，届时应连同
    // imageaiutil 的调用约定一起复核，再决定是否改断言。
    const QByteArray a = qToPercentEncoding(QString("a"));
    CHECK_EQ(a.size(), 2);          // 逻辑 1
    const QByteArray sp = qToPercentEncoding(QString("a b"));
    CHECK_EQ(sp.size(), 6);         // 逻辑 5
    const QByteArray empty = qToPercentEncoding(QString(""));
    CHECK_EQ(empty.size(), 1);      // 逻辑 0
    // 但按 C 字符串用是对的（data() 上的 NUL 终止完好）
    CHECK_EQ(QCString(a.data(), a.size()).length(), 1u);
}

TEST_CASE("qUrlRawString: 剥掉 Qt3 对无 scheme 输入注入的 file: 污染")
{
    // 头文件记明：Qt3 对相对 "d/e"、根 "/root/x"、协议相对 "//h/x" 都把
    // protocol 默认成 file，toString() 变成 "file:d/e" 等。垫片用 path() 还原。
    CHECK(raw(QUrl("/root/x")) == QString("/root/x"));
    CHECK(raw(QUrl("d/e")) == QString("d/e"));
    CHECK(raw(QUrl("a.txt")) == QString("a.txt"));
    // 有 scheme 的不受影响，原样返回
    CHECK(raw(QUrl("http://h:8080/p")) == QString("http://h:8080/p"));
    CHECK(raw(QUrl("https://h/p")) == QString("https://h/p"));
}

TEST_CASE("qUrlScheme: 取首个 ':' 前一段并转小写，无 scheme 返回空串")
{
    CHECK(qUrlScheme(QUrl("http://h/p")) == QString("http"));
    CHECK(qUrlScheme(QUrl("https://h/p")) == QString("https"));
    // 转小写
    CHECK(qUrlScheme(QUrl("HTTP://h/p")) == QString("http"));
    CHECK(qUrlScheme(QUrl("HtTpS://h/p")) == QString("https"));
    // 无 scheme：Qt3 的 file: 污染要先被 qUrlRawString 剥掉，故为空
    CHECK(qUrlScheme(QUrl("/root/x")).isEmpty());
    CHECK(qUrlScheme(QUrl("d/e")).isEmpty());
    // 冒号在首字符（相对量: 0 位）时 colon <= 0 → 返回空串
    CHECK(qUrlScheme(QUrl(":x")).isEmpty());
}

TEST_CASE("qUrlIsRelative: http(s) 算绝对，其余算相对；协议相对在 Qt3 降级为相对")
{
    CHECK(qUrlIsRelative(QUrl("http://h/p")) == false);
    CHECK(qUrlIsRelative(QUrl("https://h/p")) == false);
    CHECK(qUrlIsRelative(QUrl("/root/x")) == true);
    CHECK(qUrlIsRelative(QUrl("d/e")) == true);
    CHECK(qUrlIsRelative(QUrl("")) == true);
    // 契约是「绝对 http(s) 之外都算相对」，故 ftp 也算相对（不是笔误）
    CHECK(qUrlIsRelative(QUrl("ftp://h/p")) == true);
    // ⚠ 协议相对「按绝对处理」那条分支（qurl_shim.h:155 的 startsWith("//")）
    //   在 Qt3 上**实际不可达**：Qt3 解析 "//h/x" 时把 host 并入 path，
    //   qUrlRawString 走 file: 还原分支后给出的是单斜杠的 "/h/x"，
    //   "//" 前缀已被吃掉。头文件 124-125 行正是这么记的，并注明对拍用例据实跳过。
    //   故这里断言的是**实测降级结果**（相对），不是注释里那句理想语义。
    CHECK(qUrlIsRelative(QUrl("//example.com/x")) == true);
    CHECK(raw(QUrl("//example.com/x")) == QString("/example.com/x"));
}

TEST_CASE("qUrlNormUrlPath: 消 '.'/'..' 与空段，保留尾斜杠与 query/fragment")
{
    // 消当前目录段
    CHECK(qUrlNormUrlPath(QString("http://h/a/./b")) == QString("http://h/a/b"));
    CHECK(qUrlNormUrlPath(QString("http://h/./a")) == QString("http://h/a"));
    // 上溯
    CHECK(qUrlNormUrlPath(QString("http://h/a/../b")) == QString("http://h/b"));
    CHECK(qUrlNormUrlPath(QString("http://h/a/b/../c")) == QString("http://h/a/c"));
    // 上溯到根再上溯：越过根的 .. 被忽略（与 Qt 同）
    CHECK(qUrlNormUrlPath(QString("http://h/../../x")) == QString("http://h/x"));
    CHECK(qUrlNormUrlPath(QString("http://h/a/../..")) == QString("http://h/"));
    // 空段被丢弃（'//' 折叠）
    CHECK(qUrlNormUrlPath(QString("http://h/a//b")) == QString("http://h/a/b"));
    // 尾斜杠保留
    CHECK(qUrlNormUrlPath(QString("http://h/a/b/")) == QString("http://h/a/b/"));
    // segment 被全部消掉 → 补回根斜杠
    CHECK(qUrlNormUrlPath(QString("http://h/a/..")) == QString("http://h/"));
    // query / fragment 原样跟在后面
    CHECK(qUrlNormUrlPath(QString("http://h/a/../b?x=1")) == QString("http://h/b?x=1"));
    CHECK(qUrlNormUrlPath(QString("http://h/a/../b#f")) == QString("http://h/b#f"));
    CHECK(qUrlNormUrlPath(QString("http://h/a/../b?x=1#f")) == QString("http://h/b?x=1#f"));
    // 无 scheme（非绝对）原样返回
    CHECK(qUrlNormUrlPath(QString("/a/../b")) == QString("/a/../b"));
    // 纯 host 无 path 原样返回
    CHECK(qUrlNormUrlPath(QString("http://h")) == QString("http://h"));
    // 已经是干净的不能被改动
    CHECK(qUrlNormUrlPath(QString("http://h/a/b")) == QString("http://h/a/b"));
}

TEST_CASE("qResolveUrl: 绝对/根路径/协议相对/目录相对 + 统一规范化")
{
    const QUrl base("http://h:8080/a/b.html");

    // relative 为空 → 原样返回 base
    CHECK(raw(qResolveUrl(base, QUrl(""))) == QString("http://h:8080/a/b.html"));
    // 绝对 URL 直接胜出（并仍走规范化）
    CHECK(raw(qResolveUrl(base, QUrl("https://other/x/../y"))) == QString("https://other/y"));
    // 以 '/' 开头 → 换成 host 根（丢掉 base 的 path 与端口后的 path）
    CHECK(raw(qResolveUrl(base, QUrl("/z/w.txt"))) == QString("http://h:8080/z/w.txt"));
    // 目录相对 → base 所在目录 + 相对名
    CHECK(raw(qResolveUrl(base, QUrl("c.png"))) == QString("http://h:8080/a/c.png"));
    // base 目录里的 '..' 交给出口处的规范化处理
    CHECK(raw(qResolveUrl(base, QUrl("../c.png"))) == QString("http://h:8080/c.png"));
    CHECK(raw(qResolveUrl(base, QUrl("./d.png"))) == QString("http://h:8080/a/d.png"));
    // ⚠ 协议相对「继承 base 的 scheme」那条分支（qResolveUrl 里的 startsWith("//")）
    //   在 Qt3 上同样不可达，同上：QString 的 "//cdn/i.png" 先被 qUrlRawString
    //   还原成单斜杠 "/cdn/i.png"，于是走的是 host 根分支。断言按实测结果写。
    CHECK(raw(qResolveUrl(base, QUrl("//cdn/i.png"))) == QString("http://h:8080/cdn/i.png"));
    // base 无 '/' 段可截时补一条
    const QUrl base2("http://h");
    CHECK(raw(qResolveUrl(base2, QUrl("rel"))) == QString("http://h/rel"));
    // base 带 query/fragment 时，目录相对只取其目录部分
    const QUrl base3("http://h/a/b.html?x=1#f");
    CHECK(raw(qResolveUrl(base3, QUrl("c.png"))) == QString("http://h/a/c.png"));
}
