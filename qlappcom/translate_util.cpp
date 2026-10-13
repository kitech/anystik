#include "translate_util.h"
#include "compat34.h"
#ifdef QT3_BUILD
#include <qvaluevector.h>
#else
#include <vector>
#endif
#include <stdint.h>

enum CharClass {
    CC_LATIN, CC_CHINESE, CC_CJK_PUNCT, CC_FULLWIDTH,
    CC_JAPANESE, CC_KOREAN, CC_EMOJI,
    CC_NUMBER, CC_SPACE, CC_OTHER
};

struct CharResult {
    CharClass cls;
    int bytes;
};

static CharResult classifyChar(uint32_t cp) {
    int bytes = (cp < 0x80) ? 1 : (cp < 0x800) ? 2 : (cp < 0x10000) ? 3 : 4;

    // CC_LATIN: Latin letters + fullwidth letters
    if ((cp >= 0x41 && cp <= 0x5A) ||
        (cp >= 0x61 && cp <= 0x7A) ||
        (cp >= 0xC0 && cp <= 0x24FF) ||
        (cp >= 0x400 && cp <= 0x4FF) ||
        (cp >= 0xFF21 && cp <= 0xFF3A) ||
        (cp >= 0xFF41 && cp <= 0xFF5A))
        return {CC_LATIN, bytes};

    // CC_CHINESE: CJK ideographs
    if ((cp >= 0x3400 && cp <= 0x4DBF) ||
        (cp >= 0x4E00 && cp <= 0x9FFF) ||
        (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0x20000 && cp <= 0x2A6DF) ||
        (cp >= 0x2A700 && cp <= 0x2B73F) ||
        (cp >= 0x2B740 && cp <= 0x2B81F) ||
        (cp >= 0x2B820 && cp <= 0x2CEAF) ||
        (cp >= 0x2CEB0 && cp <= 0x2EBEF) ||
        (cp >= 0x2EBF0 && cp <= 0x2F7FF) ||
        (cp >= 0x2F800 && cp <= 0x2FA1F))
        return {CC_CHINESE, bytes};

    // CC_CJK_PUNCT: CJK symbols & punctuation, CJK compatibility forms
    if ((cp >= 0x3000 && cp <= 0x303F) ||
        (cp >= 0xFE30 && cp <= 0xFE4F))
        return {CC_CJK_PUNCT, bytes};

    // CC_FULLWIDTH: fullwidth symbols (not letters, not numbers)
    if ((cp >= 0xFF01 && cp <= 0xFF20) ||
        (cp >= 0xFF3B && cp <= 0xFF40) ||
        (cp >= 0xFF5B && cp <= 0xFF60))
        return {CC_FULLWIDTH, bytes};

    // CC_JAPANESE: Hiragana + Katakana + halfwidth Katakana
    if ((cp >= 0x3040 && cp <= 0x309F) ||
        (cp >= 0x30A0 && cp <= 0x30FF) ||
        (cp >= 0xFF61 && cp <= 0xFF9F))
        return {CC_JAPANESE, bytes};

    // CC_KOREAN: Hangul
    if ((cp >= 0x1100 && cp <= 0x11FF) ||
        (cp >= 0xAC00 && cp <= 0xD7AF) ||
        (cp >= 0xFFA0 && cp <= 0xFFDC))
        return {CC_KOREAN, bytes};

    // CC_EMOJI: ZWJ + Variation Selectors + Emoji symbols
    if (cp == 0x200D ||
        (cp >= 0xFE00 && cp <= 0xFE0F) ||
        (cp >= 0x2600 && cp <= 0x27BF) ||
        (cp >= 0x1F300 && cp <= 0x1FAFF) ||
        (cp >= 0x1FA70 && cp <= 0x1FAFF))
        return {CC_EMOJI, bytes};

    // CC_NUMBER: ASCII digits + fullwidth digits
    if ((cp >= 0x30 && cp <= 0x39) ||
        (cp >= 0xFF10 && cp <= 0xFF19))
        return {CC_NUMBER, bytes};

    // CC_SPACE: whitespace
    if (cp == 0x20 || cp == 0xA0 || cp == 0x3000 ||
        (cp >= 0x2002 && cp <= 0x200B) ||
        cp == 0x202F || cp == 0x205F)
        return {CC_SPACE, bytes};

    return {CC_OTHER, bytes};
}

struct NonTranslatableSpan {
    int start;
    int end;
};

#ifdef QT3_BUILD
typedef QValueVector<NonTranslatableSpan> SpanVector;
#define SpanVector_append(vec, item) (vec).append(item)
#define SpanVector_isEmpty(vec) (vec).isEmpty()
#else
typedef std::vector<NonTranslatableSpan> SpanVector;
#define SpanVector_append(vec, item) (vec).push_back(item)
#define SpanVector_isEmpty(vec) (vec).empty()
#endif

