#ifndef QLSTIK_QSTRING_SHIM_H
#define QLSTIK_QSTRING_SHIM_H

// qUtf8Printable 是 Qt 5.10 才有的宏；Qt3/Qt4 用同义实现。
// 安全性：toUtf8()/utf8() 返回的临时对象活到整个函数调用表达式结束，
//        故宏只能用于调用实参处（与 Qt 官方对 qUtf8Printable 的约束一致）。
// Qt5.10+/Qt6 由 #if 自动跳过，用 Qt 自身的宏。
//
// 本头汇总「与 Qt 版本相关的 QString 侧缺口」，每项自带版本守卫：
//   qUtf8Printable  Qt <5.10   宏
//   QStringLiteral Qt <4.1    宏
//   QLatin1Char    Qt <4.0    宏
// 三个都是函数式宏，未带 ( 时不展开，不会误伤同名类型声明。

#ifndef QT_VERSION
#include <qglobal.h>
#endif

#if QT_VERSION < 0x050a00
#if QT_VERSION >= 0x040000
#define qUtf8Printable(string) (string).toUtf8().constData()
#else
// Qt3 的 QString::utf8() 返回 QCString（QByteArray 子类），无 constData()
#define qUtf8Printable(string) ((const char*)(string).utf8().data())
#endif
#endif

// QStringLiteral 是 Qt 5.0 才引入（Qt3/Qt4 都没有，不是 4.1）。
// Qt 官方版把字面量编成 UTF-16 静态数据（零运行时转换）；Qt3/Qt4 只能运行时
// 从 UTF-8 转换。语义等价（返回值都是独立 QString，无临时悬垂），仅慢一点。
#if QT_VERSION < 0x050000
#define QStringLiteral(str) QString::fromUtf8(str)
#endif

// Qt4.0 才引入 QLatin1Char。Qt3 的 QChar 有 char 构造函数，直接替代。
#if QT_VERSION < 0x040000
#define QLatin1Char(c) QChar(c)
#endif

// QLatin1String 是 Qt 4.0 才引入的类；Qt3 无此类，且 Qt3 的 QString 也没有
// startsWith(const QLatin1String&)/operator== 的 QLatin1String 重载。用一个最小
// 类 + 隐式 QString 转换垫上：调用点的 QLatin1String("…") 在需要 const QString&
// 处（startsWith/arg/==）经一次用户转换编译通过，latin1 语义等价。转 const char*
// 就够，但不给（两次用户转换是非法的），只给 operator QString()。
#if QT_VERSION < 0x040000
class QLatin1String
{
public:
    explicit QLatin1String(const char* s) : m_s(s) {}
    operator QString() const { return QString::fromLatin1(m_s); }
private:
    const char* m_s;
};
#endif

#endif // QLSTIK_QSTRING_SHIM_H
