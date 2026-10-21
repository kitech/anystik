#include "linkpreview.h"

#include <vector>
#include <string.h>
#include <dlfcn.h>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#else
#include <iconv.h>
#include <errno.h>
#endif

// 编译期选择 HTML 解析器：
//   全平台统一使用 vendor 内嵌 gumbo（源码编译，不依赖系统 libgumbo）。
// -DLINKPREVIEW_NO_GUMBO 仍可强制切换到内置手写浅扫描器（应急/测试）。
#ifndef LINKPREVIEW_NO_GUMBO
#include <gumbo.h>
#endif

static inline int lpFind(const QString& s, QChar c, int from = 0) {
#ifdef QT3_BUILD
    return s.find(c, from);
#else
    return s.indexOf(c, from);
#endif
}
static inline int lpFind(const QString& s, const QString& sub, int from = 0) {
#ifdef QT3_BUILD
    return s.find(sub, from);
#else
    return s.indexOf(sub, from);
#endif
}
static inline int lpFindCI(const QString& s, const QString& sub, int from = 0) {
#ifdef QT3_BUILD
    return s.find(sub, from, false);
#else
    return s.indexOf(sub, from, Qt::CaseInsensitive);
#endif
}
static inline QString lpLower(const QString& s) {
#ifdef QT3_BUILD
    return s.lower();
#else
    return s.toLower();
#endif
}
static inline QString lpTrim(const QString& s) {
#ifdef QT3_BUILD
    return s.stripWhiteSpace();
#else
    return s.trimmed();
#endif
}
static inline QByteArray lpMidArr(const QByteArray& a, int from, int len) {
#ifdef QT3_BUILD
    QByteArray out;
    if (from < 0) { from = 0; }
    if ((int)a.size() < from + len) { len = (int)a.size() - from; }
    if (len <= 0) { return out; }
    out.resize((uint)len);
    memcpy(out.data(), a.data() + from, (size_t)len);
    return out;
#else
    return a.mid(from, len);
#endif
}

static QString lpCollapseWs(const QString& in) {
    QString out;
    out.reserve(in.length());
    bool prevSpace = false;
    for (int i = 0; i < in.length(); ++i) {
        QChar c = in[i];
        bool ws = (c == QChar(' ') || c == QChar('\t') || c == QChar('\n') || c == QChar('\r'));
        if (ws) {
            if (!prevSpace && !out.isEmpty()) { out += QChar(' '); }
            prevSpace = true;
        } else { out += c; prevSpace = false; }
    }
    while (out.length() > 0 && out[out.length() - 1] == QChar(' ')) {
        out = out.left(out.length() - 1);
    }
    return out;
}

static QString lpTagAttr(const QString& tag, const QString& want) {
    QString lower = lpLower(tag);
    int n = lower.length();
    int i = 0;
    while (i < n) {
        while (i < n && (lower[i].isSpace() || lower[i] == QChar('/'))) { i++; }
        if (i >= n) { break; }
        int ns = i;
        while (i < n && !lower[i].isSpace() && lower[i] != QChar('=') && lower[i] != QChar('/')) { i++; }
        QString key = lower.mid(ns, i - ns);
        while (i < n && lower[i].isSpace()) { i++; }
        if (i < n && lower[i] == QChar('=')) {
            i++;
            while (i < n && lower[i].isSpace()) { i++; }
            QString val;
            if (i < n && (tag[i] == QChar('"') || tag[i] == QChar('\''))) {
                QChar q = tag[i];
                int vs = ++i;
                int end = lpFind(tag, q, i);
                val = (end < 0) ? tag.mid(vs) : tag.mid(vs, end - vs);
                i = (end < 0) ? n : end + 1;
            } else {
                int vs = i;
                while (i < n && !lower[i].isSpace() && lower[i] != QChar('/')) { i++; }
                val = tag.mid(vs, i - vs);
            }
            if (key == want) { return val; }
        }
    }
    return QString();
}

static bool lpCharsetIsBanned(const QString& cs) {
    return cs == "utf-7" || cs == "utf-1" || cs == "scsu"
        || cs == "bocu-1" || cs == "cesu-8" || cs == "utf-ebcdic"
        || cs.startsWith("iso-2022");
}

