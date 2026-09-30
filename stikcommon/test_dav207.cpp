// stikcommon/test_dav207.cpp —— DAV 207 多状态解析契约（Qt3 单端，零网络）
//
// 被测对象：stikcommon/dav207pugi.cpp 实现的 dav207iface.h 契约
//   Dav207::parseMultiStatus(body, len, rootPath, out)
//   Dav207::unescapePath(href, out)
// 该接口刻意**不含任何 Qt 类型**（全 std::string / 整数），故
//   · 行为在 Qt3 与 Qt5/6 上完全一致；
//   · 断言失败时 doctest 能正常打印 std::string（不像 QString 有 null 崩溃问题）。
//
// 用语约定（dav207iface.h 契约，勿凭印象改）：
//   · href 存**原始未解码**文本（已去首尾空白），path 才是剥掉 rootPath/authority 后的
//   · 只输出**至少有一个 2xx propstat** 的 resource
//   · 零 propstat 的 response 仍会带 response 级 status 走完回调（neon ne_207.c:255-265）
//   · unescapePath：非法 %XX 返回 false（neon 整条 NULL），'+' 原样保留，
//     纯字节解码不做 UTF-8 校验，只解 path 分量（先剥 fragment/query/authority）
//
// 样本来源：/tmp/opencode/r207.xml 是真实 wsgidav 抓包（2481 字节），原样内嵌。
// 它用的是 **ns0: 前缀 + xmlns:ns0="DAV:"**，正好覆盖"命名空间前缀无关"这条 ——
// dav207iface.h 开头列的坑 4 就是 QDom 只按本地名匹配导致的前缀敏感问题。

#include <string>
#include <vector>

#include "doctest/doctest.h"

#include "dav207iface.h"

using Dav207::Resource;

// ── 内嵌样本 ───────────────────────────────────────────────────────

// 真实 wsgidav 207 响应：3 个 collection，ns0: 前缀。
static const char* kReal207 =
    R"XML(<?xml version="1.0" encoding="utf-8" ?>
<ns0:multistatus xmlns:ns0="DAV:"><ns0:response><ns0:href>/anystik/</ns0:href><ns0:propstat><ns0:prop><ns0:resourcetype><ns0:collection /></ns0:resourcetype><ns0:creationdate>2026-09-30T09:36:47Z</ns0:creationdate><ns0:quota-used-bytes>511000576</ns0:quota-used-bytes><ns0:quota-available-bytes>2553118720</ns0:quota-available-bytes><ns0:getlastmodified>Wed, 30 Sep 2026 09:36:47 GMT</ns0:getlastmodified><ns0:displayname>anystik</ns0:displayname><ns0:lockdiscovery /><ns0:supportedlock><ns0:lockentry><ns0:lockscope><ns0:exclusive /></ns0:lockscope><ns0:locktype><ns0:write /></ns0:locktype></ns0:lockentry><ns0:lockentry><ns0:lockscope><ns0:shared /></ns0:lockscope><ns0:locktype><ns0:write /></ns0:locktype></ns0:lockentry></ns0:supportedlock></ns0:prop><ns0:status>HTTP/1.1 200 OK</ns0:status></ns0:propstat></ns0:response><ns0:response><ns0:href>/anystik/packB/</ns0:href><ns0:propstat><ns0:prop><ns0:resourcetype><ns0:collection /></ns0:resourcetype><ns0:creationdate>2026-09-30T09:36:47Z</ns0:creationdate><ns0:quota-used-bytes>511000576</ns0:quota-used-bytes><ns0:quota-available-bytes>2553118720</ns0:quota-available-bytes><ns0:getlastmodified>Wed, 30 Sep 2026 09:36:47 GMT</ns0:getlastmodified><ns0:displayname>packB</ns0:displayname><ns0:lockdiscovery /><ns0:supportedlock><ns0:lockentry><ns0:lockscope><ns0:exclusive /></ns0:lockscope><ns0:locktype><ns0:write /></ns0:locktype></ns0:lockentry><ns0:lockentry><ns0:lockscope><ns0:shared /></ns0:lockscope><ns0:locktype><ns0:write /></ns0:locktype></ns0:lockentry></ns0:supportedlock></ns0:prop><ns0:status>HTTP/1.1 200 OK</ns0:status></ns0:propstat></ns0:response><ns0:response><ns0:href>/anystik/pastes/</ns0:href><ns0:propstat><ns0:prop><ns0:resourcetype><ns0:collection /></ns0:resourcetype><ns0:creationdate>2026-09-30T09:36:46Z</ns0:creationdate><ns0:quota-used-bytes>511000576</ns0:quota-used-bytes><ns0:quota-available-bytes>2553118720</ns0:quota-available-bytes><ns0:getlastmodified>Wed, 30 Sep 2026 09:36:46 GMT</ns0:getlastmodified><ns0:displayname>pastes</ns0:displayname><ns0:lockdiscovery /><ns0:supportedlock><ns0:lockentry><ns0:lockscope><ns0:exclusive /></ns0:lockscope><ns0:locktype><ns0:write /></ns0:locktype></ns0:lockentry><ns0:lockentry><ns0:lockscope><ns0:shared /></ns0:lockscope><ns0:locktype><ns0:write /></ns0:locktype></ns0:lockentry></ns0:supportedlock></ns0:prop><ns0:status>HTTP/1.1 200 OK</ns0:status></ns0:propstat></ns0:response></ns0:multistatus>)XML";

