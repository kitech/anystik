#ifndef STIKCOMMON_JSONL_LWW_STORE_H
#define STIKCOMMON_JSONL_LWW_STORE_H

// JSONL 逐记录 LWW 折叠存储（纯 QtCore，Qt3/4/5/6 通用）。
// 记录 = 单行 JSON 对象；key 字段固定 "rel"，时间戳字段固定 "mtime"
//（epoch 毫秒）。同 key 取 mtime 大者；相等时 "desc" 字典序大者胜
//（确定性 tie-break，保证两端收敛）。坏行 / 非对象行跳过。
//
// 底座：QJson / QSaveFile 复用 stikcommon 的跨版本垫片（Qt5.1 才引入），
// Qt5+ 走原生。构造 QByteArray 一律走 (int) 分配 + memcpy，因为 Qt3 的
// QByteArray 没有 (const char*, int) 构造函数。
#include "qglobaltype_shim.h"

#ifdef QT3_BUILD
#include <qstring.h>
#include <qmap.h>
#else
#include <QString>
#include <QMap>
#endif

#if QT_VERSION < 0x050000
#include "qjson_shim.h"
#else
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#endif

#include <string>

namespace jsonl_lww {

// 读整个文件（不存在/打不开/读失败 → 空串）；不做任何解释。
std::string readFileRaw(const QString& absPath);

// 把 data 的每一行并入 io（坏行跳过；同 key 按 LWW 覆盖/插入）。
void foldInto(QMap<QString, QJsonObject>& io, const std::string& data);

// 解析整段为折叠后的映射。
QMap<QString, QJsonObject> parse(const std::string& data);

// 序列化为 JSONL（每行 Compact + '\n'；QMap 有序 → 输出确定性）。
std::string serialize(const QMap<QString, QJsonObject>& m);

// 原子整写（QSaveFile；自动建父目录）。成功返回 true。
bool writeFile(const QString& absPath, const QMap<QString, QJsonObject>& m);

} // namespace jsonl_lww

#endif // STIKCOMMON_JSONL_LWW_STORE_H
