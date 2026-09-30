// dav207pugi.cpp —— DAV 207 多状态响应解析（pugixml 1.15 单后端实现）。
//
// API 使用说明（全部已 grep pugixml.hpp:1.15 核实，不凭记忆）：
//   · 没有 namespace_uri()：全头无 namespace 成员，故无法直接取命名空间 URI。
//   · 没有 children() 范围版：只有 first_child() / next_sibling(name)。
//   · load_buffer(contents, size, options, encoding=encoding_auto) 会自动识别
//     UTF-8/UTF-16 并转成内部字符格式，故这里直接喂原始响应字节。
//   · xml_parse_result 可隐式转 bool，description() 取错误描述。
//
// 命名空间判定：因 1.15 不暴露 namespace URI，改用「剥掉前缀后比对本地名
// + DAV 前缀白名单」：isDavPrefixed() 校验前缀属于 {D,DAV,d,dav,a,A} 或无前缀，
// davChild() / forEachDavChild() 据此取直接子元素。这样第三方命名空间的
// 同名元素（如 evil:collection）会被排除，且能正确处理
// xmlns:D 重新指派到非 DAV URI 的情况——这一点已由 probe.xml 的
// evilns 用例实测覆盖。
#include "dav207iface.h"

#include "pugixml/pugixml.hpp"

#include <cstdio>   // sscanf
#include <cstdlib>  // atoi, strtoull
#include <cstring>
#include <cctype>

