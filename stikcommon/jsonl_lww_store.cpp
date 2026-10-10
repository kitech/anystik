#include "jsonl_lww_store.h"

#include "qglobaltype_shim.h"     // qint64 / qAbsPath / qMkdir（四版本统一）
#include "qstring_shim.h"         // QStringLiteral（Qt<4.1）；Qt4 安全

#ifdef QT3_BUILD
#include <qfile.h>
#include <qiodevice.h>
#else
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QIODevice>
#endif

#if QT_VERSION < 0x050000
#include "qsavefile_shim.h"       // Qt3/Qt4（Qt5.1 才有 QSaveFile）
#else
#include <QSaveFile>
#endif

#include <string>
#include <string.h>               // memcpy

namespace jsonl_lww {

namespace {

// Qt3 的 QIODevice 用 IO_* 宏；Qt4+ 用 QIODevice::OpenMode 枚举。
#ifdef QT3_BUILD
static const int kReadMode  = IO_ReadOnly;
static const int kWriteMode = IO_WriteOnly;
#else
static const QIODevice::OpenMode kReadMode  = QIODevice::ReadOnly;
static const QIODevice::OpenMode kWriteMode = QIODevice::WriteOnly;
#endif

// 跨版本无法用统一构造：Qt3 只有 QByteArray(int)；Qt6 只有
// QByteArray(const char*, qsizetype) 而**无** QByteArray(int)。用 resize +
// memcpy 两版本通吃（resize 与 data() 四版本俱在）。
QByteArray toBa(const std::string& s)
{
    QByteArray ba;
    ba.resize(int(s.size()));
    if (!s.empty()) {
        memcpy(ba.data(), s.data(), s.size());
    }
    return ba;
}


std::string fromBa(const QByteArray& ba)
{
    return std::string(ba.data(), size_t(ba.size()));
}

// 候选 c 是否比现存的 o 更"新"：mtime 大者胜；相等时 "desc" 字典序大者胜。
bool newerThan(const QJsonObject& cand, const QJsonObject& old)
{
    const double a = cand.value(QStringLiteral("mtime")).toDouble(0);
    const double b = old.value(QStringLiteral("mtime")).toDouble(0);
    if (a != b) {
        return a > b;
    }
    return cand.value(QStringLiteral("desc")).toString()
         > old.value(QStringLiteral("desc")).toString();
}

void foldLine(QMap<QString, QJsonObject>& io, const std::string& line)
{
    if (line.empty()) {
        return;
    }
    const QJsonDocument d = QJsonDocument::fromJson(toBa(line));
    if (!d.isObject()) {
        return;                       // 坏行 / 非对象：跳过，不致命
    }
    const QJsonObject obj = d.object();
    const QString key = obj.value(QStringLiteral("rel")).toString();
    if (key.isEmpty()) {
        return;
    }
    QMap<QString, QJsonObject>::iterator it = io.find(key);
    if (it == io.end()) {
        io.insert(key, obj);
    } else if (newerThan(obj, *it)) {
        *it = obj;
    }
}

} // namespace

std::string readFileRaw(const QString& absPath)
{
    QFile f(absPath);
    if (!f.exists() || !f.open(kReadMode)) {
        return std::string();
    }
    return fromBa(f.readAll());
}

void foldInto(QMap<QString, QJsonObject>& io, const std::string& data)
{
    std::string line;
    line.reserve(256);
    for (size_t i = 0; i < data.size(); ++i) {
        const char ch = data[i];
        if (ch == '\n') {
            foldLine(io, line);
            line.clear();
        } else if (ch != '\r') {
            line.push_back(ch);
        }
    }
    foldLine(io, line);               // 末行可能无换行符
}

QMap<QString, QJsonObject> parse(const std::string& data)
{
    QMap<QString, QJsonObject> m;
    foldInto(m, data);
    return m;
}

std::string serialize(const QMap<QString, QJsonObject>& m)
{
    std::string out;
    for (QMap<QString, QJsonObject>::const_iterator it = m.constBegin();
         it != m.constEnd(); ++it) {
        const QByteArray one =
            QJsonDocument(*it).toJson(QJsonDocument::Compact);
        out.append(one.data(), size_t(one.size()));
        out.push_back('\n');
    }
    return out;
}

bool writeFile(const QString& absPath, const QMap<QString, QJsonObject>& m)
{
    if (!qMkdir(qAbsPath(QFileInfo(absPath)))) {
        return false;
    }
    QSaveFile f(absPath);
    if (!f.open(kWriteMode)) {
        return false;
    }
    const std::string s = serialize(m);
    if (f.write(toBa(s)) < 0) {
        return false;
    }
    return f.commit();
}

} // namespace jsonl_lww
