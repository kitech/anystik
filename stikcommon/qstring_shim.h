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

#endif // QLSTIK_QSTRING_SHIM_H