static QString lpNormalizeCharset(const QString& in) {
    QString cs = lpTrim(in);
    if (!cs.isEmpty() && (cs[0] == QChar('"') || cs[0] == QChar('\''))) {
        // 引号包裹：取引号内内容，忽略其后的 ";" 等参数
        QChar q = cs[0];
        int end = lpFind(cs, q, 1);
        cs = (end < 0) ? cs.mid(1) : cs.mid(1, end - 1);
    } else {
        // 未加引号：截断到首个 ';'/空白/引号，避免 "charset=X; foo=bar" 整段传给 iconv
        int cut = -1;
        for (int i = 0; i < cs.length(); ++i) {
            QChar c = cs[i];
            if (c == QChar(';') || c == QChar('"') || c == QChar('\'') || c.isSpace()) {
                cut = i;
                break;
            }
        }
        if (cut >= 0) { cs = cs.left(cut); }
    }
    cs = lpLower(lpTrim(cs));
    if (cs.isEmpty() || lpCharsetIsBanned(cs)) { return QString(); }
    if (cs == "utf8") { return "utf-8"; }
    if (cs == "x-gbk" || cs == "cp936" || cs == "ms936") { return "gbk"; }
    if (cs == "gb_2312-80" || cs == "chinese" || cs == "csgb2312") { return "gb2312"; }
    if (cs == "latin1" || cs == "l1" || cs == "iso8859-1" || cs == "iso-8859-1"
        || cs == "us-ascii" || cs == "ascii" || cs == "ansi_x3.4-1968") {
        return "windows-1252";
    }
    if (cs == "shift-jis" || cs == "sjis" || cs == "csshiftjis" || cs == "ms_kanji" || cs == "x-sjis") {
        return "shift_jis";
    }
    if (cs == "eucjp" || cs == "x-euc-jp") { return "euc-jp"; }
    return cs;
}

static QString lpCharsetFromContentType(const QString& ct) {
    int i = lpFindCI(ct, "charset");
    if (i < 0) { return QString(); }
    int eq = lpFind(ct, QChar('='), i);
    if (eq < 0) { return QString(); }
    return lpNormalizeCharset(ct.mid(eq + 1));
}

static QString lpMetaAttrCharset(const QString& tag) {
    QString cs = lpTagAttr(tag, "charset");
    if (!cs.isEmpty()) { return lpNormalizeCharset(cs); }
    if (lpLower(lpTrim(lpTagAttr(tag, "http-equiv"))) == "content-type") {
        QString content = lpTagAttr(tag, "content");
        int i = lpFindCI(content, "charset");
        if (i >= 0) {
            int eq = lpFind(content, QChar('='), i);
            if (eq >= 0) { return lpNormalizeCharset(content.mid(eq + 1)); }
        }
    }
    return QString();
}

static QString lpMetaPrescanCharset(const QByteArray& raw) {
    int n = raw.size();
    if (n > 1024) { n = 1024; }
    const unsigned char* p = (const unsigned char*)raw.data();
    QString s;
    s.reserve(n);
    for (int i = 0; i < n; ++i) {
        unsigned char c = p[i];
        s += (c < 0x80) ? QChar((ushort)c) : QChar(' ');
    }
    int pos = 0;
    while (pos < n) {
        int lt = lpFind(s, QChar('<'), pos);
        if (lt < 0) { break; }
        if (lt + 1 >= n || lpFindCI(s, "meta", lt + 1) != lt + 1) { pos = lt + 1; continue; }
        int gt = lpFind(s, QChar('>'), lt + 1);
        if (gt < 0) { break; }
        QString cs = lpMetaAttrCharset(s.mid(lt + 1, gt - lt - 1));
        if (!cs.isEmpty()) { return cs; }
        pos = gt + 1;
    }
    return QString();
}