static SpanVector extractNonTranslatableSpans(const QString& text) {
    SpanVector spans;

Q36RegExp urlRe("https?://[^\\s<>\"']+", false);
    Q36RegExp mailRe("[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}", false);
    Q36RegExp idRe("@[a-zA-Z0-9._=-]+:[a-zA-Z0-9._=-]+", false);

    // URL（最高优先级，避免 URL 内嵌 Matrix ID 重复匹配）
    int pos = 0;
    while (true) {
#ifdef QT3_BUILD
        pos = urlRe.search(text, pos);
#else
        pos = urlRe.indexIn(text, pos);
#endif
        if (pos == -1) { break; }
        int len = urlRe.matchedLength();
        while (len > 0 && (text[pos+len-1] == '.' || text[pos+len-1] == ',' ||
               text[pos+len-1] == ';' || text[pos+len-1] == ':' ||
               text[pos+len-1] == '!' || text[pos+len-1] == '?' ||
               text[pos+len-1] == ')' || text[pos+len-1] == ']' ||
               text[pos+len-1] == '}' || text[pos+len-1] == '>')) {
            len--;
        }
        NonTranslatableSpan span = {pos, pos + len};
        SpanVector_append(spans, span);
        pos += len;
    }

    // 邮件
    pos = 0;
    while (true) {
#ifdef QT3_BUILD
        pos = mailRe.search(text, pos);
#else
        pos = mailRe.indexIn(text, pos);
#endif
        if (pos == -1) { break; }
        NonTranslatableSpan span = {pos, pos + mailRe.matchedLength()};
        SpanVector_append(spans, span);
        pos += mailRe.matchedLength();
    }

    // Matrix ID（@user:server）
    pos = 0;
    while (true) {
#ifdef QT3_BUILD
        pos = idRe.search(text, pos);
#else
        pos = idRe.indexIn(text, pos);
#endif
        if (pos == -1) { break; }
        NonTranslatableSpan span = {pos, pos + idRe.matchedLength()};
        SpanVector_append(spans, span);
        pos += idRe.matchedLength();
    }

    if (SpanVector_isEmpty(spans)) { return spans; }

    // 排序 + 合并重叠区间（冒泡排序，Qt3 无 qSort）
    for (int i = 0; i < spans.size(); ++i) {
        for (int j = i + 1; j < spans.size(); ++j) {
            if (spans[j].start < spans[i].start) {
                NonTranslatableSpan tmp = spans[i];
                spans[i] = spans[j];
                spans[j] = tmp;
            }
        }
    }
    SpanVector merged;
    SpanVector_append(merged, spans[0]);
    for (int i = 1; i < spans.size(); ++i) {
        NonTranslatableSpan& last = merged[merged.size() - 1];
        if (spans[i].start < last.end) {
            if (spans[i].end > last.end) { last.end = spans[i].end; }
        } else {
            SpanVector_append(merged, spans[i]);
        }
    }
    return merged;
}

static bool isInNonTranslatableSpan(int pos, const SpanVector& spans) {
    for (int i = 0; i < spans.size(); ++i) {
        if (pos < spans[i].start) { break; }
        if (pos < spans[i].end) { return true; }
    }
    return false;
}

bool isNeedTranslateToChinese(const QString& text) {
    SpanVector nonTransSpans = extractNonTranslatableSpans(text);
    int need = 0, noneed = 0;
    int spanIdx = 0;
    for (int i = 0; i < text.length(); ++i) {
        // 跳过不可翻译区间
        if (spanIdx < nonTransSpans.size()) {
            if (i >= nonTransSpans[spanIdx].end) { ++spanIdx; }
            if (spanIdx < nonTransSpans.size() &&
                i >= nonTransSpans[spanIdx].start && i < nonTransSpans[spanIdx].end) {
                uint32_t skipCp = text[i].unicode();
                if (skipCp >= 0xD800 && skipCp <= 0xDBFF && i + 1 < text.length()) { ++i; }
                continue;
            }
        }
        uint32_t cp = text[i].unicode();
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.length()) {
            uint32_t low = text[i + 1].unicode();
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            ++i;
        }
        CharResult r = classifyChar(cp);
        if (r.cls == CC_LATIN) {
            need += r.bytes;
        } else if (r.cls == CC_SPACE || r.cls == CC_NUMBER) {
            // 空格和数字不计入任何一组（英文特征但作为中性）
        } else if (r.cls == CC_JAPANESE || r.cls == CC_KOREAN) {
            need += r.bytes * 2;
        } else {
            noneed += r.bytes;
        }
    }
    if (need == 0 && noneed == 0) {
        qWarning("isNeedTranslateToChinese: need=%d noneed=%d result=NO_NEED(no text) text=[%.8192s]",
                 need, noneed, qToUtf8(text).data());
        return false;
    }
    float ratio = (float)noneed / (noneed + need);
    qWarning("isNeedTranslateToChinese: need=%d noneed=%d ratio=%.4f result=%s text=[%.8192s]",
             need, noneed, ratio,
             ratio < 0.27f ? "NEED" : "NO_NEED",
             qToUtf8(text).data());
    return ratio < 0.27f;
}

bool needsAutoTranslate(const QString& text, const QString& targetLang) {
    if (targetLang.startsWith("zh")) { return isNeedTranslateToChinese(text); }
    return true;
}