// 便于拼小样本的最小外壳
static std::string ms(const std::string& inner)
{
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
           "<D:multistatus xmlns:D=\"DAV:\">" + inner + "</D:multistatus>";
}

// 单个 200 propstat 的 response
static std::string okResponse(const std::string& href, const std::string& props)
{
    return "<D:response><D:href>" + href + "</D:href>"
           "<D:propstat><D:prop>" + props + "</D:prop>"
           "<D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response>";
}

static bool parse(const std::string& body, const std::string& rootPath,
                  std::vector<Resource>& out)
{
    return Dav207::parseMultiStatus(body.data(), body.size(), rootPath, out);
}

// ── 真实样本 ───────────────────────────────────────────────────────

TEST_CASE("dav207: 真实 wsgidav 样本解出 3 个 collection")
{
    std::vector<Resource> out;
    CHECK(parse(kReal207, "", out));
    REQUIRE(out.size() == 3);

    // 顺序与文档中出现顺序一致
    CHECK(out[0].href == "/anystik/");
    CHECK(out[1].href == "/anystik/packB/");
    CHECK(out[2].href == "/anystik/pastes/");

    // 三个都是 collection（<ns0:collection/> 存在）
    CHECK(out[0].isDir == true);
    CHECK(out[1].isDir == true);
    CHECK(out[2].isDir == true);

    // 全部 valid
    CHECK(out[0].valid == true);
    CHECK(out[1].valid == true);
    CHECK(out[2].valid == true);

    // getlastmodified → epoch 毫秒。基准：2026-09-30T09:36:47Z = 1790761007
    CHECK(out[0].msec == 1790761007000LL);
    CHECK(out[1].msec == 1790761007000LL);
    CHECK(out[2].msec == 1790761006000LL);   // pastes 早 1 秒

    // 样本里没有 getcontentlength → size 保持 0
    CHECK(out[0].size == 0ULL);
}

TEST_CASE("dav207: 真实样本只有 propstat 级 status，故 hasStatus 为 false")
{
    std::vector<Resource> out;
    REQUIRE(parse(kReal207, "", out));
    REQUIRE(out.size() == 3);
    // ⚠ Resource::status 的语义是「**response 级**状态码」（dav207iface.h 字段注释），
    //   而本样本每个 response 里只有一个 200 的 propstat、没有 response 级 status，
    //   所以 hasStatus 必须是 false、status 必须是 0。
    //   propstat 级 status 只用来做「是否 2xx」的过滤，不回填到这两个字段。
    //   （response 级 status 的回填见下一个用例。）
    for (size_t i = 0; i < out.size(); ++i) {
        CHECK(out[i].hasStatus == false);
        CHECK(out[i].status == 0);
    }
}

