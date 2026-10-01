#ifndef QLSTIK_QBYTEARRAYVIEW_SHIM_H
#define QLSTIK_QBYTEARRAYVIEW_SHIM_H

// QByteArrayView 垫片（Qt3 专用）。
//
// QByteArrayView 是 Qt 5.10 才引入的只读字节视图，Qt3/Qt4/Qt5.9 均无。
// 本 shim 只补 stickerstore.cpp 实际用到的那一处形态：
//
//   fileMd5()（stickerstore.cpp:3317）：
//       hash.addData(QByteArrayView(buf.constData(), int(got)));
//
// 即「指针 + 长度」构造，构造完立刻作为实参传给 QCryptographicHash::addData()，
// 用完即弃——没有存储期、没有悬垂风险（Qt6 原生 QByteArrayView 同样不持有内存）。
//
// ⚠ 只实现 Qt6 QByteArrayView 的子集（构造 / data / size / isEmpty /
//   constData）。调用方一旦用到 slice()、removePrefix()、first()/last()、
//   charAt()、operator== 等，须在此补。
// ⚠ Qt6 的长度类型是 qsizetype（qint64）；Qt3 无 qsizetype，此处用 int。
//   stickerstore.cpp 显式写了 int(got)，且 QIODevice 单次读取量受缓冲区
//   （本处 64 KiB）约束，int 足够，不会溢出。

#if QT_VERSION < 0x050a00

#include <qcstring.h>

class QByteArrayView
{
public:
    QByteArrayView() : m_data(0), m_size(0) {}
    QByteArrayView(const char* data, int size)
        : m_data(data), m_size(data ? size : 0) {}

    const char* data() const { return m_data; }
    const char* constData() const { return m_data; }
    int size() const { return m_size; }
    bool isEmpty() const { return m_size == 0; }

private:
    const char* m_data;
    int m_size;
};

#endif // QT_VERSION < 0x050a00
#endif // QLSTIK_QBYTEARRAYVIEW_SHIM_H