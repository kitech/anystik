#ifndef QLSTIK_QURL_SHIM_H
#define QLSTIK_QURL_SHIM_H

// Qt3 QUrl 缺口：QUrlQuery（Qt5 引入）、QUrl::toPercentEncoding / isRelative /
// resolved（Qt4 引入）。Qt3 原生有 query()/toString()/host()/isValid()。
// 本头提供三个等价自由函数 + QUrlQuery 类（仅 QT3_BUILD 编译；Qt4+ 走原生，
// Qt6 的 QUrlQuery 是 QtCore 原生类，本头不碰）。语义与 Qt 原生逐项对齐，
// 见移植计划 §6.3d / qnam-test 对拍用例。

#include <qglobal.h>

#if QT_VERSION < 0x040000
#include <qurl.h>
#include <qstring.h>
#include <qcstring.h>
#include <string>
#include <vector>

// ── 查询值解码（对齐 QUrlQuery 语义）：仅 %XX→UTF-8 字节；'+' 保留不解码──
// （Qt 的 QUrlQuery 不做 application/x-www-form-urlencoded 的空格转换，实测
// queryItemValue("q") 对 "q=cat+red" 返回 "cat+red"。对拍三端一致。）
static inline int qUrlHexVal(const QChar c)
{
    const unsigned u = c.unicode();
    if (u >= '0' && u <= '9') {
        return int(u - '0');
    }
    if (u >= 'A' && u <= 'F') {
        return int(u - 'A' + 10);
    }
    if (u >= 'a' && u <= 'f') {
        return int(u - 'a' + 10);
    }
    return -1;
}

static inline QString qUrlQueryDecode(const QString& raw)
{
    QCString bytes;   // QCString 无 reserve，append 自增长（query 极短，性能无碍）
    int i = 0;
    const int n = raw.length();
    while (i < n) {
        const QChar c = raw[i];
        if (c == '%' && i + 2 < n) {
            const int h = qUrlHexVal(raw[i + 1]);
            const int l = qUrlHexVal(raw[i + 2]);
            if (h >= 0 && l >= 0) {
                bytes += (char)((h << 4) | l);
                i += 3;
            } else {
                bytes += '%';
                ++i;
            }
        } else if (c.unicode() < 0x80) {
            bytes += (char)c.unicode();
            ++i;
        } else {
            bytes += '?';   // 非 ASCII 不在 Bing query 描述常态范围内，务实替换
            ++i;
        }
    }
    return QString::fromUtf8(bytes);
}

// ── QUrlQuery：只读 query 提取（imageaiutil 只用 queryItemValue）──
class QUrlQuery
{
public:
    explicit QUrlQuery(const QUrl& url) : m_query(url.query()) {}
    QString queryItemValue(const QString& key) const
    {
        int pos = 0;
        const int len = m_query.length();
        while (pos < len) {
            const int amp = m_query.find(QChar('&'), pos);
            const QString pair = amp == -1 ? m_query.mid(pos)
                                           : m_query.mid(pos, amp - pos);
            const int eq = pair.find(QChar('='));
            const QString k = eq == -1 ? pair : pair.left(eq);
            if (k == key) {
                return eq == -1 ? QString()
                                : qUrlQueryDecode(pair.mid(eq + 1));
            }
            if (amp == -1) {
                break;
            }
            pos = amp + 1;
        }
        return QString();
    }

private:
    QString m_query;
};

// ── URL 百分号编码（-_.~ 保持，保留字符转 %XX，UTF-8 输入）──
static inline QByteArray qToPercentEncoding(const QString& input)
{
    const QCString utf8 = input.utf8();
    static const char* HEX = "0123456789ABCDEF";
    std::string out;
    out.reserve(utf8.length() * 3);
    for (uint i = 0; i < utf8.length(); ++i) {
        const unsigned char c = (unsigned char)utf8.at((int)i);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += HEX[c >> 4];
            out += HEX[c & 0xF];
        }
    }
    // 精确长度构造。
    //
    // ⚠ 踩过的坑：QCString → QByteArray 的隐式转换会**按 QCString::length()
    //   拷贝**，而 Qt3 的 length() **含终止 NUL**（实测「猫喵」的 18 字节编码
    //   结果返回 size()==19，末字节 0x00）。fromUtf8 会丢掉尾部 NUL，故
    //   imagesearchclient 的 QString::fromUtf8(QLSTIK_PCT_ENCODE(kw)) 这条链
    //   实测干净（url=[https://x/y?q=%E7%8C%AB%E5%92%AA]，无 NUL）；但任何按
    //   size() 切片或交给 C API 的用法都会被尾部 NUL 坑到，故这里给精确长度。
    //
    // ⚠ Qt3.3 的 QByteArray **只有 QByteArray() 和 QByteArray(int size)**
    //   两个构造函数（实测声明见 /opt/qt338sh/include/qcstring.h），
    //   没有 Qt3.4+/Qt4 的 QByteArray(const char*, uint)，故不能一步构造，
    //   只能先按 size 分配再逐字节填。QCString(const char*, uint) 也不行：
    //   它的结果 length() 同样含 NUL，切过去还是多 1。
    QByteArray ba((int)out.size());
    char* d = ba.data();
    for (size_t i = 0; i < out.size(); ++i) {
        d[i] = out[i];
    }
    return ba;
}

