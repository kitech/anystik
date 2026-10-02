// QNaturalSort 的 Qt3 垫片。
//
// vendor 原件：anystik/vendor/qwebdav/qnaturalsort.{h,cpp}（59 + 127 行，
// 由 anystik/CMakeLists.txt:115 编译进 anystik）。它不能直接用于 Qt3，原因有
// 三类，全部经核实（Qt3 头文件行号已注明）：
//
//  1. 模块头写法 `#include <QtCore>`（qnaturalsort.h:47）。Qt3 没有 QtCore/
//     QtNetwork 这类模块目录，`<QtCore>` 在 Qt3 头搜索路径下不存在 →
//     fatal error: QtCore: No such file or directory。同样的写法也在
//     qwebdav_global.h:4（`<QtCore/qglobal.h>`）。
//  2. Qt3 无 `Qt::CaseSensitivity` 枚举。qnamespace.h 里只有
//     `StringComparisonMode { CaseSensitive = 0x00001, ... }`（:990-995），
//     名字和类型都不同，且 CaseInsensitive 不在其中。
//  3. QString/QChar 接口差异：
//     - QChar 无 isLower() / toLower()，只有 `lower()`（qstring.h:190）。
//     - QString::compare 只有两参静态重载（qstring.h:682），没有带
//       大小写标志的第三参；caseSensitive 比较靠 QString::compare(s, bool)
//       的实例版本，本类需要的是 (s1,s2,cs) 三参形态。
//     - localeAwareCompare 只有 (QString, QString)（qstring.h:686），
//       没有 (QChar, QChar) 重载。
//
// 算法逐行照搬 vendor（数字段先比较长度再逐位、回退到整体 compare），
// 仅把上述三处 API 换成 Qt3 形态，不改任何排序语义：
//   - 大小写标志：Qt3 用 `bool cs`（true = 区分大小写），与 Qt3
//     QString::compare(s, bool) 的惯例一致。
//   - 单字符小写：QChar::lower()。
//   - 单字符比较：QString(qchar).localeAwareCompare(QString(qchar2))，
//     Qt3 无 (QChar,QChar) 重载，用 QString 包裹是等价路径。
//   - 末尾 compare：cs 时 compare(s1,s2)，否则 compare(s1.lower(), s2.lower())。
//
// 调用点（anystik/src/stickerstore.cpp 的 stable_sort 路径）只用两参
// naturalCompare(a, b)，即默认不区分大小写 —— 与 vendor 的默认值
// Qt::CaseInsensitive 语义一致，故 Qt3 侧默认 bool cs = false。
//  4. Qt3 头文件名全小写且无驼峰别名：`/opt/qt338sh/include` 下 344 个 .h
//     全是 qstring.h / qdir.h 这种形式，没有 <QString>（Qt4.0 才加别名）。
//     故本垫片一律用 <qstring.h>。
#ifndef QNATURALSORT_SHIM_H
#define QNATURALSORT_SHIM_H

#include <qstring.h>

class QNaturalSort
{
public:
    QNaturalSort();

    // cs=true 区分大小写（对齐 Qt4+ 的 Qt::CaseSensitive），
    // 默认 false 对齐 vendor 原签名默认值 Qt::CaseInsensitive。
    static int naturalCompare(const QString& s1, const QString& s2,
                              bool cs = false);
};

inline QNaturalSort::QNaturalSort()
{
}

// 与 vendor qnaturalsort.cpp:50-52 的 getNextChar 逐字对应，边界是
// location < length（不是 <=），越界返回空 QChar。
inline QChar qNatSortNextChar(const QString& s, int location)
{
    return (location < s.length()) ? s.at(location) : QChar();
}

// ⚠【不能用 localeAwareCompare】Qt3 的 QString::localeAwareCompare() 查
//   当前 LC_COLLATE，而 QApplication 构造会 setlocale(LC_ALL, "") 把
//   locale 从 "C" 换成用户 locale（实测 /opt/qt338sh + en_US.UTF-8）：
//     localeAwareCompare(QChar('A'), QChar('a'))
//       C locale      → -32   （'A' < 'a'，码位序）
//       en_US.UTF-8   → +5    （'A' > 'a'，glibc collation 小写在前）
//   后果是 naturalCompare 的结果**取决于进程是否建过 QApplication**，
//   同一批贴纸在 GUI 里和命令行/测试里的排序不同 —— 顺序不稳定且难复现。
//   vendor（Qt5/6）也用 localeAwareCompare，故 Qt6 侧同样有此性质；本垫片
//   改用码位比较，让结果与 locale 无关、与 Qt3 的 QString::compare 同源。
//   对 ASCII 名称两种排序一致；差异只出现在 locale collation 与码位序
//   不符的字符上（如大写/小写、CJK 之外的符号）。
inline int qNatSortCompareCaseSensitive(const QString& a, const QString& b)
{
    const int n = (a.length() < b.length()) ? a.length() : b.length();
    for (int i = 0; i < n; ++i) {
        const uint ua = a.at(i).unicode();
        const uint ub = b.at(i).unicode();
        if (ua != ub) return (ua < ub) ? -1 : 1;
    }
    if (a.length() == b.length()) return 0;
    return (a.length() < b.length()) ? -1 : 1;
}

