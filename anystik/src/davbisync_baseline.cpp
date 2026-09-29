#include "davbisync_baseline.h"

// qglobaltype_shim.h：qint64(Qt3) / qAbsPath() / qMkdir()，四版本统一走它
#include "qglobaltype_shim.h"

#ifdef QT3_BUILD
// Qt3.5 无 CamelCase 转发头（只有 qfileinfo.h / qdir.h / qfile.h / qiodevice.h）
//（qglobaltype_shim.h 已在 Qt3 分支 include qstring/qdir/qfileinfo）
#include <qfile.h>
#include <qiodevice.h>
#else
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QIODevice>
#endif
#include "qstring_shim.h"   // QStringLiteral(Qt<4.1) / QLatin1Char(Qt<4.0) 垫片

#if QT_VERSION < 0x050000
// QStandardPaths / QSaveFile / QJson 三者都是 Qt5 才引入（Qt3.5 与 Qt4.8.7
// 均无对应头文件），Qt3/Qt4 走 stikcommon 垫片，底座 cJSON 见 qldox.pri。
#include "qstandardpaths_shim.h"
#include "qsavefile_shim.h"
#include "qjson_shim.h"
#else
#include <QStandardPaths>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#endif

namespace davbisync {

namespace {

// ══ Qt3 打开模式适配 ═══════════════════════════════════════════════════
// Qt3 的 QIODevice 用 IO_ReadOnly 宏且无枚举成员，只能按分支映射成常量；
// Qt4+ 直接用 QIODevice::ReadOnly 枚举（QFile::open 收 QIODevice::OpenMode，
// 用 int 会导致枚举/QFlags 歧义）。业务代码统一写 kBaselineXxxMode。
#ifdef QT3_BUILD
static const int kBaselineReadMode  = IO_ReadOnly;
static const int kBaselineWriteMode = IO_WriteOnly;
#else
static const QIODevice::OpenMode kBaselineReadMode  = QIODevice::ReadOnly;
static const QIODevice::OpenMode kBaselineWriteMode = QIODevice::WriteOnly;
#endif

QString baselinePath()
{
    const QString dir = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    return dir + QStringLiteral("/davbisync_baseline.json");
}

// 一次性兼容迁移，把历史键归一到业务 dbRel（相对路径）：
// 1) 旧版以云根（anystik/）为前缀；
// 2) 某次回归曾写入绝对本地路径（存储 base + '/' + dbRel）。
// 仅命中时剥除；新写出的键天然是相对 dbRel → 幂等（之后任何装载不再变）。
// baseDir 即状态文件所在目录（AppLocalDataLocation = 存储 root）。
QString compatRel(QString key, const QString& baseDir)
{
    const QString prefix = QStringLiteral("anystik/");
    if (key.startsWith(prefix)) {
        key = key.mid(prefix.length());
    }
    if (!baseDir.isEmpty()) {
        const QString basePrefix = baseDir + QLatin1Char('/');
        if (key.startsWith(basePrefix)) {
            key = key.mid(basePrefix.length());   // length(): Qt3 无 size()，四版本通用
        }
    }
    return key;
}

void writeEntry(QJsonObject& obj, const BaselineEntry& e)
{
    obj.insert(QStringLiteral("size"), double(e.size));
    obj.insert(QStringLiteral("mtimeMsec"), double(e.mtimeMsec));
}

BaselineEntry readEntry(const QJsonObject& obj)
{
    BaselineEntry e;
    // 新键优先，旧单字母键（s/m）回退兼容一次性读取
    const QJsonValue sv = obj.value(QStringLiteral("size")).isUndefined()
        ? obj.value(QStringLiteral("s")) : obj.value(QStringLiteral("size"));
    e.size = qint64(sv.toDouble(-1));
    const QJsonValue mv = obj.value(QStringLiteral("mtimeMsec")).isUndefined()
        ? obj.value(QStringLiteral("m")) : obj.value(QStringLiteral("mtimeMsec"));
    e.mtimeMsec = qint64(mv.toDouble(0));
    return e;
}

} // namespace

bool baselineLoad(SyncBaseline* out)
{
    if (!out) {
        return false;
    }
    *out = SyncBaseline();
    QFile f(baselinePath());
    if (!f.exists()) {
        return false;
    }
    if (!f.open(kBaselineReadMode)) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isEmpty()) {
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.isEmpty()) {
        return false;
    }
    out->schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(1);
    out->lastMode = root.value(QStringLiteral("lastMode")).toString();
    out->lastRunMsec =
        qint64(root.value(QStringLiteral("lastRunMsec")).toDouble(0));

    const QJsonObject local = root.value(QStringLiteral("local")).toObject();
    const QString baseDir = qAbsPath(QFileInfo(baselinePath()));
    for (auto it = local.constBegin(); it != local.constEnd(); ++it) {
        out->local.insert(compatRel(it.key(), baseDir),
                          readEntry((*it).toObject()));
    }
    const QJsonObject cloud = root.value(QStringLiteral("cloud")).toObject();
    for (auto it = cloud.constBegin(); it != cloud.constEnd(); ++it) {
        out->cloud.insert(compatRel(it.key(), baseDir),
                          readEntry((*it).toObject()));
    }
    return true;
}

bool baselineSave(const SyncBaseline& b)
{
    const QString path = baselinePath();
    qMkdir(qAbsPath(QFileInfo(path)));

    QSaveFile f(path);
    if (!f.open(kBaselineWriteMode)) {
        return false;
    }
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), b.schemaVersion);
    root.insert(QStringLiteral("lastMode"), b.lastMode);
    root.insert(QStringLiteral("lastRunMsec"), double(b.lastRunMsec));

    QJsonObject local;
    for (auto it = b.local.constBegin(); it != b.local.constEnd(); ++it) {
        QJsonObject e;
        writeEntry(e, *it);
        local.insert(it.key(), e);
    }
    QJsonObject cloud;
    for (auto it = b.cloud.constBegin(); it != b.cloud.constEnd(); ++it) {
        QJsonObject e;
        writeEntry(e, *it);
        cloud.insert(it.key(), e);
    }
    root.insert(QStringLiteral("local"), local);
    root.insert(QStringLiteral("cloud"), cloud);

    if (f.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) < 0) {
        return false;
    }
    return f.commit();
}

} // namespace davbisync