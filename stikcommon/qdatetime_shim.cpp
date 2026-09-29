// Qt 3.5.0 把 QDate / QTime / QDateTime 全部放在 qdatetime.h 里，没有独立的
// qdate.h / qtime.h（那是 Qt4 才拆开的）。
#include "qdatetime_shim.h"

#include <qstring.h>

#include <cctype>

namespace {

const char* const kMonthShort[12] = {
    "jan", "feb", "mar", "april_short_placeholder", "may", "jun",
    "jul", "aug", "sep", "oct", "nov", "dec"
};
const char* const kMonthShortFixed[12] = {
    "jan", "feb", "mar", "apr", "may", "jun",
    "jul", "aug", "sep", "oct", "nov", "dec"
};

const char* const kDayShort[7] = {
    "mon", "tue", "wed", "thu", "fri", "sat", "sun"
};

const char* const kMonthFull[12] = {
    "january", "february", "march", "april", "may", "june",
    "july", "august", "september", "october", "november", "december"
};

const char* const kDayFull[7] = {
    "monday", "tuesday", "wednesday", "thursday",
    "friday", "saturday", "sunday"
};

const QDate kEpochDate(1970, 1, 1);

QString capitalizeFirst(const char* text)
{
    QString s(text);
    if (!s.isEmpty()) {
        s[0] = s[0].upper();
    }
    return s;
}

int monthFromName(const QString& name)
{
    if (name.length() < 3) {
        return 0;
    }
    const QString key = name.left(3).lower();
    for (int i = 0; i < 12; ++i) {
        if (key == kMonthShortFixed[i]) {
            return i + 1;
        }
    }
    return 0;
}

QString padStr(int value, int width)
{
    QString s = QString::number(value);
    while (s.length() < width) {
        s.prepend("0");
    }
    return s;
}

// 墙钟秒（把 d+t 当 UTC 墙钟解释）。qDateTimeToMsecs/qDateTimeFromMsecs 互逆。
qint64 wallToSecs(const QDate& d, const QTime& t)
{
    const qint64 days = (qint64)kEpochDate.daysTo(d);
    return days * 86400LL
         + (qint64)t.hour() * 3600LL
         + (qint64)t.minute() * 60LL
         + (qint64)t.second();
}

bool readNumber(const QString& s, int& p, int maxDigits, int& out)
{
    const int n = s.length();
    while (p < n && !s[p].isDigit()) {
        ++p;
    }
    if (p >= n) {
        return false;
    }
    int count = 0;
    int value = 0;
    while (p + count < n && count < maxDigits && s[p + count].isDigit()) {
        value = value * 10 + s[p + count].digitValue();
        ++count;
    }
    if (count == 0) {
        return false;
    }
    p += count;
    out = value;
    return true;
}

bool readWord(const QString& s, int& p, QString& out)
{
    const int n = s.length();
    const int start = p;
    while (p < n && s[p].isLetter()) {
        ++p;
    }
    if (p == start) {
        return false;
    }
    out = s.mid(start, p - start);
    return true;
}

int dayNameToIndex(const QString& word)
{
    if (word.length() < 3) {
        return -1;
    }
    const QString key = word.left(3).lower();
    for (int i = 0; i < 7; ++i) {
        if (key == kDayShort[i]) {
            return i;
        }
    }
    return -1;
}

// 解析 zzz：Z / GMT / UTC / +hhmm / +hh:mm / -hh:mm。返回 false 表示此处不是时区。
bool readTimeZone(const QString& s, int& p, int& offsetSeconds)
{
    const int n = s.length();
    if (p < n && (s[p] == 'Z' || s[p] == 'z')) {
        ++p;
        offsetSeconds = 0;
        return true;
    }
    if (p < n && (s[p] == '+' || s[p] == '-')) {
        // 从符号之后开始扫：s[p] 本身是 '+'/'-'，不是数字也不是冒号，
        // 若从 p 起扫会立刻停下、得到空 zone。
        int k = p + 1;
        while (k < n && (s[k].isDigit() || s[k] == ':')) {
            ++k;
        }
        const QString zone = s.mid(p, k - p);
        bool ok = false;
        if (zone.length() == 5 || zone.length() == 6) {
            const int sign = (zone[0] == '-') ? -1 : 1;
            const int hh = zone.mid(1, 2).toInt(&ok);
            const int mm = (zone.length() == 6) ? zone.mid(4, 2).toInt(&ok) : zone.mid(3, 2).toInt(&ok);
            if (ok && hh < 100 && mm < 100) {
                offsetSeconds = sign * (hh * 3600 + mm * 60);
                p = k;
                return true;
            }
        }
        return false;
    }
    if (p + 2 < n) {
        const QString tail = s.mid(p, 3).upper();
        if (tail == "GMT" || tail == "UTC") {
            p += 3;
            offsetSeconds = 0;
            return true;
        }
    }
    return false;
}

struct ParsedFields {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int msec;
    bool haveYear;
    bool haveMonth;
    bool haveDay;
    bool haveTime;
    int tzOffset;
    bool haveTz;
    bool ok;

