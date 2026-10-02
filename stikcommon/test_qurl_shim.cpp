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

// qUrlRemoveFilename 的断言直接比 toString()（不用 qUrlRawString）：本函数的
// 4 处生产/对拍输入全带 "https://" scheme，不受 Qt3 无-scheme 污染影响。
static QString rmf(const QString& s)
{
    return qUrlRemoveFilename(QUrl(s)).toString();
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
    CHECK(qUrlQueryDecode(QString("%E4%B8%AD")) == QString::fromUtf8("中"));
    // 非 ASCII 的**输入**字符不在常态范围，契约是替换为 '?'
    CHECK(qUrlQueryDecode(QString::fromUtf8("中")) == QString("?"));
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
//   数组」而静默错分配。改用 QString 作比较右操作数。
//
// ⚠ 也不能再用 QCString(got.data(), got.size())：那个是 qstrncpy 语义，
//   **按 maxlen 少拷一字符**。qToPercentEncoding 修成精确长度后（不再带尾 NUL），
//   这么取会砍掉最后一个字符，实测 "a b" 得 "a%20" 而非 "a%20b"。
//   现在改走 QString::fromLatin1(data, size)——Qt3 有 (const char*, int) 重载，
//   长度显式、无 NUL 语义，且结果全为 ASCII（百分号编码输出必然如此）。
static void checkEnc(const QString& in, const char* want)
{
    const QByteArray got = qToPercentEncoding(in);
    const QString gotStr = QString::fromLatin1(got.data(), (int)got.size());
    CHECK(gotStr == QString(want));
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
    checkEnc(QString::fromUtf8("中"), "%E4%B8%AD");
    // 空串
    const QByteArray empty = qToPercentEncoding(QString(""));
    CHECK(QString::fromLatin1(QCString(empty.data(), empty.size())).isEmpty());
}

TEST_CASE("qToPercentEncoding: 返回定长 QByteArray，无尾部 NUL（已修的缺陷）")
{
    // ★ 这条原是「已知缺陷」用例：函数声明返回 QByteArray，实现却是
    //   `return QCString(out.c_str())`。QCString 按 NUL 终止分配
    //   （qcstring.h:135「allocate size incl. \0」），派生→基类切片拷贝时把
    //   那个 NUL 也复制进 QMemArray<char>，故 size() 比逻辑长度大 1。
    //   实测「猫喵」18 字节编码结果返回 size()==19、末字节 0x00。
    // 现已改为「先 QByteArray(int size) 精确分配 + 逐字节填」
    // （Qt3.3 的 QByteArray 只有 QByteArray() / QByteArray(int) 两个构造函数，
    //  没有 Qt3.4+/Qt4 的 QByteArray(const char*, uint)），size() 回到逻辑长度。
    //
    // 当初没炸是因为生产调用方都把它当 C 字符串用（QString::fromLatin1 /
    //   fromUtf8 按 strlen 取长，末尾 NUL 不可见）。但 imagesearchclient 的
    //   QString::fromUtf8(QLSTIK_PCT_ENCODE(kw)) 这条链一旦有人改成按 .size()
    //   切片，NUL 就会进 URL，故按精确长度钉死。
    const QByteArray a = qToPercentEncoding(QString("a"));
    CHECK_EQ(a.size(), 1);
    const QByteArray sp = qToPercentEncoding(QString("a b"));
    CHECK_EQ(sp.size(), 5);
    const QByteArray empty = qToPercentEncoding(QString(""));
    CHECK_EQ(empty.size(), 0);
    // 非 ASCII：末字节是**编码字符**（某个 hex 位），不是 0x00
    // ⚠ 原先此处写的是 "\xe7\x8c\xab\xe5\x92\xaa"，末字节 0xAA；那串解出来是
    //   U+54AA 而**不是**「喵」(U+55B5，正确末字节为 0xB5)。断言本身自洽所以
    //   没暴露问题，但会让后来者以为 %E5%92%AA 就是「喵」。已改为 CJK 原文。
    const QByteArray cn = qToPercentEncoding(QString::fromUtf8("猫喵"));
    CHECK_EQ(cn.size(), 18);                   // 6 个 UTF-8 字节 × 3
    const unsigned char lastByte = (unsigned char)cn.at(cn.size() - 1);
    CHECK(lastByte != 0x00);                   // 尾 NUL 会污染 URL
    CHECK(lastByte == '5');                    // 0xB5 的高位 hex 位
    // 整串按精确长度读回来（不能用 QCString(data, size)，那是 qstrncpy 少拷一字符）
    CHECK(QString::fromLatin1(cn.data(), (int)cn.size())
          == QString("%E7%8C%AB%E5%96%B5"));
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

TEST_CASE("qUrlRemoveFilename: 等价 QUrl::adjusted(QUrl::RemoveFilename)")
{
    // 期望值全部取自 **Qt6.7.3 实测**（/tmp/opencode/probe-b2/url6.cpp 直接调
    // QUrl::adjusted(QUrl::RemoveFilename) 打出来），不凭记忆写：
    //   "https://a.com/x/y.html?q=1"    → "https://a.com/x/?q=1"   ← query 保留
    //   "https://a.com/x/y.html?q=1#f" → "https://a.com/x/?q=1#f" ← fragment 也保留
    //   "https://a.com/x/"             → "https://a.com/x/"        （原样）
    //   "https://a.com/"               → "https://a.com/"          （原样）
    //   "https://a.com"                → "https://a.com"           （不加尾斜杠）
    // 早先按记忆写过「丢 query/fragment」和「无 path 也补斜杠」两处，Qt6 实测
    // 证明都是错的，已改实现（只动 path 分量，query/fragment 由 setPath 自然保留）。
    CHECK(rmf("https://a.com/x/y.html?q=1")    == QString("https://a.com/x/?q=1"));
    CHECK(rmf("https://a.com/x/y.html?q=1#f") == QString("https://a.com/x/?q=1#f"));
    CHECK(rmf("https://a.com/x/")             == QString("https://a.com/x/"));
    CHECK(rmf("https://a.com/")               == QString("https://a.com/"));
    CHECK(rmf("https://www.qudoutu.cn/hot/")  == QString("https://www.qudoutu.cn/hot/"));
    CHECK(rmf("https://koishi.js.org/QFace/assets/qq_emoji/_index.json")
          == QString("https://koishi.js.org/QFace/assets/qq_emoji/"));
    // query 里的 '/' 不能把截断点带偏：path 分量是 /a/b.html，应截到 /a/
    CHECK(rmf("https://h/a/b.html?q=x/y") == QString("https://h/a/?q=x/y"));

    // ⚠ 与 Qt6 的一处残留差异：Qt3 的 QUrl 自己会把空 path 规范成 "/"，
    //   故 QUrl("https://a.com").toString() 在 Qt3 下已是 "https://a.com/"
    //   （实测 path()=="/"、toString()=="https://a.com/"）。本函数已原样返回 u，
    //   差异来自 Qt3 的 toString 而非本函数，故此处按 Qt3 实测写断言。
    //   对 sitelistclient 无影响：kSites 里 6 个 HTML 站的 defaultUrl 全以 '/'
    //   结尾，path 已是目录走原样分支；QFace 有显式 baseUrl，走不到这里。
    CHECK(rmf("https://a.com") == QString("https://a.com/"));
    CHECK_EQ(QUrl(QString("https://a.com")).path(), QString("/"));
}

// ═══════════════════════════════════════════════════════════════════════════
// CJK（中文）—— 本仓真实 URL 的关键词就是中文
// ═══════════════════════════════════════════════════════════════════════════
//
// ⚠⚠ 为什么这组必须单独钉：URL 链路上任何一处把 UTF-8 当 Latin-1 处理，
//   中文都会变成乱码，且**全程不报错**（用户只看到搜不到图）。而 ASCII 用例
//   对这类缺陷完全免疫 —— 这是本文件此前最大的盲区。
//
// ⚠ 构造一律走 QString::fromUtf8，**不能**写 QString("中文")：Qt3 的
//   QString(const char*) 是按 Latin-1 逐字节转的（qt/qstring.h:qstring(const
//   char*)），中文会被展开成 6 个高位字符的串，且不报错。同理不得用 latin1()。
//
// ⚠ 下面第一条用例把字面量的**码点**钉死：期望值与实际值都由同一段中文字面量
//   经 fromUtf8 构造，若有人把构造改成 latin1，两边会一起错、断言照样通过，
//   测试变成假绿。码点基准由 python3 按 UTF-8 算出，非手算。

TEST_CASE("CJK 字面量码点自检（防测试数据自身走错编码）")
{
    const QString mao = QString::fromUtf8("猫喵");
    CHECK_EQ(mao.length(), 2);
    CHECK(mao.at(0) == QChar(0x732B));         // 猫
    CHECK(mao.at(1) == QChar(0x55B5));         // 喵

    const QString zhan = QString::fromUtf8("站点");
    CHECK_EQ(zhan.length(), 2);
    CHECK(zhan.at(0) == QChar(0x7AD9));         // 站
    CHECK(zhan.at(1) == QChar(0x70B9));         // 点

    const QString tie = QString::fromUtf8("贴纸.png");
    CHECK_EQ(tie.length(), 6);
    CHECK(tie.at(0) == QChar(0x8D34));          // 贴
    CHECK(tie.at(1) == QChar(0x7EB8));          // 纸
    CHECK(tie.at(2) == QChar('.'));
    CHECK(tie.at(5) == QChar('g'));

    // 反向：同一个串的 UTF-8 字节必须是 6 字节（若有人误用 latin1 构造，
    // 这里的长度会变成 4 —— 2 个汉字各被当成 2 个字节的高位字符）。
    // ⚠ 必须用 length() 而不是 size()：Qt3 的 QString::utf8() 返回 QCString，
    //   其 size() **含尾 NUL**（utf8().size()=7 / length()=6），见
    //   qstring_shim.h:101 记的同一条坑。
    CHECK_EQ(zhan.utf8().length(), 6);
    CHECK_EQ(zhan.utf8().size(), 7);
}

// ⚠ 多字节 CJK 的百分号编码：必须按 **UTF-8 逐字节**编码，且十六进制大写。
//   若误用 latin1()，2 个汉字会各变成 1 个 %XX（结果只有 6 字符而非 18）。
TEST_CASE("qToPercentEncoding: 多字节 CJK 按 UTF-8 逐字节编码")
{
    checkEnc(QString::fromUtf8("猫喵"), "%E7%8C%AB%E5%96%B5");
    checkEnc(QString::fromUtf8("站点"), "%E7%AB%99%E7%82%B9");
    // 中英混排：中文段编码、ASCII 段原样，两者交界不得多/少一个 %
    checkEnc(QString::fromUtf8("站a1"), "%E7%AB%99a1");
    // ⚠ 结果里绝不能出现裸的非 ASCII 字节（Latin-1 漏编码的特征）
    const QByteArray got = qToPercentEncoding(QString::fromUtf8("站"));
    CHECK_EQ(got.size(), 9);                 // 3 字节 → 每字节 3 字符
    for (int i = 0; i < got.size(); ++i) {
        CHECK(((unsigned char)got.at(i)) < 0x80);
    }
}

// ⚠ 百分号编码的中文必须能解回原文（编码/解码成对）。这才是产品真正依赖的：
//   搜索词「猫喵」→ q=%E7%8C%AB%E5%96%B5 → 解析回「猫喵」去拼请求。
TEST_CASE("qUrlQueryDecode: 百分号编码的中文解回原文，与编码成对")
{
    const QString mao = QString::fromUtf8("猫喵");
    CHECK(qUrlQueryDecode(QString("%E7%8C%AB%E5%96%B5")) == mao);
    // 小写 hex 同样要解（URL 里小写 %xx 合法且常见）
    CHECK(qUrlQueryDecode(QString("%e7%8c%ab%e5%96%b5")) == mao);
    // 编码→解码 往返：把 qToPercentEncoding 的输出直接喂回去
    const QByteArray enc = qToPercentEncoding(mao);
    CHECK(qUrlQueryDecode(QString::fromLatin1(enc.data(), (int)enc.size())) == mao);
    // ⚠ 而**未编码**的中文按契约替换为 '?'（逐字符）：2 个汉字 → 2 个 '?'
    CHECK(qUrlQueryDecode(mao) == QString("??"));
    CHECK_EQ(qUrlQueryDecode(mao).length(), 2);
}

// ⚠ CJK 出现在 URL **路径**里（WebDAV 上按站点/贴纸名建目录）：
//   Qt3 的 QUrl 不对 path 做百分号编码，中文应原样保留，不得被改写或吞掉。
//   （实测 Qt3 QUrl::path() 对 "http://h/站点/贴纸.png" 返回含 U+7AD9/U+70B9
//     的原串，见探针；故这些断言按「原样保留」写，而不是按「被编码」写。）
TEST_CASE("qUrlNormUrlPath: CJK 路径段原样保留，且照常消解 . 与 ..")
{
    // 站点/贴纸.png → 消掉 ./ 与 sub/../ 之后仍是 站点/贴纸.png
    CHECK(qUrlNormUrlPath(QString::fromUtf8("http://h/站点/./sub/../贴纸.png"))
          == QString::fromUtf8("http://h/站点/贴纸.png"));
    // 尾斜杠必须保留（WebDAV 目录遍历靠它区分文件与集合）
    CHECK(qUrlNormUrlPath(QString::fromUtf8("http://h/站点/"))
          == QString::fromUtf8("http://h/站点/"));
    // 已干净则原样
    CHECK(qUrlNormUrlPath(QString::fromUtf8("http://h/站点/贴纸.png"))
          == QString::fromUtf8("http://h/站点/贴纸.png"));
}

TEST_CASE("qResolveUrl: base 目录 + CJK 相对名，拼接后中文不被改写")
{
    const QUrl base(QString::fromUtf8("http://h:8080/a/b.html"));
    // 目录相对 + 中文两级路径
    CHECK(raw(qResolveUrl(base, QUrl(QString::fromUtf8("站点/贴纸.png"))))
          == QString::fromUtf8("http://h:8080/a/站点/贴纸.png"));
    // 绝对 URL 里带中文：绝对分支胜出，中文保留
    CHECK(raw(qResolveUrl(base, QUrl(QString::fromUtf8("https://o/站/../贴.png"))))
          == QString::fromUtf8("https://o/贴.png"));
    // 中文文件名 + 上跳一级
    CHECK(raw(qResolveUrl(base, QUrl(QString::fromUtf8("../贴纸.png"))))
          == QString::fromUtf8("http://h:8080/贴纸.png"));
}

TEST_CASE("qUrlRemoveFilename: CJK 文件名截掉后留目录，query 仍保留")
{
    CHECK(rmf(QString::fromUtf8("https://a.com/站点/贴纸.png?q=1"))
          == QString::fromUtf8("https://a.com/站点/?q=1"));
    // 目录（带尾斜杠）→ 原样
    CHECK(rmf(QString::fromUtf8("https://a.com/站点/"))
          == QString::fromUtf8("https://a.com/站点/"));
}