// ── QUrl::adjusted(QUrl::RemoveFilename) 等价 ───────────────────────────
// sitelistclient.cpp:166 用 `url.adjusted(QUrl::RemoveFilename)` 把列表页 URL
// 变成「目录基址」（供 <img src> 相对路径解析）。Qt3 QUrl 无 adjusted()（Qt4.0
// 才引入），也无 RemoveFilename 枚举。
//
// ★ 语义以 Qt6.7.3 实测为准（早先按记忆写错过两处，见下方「已修正」）：
//   "https://a.com/x/y.html?q=1"  → "https://a.com/x/?q=1"   ← **query 保留**
//   "https://a.com/x/y.html?q=1#f"→ "https://a.com/x/?q=1#f" ← fragment 也保留
//   "https://a.com/x/"            → "https://a.com/x/"        （原样）
//   "https://a.com/"              → "https://a.com/"          （原样）
//   "https://a.com"               → "https://a.com"           （**不加尾斜杠**）
//   "https://www.qudoutu.cn/hot/" → "https://www.qudoutu.cn/hot/"
// 已修正的错误认知：① RemoveFilename 不丢 query/fragment；② 无 path 时不补斜杠。
//
// 实现只改 path 分量（用 QUrl 自己的 path()/setPath()），query/fragment
// 自然被 setPath 保留——不要去动整串，否则 query 里的 '/' 会把 findRev 带偏。
//
// 已知残留差异（Qt3 自身的 toString 规范化，与本函数无关）：
//   输入 "https://a.com" 时本函数原样返回 u，但 Qt3 的 QUrl 会把空 path 规范成
//   "/"（path() 返回 "/"，toString() 返回 "https://a.com/"），Qt6 则保持
//   "https://a.com"。实测：QT3 path("/")、toString("https://a.com/")。
//   对 sitelistclient 无影响 —— kSites 里 6 个 HTML 站的 defaultUrl 全部以 '/'
//   结尾，path 已是目录，本函数原样返回，QFace 有显式 baseUrl 走不到这里。
//   （同类 Qt3 toString 污染见下方 qUrlRawString 的说明。）
static inline QUrl qUrlRemoveFilename(const QUrl& u)
{
    QString p = u.path();
    if (p.isEmpty() || p.endsWith("/")) {
        return u;                        // 无 path，或已是目录：原样
    }
    const int slash = p.findRev(QChar('/'));
    QUrl out(u);
    if (slash == -1) {
        out.setPath("/");                // 相对路径且不含 '/'：退到根
    } else {
        out.setPath(p.left(slash + 1));
    }
    return out;
}

// ── Qt3 QUrl 的"原始串"提取 ──
// Qt3 对无 scheme 的输入（相对 "d/e"、根 "/root/x"、协议相对 "//h/x"）把 protocol
// 默认成 "file"，toString() 于是得到 "file:d/e" / "file:/root/x"（污染）。用 path()
// 还原：无 scheme 的 QUrl 其 path() 即原始相对/根路径部分。
// 注意：Qt3 解析 "//h/x" 时 host 并入 path（path="/h/x"），协议相对的 host 无法还原，
// 垫片按根路径降级处理（对拍用例据实跳过，见 §6.3d 说明）。
static inline QString qUrlRawString(const QUrl& u)
{
    const QString s = u.toString();
    if (s.startsWith("file:")) {
        return u.path();
    }
    return s;
}

// ── scheme 提取（Qt3 QUrl 无 scheme()）──
// Qt3 只有 protocol()，且对无 scheme 的输入默认返回 "file"（同 qUrlRawString 的坑），
// 故同样从原始串取：首个 ':' 之前的一段，转小写。相对/根路径返回空串。
static inline QString qUrlScheme(const QUrl& u)
{
    const QString s = qUrlRawString(u);
    const int colon = s.find(QChar(':'));
    if (colon <= 0) {
        return QString();
    }
    return s.left(colon).lower();
}