    ParsedFields()
        : year(0), month(1), day(1), hour(0), minute(0), second(0), msec(0)
        , haveYear(false), haveMonth(false), haveDay(false), haveTime(false)
        , tzOffset(0), haveTz(false), ok(false)
    {
    }
};

ParsedFields scanFormat(const QString& input, const char* fmt)
{
    ParsedFields f;
    const int n = input.length();
    int p = 0;
    int i = 0;
    while (fmt[i] != '\0') {
        const char c = fmt[i];
        int len = 1;
        while (fmt[i + len] == c) {
            ++len;
        }
        if (std::isalpha((unsigned char)c) != 0) {
            if (c == 'y') {
                int v = 0;
                if (!readNumber(input, p, len >= 3 ? 4 : 2, v)) {
                    return f;
                }
                f.year = (len >= 3) ? v : ((v < 70) ? (2000 + v) : (1900 + v));
                f.haveYear = true;
            } else if (c == 'M') {
                if (len <= 2) {
                    int v = 0;
                    if (!readNumber(input, p, 2, v)) {
                        return f;
                    }
                    f.month = v;
                    f.haveMonth = true;
                } else {
                    QString word;
                    if (!readWord(input, p, word)) {
                        return f;
                    }
                    const int m = monthFromName(word);
                    if (m == 0) {
                        return f;
                    }
                    f.month = m;
                    f.haveMonth = true;
                }
            } else if (c == 'd') {
                if (len <= 2) {
                    int v = 0;
                    if (!readNumber(input, p, 2, v)) {
                        return f;
                    }
                    f.day = v;
                    f.haveDay = true;
                } else {
                    QString word;
                    if (!readWord(input, p, word)) {
                        return f;
                    }
                    if (dayNameToIndex(word) < 0) {
                        return f;
                    }
                }
            } else if (c == 'H' || c == 'h') {
                int v = 0;
                if (!readNumber(input, p, 2, v)) {
                    return f;
                }
                f.hour = v;
                f.haveTime = true;
            } else if (c == 'm') {
                int v = 0;
                if (!readNumber(input, p, 2, v)) {
                    return f;
                }
                f.minute = v;
                f.haveTime = true;
            } else if (c == 's') {
                int v = 0;
                if (!readNumber(input, p, 2, v)) {
                    return f;
                }
                f.second = v;
                f.haveTime = true;
            } else if (c == 'z' || c == 'Z') {
                int v = 0;
                if (!readTimeZone(input, p, v)) {
                    return f;
                }
                f.tzOffset = v;
                f.haveTz = true;
            } else {
                // t / tt / AP / ap：不参与数值解析
            }
        } else if (p < n && input[p] == QChar(c)) {
            ++p;
        }
        i += len;
    }
    f.ok = true;
    return f;
}

QDateTime buildFromFields(const ParsedFields& f)
{
    if (!f.ok || !f.haveYear) {
        return QDateTime();
    }
    const QDate d(f.year, f.month, f.haveDay ? f.day : 1);
    if (!d.isValid()) {
        return QDateTime();
    }
    const QTime t(f.hour, f.minute, f.second, f.msec);
    if (!t.isValid()) {
        return QDateTime();
    }
    if (!f.haveTz) {
        // 无时区信息：按墙钟原样保留（DAV/Last-Modified 场景不会走到这里）
        return QDateTime(d, t);
    }
    // 有时区：归一到 UTC 墙钟（qDateTimeToMsecs 约定）
    return qDateTimeFromMsecs((wallToSecs(d, t) - (qint64)f.tzOffset) * 1000LL);
}

} // namespace

qint64 qDateTimeToMsecs(const QDateTime& dt)
{
    if (!dt.isValid()) {
        return 0;
    }
    const QTime t = dt.time();
    return (wallToSecs(dt.date(), t)) * 1000LL + (qint64)t.msec();
}

QDateTime qDateTimeFromMsecs(qint64 msecs)
{
    qint64 days = msecs / 86400000LL;
    qint64 rem = msecs % 86400000LL;
    if (rem < 0) {
        rem += 86400000LL;
        --days;
    }
    const QDate d = kEpochDate.addDays((int)days);
    if (!d.isValid()) {
        return QDateTime();
    }
    const QTime t((int)(rem / 3600000LL),
                  (int)((rem / 60000LL) % 60LL),
                  (int)((rem / 1000LL) % 60LL),
                  (int)(rem % 1000LL));
    return QDateTime(d, t);
}

