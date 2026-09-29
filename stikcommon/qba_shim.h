#ifndef QLSTIK_QBA_SHIM_H
#define QLSTIK_QBA_SHIM_H

// QByteArrayLiteral 是 Qt 5.4 才引入的宏（Qt3/Qt4 没有）。
// Qt3 的 QByteArray 是 typedef QMemArray<char>（无 const char* 构造、无静态 number、
// 无 operator+），这里把字面量落到 QCString（Qt3 特有，public 继承自 QByteArray，
// 自带 operator+ / setNum）。语义等价：返回的都是「UTF-8 字节串」对象，仅类型名不同。
// 注意 QCString 含尾 NUL —— 需要精确长度用 length()/data() 自行处理（toCleanQBA 惯例）。
#ifndef QT_VERSION
#include <qglobal.h>
#endif
#if QT_VERSION >= 0x040000 && QT_VERSION < 0x050400
#define QByteArrayLiteral(s) QByteArray((s))
#endif
#if QT_VERSION < 0x040000
#define QByteArrayLiteral(s) QCString((s))
#endif

#endif // QLSTIK_QBA_SHIM_H