static QString lpDetectBom(const QByteArray& b) {
    const unsigned char* p = (const unsigned char*)b.data();
    int n = b.size();
    if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) { return "utf-8"; }
    if (n >= 4 && p[0] == 0xFF && p[1] == 0xFE && p[2] == 0x00 && p[3] == 0x00) { return "utf-32le"; }
    if (n >= 4 && p[0] == 0x00 && p[1] == 0x00 && p[2] == 0xFE && p[3] == 0xFF) { return "utf-32be"; }
    if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) { return "utf-16le"; }
    if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) { return "utf-16be"; }
    return QString();
}

static int lpBomLen(const QString& bom) {
    if (bom == "utf-8") { return 3; }
    if (bom == "utf-16le" || bom == "utf-16be") { return 2; }
    if (bom == "utf-32le" || bom == "utf-32be") { return 4; }
    return 0;
}

static QString lpDetectByUchardet(const QByteArray& raw) {
    void* h = dlopen("libuchardet.so.0", RTLD_LAZY);
    if (!h) { h = dlopen("libuchardet.so", RTLD_LAZY); }
    if (!h) { return QString(); }
    void* (*u_new)() = (void* (*)())dlsym(h, "uchardet_new");
    void (*u_del)(void*) = (void (*)(void*))dlsym(h, "uchardet_delete");
    int (*u_hd)(void*, const char*, size_t) = (int (*)(void*, const char*, size_t))dlsym(h, "uchardet_handle_data");
    void (*u_end)(void*) = (void (*)(void*))dlsym(h, "uchardet_data_end");
    const char* (*u_get)(void*) = (const char* (*)(void*))dlsym(h, "uchardet_get_charset");
    if (!(u_new && u_del && u_hd && u_end && u_get)) { dlclose(h); return QString(); }
    void* ud = u_new();
    if (!ud) { dlclose(h); return QString(); }
    QString res;
    if (u_hd(ud, (const char*)raw.data(), (size_t)raw.size()) == 0) {
        u_end(ud);
        const char* cs = u_get(ud);
        if (cs && cs[0]) { res = lpNormalizeCharset(QString::fromUtf8(cs)); }
    }
    u_del(ud);
    dlclose(h);
    return res;
}

#ifdef Q_OS_ANDROID
// Android bionic 无可用 iconv（API<28 无声明；API≥28 也仅支持 utf/ascii/utf16/utf32）。
// 改用 Java java.nio.charset（ICU4J，覆盖 GBK/GB18030/Big5/Shift_JIS/windows-1252 等）。
// Java 侧内部捕获异常，失败返回 null → 此处返回空 QByteArray，交由上层兜底。
static QByteArray lpIconvToUtf8(const QByteArray& in, const QString& charset) {
    if (in.isEmpty()) { return in; }
    QJniEnvironment env;
    JNIEnv* jenv = env.jniEnv();
    if (!jenv) { return QByteArray(); }
    jbyteArray jbytes = jenv->NewByteArray((jsize)in.size());
    if (!jbytes) { return QByteArray(); }
    jenv->SetByteArrayRegion(jbytes, 0, (jsize)in.size(), (const jbyte*)in.data());
    QJniObject jcs = QJniObject::fromString(charset);
    QJniObject result = QJniObject::callStaticObjectMethod(
        "io/fedlet/mobutil/MobUtil", "convertToUtf8",
        "([BLjava/lang/String;)[B",
        jbytes, jcs.object<jstring>());
    jenv->DeleteLocalRef(jbytes);
    if (!result.isValid()) { return QByteArray(); }
    jbyteArray jout = result.object<jbyteArray>();
    if (!jout) { return QByteArray(); }
    jsize len = jenv->GetArrayLength(jout);
    if (len <= 0) { return QByteArray(); }
    QByteArray out;
    out.resize((int)len);
    jenv->GetByteArrayRegion(jout, 0, len, (jbyte*)out.data());
    return out;
}
#else
static QByteArray lpIconvToUtf8(const QByteArray& in, const QString& charset) {
    if (in.isEmpty()) { return in; }
    QByteArray from = qToUtf8(charset);
    iconv_t cd = iconv_open("UTF-8", from.data());
    if (cd == (iconv_t)-1) { return QByteArray(); }
    QByteArray out;
    out.resize(in.size() * 2 + 32);
    char* inPtr = (char*)in.data();
    size_t inLeft = (size_t)in.size();
    char* outPtr = out.data();
    size_t outLeft = (size_t)out.size();
    size_t r;
    while ((r = iconv(cd, &inPtr, &inLeft, &outPtr, &outLeft)) == (size_t)-1) {
        if (errno == E2BIG) {
            int used = (int)(outPtr - out.data());
            out.resize(out.size() * 2);
            outPtr = out.data() + used;
            outLeft = (size_t)(out.size() - used);
            continue;
        }
        if (outLeft >= 3) {
            outPtr[0] = (char)0xEF;
            outPtr[1] = (char)0xBF;
            outPtr[2] = (char)0xBD;
            outPtr += 3;
            outLeft -= 3;
        }
        if (inLeft == 0) { break; }
        inPtr++;
        inLeft--;
    }
    iconv_close(cd);
    int used = (int)(outPtr - out.data());
    out.resize(used);
    return out;
}
#endif