// 单字符比较：cs=false 时先 lower()，然后按码位比。
// 返回 -1/0/1（调用方 normalize 后直接 return）。
inline int qNatSortCompareChars(QChar a, QChar b, bool cs)
{
    if (!cs) {
        a = a.lower();      // Qt3 只有 lower()，无 toLower()/isLower()
        b = b.lower();
    }
    const uint ua = a.unicode();
    const uint ub = b.unicode();
    if (ua < ub) return -1;
    if (ua > ub) return 1;
    return 0;
}

inline int QNaturalSort::naturalCompare(const QString& s1, const QString& s2,
                                        bool cs)
{
    // ⚠ Qt3 的 QString 无 count()（Qt4.0 才有），只有 length()。
    for (int l1 = 0, l2 = 0; l1 <= s1.length() && l2 <= s2.length(); ++l1, ++l2) {
        // skip spaces, tabs and 0's
        QChar c1 = qNatSortNextChar(s1, l1);
        while (c1.isSpace())
            c1 = qNatSortNextChar(s1, ++l1);
        QChar c2 = qNatSortNextChar(s2, l2);
        while (c2.isSpace())
            c2 = qNatSortNextChar(s2, ++l2);

        if (c1.isDigit() && c2.isDigit()) {
            while (c1.digitValue() == 0)
                c1 = qNatSortNextChar(s1, ++l1);
            while (c2.digitValue() == 0)
                c2 = qNatSortNextChar(s2, ++l2);

            int lookAheadLocation1 = l1;
            int lookAheadLocation2 = l2;
            int currentReturnValue = 0;
            // find the last digit, setting currentReturnValue as we go if it isn't equal
            for (QChar lookAhead1 = c1, lookAhead2 = c2;
                 (lookAheadLocation1 <= s1.length() && lookAheadLocation2 <= s2.length());
                 lookAhead1 = qNatSortNextChar(s1, ++lookAheadLocation1),
                 lookAhead2 = qNatSortNextChar(s2, ++lookAheadLocation2)) {
                bool is1ADigit = !lookAhead1.isNull() && lookAhead1.isDigit();
                bool is2ADigit = !lookAhead2.isNull() && lookAhead2.isDigit();
                if (!is1ADigit && !is2ADigit)
                    break;
                if (!is1ADigit)
                    return -1;
                if (!is2ADigit)
                    return 1;
                if (currentReturnValue == 0) {
                    if (lookAhead1 < lookAhead2) {
                        currentReturnValue = -1;
                    } else if (lookAhead1 > lookAhead2) {
                        currentReturnValue = 1;
                    }
                }
            }
            if (currentReturnValue != 0)
                return currentReturnValue;
        }

        // ⚠ 按码位比，不走 localeAwareCompare —— 原因见
        //   qNatSortCompareChars 上方的注释（locale 会让结果不稳定）。
        const int r = qNatSortCompareChars(c1, c2, cs);
        if (r != 0)
            return r;
    }
    // ⚠ vendor 源码此处原注释写的是 "The two strings are the same
    //   (02 == 2) so fall back to the normal sort"，**实测与该说法不符**，
    //   已按实测更正（2026-10，Qt3.5 /opt/qt338sh 隔离探针）：
    //     naturalCompare("02","2")   = -2
    //     naturalCompare("002","2")  = -2
    //     naturalCompare("a02","a2") = -2
    //   数字段比较确实跳过了前导 0（currentReturnValue 保持 0），但逐字符
    //   循环此时已到末尾，于是落到下面这行；而 **Qt3 的 QString::compare
    //   不归一化到 -1/0/1**，返回首个差异字符的码位差（'0'=0x30 vs
    //   '2'=0x32 → -2）。所以最终定序是 "02" < "2"，并非相等。
    //   这对调用方无影响：stickerstore 把它交给 std::stable_sort，排序只
    //   关心 <0/==0/>0，-2 与 -1 等价。但**返回值可能超出 [-1,1]**，
    //   使用方不可写 CHECK_EQ(result, -1) 之类断言。
    //   回归用例见 test_qnaturalsort_shim.cpp 的「数字段跳过前导 0」。
    //
    // ⚠ cs=true 必须走 qNatSortCompareCaseSensitive，**不能**写
    //   QString::compare(s1, s2)：vendor（Qt5/6）用的是
    //     return QString::compare(s1, s2, cs);      // 3 参数，带标志
    //   而 **Qt3 的 QString::compare 只有两参版本**（qstring.h:682-683：
    //     static int compare(const QString&, const QString&) { return s1.compare(s2); }
    //   ）—— 3 参数重载是 Qt4 才加的，且 Qt3 的两参版**永远不区分大小写**。
    //   照抄 vendor 会让 cs=true 静默退化成不区分大小写：
    //   实测 cs=true 时 "ABC" 与 "abc" 走到这里会返回 0（视为相等）。
    if (cs)
        return qNatSortCompareCaseSensitive(s1, s2);
    return QString::compare(s1.lower(), s2.lower());
}

#endif // QNATURALSORT_SHIM_H