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

#endif // QLSTIK_QDATETIME_SHIM_H