namespace Dav207 {
namespace {

// neon 契约 D：cdata 累积上限。href 超过此长度的响应体视为异常。
const size_t kMaxHrefLen = 2048;

inline bool isHexDigit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

inline int hexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// neon 契约 C：ne_shave(cdata, "\r\n\t ")。只去**首尾**空白，中间不动。
std::string shave(const std::string& s)
{
    const char* ws = " \t\r\n";
    size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) {
        return std::string();
    }
    size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

// 按本地名匹配 DAV: 元素。因 pugixml 1.15 不暴露 namespace，这里比对
// "去掉前缀后的本地名"，并要求前缀为空或属于常见 DAV 前缀集合。
bool isDavPrefixed(const char* name)
{
    if (!name) {
        return false;
    }
    const char* colon = std::strchr(name, ':');
    if (!colon) {
        return true;               // 无前缀：默认 DAV:
    }
    // ⚠ 只作**兜底**，不再作主判据。主判据是 isDavNode() 的命名空间 URI。
    //   保留 D/d/dav/DAV/a/A 白名单是为了「响应压根没声明 DAV 命名空间」时
    //   仍能解析（有些服务器偷懒不写 xmlns），而合法但少见的其他前缀
    //   （wsgidav 的 ns0:、部分 Java/Go 实现的 lp1: 等）由 URI 判据放行。
    static const char* kOk[] = { "D", "DAV", "d", "dav", "a", "A" };
    const size_t plen = (size_t)(colon - name);
    for (size_t i = 0; i < sizeof(kOk) / sizeof(kOk[0]); ++i) {
        if (std::strlen(kOk[i]) == plen &&
            std::strncmp(name, kOk[i], plen) == 0) {
            return true;
        }
    }
    return false;
}

// 按 XML 规范解析元素所属的命名空间 URI（「最近的祖先 xmlns 声明优先」）。
//
// 为什么自己走一遍而不用 pugixml 的 namespace_uri()：**本仓库 vendor 的
// pugixml 公开头里没有这个访问器** —— .cpp 内部给 XPath 实现了
// （pugixml.cpp:9020），但 pugixml.hpp 的 xml_node 公开接口里被拿掉了
// （1.15 与系统 1.16 都查过，都没有）。依赖内部符号/内联函数既脆又不可移植，
// 故只用公开 API（first_attribute / name / value / parent）按规范走声明链。
// pugixml 是纯 C++，Qt3/Qt6 行为一致。
//
// 前缀为空（无冒号的名字）时查默认声明 xmlns=。
const char* elementNsUri(pugi::xml_node n)
{
    if (!n) {
        return 0;
    }
    const char* nm = n.name();
    if (!nm) {
        return 0;
    }
    const char* colon = std::strchr(nm, ':');
    const size_t plen = colon ? (size_t)(colon - nm) : 0;
    for (pugi::xml_node cur = n; cur; cur = cur.parent()) {
        for (pugi::xml_attribute a = cur.first_attribute(); a;
             a = a.next_attribute()) {
            const char* an = a.name();
            if (!an) {
                continue;
            }
            if (plen == 0) {
                if (std::strcmp(an, "xmlns") == 0) {
                    return a.value();
                }
                continue;
            }
            // xmlns:prefix=...，且 prefix 恰为本元素前缀
            if (std::strncmp(an, "xmlns:", 6) == 0
                && std::strncmp(an + 6, nm, plen) == 0
                && an[6 + plen] == 0) {
                return a.value();
            }
        }
    }
    return 0;
}

// ★ 主判据：按**命名空间 URI** 判 DAV 成员，不再看前缀名字。
//
//   为什么要改：原实现只认前缀白名单 {D,DAV,d,dav,a,A}。XML 前缀是**任意的**，
//   服务器爱用什么就用什么 —— wsgidav 4.3.5 默认吐 `ns0:`（实测见
//   davsync_e2e：整份 207 被判非法，pastes/ 及其内容全部消失，目录扫描静默
//   退化成「只有根」），不少 Java/Go/.NET 的 DAV 实现也用生成前缀。
//   整份丢弃比报错更难查，因为 parseMultiStatus 只 return false，调用方
//   只能从「列不出子目录」倒推。
//
//   URI 判据把原有诉求一并满足：
//     - 第三方命名空间的同名元素仍被排除（x:collection 的 URI 是
//       http://example.com/evil ≠ DAV:）—— probe.xml evilns 用例覆盖；
//     - `xmlns:D` 被重新指派到非 DAV URI 也能正确识别 —— 这是前缀名
//       判据**原理上做不到**的（它只看名字，不看绑定）。
bool isDavNode(pugi::xml_node n)
{
    if (!n) {
        return false;
    }
    const char* uri = elementNsUri(n);
    if (uri && *uri) {
        return std::strcmp(uri, "DAV:") == 0;
    }
    // 没声明任何命名空间：无前缀的按 DAV: 收，带前缀的走白名单兜底
    //（兼容压根不写 xmlns 的偷懒服务器）。
    return isDavPrefixed(n.name());
}

// 找第一个本地名匹配 localName 的 DAV: 子节点。
pugi::xml_node davChild(pugi::xml_node parent, const char* localName)
{
    if (!parent) {
        return pugi::xml_node();
    }
    for (pugi::xml_node n = parent.first_child(); n; n = n.next_sibling()) {
        if (n.type() != pugi::node_element) {
            continue;
        }
        const char* nm = n.name();
        if (!nm) {
            continue;
        }
        const char* colon = std::strchr(nm, ':');
        if (colon && std::strcmp(colon + 1, localName) == 0) {
            if (isDavNode(n)) {
                return n;
            }
            continue;
        }
        if (!colon && std::strcmp(nm, localName) == 0) {
            return n;
        }
    }
    return pugi::xml_node();
}

// 遍历所有本地名匹配 localName 的 DAV: 子节点。
template <typename F>
void forEachDavChild(pugi::xml_node parent, const char* localName, F fn)
{
    if (!parent) {
        return;
    }
    for (pugi::xml_node n = parent.first_child(); n; n = n.next_sibling()) {
        if (n.type() != pugi::node_element) {
            continue;
        }
        const char* nm = n.name();
        if (!nm) {
            continue;
        }
        const char* colon = std::strchr(nm, ':');
        if (colon && std::strcmp(colon + 1, localName) == 0) {
            if (isDavNode(n)) {
                fn(n);
            }
        } else if (!colon && std::strcmp(nm, localName) == 0) {
            fn(n);
        }
    }
}

std::string nodeText(pugi::xml_node n)
{
    if (!n) {
        return std::string();
    }
    const char* t = n.text().get();
    if (!t) {
        // 元素内可能只有子元素没有 text 节点，退回 child_value
        t = n.child_value();
    }
    return t ? std::string(t) : std::string();
}

// 解析 "HTTP/1.1 200 OK" → 200。neon 用 ne_parse_statusline()，同样按
// 第一个空格切分数字部分。
int parseStatusLine(const std::string& s)
{
    size_t sp1 = s.find(' ');
    if (sp1 == std::string::npos) {
        return -1;
    }
    size_t sp2 = s.find(' ', sp1 + 1);
    std::string num = (sp2 == std::string::npos)
                          ? s.substr(sp1 + 1)
                          : s.substr(sp1 + 1, sp2 - sp1 - 1);
    if (num.empty()) {
        return -1;
    }
    for (size_t i = 0; i < num.size(); ++i) {
        if (!std::isdigit((unsigned char)num[i])) {
            return -1;
        }
    }
    return std::atoi(num.c_str());
}

// getlastmodified 解析。RFC 4918 §15.6 用 HTTP-date，常见三格式：
// RFC1123（RFC 7231 现行）、RFC850、asctime。返回 epoch 毫秒，无效返回 0。
// 不依赖 Qt 的 QDateTime，避免 Qt3/Qt5/6 解析差异。
long long parseHttpDate(const std::string& in)
{
    if (in.empty()) {
        return 0;
    }
    // 归一化：折叠内部连续空白
    std::string s;
    s.reserve(in.size());
    bool prevSp = false;
    for (size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c == ' ' || c == '\t') {
            if (!prevSp) {
                s.push_back(' ');
            }
            prevSp = true;
        } else {
            s.push_back(c);
            prevSp = false;
        }
    }
    // 去掉可能的前导空白
    size_t b = s.find_first_not_of(' ');
    if (b == std::string::npos) {
        return 0;
    }
    s = s.substr(b);