TEST_CASE("dav207: response 级 status 单独回填 status/hasStatus")
{
    std::vector<Resource> out;
    // 同一 response 里既有 response 级 423，又有 2xx propstat：
    //   · 因存在 2xx propstat → 该 resource **仍被输出**（只输出 2xx propstat 的契约）
    //   · response 级 423 回填到 status/hasStatus，让调用方知道"整体失败"
    const std::string body = ms(
        "<D:response><D:href>/locked</D:href>"
        "<D:status>HTTP/1.1 423 Locked</D:status>"
        "<D:propstat><D:prop><D:getcontentlength>7</D:getcontentlength></D:prop>"
        "<D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response>");
    REQUIRE(parse(body, "", out));
    REQUIRE(out.size() == 1);
    CHECK(out[0].hasStatus == true);
    CHECK(out[0].status == 423);
    // 2xx propstat 的属性照常取到
    CHECK(out[0].size == 7ULL);
}

TEST_CASE("dav207: rootPath 非空时从 path 里剥离")
{
    std::vector<Resource> out;
    REQUIRE(parse(kReal207, "/anystik", out));
    REQUIRE(out.size() == 3);
    // href 保持原样（原始未解码文本）
    CHECK(out[0].href == "/anystik/");
    // path 剥掉 rootPath
    CHECK(out[0].path == "/");
    CHECK(out[1].path == "/packB/");
    CHECK(out[2].path == "/pastes/");
}

// 把 XML 的 DAV 前缀整体改名：既要改 xmlns 声明（xmlns:D=），也要改元素前缀（D:）。
// 只改后者会让前缀未声明 → 解析直接失败（这正是第一次写这个用例时踩的坑）。
static std::string retargetPrefix(const std::string& in, const std::string& to)
{
    std::string s = in;
    const std::string declOld = "xmlns:D=";
    const size_t dp = s.find(declOld);
    if (dp != std::string::npos) {
        s.replace(dp, declOld.size(), "xmlns:" + to + "=");
    }
    size_t p = s.find("D:");
    while (p != std::string::npos) {
        s.replace(p, 2, to + ":");
        p = s.find("D:", p + to.size() + 1);
    }
    return s;
}

TEST_CASE("dav207: 命名空间前缀无关（ns0: 与 D: 必须等价）")
{
    // 同一份内容换前缀，结果必须逐字段相同
    std::vector<Resource> a;
    std::vector<Resource> b;
    const std::string inner = okResponse("/x/", "<D:resourcetype><D:collection/></D:resourcetype>");
    const std::string bodyD = ms(inner);
    REQUIRE(parse(bodyD, "", a));
    const std::string alt = retargetPrefix(bodyD, "ns0");
    REQUIRE(parse(alt, "", b));          // 声明与前缀都改了才应解析成功
    REQUIRE(a.size() == 1);
    REQUIRE(b.size() == 1);
    CHECK(a[0].href == b[0].href);
    CHECK(a[0].path == b[0].path);
    CHECK(a[0].isDir == b[0].isDir);
    // 换前缀后仍是 collection —— 若解析按字面前缀匹配，这里会 false
    CHECK(b[0].isDir == true);
}

// ── propstat 过滤 ──────────────────────────────────────────────────

TEST_CASE("dav207: 非 2xx propstat 的 resource 被过滤掉")
{
    std::vector<Resource> out;
    const std::string body = ms(
        "<D:response><D:href>/missing</D:href>"
        "<D:propstat><D:prop><D:getcontentlength>5</D:getcontentlength></D:prop>"
        "<D:status>HTTP/1.1 404 Not Found</D:status></D:propstat></D:response>");
    CHECK(parse(body, "", out));
    // 契约：只输出至少有一个 2xx propstat 的 resource
    CHECK(out.size() == 0);
}

