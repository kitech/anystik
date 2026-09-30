#ifndef QLSTIK_QREGULAREXPRESSION_SHIM_H
#define QLSTIK_QREGULAREXPRESSION_SHIM_H

// QRegularExpression 是 Qt 5.0 才引入的（Qt3.5 与 Qt4.8 的 QtCore 下都没有）。
// Qt3 侧用 QRegExp 作后端垫出同名 API，供 sitelistclient（HTML <img> 抓取）
// 与 imagesearchclient（Bing/Yandex 的 m= / data-bem= 属性里嵌 JSON）解析使用。
//
// ★ 只覆盖两个消费方**实际用到**的方法面（实测自 anystik/src/sitelistclient.cpp
//   与 anystik/src/imagesearchclient.cpp，qt-5.15 与 qt-6.x 同）：
//     构造(pat, options) / globalMatch(text) / match(text) /
//     迭代器 hasNext()+next() / 匹配对象 hasMatch()+captured(int)
//   未覆盖：captured(QString)/namedCaptures/captureCount/regularExpression()/
//   split() 等。缺什么再补，不预先扩张。
//
// ★★ 两条已实测的引擎差异（不是 bug，改写也消不掉，故在此写明）：
//
// 1) `.` 在 QRegExp 下**默认就匹配换行**；PCRE 需 DotMatchesEverythingOption。
//    故该 option 在本垫片里是**已经生效的空操作**。两个消费方都传了它
//    （sitelistclient 传 CaseInsensitive，imagesearchclient 两者都传），
//    方向一致，无影响。HTML 属性值内不含裸换行，行为等价。
//
// 2) QRegExp 无「惰性量词」语法糖以外的差异已被 qreRewriteLazy 消化，
//    但 **QRegExp::setMinimal(TRUE) 是整模式开关**，会把模式里所有量词都
//    改成最小匹配，无法表达「同一模式内既有贪婪又有懒惰」。故本垫片走
//    逐量词改写（见 qreRewriteLazy），不依赖 setMinimal。

#ifndef QT_VERSION
#include <qglobal.h>
#endif

#if QT_VERSION < 0x050000

#include <qregexp.h>
#include <qstring.h>
#include <qstringlist.h>
#include <vector>            // Match 快照存各捕获的起点/长度

namespace qlstik_qt3 {

// ── PatternOptions ───────────────────────────────────────────────────
// Qt3 完全没有 QFlags / Q_DECLARE_FLAGS（实测 /opt/qt338sh/include 全目录
// grep 零命中；Qt4.0 才引入）。这里给最小 flags 载体：只需支持从枚举隐式
// 构造、`&` 单个枚举、`==`。之所以必须是独立类型而非裸 int：调用点写
//     QRegularExpression(pat, QRegularExpression::CaseInsensitiveOption)
// 第二参数在 Qt5 原生类型是 QFlags<PatternOption>，签名须与之兼容。
class PatternOptions
{
public:
    PatternOptions() : m_v(0) {}
    PatternOptions(int v) : m_v(v) {}          // 枚举/int → flags 隐式构造
    int toInt() const { return m_v; }
    bool testFlag(int e) const { return (m_v & e) != 0; }
    bool operator==(const PatternOptions& o) const { return m_v == o.m_v; }
    bool operator!=(const PatternOptions& o) const { return m_v != o.m_v; }
private:
    int m_v;
};

// 懒惰量词改写：`ATOM *?`（ATOM 为单字符原子，且后继为单字符字面量 c）
// 等价于 `ATOM [^c]*`，随后 c 仍由主循环输出。
//
// 依据（实测）：PCRE 惰性 = 尽量少匹配后要求余下模式成立。
//   `m="(.*?)"` 于 `x m="aa" y m="bb" z`
//     PCRE   → cap1 = "aa"
//     `m="([^"]*)"`（Qt3 QRegExp）→ cap1 = "aa"   （实测一致）
//   `a*?b` 于 `aab`
//     PCRE   → cap = "aa"
//     `a[^b]*b`（QRegExp）        → cap = "aa"   （等价：a 之后不能再有 b）
//
// 不改写的情形（保守，交由 isValid()/errorString() 暴露，绝不静默给错结果）：
//   * 后继不是单字符字面量（是 \x / ( / [ / . 或模式末尾）
//   * 原子不是单字符（是分组 (...) 或紧跟量词的字符）
//   * 懒惰量词带 {n,m} 数值区间（贪心下界会算错）
// 注意：**捕获组内部的懒惰量词必须照常改写**。imagesearchclient 的真实模式是
//   m="(.*?)" / data-bem="(.*?)"，`*?` 正在组内。实测「整组跳过」实现时
//   isValid()==0、errorString()=="bad repetition syntax"，整个解析静默失效。
//   改写只把量词换成 `[^c]*`，不新增/删除任何括号，故组编号天然不变。
class QRegularExpression
{
public:
    enum PatternOption {
        NoPatternOption                = 0x0000,
        CaseInsensitiveOption          = 0x0001,
        DotMatchesEverythingOption     = 0x0002,
        MultilineOption                = 0x0004
    };

