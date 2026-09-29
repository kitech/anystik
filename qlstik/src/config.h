#ifndef QLSTIK_CONFIG_H
#define QLSTIK_CONFIG_H

// qlstik 壳配置：uilang / style / dark 三键
// 存储：~/.config/qlstik/config.json（cJSON，只用 qldox 带来的 cJSON，不引 hjson）
// 模式参照 qltox/config.cpp（只读参考），差异：
//   * 目录名 qlstik（不碰 qltox 的 ~/.config/qltox）
//   * 纯 cJSON 读写（qltox 的 value/setValue 走 hjson，loadRoot/saveRoot 走 cJSON，
//     两套并存；qlstik 这里只留 cJSON 一条路径）
//   * 带默认值回退，缺键/坏文件都不崩
#include "compat34.h"

class Config {
public:
    // 取值；键不存在且无默认时返回空串
    static QString value(const QString& key, const QString& def = QString());
    // 写值（读-改-写，保留其它键）；成功返回 true
    static bool setValue(const QString& key, const QString& value);

    // 便捷读取：语言（zh-CN / zh-TW / en-US）
    static QString uiLang();
    // 便捷读取：皮肤 id（qtFusion / macOS / windows / material / github）
    static QString styleId();
    // 便捷读取：深色模式
    static bool darkMode();

    static QString configDir();
    static QString configFilePath();
};

#endif // QLSTIK_CONFIG_H