TEST_CASE("dav207: 混合 propstat 时取 2xx 的那份属性")
{
    std::vector<Resource> out;
    // 第一个 propstat 404（getcontentlength=999），第二个 200（=42）
    const std::string body = ms(
        "<D:response><D:href>/f</D:href>"
        "<D:propstat><D:prop><D:getcontentlength>999</D:getcontentlength></D:prop>"
        "<D:status>HTTP/1.1 404 Not Found</D:status></D:propstat>"
        "<D:propstat><D:prop><D:getcontentlength>42</D:getcontentlength>"
        "<D:resourcetype><D:collection/></D:resourcetype></D:prop>"
        "<D:status>HTTP/1.1 200 OK</D:status></D:propstat>"
        "</D:response>");
    REQUIRE(parse(body, "", out));
    REQUIRE(out.size() == 1);
    // 200 那份胜出：不是 999
    CHECK(out[0].size == 42ULL);
    // 2xx propstat 里有 collection → isDir
    CHECK(out[0].isDir == true);
}

TEST_CASE("dav207: 零 propstat 但有 response 级 status")
{
    std::vector<Resource> out;
    // 契约（neon ne_207.c:255-265）：零 propstat 的 response 仍走完 end_response
    // 并携带 response 级 status —— 「零 propstat 就不回调」是错的说法。
    const std::string body = ms(
        "<D:response><D:href>/only-status</D:href>"
        "<D:status>HTTP/1.1 423 Locked</D:status></D:response>");
    parse(body, "", out);
    // 这里只断言"不崩且不产生 2xx 数据"；是否输出由上面「只输出 2xx propstat」契约决定
    for (size_t i = 0; i < out.size(); ++i) {
        CHECK(out[i].valid == true);
    }
}

// ── 属性提取 ───────────────────────────────────────────────────────

TEST_CASE("dav207: getcontentlength 与非 collection")
{
    std::vector<Resource> out;
    const std::string body = ms(okResponse(
        "/file.txt", "<D:getcontentlength>1234</D:getcontentlength>"));
    REQUIRE(parse(body, "", out));
    REQUIRE(out.size() == 1);
    CHECK(out[0].size == 1234ULL);
    // 没有 <collection/> → 非目录
    CHECK(out[0].isDir == false);
    CHECK(out[0].href == "/file.txt");
}

TEST_CASE("dav207: 空 resourcetype 也算非 collection")
{
    std::vector<Resource> out;
    const std::string body = ms(okResponse(
        "/f", "<D:resourcetype></D:resourcetype>"));
    REQUIRE(parse(body, "", out));
    REQUIRE(out.size() == 1);
    CHECK(out[0].isDir == false);
}

TEST_CASE("dav207: 无关属性不影响解析")
{
    std::vector<Resource> out;
    // 创建时间、displayname、锁信息等都不属于本接口要取的字段，应被忽略而不报错
    const std::string body = ms(okResponse("/d/", 
        "<D:resourcetype><D:collection/></D:resourcetype>"
        "<D:creationdate>2026-09-30T09:36:47Z</D:creationdate>"
        "<D:displayname>名字</D:displayname>"
        "<D:supportedlock><D:lockentry><D:locktype><D:write/></D:locktype>"
        "</D:lockentry></D:supportedlock>"));
    REQUIRE(parse(body, "", out));
    REQUIRE(out.size() == 1);
    CHECK(out[0].isDir == true);
}

TEST_CASE("dav207: 多个 response 的 href 去首尾空白")
{
    std::vector<Resource> out;
    // href 里的换行/缩进必须被剥掉（契约 A：先 ne_shave 再解析）
    const std::string body = ms(
        "<D:response><D:href>\n    /spaced/   \n  </D:href>"
        "<D:propstat><D:prop><D:resourcetype><D:collection/></D:resourcetype></D:prop>"
        "<D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response>");
    REQUIRE(parse(body, "", out));
    REQUIRE(out.size() == 1);
    CHECK(out[0].href == "/spaced/");
}

// ── 非法输入 ───────────────────────────────────────────────────────

TEST_CASE("dav207: 非法 XML / 非 multistatus 根 / 空体 返回 false")
{
    std::vector<Resource> out;
    CHECK(parse("<D:multistatus xmlns:D=\"DAV:\">", "", out) == false);   // 未闭合
    CHECK(parse("<html><body>hi</body></html>", "", out) == false);      // 根元素不对
    CHECK(parse("", "", out) == false);                                   // 空体
    CHECK(parse("   ", "", out) == false);                                // 纯空白
    CHECK(parse("not xml at all", "", out) == false);
}

