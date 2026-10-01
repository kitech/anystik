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

        if (!cs) {
            c1 = c1.lower();       // Qt3 只有 lower()，无 isLower()/toLower()
            c2 = c2.lower();
        }
        // Qt3 的 localeAwareCompare 无 (QChar,QChar) 重载（qstring.h:686），
        // 用 QString 包裹单字符是等价路径。
        int r = QString(c1).localeAwareCompare(QString(c2));
        if (r < 0)
            return -1;
        if (r > 0)
            return 1;
    }
    // The two strings are the same (02 == 2) so fall back to the normal sort
    if (cs)
        return QString::compare(s1, s2);
    return QString::compare(s1.lower(), s2.lower());
}

#endif // QNATURALSORT_SHIM_H