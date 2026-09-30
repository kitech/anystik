// dav207iface.h —— DAV 207 多状态响应的解析后端选择与统一契约。
//
// 为什么要这层：Qt 自带的 XML API（QDom / QString）在 Qt3 与 Qt5/6 之间语义
// **不一致**，实测踩到的坑（每一条都造成过数据被破坏而不是报错）：
//   1. QString::utf8() 与 QString::latin1() 共用同一个静态转换缓冲区，同一
//      表达式里先后调用会互相覆盖，打印出的字符串可能是空的或半截的。
//   2. QString(const char*) 在 Qt3 按 Latin-1 逐字节解释源文件里的 UTF-8 字节，
//      "笔记"（6 字节）会变成 6 个 U+00xx 字符，长度都对不上。
//   3. QDomDocument::setContent 的 QByteArray / QString 重载在 Qt3 的编码
//      语义与 Qt5+ 不同。
//   4. QDom 的 elementsByTagName() 只按**本地名**匹配，忽略命名空间，
//      第三方命名空间的同名元素会被误判。
// 因此这里把解析层完全从 Qt 抽离，接口内一律用 std::string（纯字节，Qt 无关）。
//
// 两个后端：**运行期**由静态变量 dav207Backend() 决定，不靠宏。
//   BackendPugixml —— pugixml 1.15，MIT，3 源文件，已随本目录引入。
//       已实测在本项目 Qt3 编译 flags 下零错误通过。
//   BackendNeonContract —— 行为契约逐条对照 neon **0.37.1**（tag）的
//       ne_207.c / ne_uri.c（LGPL v2.1），但**不复制其代码**，只复现其
//       可观察行为。选它是为了拿第二个独立实现交叉验证同一份 207 样本。
//
// neon 0.37.1 行为契约（出处已核实，勿凭印象改；详见 vendorinfos.md）：
//   A. ne_207.c:192-222  end_element(ELM_href)：先 ne_shave(cdata,"\r\n\t ")
//      去首尾空白，再 ne_uri_parse()，成功才 start_response() 并置
//      in_response=1。也就是说 **in_response 由 href 而非 propstat 决定**。
//   B. ne_207.c:255-265  end_element(ELM_response)：`if (!p->in_response) break;`
//      —— 零 propstat 的 response **照样触发 end_response**，并携带
//      response 级 status。所以「零 propstat 就不回调」是错的说法；
//      零 propstat 的过滤发生在 ne_props.c，不在 ne_207.c。
//   C. ne_uri.c:487 ne_path_unescape()：遇 '%' 时校验后两位必须是 isxdigit，
//      **否则整条 free 并返回 NULL**（不是原样保留）；解码是纯字节级
//      strtol(buf,16)，不做 UTF-8 校验；非 '%' 字符**原样复制**，因此
//      URI path 里的 '+' 是字面量、不是空格。
//   D. ne_207.c:130-142  cdata 累积上限 2048 字节；href 还会再 ne_shave 一次。
//
// neon 为何没做第二个后端（避免后人重走）：neon 自己**没有** XML 解析器，
// `ne_xml.c` 只是 SAX 包装层，第 41/56 行分别 `#if HAVE_EXPAT` /
// `#elif HAVE_LIBXML`，第 66-67 行是 `#else` + `#error need an XML parser`。
// 不给外部 XML 库就编译不过，要绕开就只能自研整个 ne_xml 解析层。故只做
// pugixml 单后端，上面的契约仅作为**语义基准**使用。
#ifndef DAV207IFACE_H
#define DAV207IFACE_H

#include <string>
#include <vector>

namespace Dav207 {

// 单个资源的解析结果。全部字段为纯字节/整数，不含 Qt 类型，
// 因此在 Qt3 与 Qt5/6 上行为完全一致。
struct Resource {
    std::string href;        // 原始 href（未解码），已去首尾空白
    std::string path;        // href 剥掉 rootPath 与 authority 后的路径
    bool        isDir;       // DAV: 命名空间的 collection 存在
    unsigned long long size; // DAV:getcontentlength
    long long   msec;        // DAV:getlastmodified 的 epoch 毫秒，0=无效
    bool        hasStatus;   // 是否存在 response 级或 propstat 级 status
    int         status;      // response 级状态码，无则 0
    bool        valid;       // 解析成功且有 href
};

// 解析 207 响应体。
//   body     原始响应字节（UTF-8/UTF-16 由后端自动识别）
//   rootPath 服务端 rootPath（如 "/root"），用于从 href 中剥离；为空则不剥
//   out      输出，成功时可能被部分填充
// 返回 false 表示 XML 本身非法或根元素不是 DAV:multistatus。
//
// 契约：只输出**至少有一个 2xx propstat**的 resource；response 级 status
// 单独放进 status/hasStatus，用于让调用方知道该资源整体失败了。
bool parseMultiStatus(const char* body, size_t len,
                      const std::string& rootPath,
                      std::vector<Resource>& out);

// 百分号解码 path 分量。
// 行为严格对照 neon ne_path_unescape（见上方契约 C）：
//   - 非法 %XX → 返回 false（neon 是整条 NULL）
//   - '+' 原样保留
//   - 纯字节解码，不做 UTF-8 校验
// 同时只解码 path 分量：会先剥掉 fragment、query、scheme://authority，
// 避免把 %2F 之类在 authority 里的编码一并搅坏。
bool unescapePath(const std::string& href, std::string& out);

} // namespace Dav207

#endif // DAV207IFACE_H
