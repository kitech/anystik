#ifndef QLSTIK_QDEBUG_SHIM_H
#define QLSTIK_QDEBUG_SHIM_H

// Qt3 无 qInfo()/qWarning() 流（qInfo Qt 5.5、QDebug 流 Qt 4.3 才引入）。
// 提供最小流对象：把 << 拼接进 QString，析构时抛给 Qt3 的 qDebug(const QString&)
// / qWarning(const QString&) 重载（qglobal.h 961/969 行，原生存在，UTF-8 直接打印）。

#if QT_VERSION < 0x040000
#include <qglobal.h>
#include <qstring.h>
#include "qstring_shim.h"
#include "qglobaltype_shim.h"

class Qt3LogStream
{
public:
    explicit Qt3LogStream(bool warn) : m_warn(warn) {}
    ~Qt3LogStream()
    {
        if (m_warn) {
            qWarning(m_acc);
        } else {
            qDebug(m_acc);
        }
    }
    Qt3LogStream& noquote() { return *this; }
    Qt3LogStream& operator<<(const QString& v) { m_acc += v; return *this; }
    Qt3LogStream& operator<<(const char* v)
    {
        m_acc += QString::fromLatin1(v);
        return *this;
    }
    Qt3LogStream& operator<<(QLatin1String v) { m_acc += QString(v); return *this; }
    Qt3LogStream& operator<<(int v)
    {
        m_acc += QString::number((long)v);
        return *this;
    }
    Qt3LogStream& operator<<(unsigned int v)
    {
        m_acc += QString::number((ulong)v);
        return *this;
    }
    Qt3LogStream& operator<<(quint64 v)
    {
        m_acc += QString::number((Q_ULLONG)v);
        return *this;
    }
    Qt3LogStream& operator<<(qint64 v)   // qglobaltype_shim 的 qint64
    {
        m_acc += QString::number((Q_LLONG)v);
        return *this;
    }
    Qt3LogStream& operator<<(bool v)
    {
        m_acc += v ? QString::fromLatin1("true")
                   : QString::fromLatin1("false");
        return *this;
    }

private:
    bool m_warn;
    QString m_acc;
};

inline Qt3LogStream qInfo() { return Qt3LogStream(false); }
inline Qt3LogStream qWarning() { return Qt3LogStream(true); }

#endif // QT_VERSION < 0x040000

#endif // QLSTIK_QDEBUG_SHIM_H