qint64 qNowMsecs()
{
    // 必须取 UTC：Qt3 QDateTime 内部只存墙钟（无 timespec 字段），
    // 本垫片约定所有 QDateTime 值按 UTC 墙钟存放。
    return qDateTimeToMsecs(QDateTime::currentDateTime(Qt::UTC));
}

QDateTime qDateTimeToUtc(const QDateTime& dt)
{
    if (!dt.isValid()) {
        return QDateTime();
    }
    // 垫片约定：值已是 UTC 墙钟存储，toUTC() 是恒等语义（与 Qt4 的
    // "保持绝对时刻、只换 spec" 一致——因为本垫片不存 spec）。
    return QDateTime(dt.date(), dt.time());
}

QString qFormatDateTime(const QDateTime& dt, const char* fmt)
{
    if (!dt.isValid() || fmt == 0) {
        return QString();
    }
    const QDate d = dt.date();
    const QTime t = dt.time();
    bool ampm = false;
    for (const char* p = fmt; *p != '\0'; ++p) {
        const char c = *p;
        // Qt 的 AM/PM 标记只有 ap / AP / t / tt；'T' 不是格式字符而是字面量
        // （ISO 的 "yyyy-MM-ddThh:mm:ss" 里就有一个 T），不能参与 ampm 判定。
        if (c == 't' || c == 'a' || c == 'A') {
            ampm = true;
        }
    }
    int hour12 = t.hour() % 12;
    if (hour12 == 0) {
        hour12 = 12;
    }
    const char* amText = (t.hour() < 12) ? "AM" : "PM";
    const char* amTextLower = (t.hour() < 12) ? "am" : "pm";
    const int dow = d.dayOfWeek();  // 1=Monday .. 7=Sunday
    const int dayIndex = (dow == 7) ? 6 : (dow - 1);

    QString out;
    int i = 0;
    while (fmt[i] != '\0') {
        const char c = fmt[i];
        int len = 1;
        while (fmt[i + len] == c) {
            ++len;
        }
        switch (c) {
        case 'y':
            out += (len >= 3) ? padStr(d.year(), 4) : padStr(d.year() % 100, 2);
            break;
        case 'M':
            if (len >= 4) {
                out += capitalizeFirst(kMonthFull[d.month() - 1]);
            } else if (len == 3) {
                out += capitalizeFirst(kMonthShortFixed[d.month() - 1]);
            } else {
                out += padStr(d.month(), len);
            }
            break;
        case 'd':
            if (len >= 4) {
                out += capitalizeFirst(kDayFull[dayIndex]);
            } else if (len == 3) {
                out += capitalizeFirst(kDayShort[dayIndex]);
            } else {
                out += padStr(d.day(), len);
            }
            break;
        case 'H':
            out += padStr(t.hour(), len);
            break;
        case 'h':
            out += padStr(ampm ? hour12 : t.hour(), len);
            break;
        case 'm':
            out += padStr(t.minute(), len);
            break;
        case 's':
            out += padStr(t.second(), len);
            break;
        case 'z':
        case 'Z':
            // 垫片不存 timespec，恒为 UTC
            out += QString("+0000");
            break;
        case 't':
        case 'a':
        case 'A':
            out += (c == 'A' || c == 'a') ? amTextLower : amText;
            break;
        default:
            for (int k = 0; k < len; ++k) {
                out += c;
            }
            break;
        }
        i += len;
    }
    return out;
}

QString qFormatRfc1123DateTime(const QDateTime& dt)
{
    if (!dt.isValid()) {
        return QString();
    }
    return qFormatDateTime(qDateTimeToUtc(dt), "ddd, dd MMM yyyy hh:mm:ss") + " GMT";
}

QDateTime qParseDateTime(const QString& s, const char* fmt)
{
    if (fmt == 0) {
        return QDateTime();
    }
    return buildFromFields(scanFormat(s.stripWhiteSpace(), fmt));
}

QDate qParseDate(const QString& s, const char* fmt)
{
    const QDateTime dt = qParseDateTime(s, fmt);
    if (!dt.isValid()) {
        return QDate();
    }
    return dt.date();
}

