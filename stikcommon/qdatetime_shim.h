#ifndef QLSTIK_QDATETIME_SHIM_H
#define QLSTIK_QDATETIME_SHIM_H

// Qt 3.5 缺 QDateTime 的 Qt4+ 语义，vendor/qwebdav 与 davbisync 都用到：
//   1) toString("yyyy-MM-dd hh:mm")            —— Qt3 无自定义格式输出
//   2) QLocale(English,UnitedStates).toString(dt, fmt) —— Qt3 无该构造与重载
//   3) toUTC() / toMSecsSinceEpoch() / fromMSecsSinceEpoch() / setMSecsSinceEpoch()
//   4) QDateTime::fromString(s, Qt::ISODate)
//   5) QLocale::toDateTime(s[, fmt]) / QLocale::toDate / QLocale::toTime
//   6) Qt3 的 QDateTime 内部只存墙钟（QDate d + QTime t，无 timespec 字段），
//      没有 Qt4+ 的 setTimeSpec/toTimeSpec。本垫片约定：所有 QDateTime 值
//      一律按"UTC 墙钟"存放，因此 qDateTimeToUtc() 是恒等变换，qNowMsecs()
//      必须取 currentDateTime(Qt::UTC)。
//
// 这里提供自由函数（与 qToBase64 同款收口风格），源码在 QT3_BUILD 分支改为调用；
// Qt4+ 走原生 API，本头定义不参与编译。
//
// 时间基准：qDateTimeToMsecs/qDateTimeFromMsecs 用 QDate::daysTo 自建绝对毫秒
// 语义（Qt3 无 toMSecsSinceEpoch，也无 toJulianDay）。

// Qt 3.5.0 没有 qint64（Qt 3.6+ 才有），qglobaltype_shim.h 已用 long long 兜底。
#include "qglobaltype_shim.h"

#include <qglobal.h>
#include <qdatetime.h>
#include <qstring.h>
#include <qlocale.h>

// ══ 全部声明都是 Qt3 专用实现的对外接口（.cpp 仅在 QT3_BUILD 下编译）══
#if QT_VERSION < 0x040000

qint64 qDateTimeToMsecs(const QDateTime& dt);
QDateTime qDateTimeFromMsecs(qint64 msecs);
qint64 qNowMsecs();

// 替 QDateTime::toUTC()：保持绝对时刻不变，输出 UTC 墙钟（Qt4 精确语义）。
QDateTime qDateTimeToUtc(const QDateTime& dt);

// 支持 yyyy yy MMMM MMM MM M dd d dddd ddd hh h mm m ss s zzz t tt AP ap
QString qFormatDateTime(const QDateTime& dt, const char* fmt);
QString qFormatRfc1123DateTime(const QDateTime& dt);

// 解析上列同一套 token；ddd/dddd 只校验不取值，zzz 读 ±hhmm 后按该偏移构造。
// 解析失败返回默认构造的 QDateTime()（Qt3 与 Qt4+ 均为 null，isValid() 为假）。
QDateTime qParseDateTime(const QString& s, const char* fmt);
QDate qParseDate(const QString& s, const char* fmt);
QTime qParseTime(const QString& s, const char* fmt);
QDateTime qParseRfc1123DateTime(const QString& s);
QDateTime qParseIsoDateTime(const QString& s);

// 替 QLocale::toDateTime(s)：依次尝试 RFC1123 / RFC850 / asctime / ISO /
// "d MMM yyyy hh:mm:ss" / "d MMM yyyy"，全失败返回无效。
QDateTime qParseDateTimeAuto(const QString& s);

// ── Qt4+ 分支：上面这些在 Qt4+ 全部有原生对应，故 .cpp 只在 Qt3 编译 ──────
// qdatetime_shim.cpp 通篇是 Qt3 专用写法（QChar::upper / QString::lower /
// stripWhiteSpace / QDateTime::currentDateTime(Qt::TimeSpec) / QDate(QDate) 等
// 6 类，Qt5+ 全被移除），无法跨版本编译。但 QWebdavLite::put() 要发 RFC1123
// Date 头，正需要 qDateTimeToUtc + qFormatDateTime 这两个。Qt4+ 直接用原生：
//   toUTC() 本就是"保持绝对时刻、输出 UTC 墙钟"，与 qDateTimeToUtc 语义完全一致；
//   QLocale::toString(QDateTime, fmt) 是 qFormatDateTime 的原生等价物。
//   解析类的 qParse* 无原生对应且当前无人调用（唯一使用者 QWebdavLite 只用上面
//   两个），故 Qt4+ 侧不提供，确需时再补。
//
// ⚠ 结构陷阱：这里必须用 #else 挂在上面的 `#if QT_VERSION < 0x040000` 对面。
//   若写成嵌套的 `#if QT_VERSION >= 0x040000`，它会被外层 Qt3 守卫套死，
//   Qt6 下永远进不去，表现为「qDateTimeToUtc was not declared」。
#else
inline QDateTime qDateTimeToUtc(const QDateTime& dt)
{
    return dt.toUTC();
}

inline QString qFormatDateTime(const QDateTime& dt, const char* fmt)
{
    return QLocale().toString(dt, QString::fromLatin1(fmt));
}

inline QString qFormatRfc1123DateTime(const QDateTime& dt)
{
    return QLocale(QLocale::English).toString(dt.toUTC(),
                                               QString("ddd, dd MMM yyyy hh:mm:ss"));
}

// 替 QDateTime::fromMSecsSinceEpoch()：Qt 3.5 **无**此成员（实测
// `grep -c fromMSecsSinceEpoch /opt/qt338sh/include/qdatetime.h` = 0），
// Qt4.1+ 原生具备。207 解析层 dav207pugi.cpp 输出的 getlastmodified 是
// epoch 毫秒，调用点需跨版本拿 QDateTime，故两侧都提供、调用点零分支。
inline QDateTime qDateTimeFromMsecs(qint64 msecs)
{
    return QDateTime::fromMSecsSinceEpoch(msecs);
}
#endif // QT_VERSION < 0x040000

// epoch 秒：Qt5.8 才引入 QDateTime::currentSecsSinceEpoch()，Qt3 完全无此符号
// （Qt3 QDateTime 侧最近的是 toTime_t()，见 qdatetime.h:200 —— 正好就是
// Unix 纪元秒，语义一致，直接转调，无需自己做时区/闰秒换算）。
// 返回 qint64 而非 time_t：Qt6 的 currentSecsSinceEpoch() 返回 qint64，
// 32 位平台下 time_t 只有 31 位有效位，装不下 2038 年后的纪元秒。
#if QT_VERSION < 0x040000
inline qint64 qDateTimeEpochSecs()
{
    return qint64(QDateTime::currentDateTime().toTime_t());
}
#else
inline qint64 qDateTimeEpochSecs()
{
    return QDateTime::currentSecsSinceEpoch();
}
#endif

#endif // QLSTIK_QDATETIME_SHIM_H
