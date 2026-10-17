#ifndef QLSTIK_CONFIG_H
#define QLSTIK_CONFIG_H

// qlstik 壳配置：language / darkMode / style
// 存储：与 anystik 共用 ~/.config/fedlet/anystik.conf（QSettings34 = Qt3 版高版本
//       QSettings，落盘格式/键序/@Variant 编解码对齐 Qt6，故两程序能互读）。
// 键名对齐 anystik：
//   language    共享（qlstik 值域 zh-CN/zh-TW/en-US；落盘 en-US→en，读回 en→en-US）
//   darkMode    共享 bool
//   storageRoot 共享（stickerlistpage 用）
//   page_*      共享（page_<id>_state=map / pageManager_history=StringList /
//                     pageManager_currentId=str，与 anystik pagemanager 同名同型）
//   style       qlstik 私有（anystik 无此键；其皮肤是 QSkinny 的 "skin" 整数）
#include "compat34.h"

class Config {
public:
    // 取值；键不存在且无默认时返回空串
    static QString value(const QString& key, const QString& def = QString());
    // 写值（读-改-写，保留其它键）；成功返回 true
    static bool setValue(const QString& key, const QString& value);

    // 便捷读取：语言（zh-CN / zh-TW / en-US）
    static QString uiLang();
    static bool setUiLang(const QString& lang);
    // 便捷读取：皮肤 id（qtFusion / macOS / windows / material / github）
    static QString styleId();
    // 便捷读取：深色模式
    static bool darkMode();
    static bool setDarkMode(bool on);

    static QString configDir();
    static QString configFilePath();
};

#endif // QLSTIK_CONFIG_H
