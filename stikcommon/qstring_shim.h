#ifndef QLSTIK_QSTRING_SHIM_H
#define QLSTIK_QSTRING_SHIM_H

// qUtf8Printable 是 Qt 5.10 才有的宏；Qt3/Qt4 用同义实现。
// 安全性：toUtf8()/utf8() 返回的临时对象活到整个函数调用表达式结束，
//        故宏只能用于调用实参处（与 Qt 官方对 qUtf8Printable 的约束一致）。
// Qt5.10+/Qt6 由 #if 自动跳过，用 Qt 自身的宏。

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

#endif // QLSTIK_QSTRING_SHIM_H