TEST_CASE("dav207: 合法的空 multistatus 返回 true 且零条目")
{
    std::vector<Resource> out;
    const std::string body = ms("");
    CHECK(parse(body, "", out) == true);
    CHECK(out.size() == 0);
}

// ── unescapePath ───────────────────────────────────────────────────

TEST_CASE("unescapePath: 常规百分号解码")
{
    std::string out;
    CHECK(Dav207::unescapePath("/a%20b", out));
    CHECK(out == "/a b");
    CHECK(Dav207::unescapePath("/%E7%AC%94%E8%AE%B0", out));   // UTF-8 "笔记"
    CHECK(out == "/笔记");
    CHECK(Dav207::unescapePath("/plain/path", out));
    CHECK(out == "/plain/path");
    CHECK(Dav207::unescapePath("", out));                     // 空串不报错
    CHECK(out == "");
}

TEST_CASE("unescapePath: '+' 原样保留（不是空格）")
{
    std::string out;
    // 契约 C：非 '%' 字符原样复制，URI path 里的 '+' 是字面量
    CHECK(Dav207::unescapePath("/a+b", out));
    CHECK(out == "/a+b");
    CHECK(Dav207::unescapePath("/a%20b+c", out));
    CHECK(out == "/a b+c");
}

TEST_CASE("unescapePath: 非法 %XX 返回 false")
{
    std::string out;
    // 契约 C：neon 遇 '%' 校验后两位必须 isxdigit，否则**整条 free 并返回 NULL**
    // （不是原样保留 %）
    CHECK(Dav207::unescapePath("/a%zzb", out) == false);
    CHECK(Dav207::unescapePath("/a%2", out) == false);       // 只有一位
    CHECK(Dav207::unescapePath("/trailing%", out) == false);
    CHECK(Dav207::unescapePath("/a%2Gb", out) == false);     // G 非 hex
}

TEST_CASE("unescapePath: 只解 path 分量，authority / query / fragment 被剥掉")
{
    std::string out;
    // 绝对 URL：只解 path 里的编码，host 保持原样
    CHECK(Dav207::unescapePath("http://h.example/a%20b", out));
    CHECK(out == "/a b");
    // ⚠ 契约原文是"会先**剥掉** fragment、query、scheme://authority"
    //   （dav207iface.h unescapePath 注释），即它们是被**丢弃**而非原样保留 ——
    //   这样才不至于把 authority 里的 %2F 之类一并搅坏。实测输出确实只剩 path。
    CHECK(Dav207::unescapePath("/a%20b?q=%20", out));
    CHECK(out == "/a b");
    CHECK(Dav207::unescapePath("/a%20b#f%20g", out));
    CHECK(out == "/a b");
    // query 里的非法 %XX 因此也不会让整体失败（它已被剥掉）
    CHECK(Dav207::unescapePath("/ok?q=%zz", out));
    CHECK(out == "/ok");
}

TEST_CASE("unescapePath: 纯字节解码，不校验 UTF-8 合法性")
{
    std::string out;
    // %FF 不是合法 UTF-8 起始字节，但契约 C 说不校验，原样解出该字节
    CHECK(Dav207::unescapePath("/%FF", out));
    CHECK(out.size() == 2);          // '/' + 1 字节
    CHECK((unsigned char)out[1] == 0xFF);
}

TEST_CASE("dav207: href 里的 %20 不影响 href 字段（href 保持未解码）")
{
    std::vector<Resource> out;
    const std::string body = ms(okResponse("/my%20file.txt", ""));
    REQUIRE(parse(body, "", out));
    REQUIRE(out.size() == 1);
    // 契约：href 存原始未解码文本
    CHECK(out[0].href == "/my%20file.txt");
    // 解码要调用方自己做 unescapePath
    std::string dec;
    CHECK(Dav207::unescapePath(out[0].href, dec));
    CHECK(dec == "/my file.txt");
}