static bool lpIsValidUtf8(const QByteArray& b) {
    const unsigned char* p = (const unsigned char*)b.data();
    int n = b.size();
    int i = 0;
    while (i < n) {
        unsigned char c = p[i];
        int need;
        if (c < 0x80) { i++; continue; }
        else if ((c & 0xE0) == 0xC0) { need = 1; }
        else if ((c & 0xF0) == 0xE0) { need = 2; }
        else if ((c & 0xF8) == 0xF0) { need = 3; }
        else { return false; }
        if (i + need >= n) { return false; }
        for (int j = 1; j <= need; ++j) {
            if ((p[i + j] & 0xC0) != 0x80) { return false; }
        }
        i += need + 1;
    }
    return true;
}

QByteArray linkPreviewDecodeToUtf8(const QByteArray& raw, const QString& httpContentType) {
    if (raw.isEmpty()) { return raw; }
    QString bom = lpDetectBom(raw);
    int bomLen = lpBomLen(bom);
    QString cs = lpCharsetFromContentType(httpContentType);
    if (cs.isEmpty()) { cs = lpMetaPrescanCharset(raw); }
    if (cs.isEmpty()) { cs = lpDetectByUchardet(raw); }
    if (cs.isEmpty()) { cs = "utf-8"; }
    if (!bom.isEmpty()) { cs = bom; }
    QByteArray body = (bomLen > 0 && raw.size() > bomLen)
                        ? lpMidArr(raw, bomLen, (int)raw.size() - bomLen) : raw;

    // 低信任的西文单字节声明：若正文实为合法 UTF-8，则优先按 UTF-8 解析。
    // 服务器常见错误：声明 ISO-8859-1/windows-1252/US-ASCII 而实际交付 UTF-8。
    // 误判极低：真单字节文本（如含 0x93/0x94 智能引号）几乎不可能整篇构成合法 UTF-8。
    if (cs == "windows-1252" || cs == "iso-8859-1" || cs == "iso-8859-15"
        || cs == "cp1252" || cs == "us-ascii" || cs == "ascii") {
        if (lpIsValidUtf8(body)) { return body; }
    }

    // 主编码尝试
    if (cs == "utf-8") {
        if (lpIsValidUtf8(body)) { return body; }
    } else {
        QByteArray out = lpIconvToUtf8(body, cs);
        if (!out.isEmpty()) { return out; }
    }

    // 主编码失败（声明为 UTF-8 实为其它编码 / charset 非法未知）：
    // 先信页面自身 meta 声明，再用 uchardet 兜底。
    QString alt = lpMetaPrescanCharset(raw);
    if (alt.isEmpty() || alt == cs) { alt = lpDetectByUchardet(body); }
    if (!alt.isEmpty() && alt != cs && alt != "utf-8") {
        QByteArray out = lpIconvToUtf8(body, alt);
        if (!out.isEmpty()) { return out; }
    }

    // 仍是 UTF-8 但含非法字节：用 iconv 清理为 U+FFFD
    QByteArray clean = lpIconvToUtf8(body, "utf-8");
    return clean.isEmpty() ? body : clean;
}

#ifdef LINKPREVIEW_NO_GUMBO

