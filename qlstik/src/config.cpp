#include "config.h"

#include "qsettings34.h"

// ═══ 路径（与 anystik 共用同一份；org=fedlet / app=anystik）═══

QString Config::configDir()
{
    return QSettings34::userConfigDir() + QString::fromLatin1("/fedlet");
}

QString Config::configFilePath()
{
    return QSettings34::filePathFor(QString::fromLatin1("fedlet"),
                                    QString::fromLatin1("anystik"));
}

// ═══ 读写 ═══

QString Config::value(const QString& key, const QString& def)
{
    QSettings34 s;
    return s.value(key, QVariant(def)).toString();
}

bool Config::setValue(const QString& key, const QString& value)
{
    QSettings34 s;
    s.setValue(key, QVariant(value));
    return s.sync();
}

// ═══ 便捷读取 ═══

QString Config::uiLang()
{
    const QString raw = value(QString::fromLatin1("language"));
    if (raw.isEmpty()) { return QString::fromLatin1("zh-CN"); }
    // anystik 存 "en"；qlstik 值域是 "en-US"
    if (raw == QString::fromLatin1("en")) { return QString::fromLatin1("en-US"); }
    return raw;
}

bool Config::setUiLang(const QString& lang)
{
    const QString store = (lang == QString::fromLatin1("en-US"))
                              ? QString::fromLatin1("en") : lang;
    return setValue(QString::fromLatin1("language"), store);
}

QString Config::styleId()
{
    return value(QString::fromLatin1("style"), QString::fromLatin1("qtFusion"));
}

bool Config::darkMode()
{
    QSettings34 s;
    // 默认 true：与 anystik 一致（anystik 侧两处读取默认值亦为 true）
    return s.valueBool(QString::fromLatin1("darkMode"), true);
}

bool Config::setDarkMode(bool on)
{
    QSettings34 s;
    s.setValue(QString::fromLatin1("darkMode"), QVariant(on));
    return s.sync();
}