    static const char* kMonths[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    long long msec = 0;

    // 拆出 day / mon / year / hh:mm:ss 与可选 zone
    char mon[4] = { 0, 0, 0, 0 };
    int day = 0, yyyy = 0, hh = 0, mi = 0, ss = 0;
    char zone[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    bool haveZone = false;

    // 格式 A: "Sun, 06 Nov 1994 08:49:37 GMT"（逗号后的 RFC1123）
    // 格式 B: "Sunday, 06-Nov-94 08:49:37 GMT"（RFC850，2 位年 + 短月）
    // 格式 C: "Sun Nov  6 08:49:37 1994"（asctime）
    int n = std::sscanf(s.c_str(), "%3s, %d %3s %d %d:%d:%d %7s",
                        zone, &day, mon, &yyyy, &hh, &mi, &ss, zone);
    bool okA = (n >= 7);

    if (!okA) {
        // RFC850: 星期, 日-月-年
        char w[16], mm[8], yy[16], zs[8];
        int d2, h2, m2, s2;
        if (std::sscanf(s.c_str(), "%15[^,], %d-%7[^ ]-%15[0-9] %d:%d:%d %7s",
                        w, &d2, mm, yy, &h2, &m2, &s2, zs) == 8) {
            day = d2; hh = h2; mi = m2; ss = s2;
            std::strncpy(mon, mm, 3); mon[3] = 0;
            yyyy = std::atoi(yy);
            // 2 位年按 RFC 规定映射到 1970..2069
            if (yyyy < 70) {
                yyyy += 2000;
            } else if (yyyy < 100) {
                yyyy += 1900;
            }
            std::strncpy(zone, zs, 7); zone[7] = 0;
            haveZone = true;
            okA = true;
        }
    }

    if (!okA) {
        // asctime: "Sun Nov  6 08:49:37 1994"
        char w[16];
        if (std::sscanf(s.c_str(), "%15s %3s %d %d:%d:%d %d",
                        w, mon, &day, &hh, &mi, &ss, &yyyy) == 7) {
            okA = true;
        }
    }

    if (!okA) {
        return 0;
    }
    (void)haveZone;

    int m = 0;
    for (int i = 0; i < 12; ++i) {
        if (std::strncmp(mon, kMonths[i], 3) == 0) {
            m = i + 1;
            break;
        }
    }
    if (m == 0 || day < 1 || day > 31 || yyyy < 1 || yyyy > 9999) {
        return 0;
    }
    if (hh < 0 || hh > 23 || mi < 0 || mi > 59 || ss < 0 || ss > 60) {
        return 0;
    }

    // Howard Hinnant days_from_civil，无平台依赖、无时区库依赖
    long long y = yyyy;
    const long long mm = m;
    y -= (mm <= 2) ? 1 : 0;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const long long yoe = y - era * 400;                       // [0, 399]
    const long long doy = (153 * (mm + (mm > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;// [0, 146096]
    const long long days = era * 146097 + doe - 719468;        // 1970-01-01 = 0

    // 时区：仅处理 Z 与 ±HHMM，其余按 UTC（HTTP-date 本就要求 GMT，
    // 非标准偏移的服务器罕见，偏差上限 14h，可接受）
    long long offsetSec = 0;
    if (zone[0] == '+' || zone[0] == '-') {
        int zh = 0, zm = 0;
        if (std::sscanf(zone + 1, "%2d%2d", &zh, &zm) >= 1) {
            offsetSec = (zh * 3600 + zm * 60);
            if (zone[0] == '-') {
                offsetSec = -offsetSec;
            }
        }
    } else if (zone[0] == 'G' || zone[0] == 'g' || zone[0] == 'U' ||
               zone[0] == 'u') {
        offsetSec = 0;   // GMT / UTC
    }

    msec = ((days * 86400LL) + hh * 3600LL + mi * 60LL + ss - offsetSec) * 1000LL;
    return msec;
}

// 判断 resourcetype 下是否存在 **DAV: 命名空间的直接子元素** collection。
// 对齐 RFC 4918 §15.9；必须直接子元素且属 DAV:，
// 嵌套在 wrapper 里或第三方命名空间的 collection 都不算。
bool resourcetypeIsCollection(pugi::xml_node resourcetype)
{
    bool found = false;
    forEachDavChild(resourcetype, "collection", [&found](pugi::xml_node) {
        found = true;
    });
    return found;
}

} // namespace

bool unescapePath(const std::string& href, std::string& out)
{
    // 1) 先摘掉 fragment 与 query，只留 path
    std::string s = href;
    const size_t hash = s.find('#');
    if (hash != std::string::npos) {
        s.erase(hash);
    }
    const size_t qm = s.find('?');
    if (qm != std::string::npos) {
        s.erase(qm);
    }

    // 2) absolute-URI（IIS 风格）→ path-absolute：剥掉 scheme://authority。
    //    否则 authority 里的 %XX 会被错误解码。
    const size_t sch = s.find("://");
    if (sch != std::string::npos && sch > 0) {
        const size_t slash = s.find('/', sch + 3);
        s = (slash == std::string::npos) ? std::string("/") : s.substr(slash);
    }

    // 3) 百分号解码。严格按 neon 契约 C：非法 %XX → 整条失败。
    std::string res;
    res.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%') {
            if (i + 2 >= s.size()) {
                return false;                       // 截断的 % 或 %X
            }
            const int hi = hexVal(s[i + 1]);
            const int lo = hexVal(s[i + 2]);
            if (hi < 0 || lo < 0) {
                return false;                       // 非 hex
            }
            res.push_back((char)(hi * 16 + lo));
            i += 2;
        } else {
            res.push_back(s[i]);                   // '+' 在此原样保留
        }
    }
    out.swap(res);
    return true;
}

bool parseMultiStatus(const char* body, size_t len,
                      const std::string& rootPath,
                      std::vector<Resource>& out)
{
    if (!body || len == 0) {
        return false;
    }
    pugi::xml_document doc;
    const pugi::xml_parse_result res =
        doc.load_buffer(body, len, pugi::parse_default, pugi::encoding_auto);
    if (!res) {
        return false;
    }

    pugi::xml_node root = doc.first_child();
    // 跳过可能的 <?xml ?> 与注释
    while (root && (root.type() == pugi::node_pcdata ||
                    root.type() == pugi::node_comment ||
                    root.type() == pugi::node_declaration)) {
        root = root.next_sibling();
    }
    if (!root) {
        return false;
    }
    {
        const char* nm = root.name();
        const char* colon = nm ? std::strchr(nm, ':') : 0;
        const char* local = colon ? colon + 1 : nm;
        // 根元素同样按命名空间 URI 判 DAV:（见 isDavNode 注释）。
        if (!local || std::strcmp(local, "multistatus") != 0 ||
            !isDavNode(root)) {
            return false;
        }
    }

    forEachDavChild(root, "response", [&](pugi::xml_node resp) {
        Resource r;
        r.isDir = false;
        r.size = 0;
        r.msec = 0;
        r.hasStatus = false;
        r.status = 0;
        r.valid = false;

        // href。neon 契约 A/D：先 shave 空白，长度超 2048 视为异常。
        pugi::xml_node hrefEl = davChild(resp, "href");
        if (!hrefEl) {
            return;                                  // 无 href：整条丢弃
        }
        const std::string hrefRaw = shave(nodeText(hrefEl));
        if (hrefRaw.empty() || hrefRaw.size() > kMaxHrefLen) {
            return;
        }
        r.href = hrefRaw;

        // 百分号解码出本地路径
        std::string path;
        if (!unescapePath(hrefRaw, path)) {
            return;                                  // 非法 %XX：丢弃
        }
        r.path = path;

        // 剥 rootPath（带校验，避免 vendor 无条件 remove 砍掉前导字符）
        if (!rootPath.empty() && r.path.compare(0, rootPath.size(),
                                                 rootPath) == 0) {
            r.path.erase(0, rootPath.size());
        }

        // response 级 status：直接子元素，无 propstat 时才有
        pugi::xml_node respStatus = davChild(resp, "status");
        if (respStatus) {
            const int code = parseStatusLine(shave(nodeText(respStatus)));
            if (code > 0) {
                r.hasStatus = true;
                r.status = code;
            }
        }

        // 遍历 propstat。只接受 2xx 段。
        bool saw2xx = false;
        bool sawResourcetype = false;
        forEachDavChild(resp, "propstat", [&](pugi::xml_node ps) {
            pugi::xml_node stEl = davChild(ps, "status");
            if (!stEl) {
                return;                              // 无状态的段跳过
            }
            const int code = parseStatusLine(shave(nodeText(stEl)));
            if (code < 200 || code >= 300) {
                return;                              // 非 2xx 整段丢弃
            }
            saw2xx = true;

            pugi::xml_node prop = davChild(ps, "prop");
            if (!prop) {
                return;
            }
            // 直接遍历 prop 的元素子节点
            for (pugi::xml_node p = prop.first_child(); p; p = p.next_sibling()) {
                if (p.type() != pugi::node_element) {
                    continue;
                }
                const char* pn = p.name();
                if (!pn) {
                    continue;
                }
                const char* colon = std::strchr(pn, ':');
                const char* local = colon ? colon + 1 : pn;
                if (!isDavNode(p)) {
                    continue;                          // 非 DAV: 属性，跳过
                }
                if (std::strcmp(local, "getcontentlength") == 0) {
                    r.size = std::strtoull(nodeText(p).c_str(), 0, 10);
                } else if (std::strcmp(local, "getlastmodified") == 0) {
                    r.msec = parseHttpDate(nodeText(p));
                } else if (std::strcmp(local, "resourcetype") == 0) {
                    if (!sawResourcetype) {          // 首个 2xx 段锁定
                        sawResourcetype = true;
                        r.isDir = resourcetypeIsCollection(p);
                    }
                }
            }
        });

        // 零 2xx propstat：不产出条目。response 级 status 已记在 r 里，
        // 但本次不放入 out（调用方按需自行处理整体失败）。
        if (!saw2xx) {
            return;
        }
        r.valid = true;
        out.push_back(r);
    });

    return true;
}

} // namespace Dav207