// ── 内置浅扫描器（Android 无 gumbo 时使用）──
// 不建 DOM；跳过 <script>/<style>，避免其中的伪 <meta>/<title> 被误取。
static inline bool lpStartsWithChar(const QString& s, QChar c) {
    return s.length() > 0 && s[0] == c;
}

static QString lpDecodeEntities(const QString& in) {
    if (lpFind(in, QChar('&')) < 0) { return in; }
    QString out;
    out.reserve(in.length());
    int i = 0;
    int n = in.length();
    while (i < n) {
        QChar ch = in[i];
        if (ch != QChar('&')) { out += ch; i++; continue; }
        int semi = lpFind(in, QChar(';'), i + 1);
        if (semi < 0 || semi - i > 12) { out += ch; i++; continue; }
        QString ent = lpLower(in.mid(i + 1, semi - i - 1));
        bool handled = true;
        if (ent == "amp") { out += QChar('&'); }
        else if (ent == "lt") { out += QChar('<'); }
        else if (ent == "gt") { out += QChar('>'); }
        else if (ent == "quot") { out += QChar('"'); }
        else if (ent == "apos" || ent == "39") { out += QChar(0x27); }
        else if (ent == "nbsp") { out += QChar(' '); }
        else if (!ent.isEmpty() && ent[0] == QChar('#')) {
            bool hex = ent.length() > 1 && ent[1] == QChar('x');
            QString num = ent.mid(hex ? 2 : 1);
            bool ok = false;
            uint code = hex ? num.toUInt(&ok, 16) : num.toUInt(&ok, 10);
            if (ok) { out += QChar((ushort)code); } else { handled = false; }
        } else { handled = false; }
        if (handled) { i = semi + 1; } else { out += ch; i++; }
    }
    return out;
}

static QString lpAttrValue(const QString& tag, const QString& attrName) {
    QString lower = lpLower(tag);
    QString key = lpLower(attrName);
    int n = lower.length();
    int k = key.length();
    int i = 0;
    while (i + k <= n) {
        int pos = lpFind(lower, key, i);
        if (pos < 0) { return QString(); }
        bool beforeOk = (pos == 0)
            || !(lower[pos - 1].isLetterOrNumber()
                 || lower[pos - 1] == QChar('-') || lower[pos - 1] == QChar(':'));
        int e = pos + k;
        while (e < n && lower[e].isSpace()) { e++; }
        if (beforeOk && e < n && lower[e] == QChar('=')) {
            e++;
            while (e < n && lower[e].isSpace()) { e++; }
            if (e < n && (tag[e] == QChar('"') || tag[e] == QChar('\''))) {
                QChar q = tag[e];
                int end = lpFind(tag, q, e + 1);
                if (end < 0) { return tag.mid(e + 1); }
                return tag.mid(e + 1, end - e - 1);
            }
            int end = e;
            while (end < n && !tag[end].isSpace() && tag[end] != QChar('>')) { end++; }
            return tag.mid(e, end - e);
        }
        i = pos + k;
    }
    return QString();
}

