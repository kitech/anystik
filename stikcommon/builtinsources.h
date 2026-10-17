#ifndef STIKCOMMON_BUILTINSOURCES_H
#define STIKCOMMON_BUILTINSOURCES_H

// 内置下载源唯一表 + 归一化/命中判定（共享非 UI 层，单一实现）。
// 本表是「唯一改源点」；anystik 与 qlstik 都只消费这一份，不得再有第二份。

#ifdef QT3_BUILD
#include <qstring.h>
// qint64 是 Qt4 引入的类型别名；Qt3 只有 Q_LONG，由本垫片提供
#include "qglobaltype_shim.h"
#else
#include <QString>
#endif

// 单条内置源。字段含义：
//   approxSize 预告约值（静态，非运行时所得）；-1 = 未获取到
//   previewUrl 在线预览页；nullptr = 无预览
//   updated    源更新时间（上游发布日期/实测首测日期）；未知填 "今日"
//   enabled    true=上线显示；false=预留隐藏（不显示、不参与残留清理、不生成 probe）
struct BuiltinSource {
    const char* name;
    const char* url;
    qint64 approxSize;
    const char* previewUrl;
    const char* updated;
    bool enabled;
};
extern const BuiltinSource kBuiltinSources[];
extern const unsigned kBuiltinSourceCount;

namespace builtinsrc {

// 剥任意 gh-proxy 代理前缀（.com/.org），防改代理导致匹配失效
QString normalizeUrl(const QString& url);

// 目录名净化：剔非法字符 / 不可打印 / 控制字符；空则返回 "pack"
QString sanitizeDirName(const QString& name);

// 命中判定：url 归一化命中 或 title == name / sanitizeDirName(name)
bool matches(const QString& url, const QString& title);

// 诊断串（url 可为空）；字段与 anystik 原 builtinSourceDiag 完全一致
QString diag(const QString& packId, const QString& url, const QString& title);

} // namespace builtinsrc

#endif // STIKCOMMON_BUILTINSOURCES_H
