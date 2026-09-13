#include "davbisync_baseline.h"

#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace davbisync {

namespace {

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
            key = key.mid(basePrefix.size());
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
    if (!f.open(QIODevice::ReadOnly)) {
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
    const QString baseDir = QFileInfo(baselinePath()).absolutePath();
    for (auto it = local.constBegin(); it != local.constEnd(); ++it) {
        out->local.insert(compatRel(it.key(), baseDir),
                          readEntry(it.value().toObject()));
    }
    const QJsonObject cloud = root.value(QStringLiteral("cloud")).toObject();
    for (auto it = cloud.constBegin(); it != cloud.constEnd(); ++it) {
        out->cloud.insert(compatRel(it.key(), baseDir),
                          readEntry(it.value().toObject()));
    }
    return true;
}

bool baselineSave(const SyncBaseline& b)
{
    const QString path = baselinePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), b.schemaVersion);
    root.insert(QStringLiteral("lastMode"), b.lastMode);
    root.insert(QStringLiteral("lastRunMsec"), double(b.lastRunMsec));

    QJsonObject local;
    for (auto it = b.local.cbegin(); it != b.local.cend(); ++it) {
        QJsonObject e;
        writeEntry(e, it.value());
        local.insert(it.key(), e);
    }
    QJsonObject cloud;
    for (auto it = b.cloud.cbegin(); it != b.cloud.cend(); ++it) {
        QJsonObject e;
        writeEntry(e, it.value());
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