QTime qParseTime(const QString& s, const char* fmt)
{
    // 不能走 buildFromFields：那条路径要求 haveYear（纯时间串没有年份）。
    // 越界的小时/分/秒会让 QTime 构造出无效值，调用方用 isValid() 判定。
    if (fmt == 0) {
        return QTime();
    }
    const ParsedFields f = scanFormat(s.stripWhiteSpace(), fmt);
    if (!f.ok) {
        return QTime();
    }
    return QTime(f.hour, f.minute, f.second, f.msec);
}

QDateTime qParseRfc1123DateTime(const QString& s)
{
    // 与 Qt6 的一处刻意差异：QLocale::toDateTime(s, "ddd, dd MMM yyyy hh:mm:ss")
    // 遇到尾部多出的 " GMT" 会判失败（实测 valid=0），而 DAV 的 Last-Modified
    // 恰恰带 GMT（上游 qwebdav 是靠 input.left(25) 先截断才通过的）。这里三段
    // 依次尝试、带 GMT 的输入同样能解析。
    const QString in = s.stripWhiteSpace();
    QDateTime dt = qParseDateTime(in, "ddd, dd MMM yyyy hh:mm:ss");
    if (dt.isValid()) {
        return dt;
    }
    dt = qParseDateTime(in, "ddd, dd MMM yyyy hh:mm:ss zzz");
    if (dt.isValid()) {
        return dt;
    }
    return qParseDateTime(in, "dddd, dd-MMM-yy hh:mm:ss");
}

QDateTime qParseIsoDateTime(const QString& s0)
{
    const QString s = s0.stripWhiteSpace();
    const int n = s.length();
    if (n == 0) {
        return QDateTime();
    }
    int end = n;
    bool haveTz = false;
    bool isUtc = false;
    int tzOffset = 0;

    if (s[end - 1] == 'Z' || s[end - 1] == 'z') {
        haveTz = true;
        isUtc = true;
        --end;
    } else {
        int k = end - 1;
        while (k >= 0 && (s[k].isDigit() || s[k] == ':')) {
            --k;
        }
        if (k >= 0 && (s[k] == '+' || s[k] == '-')) {
            int v = 0;
            int p = k;
            if (readTimeZone(s, p, v)) {
                haveTz = true;
                tzOffset = v;
                end = k;
            }
        } else if (end >= 3) {
            const QString tail = s.mid(end - 3, 3).upper();
            if (tail == "GMT" || tail == "UTC") {
                haveTz = true;
                isUtc = true;
                end -= 3;
            }
        }
    }

    const QString body = s.left(end);
    int sep = body.find(QChar('T'));
    if (sep < 0) {
        sep = body.find(QChar(' '));
    }
    const QString datePart = (sep < 0) ? body : body.left(sep);
    QString timePart = (sep < 0) ? QString() : body.mid(sep + 1);

    const QDate d = qParseDate(datePart, "yyyy-MM-dd");
    if (!d.isValid()) {
        return QDateTime();
    }
    QTime t(0, 0, 0);
    if (!timePart.isEmpty()) {
        const int dot = timePart.find(QChar('.'));
        if (dot >= 0) {
            timePart = timePart.left(dot);
        }
        t = qParseTime(timePart, "hh:mm:ss");
        if (!t.isValid()) {
            t = qParseTime(timePart, "hh:mm");
        }
        if (!t.isValid()) {
            t = qParseTime(timePart, "hh");
        }
        if (!t.isValid()) {
            return QDateTime();
        }
    }
    if (!haveTz) {
        return QDateTime(d, t);
    }
    if (isUtc || tzOffset == 0) {
        return qDateTimeFromMsecs(wallToSecs(d, t) * 1000LL);
    }
    return qDateTimeFromMsecs((wallToSecs(d, t) - (qint64)tzOffset) * 1000LL);
}

QDateTime qParseDateTimeAuto(const QString& s0)
{
    const QString s = s0.stripWhiteSpace();
    if (s.isEmpty()) {
        return QDateTime();
    }
    QDateTime dt = qParseIsoDateTime(s);
    if (dt.isValid()) {
        return dt;
    }
    dt = qParseRfc1123DateTime(s);
    if (dt.isValid()) {
        return dt;
    }
    dt = qParseDateTime(s, "ddd MMM d hh:mm:ss yyyy");
    if (dt.isValid()) {
        return dt;
    }
    dt = qParseDateTime(s, "d MMM yyyy hh:mm:ss");
    if (dt.isValid()) {
        return dt;
    }
    dt = qParseDateTime(s, "d MMM yyyy");
    if (dt.isValid()) {
        return dt;
    }
    dt = qParseDateTime(s, "yyyy-MM-dd hh:mm:ss");
    if (dt.isValid()) {
        return dt;
    }
    return qParseDateTime(s, "yyyy-MM-dd");
}