    QRegularExpression(const QString& pattern,
                       PatternOptions options = PatternOptions())
        : m_pattern(pattern)
        , m_options(options)
    {
        QRegExp rx(qreRewriteLazy(pattern),
                   // QRegExp 第二参是 caseSensitive（默认 TRUE），与 Qt5 相反
                   !options.testFlag(CaseInsensitiveOption));
        m_rx = new QRegExp(rx);
    }

    ~QRegularExpression() { delete m_rx; }

    // 内部持 QRegExp 堆指针，浅拷贝会双重释放，故禁拷贝
    QRegularExpression(const QRegularExpression&) = delete;
    QRegularExpression& operator=(const QRegularExpression&) = delete;

    QString pattern() const { return m_pattern; }
    // 实际交给 QRegExp 的模式（经懒惰量词改写）。Qt5 原生类无对应方法，
    // 仅为测试与排错暴露：QRegExp 静默失配时，靠它能一眼看出改写成什么。
    QString effectivePattern() const { return m_rx->pattern(); }
    PatternOptions patternOptions() const { return m_options; }
    bool isValid() const { return m_rx->isValid(); }
    QString errorString() const { return m_rx->errorString(); }

    // 匹配快照：文本 + 位置。QRegExp 的 cap()/pos() 都是读**当前匹配状态**的
    // 非 const 方法，状态一被下一次 search() 推进就全变，故必须整体拷贝。
    struct Snapshot {
        bool ok;
        int start;      // 整段起点
        int length;     // 整段长度
        QStringList texts;
        std::vector<int> starts;    // 各捕获起点
        std::vector<int> lengths;   // 各捕获长度（UTF-16 码元，与 Qt6 同）
    };

    static Snapshot qreSnapshot(QRegExp* rx, int at)
    {
        Snapshot s;
        s.ok = true;
        s.start = at;
        s.length = rx->matchedLength();
        const int n = rx->numCaptures();
        // 注意：Qt3 的 QStringList **没有 reserve()**（实测编译报
        // 'class QStringList' has no member named 'reserve'），只有
        // std::vector 那两个能 reserve。
        s.starts.reserve(n + 1);
        s.lengths.reserve(n + 1);
        for (int i = 0; i <= n; ++i) {
            const QString c = rx->cap(i);
            s.texts << c;
            s.starts.push_back(rx->pos(i));
            s.lengths.push_back(c.length());
        }
        return s;
    }

    // 匹配结果。**在构造时就把捕获快照下来**，不持有 QRegExp 指针。
    //
    // ★ 这里踩过一个坑：初版 Match 持 `const QRegExp*` + const_cast 调
    //   cap()，以为「cap() 只读 priv、不推进匹配位置」所以延迟读也安全。
    //   实测不成立 —— QRegExp 的匹配状态存在同一个对象里，迭代器 next()
    //   一 seek，**先前返回的那个 Match 再读 captured() 就拿到下一次匹配的
    //   捕获**。探针现象：模式 m="(.*?)" 于 `x m="aa" y m="bb" z`
    //   应得 [aa] [bb]，实际输出 [bb] []（第 1 次读到第 2 次的值，
    //   第 2 次已无匹配故为空）。改成快照后正确。
    class Match
    {
    public:
        Match() : m_ok(false), m_start(-1), m_length(-1) {}
        explicit Match(const Snapshot& s)
            : m_ok(s.ok), m_start(s.start), m_length(s.length)
            , m_texts(s.texts), m_starts(s.starts), m_lengths(s.lengths) {}

        bool hasMatch() const { return m_ok; }
        bool isValid() const { return m_ok; }
        QString captured(int nth = 0) const
        {
            if (!m_ok || nth < 0 || nth >= (int)m_texts.size()) return QString();
            return m_texts[nth];
        }
        // 签名与 Qt6 对齐（默认参数 = 整段）。
        //
        // ★ 与 Qt6 的差异：**未参与匹配的捕获**，Qt6 返回 -1，Qt3 的
        //   QRegExp::pos() 返回 0（实测 `(a)|(b)` 匹配 "b" 时 cap1 → pos=0
        //   len=0，而 cap2 → pos=0 len=1；cap(1) 是空串）。QRegExp 无从区分
        //   「未参与」与「参与了但匹配到空串」，故本垫片照实返回 Qt3 的值，
        //   不伪造 -1。判断请用 captured(nth).isEmpty()，别用 pos<0。
        //   越界 nth 一律返回 -1（实测 capturedStart(9) == -1）。
        int capturedStart(int nth = 0) const
        {
            if (!m_ok || nth < 0 || nth >= (int)m_starts.size()) return -1;
            return m_starts[nth];
        }
        int capturedLength(int nth = 0) const
        {
            if (!m_ok || nth < 0 || nth >= (int)m_lengths.size()) return -1;
            return m_lengths[nth];
        }
        int captureCount() const { return m_ok ? (int)m_texts.size() - 1 : 0; }
        QStringList capturedTexts() const { return m_texts; }