// ── 相对判定：绝对 http(s) 之外都算相对 ──
static inline bool qUrlIsRelative(const QUrl& u)
{
    const QString s = qUrlRawString(u);
    if (s.isEmpty()) {
        return true;
    }
    if (s.startsWith("//")) {
        return false;     // 协议相对（如 "//example.com/x"），按绝对处理
    }
    return !s.startsWith("http://") && !s.startsWith("https://");
}

// ── 绝对 URL 的 path 规范化（RFC 3986 §5.2.4 实用子集）──
// 消除 "./" 与 "../"（含跨目录上溯）；协议相对/相对串原样返回。
// 供 qResolveUrl 对最终绝对 URL 统一处理，使 ../ 行为与 Qt::resolved 一致。
static inline QString qUrlNormUrlPath(const QString& url)
{
    const int colon = url.find(QChar(':'));
    if (colon <= 0) {
        return url;   // 无 scheme：非绝对，不动
    }
    const int hostStart = url.find('/', colon + 3);   // "scheme://host" 后首 '/'
    if (hostStart == -1) {
        return url;   // 纯 host 无 path
    }
    int pend = url.length();
    const int q = url.find(QChar('?'));
    if (q != -1 && q < pend) {
        pend = q;
    }
    const int frag = url.find(QChar('#'));
    if (frag != -1 && frag < pend) {
        pend = frag;
    }
    if (pend <= hostStart) {
        return url;
    }
    const QString path = url.mid(hostStart, pend - hostStart);   // 以 '/' 开头
    std::vector<QString> segs;
    int i = 1;   // 跳过 path 首 '/'
    const int plen = path.length();
    while (i < plen) {
        int j = path.find(QChar('/'), i);
        if (j == -1) {
            j = plen;
        }
        const QString seg = path.mid(i, j - i);
        if (seg == ".") {
            // 当前目录段：跳过
        } else if (seg == "..") {
            if (!segs.empty()) {
                segs.pop_back();   // 上溯：且不越根（Qt 同：根上..被忽略）
            }
        } else if (!seg.isEmpty()) {
            segs.push_back(seg);
        }
        i = j + 1;
    }
    QString norm = url.left(hostStart);
for (size_t k = 0; k < segs.size(); ++k) {
        norm += "/";
        norm += segs[k];
    }
    if (segs.empty()) {
        norm += "/";   // path 清空 → 根
    }
    if (path[plen - 1] == '/') {
        norm += "/";   // 原以 '/' 结尾则保留尾部斜杠（Qt 同）
    }
    norm += url.mid(pend);
    return norm;
}

// ── QUrl::resolved 等价（imageaiutil Bing 重定向跟随场景）──
// base 是绝对 URL，relative 可为绝对、"/路径"（host 根）、"//host/x"（协议相对，继承
// base scheme）或"路径"（base 目录）。统一出口做 path 规范化（../ 齐 Qt）。
static inline QUrl qResolveUrl(const QUrl& base, const QUrl& relative)
{
    const QString rs = qUrlRawString(relative);
    if (rs.isEmpty()) {
        return base;
    }
    QString result;
    if (rs.startsWith("//")) {
        // 协议相对：继承 base 的 scheme
        const QString bs = base.toString();
        const int colon = bs.find(QChar(':'));
        result = (colon == -1 ? rs : bs.left(colon + 1) + rs);
    } else if (!qUrlIsRelative(relative)) {
        result = rs;
    } else if (rs.startsWith("/")) {
        // host 根：取 scheme://host 后接相对路径
        result = base.toString();
        const int cut = result.find('/', 7);
        if (cut == -1) {
            result += rs;
        } else {
            result.truncate(cut);
            result += rs;
        }
    } else {
        // base 目录 + 相对路径
        result = base.toString();
        int cut = result.find(QChar('?'));
        if (cut != -1) {
            result.truncate(cut);
        }
        cut = result.find(QChar('#'));
        if (cut != -1) {
            result.truncate(cut);
        }
        cut = result.findRev('/');
        if (cut == -1) {
            result += "/";   // base 无 '/' 段可截，补一条
        } else {
            result.truncate(cut + 1);
        }
        result += rs;
    }
    return QUrl(qUrlNormUrlPath(result));
}

#endif // QT_VERSION < 0x040000

#endif // QLSTIK_QURL_SHIM_H