static void lpScanPreview(const QString& s, LinkPreviewData& out) {
    QString titleTag, ogTitle, twTitle, ogDesc, twDesc, metaDesc, site;
    int n = s.length();
    int i = 0;
    while (i < n) {
        int lt = lpFind(s, QChar('<'), i);
        if (lt < 0) { break; }
        int gt = lpFind(s, QChar('>'), lt + 1);
        if (gt < 0) { break; }
        QString tag = s.mid(lt + 1, gt - lt - 1);
        i = gt + 1;

        QString lower = lpLower(tag);
        if (lpStartsWithChar(lower, QChar('/')) || lpStartsWithChar(lower, QChar('!'))
            || lpStartsWithChar(lower, QChar('?'))) { continue; }
        int p = 0;
        while (p < lower.length() && lower[p].isLetterOrNumber()) { p++; }
        QString name = lower.left(p);

        if (name == "script" || name == "style") {
            int close = lpFindCI(s, QString("</") + name, i);
            if (close >= 0) { i = close; }
            continue;
        }
        if (name == "title" && titleTag.isEmpty()) {
            int close = lpFindCI(s, QString("</title"), i);
            if (close >= 0) {
                titleTag = lpCollapseWs(lpDecodeEntities(s.mid(i, close - i)));
                i = close;
            }
            continue;
        }
        if (name == "meta") {
            QString prop = lpAttrValue(tag, "property");
            if (prop.isEmpty()) { prop = lpAttrValue(tag, "name"); }
            QString content = lpAttrValue(tag, "content");
            if (prop.isEmpty() || content.isEmpty()) { continue; }
            QString key = lpLower(prop);
            QString v = lpCollapseWs(lpDecodeEntities(content));
            if (v.isEmpty()) { continue; }
            if (key == "og:title") { if (ogTitle.isEmpty()) { ogTitle = v; } }
            else if (key == "twitter:title") { if (twTitle.isEmpty()) { twTitle = v; } }
            else if (key == "og:description") { if (ogDesc.isEmpty()) { ogDesc = v; } }
            else if (key == "twitter:description") { if (twDesc.isEmpty()) { twDesc = v; } }
            else if (key == "og:site_name") { if (site.isEmpty()) { site = v; } }
            else if (key == "description") { if (metaDesc.isEmpty()) { metaDesc = v; } }
            continue;
        }
    }
    out.title = !ogTitle.isEmpty() ? ogTitle : (!twTitle.isEmpty() ? twTitle : titleTag);
    out.description = !ogDesc.isEmpty() ? ogDesc : (!twDesc.isEmpty() ? twDesc : metaDesc);
    out.siteName = site;
    out.ok = !(out.title.isEmpty() && out.description.isEmpty());
}

#else  // gumbo

static QString lpCollectText(const GumboNode* root) {
    QString out;
    std::vector<const GumboNode*> st;
    st.push_back(root);
    while (!st.empty()) {
        const GumboNode* n = st.back();
        st.pop_back();
        if (!n) { continue; }
        if (n->type == GUMBO_NODE_TEXT || n->type == GUMBO_NODE_CDATA) {
            if (n->v.text.text) { out += QString::fromUtf8(n->v.text.text); }
        } else if (n->type == GUMBO_NODE_ELEMENT) {
            const GumboVector& c = n->v.element.children;
            for (unsigned int i = 0; i < c.length; ++i) { st.push_back((GumboNode*)c.data[i]); }
        }
    }
    return out;
}

static void lpWalkPreview(const GumboNode* root, LinkPreviewData& out) {
    QString titleTag, ogTitle, twTitle, ogDesc, twDesc, metaDesc, site;
    bool gotTitle = false;
    std::vector<const GumboNode*> st;
    st.push_back(root);
    while (!st.empty()) {
        const GumboNode* n = st.back();
        st.pop_back();
        if (!n) { continue; }
        if (n->type == GUMBO_NODE_DOCUMENT) {
            const GumboVector& c = n->v.document.children;
            for (unsigned int i = 0; i < c.length; ++i) { st.push_back((GumboNode*)c.data[i]); }
            continue;
        }
        if (n->type != GUMBO_NODE_ELEMENT) { continue; }
        GumboTag tag = n->v.element.tag;
        if (tag == GUMBO_TAG_META) {
            const GumboVector& attrs = n->v.element.attributes;
            GumboAttribute* p = gumbo_get_attribute(&attrs, "property");
            if (!p) { p = gumbo_get_attribute(&attrs, "name"); }
            GumboAttribute* c = gumbo_get_attribute(&attrs, "content");
            if (p && c && p->value && c->value) {
                QString k = lpLower(QString::fromUtf8(p->value));
                QString v = lpCollapseWs(QString::fromUtf8(c->value));
                if (!v.isEmpty()) {
                    if (k == "og:title" && ogTitle.isEmpty()) { ogTitle = v; }
                    else if (k == "twitter:title" && twTitle.isEmpty()) { twTitle = v; }
                    else if (k == "og:description" && ogDesc.isEmpty()) { ogDesc = v; }
                    else if (k == "twitter:description" && twDesc.isEmpty()) { twDesc = v; }
                    else if (k == "og:site_name" && site.isEmpty()) { site = v; }
                    else if (k == "description" && metaDesc.isEmpty()) { metaDesc = v; }
                }
            }
        } else if (tag == GUMBO_TAG_TITLE && !gotTitle) {
            titleTag = lpCollapseWs(lpCollectText(n));
            gotTitle = true;
        }
        const GumboVector& ch = n->v.element.children;
        for (unsigned int i = 0; i < ch.length; ++i) { st.push_back((GumboNode*)ch.data[i]); }
    }
    out.title = !ogTitle.isEmpty() ? ogTitle : (!twTitle.isEmpty() ? twTitle : titleTag);
    out.description = !ogDesc.isEmpty() ? ogDesc : (!twDesc.isEmpty() ? twDesc : metaDesc);
    out.siteName = site;
    out.ok = !(out.title.isEmpty() && out.description.isEmpty());
}