    private:
        bool m_ok;
        int m_start;
        int m_length;
        QStringList m_texts;
        std::vector<int> m_starts;
        std::vector<int> m_lengths;
    };

    // 全局匹配迭代器。推进方式 = 命中位置 + matchedLength()，与 Qt5
    // QRegularExpression::globalMatch 的推进语义一致（实测 m="(.*?)" 于
    // `x m="aa" y m="bb" z` 得 2 次，依次 aa / bb）。
    class MatchIterator
    {
    public:
        MatchIterator() : m_rx(0), m_offset(0) {}
        MatchIterator(const QRegExp* rx, const QString& text, int offset)
            : m_rx(rx), m_text(text), m_offset(offset), m_pending(seek()) {}

        bool hasNext() const { return m_pending.hasMatch(); }

        Match next()
        {
            if (!m_pending.hasMatch()) return Match();
            const Match m = m_pending;
            m_offset += m.capturedLength();
            m_pending = seek();
            return m;
        }

        Match peekNext() const { return m_pending; }

    private:
        // 每次 search 后立刻把捕获快照成 Match（见上方 Match 的坑注释）
        Match seek()
        {
            if (!m_rx) return Match();
            const int at = m_rx->search(m_text, m_offset);
            if (at == -1) return Match();
            return Match(qreSnapshot(const_cast<QRegExp*>(m_rx), at));
        }

        const QRegExp* m_rx;
        QString m_text;
        int m_offset;
        Match m_pending;
    };

    // match()/globalMatch() 必须是 const：调用点是 `static const
    // QRegularExpression re(...)`（imagesearchclient / sitelistclient 都这么写，
    // 静态局部量省得每次重建正则）。Qt5+ 原生 QRegularExpression 的这两个方法
    // 也是 const，调用点因此无需版本分支。
    Match match(const QString& text, int offset = 0) const
    {
        const int at = m_rx->search(text, offset);
        if (at == -1) return Match();
        return Match(qreSnapshot(const_cast<QRegExp*>(m_rx), at));
    }

    MatchIterator globalMatch(const QString& text, int offset = 0) const
    {
        return MatchIterator(m_rx, text, offset);
    }

private:
    // ── 懒惰量词改写 ────────────────────────────────────────────────
    // 改写 `ATOM *?` → `ATOM [^c]*`（c = 后继单字符字面量）。
    //
    // 关键：必须**递归进入捕获组**。真实消费点 imagesearchclient 的模式是
    //     m="(.*?)"
    // 其 `*?` 正在组内（实测：按「整组跳过、不改写」实现时 isValid()==0、
    // errorString()=="bad repetition syntax"，整个解析静默失效）。
    //
    // 递归不会打乱捕获组编号：改写只把 `*?` 换成 `[^c]*`，**不新增/删除
    // 任何括号**，故 group(n) 的对应关系不变。
    //
    // 不改写的情形（保守保留原文，由 isValid()/errorString() 暴露失败，
    // 绝不静默给出错误的贪婪结果）：
    //   * 后继不是单字符字面量（是 \x / ( / [ / . 等）
    //   * 懒惰量词带 {n,m} 数值区间（前一个字符是 '}'，此处不识别为量词）
    static QString qreRewriteLazy(const QString& in)
    {
        QString out;
        out.reserve(in.length() + 8);
        qreRewriteInto(in, out, 0, in.length());
        return out;
    }

