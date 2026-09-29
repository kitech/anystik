#include "config.h"

#include "cJSON.h"

#ifdef QT3_BUILD
#include <qdir.h>
#else
#include <QDir>
#endif

#include <fstream>
#include <sstream>
#include <map>

// ═══ 路径 ═══

QString Config::configDir()
{
#ifdef QT3_BUILD
    return QDir::homeDirPath() + "/.config/qlstik";
#else
    return QDir::homePath() + "/.config/qlstik";
#endif
}

QString Config::configFilePath()
{
    return configDir() + "/config.json";
}

// ═══ 内部工具 ═══

static std::string configFilePathStd()
{
    return std::string(qToUtf8(Config::configFilePath()).data());
}

static cJSON* loadConfigRoot()
{
    std::ifstream ifs(configFilePathStd());
    if (!ifs.is_open()) { return nullptr; }
    std::stringstream ss;
    ss << ifs.rdbuf();
    return cJSON_Parse(ss.str().c_str());
}

static bool ensureConfigDir()
{
    QString dir = Config::configDir();
    QDir d(dir);
    if (d.exists()) { return true; }
    return qMkdir(dir);
}

static bool saveConfigRoot(cJSON* root)
{
    if (!root) { return false; }
    if (!ensureConfigDir()) { return false; }
    char* jsonStr = cJSON_Print(root);
    if (!jsonStr) { return false; }
    std::ofstream ofs(configFilePathStd());
    bool ok = ofs.is_open();
    if (ok) { ofs << jsonStr; }
    free(jsonStr);
    return ok;
}

// ═══ 读写 ═══

QString Config::value(const QString& key, const QString& def)
{
    cJSON* root = loadConfigRoot();
    if (!root) { return def; }
    QString out = def;
    cJSON* item = cJSON_GetObjectItem(root, qToUtf8(key).data());
    if (cJSON_IsString(item)) {
        out = qFromUtf8(item->valuestring);
    } else if (item && (cJSON_IsTrue(item) || cJSON_IsFalse(item))) {
        out = cJSON_IsTrue(item) ? "true" : "false";
    }
    cJSON_Delete(root);
    return out;
}

bool Config::setValue(const QString& key, const QString& value)
{
    cJSON* root = loadConfigRoot();
    if (!root) {
        root = cJSON_CreateObject();
        if (!root) { return false; }
    }
    const char* k = qToUtf8(key).data();
    // cJSON 的 add_item_to_object 是**追加**不是替换：不清旧键会写出重复键，
    // 而 cJSON_GetObjectItem 取首个匹配 → 回读拿到旧值。必须先删。
    if (cJSON_GetObjectItem(root, k)) {
        cJSON_DeleteItemFromObject(root, k);
    }
    // cJSON_AddStringToObject 会 strdup 该字符串
    cJSON_AddStringToObject(root, k, qToUtf8(value).data());
    bool ok = saveConfigRoot(root);
    cJSON_Delete(root);
    return ok;
}

// ═══ 便捷读取 ═══

QString Config::uiLang()
{
    return value("uilang", "zh-CN");
}

QString Config::styleId()
{
    return value("style", "qtFusion");
}

bool Config::darkMode()
{
    return value("dark", "true") == "true";
}
