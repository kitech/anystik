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

void writeEntry(QJsonObject& obj, const BaselineEntry& e)
{
    obj.insert(QStringLiteral("s"), double(e.size));
    obj.insert(QStringLiteral("m"), double(e.mtimeMsec));
}

BaselineEntry readEntry(const QJsonObject& obj)
{
    BaselineEntry e;
    e.size = qint64(obj.value(QStringLiteral("s")).toDouble(-1));
    e.mtimeMsec = qint64(obj.value(QStringLiteral("m")).toDouble(0));
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
    for (auto it = local.constBegin(); it != local.constEnd(); ++it) {
        out->local.insert(it.key(), readEntry(it.value().toObject()));
    }
    const QJsonObject cloud = root.value(QStringLiteral("cloud")).toObject();
    for (auto it = cloud.constBegin(); it != cloud.constEnd(); ++it) {
        out->cloud.insert(it.key(), readEntry(it.value().toObject()));
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