#endif  // LINKPREVIEW_NO_GUMBO

bool parseLinkPreviewHtml(const QByteArray& body, const QString& httpContentType,
                          const QString& urlKey, LinkPreviewData& out) {
    out = LinkPreviewData();
    out.url = linkPreviewNormalizeUrl(urlKey);
    if (body.isEmpty()) { return false; }
    QByteArray utf8 = linkPreviewDecodeToUtf8(body, httpContentType);
    if (utf8.isEmpty()) { return false; }
    if (utf8.size() > 400 * 1024) { utf8.resize(400 * 1024); }
#ifdef LINKPREVIEW_NO_GUMBO
    QString s = QString::fromUtf8(utf8.data(), utf8.size());
    lpScanPreview(s, out);
    return out.ok;
#else
    GumboOutput* g = gumbo_parse_with_options(
        &kGumboDefaultOptions, utf8.data(), (size_t)utf8.size());
    if (!g) { return false; }
    if (g->root) { lpWalkPreview(g->root, out); }
    gumbo_destroy_output(&kGumboDefaultOptions, g);
    return out.ok;
#endif
}

QString linkPreviewNormalizeUrl(const QString& url) {
    QString u = lpTrim(url);
    int hash = lpFind(u, QChar('#'));
    if (hash >= 0) { u = u.left(hash); }
    return u;
}

static std::string lpKey(const QString& urlKey) {
    QByteArray b = qToUtf8(urlKey);
    return std::string(b.data(), (size_t)b.size());
}

LinkPreviewCache& LinkPreviewCache::inst() {
    static LinkPreviewCache s;
    return s;
}

bool LinkPreviewCache::get(const QString& urlKey, LinkPreviewData& out) {
    std::map<std::string, Entry>::iterator it = m_map.find(lpKey(urlKey));
    if (it == m_map.end()) { return false; }
    if (!it->second.data.ok && (std::time(0) - it->second.stamp) > kNegativeTtlSec) {
        m_map.erase(it);
        return false;
    }
    out = it->second.data;
    return true;
}

void LinkPreviewCache::put(const QString& urlKey, const LinkPreviewData& data) {
    Entry e;
    e.data = data;
    e.stamp = std::time(0);
    m_map[lpKey(urlKey)] = e;
    evictIfNeeded();
}

bool LinkPreviewCache::isInflight(const QString& urlKey) const {
    return m_inflight.find(lpKey(urlKey)) != m_inflight.end();
}
void LinkPreviewCache::markInflight(const QString& urlKey) { m_inflight.insert(lpKey(urlKey)); }
void LinkPreviewCache::clearInflight(const QString& urlKey) { m_inflight.erase(lpKey(urlKey)); }
void LinkPreviewCache::clearAll() { m_map.clear(); m_inflight.clear(); }

void LinkPreviewCache::evictIfNeeded() {
    while (m_map.size() > kCapacity) {
        std::map<std::string, Entry>::iterator oldest = m_map.begin();
        for (std::map<std::string, Entry>::iterator it = m_map.begin(); it != m_map.end(); ++it) {
            if (it->second.stamp < oldest->second.stamp) { oldest = it; }
        }
        m_map.erase(oldest);
    }
}