    // 单趟线性扫描（**不递归**）。捕获组的 '(' / ')' 按普通字符拷贝即可：
    // 改写只把 `*?` 换成 `[^c]*`，不新增/删除括号，捕获组编号天然不变。
    // atomStart：out 中「当前待量化原子」的起始下标。命中懒惰量词时要先把
    // 这个原子**从 out 里截掉**，再写 `[^c]*`——否则会得到 `(.[^"]*)`
    // （`.` 残留，语义变成「任意 1 字符 + 非引号串」）而丢掉捕获内容。
    // 分组 `(ab)` 是多字符原子，本函数不量化它（atomStart 记 -1）。
    static void qreRewriteInto(const QString& in, QString& out, int from, int to)
    {
        int i = from;
        int atomStart = -1;
        while (i < to) {
            const QChar c = in.at(i);

            if (c == QChar('[')) {                 // 字符类：单字符原子
                const int nx = qreSkipClass(in, i, to);
                atomStart = out.length();
                out += in.mid(i, nx - i);
                i = nx;
                continue;
            }
            if (c == QChar('\\')) {                // 转义序列：整体是一个原子
                atomStart = out.length();
                out += c;
                if (i + 1 < to) out += in.at(i + 1);
                i += 2;
                continue;
            }
            if (c == QChar('*') || c == QChar('+') || c == QChar('?')) {
                QChar lit;
                if (atomStart >= 0 && qreLazySuccessor(in, i, to, lit)) {
                    // `*?` / `??` 允许 0 次：原子本体可整体丢掉，等价于
                    // 「任意非 c 字符」。`+?` 至少 1 次：**必须保留原子**，
                    // 否则 `a+?b` 会被改成 `[^b]*b`，在 "b" 上误配（PCRE
                    // 要求至少一个 a，实测这是 `+?` 与 `*?` 的唯一差别）。
                    if (c != QChar('+')) {
                        out.truncate(atomStart);
                    }
                    out += QChar('[');
                    out += QChar('^');
                    qreAppendEscapedInClass(out, lit);
                    out += QChar(']');
                    out += QChar('*');
                    atomStart = -1;
                    i += 2;                        // 吃掉 "量词" 与 "?"
                    continue;                      // 后继字面量留给本层循环
                }
                atomStart = -1;                   // 贪婪量词：其结果不是可量化原子
                out += c;
                ++i;
                continue;
            }
            if (c == QChar('(') || c == QChar(')') || c == QChar('^')
                || c == QChar('$') || c == QChar('|')) {
                // 分组括号与断言：零宽，不是可量化原子。
                // 这里**不跳组**——线性扫描自然走进组内，组里的懒惰量词
                // 才能被改写（imagesearchclient 的 `m="(.*?)"` 正在组内）。
                // 改写不新增/删除括号，捕获组编号因此天然不变。
                atomStart = -1;
                out += c;
                ++i;
                continue;
            }
            atomStart = out.length();              // 普通字面量：单字符原子
            out += c;
            ++i;
        }
    }

    // in[i] 是量词字符。判定它是否为懒惰量词，并取出「后继字面量」到 lit。
    //
    // ★ 关键：后继字面量要**穿过捕获组的右括号**去找。真实消费点
    // imagesearchclient 的模式是 m="(.*?)"，其 `*?` 位于组内末尾，直接看
    // 下一个字符只会看到 ')'（实测：按「不穿透」实现时 isValid()==0、
    // errorString()=="bad repetition syntax"，整个解析静默失效）。
    // PCRE 里 ')' 是零宽断言，直接跳过即可找到真正的后继字符。
    static bool qreLazySuccessor(const QString& in, int i, int to, QChar& lit)
    {
        if (i + 1 >= to || in.at(i + 1) != QChar('?')) return false;
        int j = i + 2;
        while (j < to && in.at(j) == QChar(')')) ++j;   // 穿过组右括号
        if (j >= to) return false;                      // 无后继，不改写
        lit = in.at(j);
        if (lit == QChar('\\') || lit == QChar('(') || lit == QChar('[')
            || lit == QChar(']') || lit == QChar('^') || lit == QChar('*')
            || lit == QChar('+') || lit == QChar('?') || lit == QChar('|')
            || lit == QChar('{') || lit == QChar('}') || lit == QChar(')')) {
            return false;      // 后继非单字符字面量，不改写
        }
        return true;
    }

    // 跳过字符类，返回 ']' 之后下标（未闭合则 to）
    static int qreSkipClass(const QString& s, int i, int to)
    {
        ++i;                                        // 越过 '['
        if (i < to && s.at(i) == QChar('^')) ++i;
        if (i < to && s.at(i) == QChar(']')) ++i;   // 类首 ']' 是字面量
        while (i < to) {
            if (s.at(i) == QChar('\\')) { i += 2; continue; }
            if (s.at(i) == QChar(']')) return i + 1;
            ++i;
        }
        return to;
    }

    // 把 c 追加进字符类上下文：需要转义时先补反斜杠
    static void qreAppendEscapedInClass(QString& out, QChar c)
    {
        if (c == QChar('^') || c == QChar(']') || c == QChar('\\')
            || c == QChar('-') || c == QChar('[')) {
            out += QChar('\\');
        }
        out += c;
    }

    QString m_pattern;
    PatternOptions m_options;
    QRegExp* m_rx;
};

} // namespace qlstik_qt3

// 文本级替换：调用点写 QRegularExpression（Qt5+ 原生类），Qt3 侧解析到垫片。
// 必须在所有 Qt 头之后包含（与其他 *_shim.h 同规约）。
#define QRegularExpression qlstik_qt3::QRegularExpression

#endif // QT_VERSION < 0x050000

#endif // QLSTIK_QREGULAREXPRESSION_SHIM_H
