#ifndef QLSTIK_QSETTINGS_SHIM_H
#define QLSTIK_QSETTINGS_SHIM_H

// ══ QSettings 的 QVariant 存取垫片 ════════════════════════════════════════
//
// 问题：Qt3 的 QSettings（即 QConf）**根本没有** value()/setValue()/remove()/
// childKeys() —— 它只有类型化的 readEntry/writeEntry/readNumEntry/
// readBoolEntry/readDoubleEntry/readListEntry/removeEntry/entryList
// （qsettings.h:75-106）。Qt4.0 才引入带 QVariant 的那套 API。
// 换句话说，不是「Qt3 的 QSettings 少几个方法」，而是**存储模型不同**：
//   Qt3  键 → 类型化文本条目，读时必须知道期望类型；
//   Qt4+ 键 → QVariant，读时带一个默认值、返回 QVariant 再 toXxx()。
//
// 本垫片做法（Qt3 侧）：把 QVariant **按其真实类型**落到对应类型的
// writeEntry 重载上，读时按调用点声明的期望类型选对应 readXxxEntry。
// 于是不需要自造序列化格式，天然复用 Qt3 原生 ini 落盘与转义。
//
// ⚠ Qt3 与 Qt6 的落盘文件**不互通**：Qt3 ini 的条目是纯文本（无类型标记），
//   同一个键在 Qt6 下读出来靠 QVariant 的运行时类型。跨版本共用一份配置会
//   出现「Qt3 写入 bool、Qt6 读到 string」之类。故调用点若涉及跨端共享
//   配置，需另行处理；本垫片只保证**同一版本内**行为与 Qt6 等价。
//
// ⚠ 键名里的 '/' 是 Qt 的 group 分隔符，两版本一致，但 **Qt3 的叶键必须带
//   前导 '/'**：实测 writeEntry("g/a") 报 "invalid key '/top'"（Qt3 隐式补
//   '/' 后变 '/g/a' 而非法），writeEntry("/g/a") 才合法；子 group 名则不带
//   前导（"g" 而非 "/g"）。故本垫片的键名参数对调用方是**原样透传**的，
//   调用点沿用 Qt6 写法即可，但自己新写 Qt3 分支时务必带上前导 '/'。
//   （qSettingsChildKeys 处的注释与探针记录了完整实测结果。）

#ifdef QT3_BUILD
#include <qsettings.h>
#include <qvariant.h>
#include <qcstring.h>
#include <qstringlist.h>
#include <qmap.h>
// qulonglong（下方 qSettingsValueULongLong 用）与 QVariantMap（qSettingsValueMap
// 用）在 Qt3 侧的名字/来源与 Qt6 不同：
//   · qulonglong → Q_ULLONG（qglobal.h:82 的 Qt3 基础整型 typedef）。Qt6 的
//     qulonglong 由 qglobal.h 提供，Qt3 裁剪 SDK 无同名符号。
//   · QVariantMap → QStringVariantMap（qvariant.h:87）。Qt6 直接叫 QVariantMap。
// 两者都由各自的垫片头补齐，本头 include 之即可（与 qbytearray_shim.h 自行
// typedef qint64 同理：不假设调用方已经间接引入过）。
#include "qglobaltype_shim.h"
#include "qvariant_shim.h"
// qSettingsJsonToMap/qSettingsMapToJson 走 qjson_shim.h 的 QJsonDocument。
// Qt3 无 QVariant::toMap/fromMap，故 map 型 entry 借 JSON 文本通道落地
// （见 qSettingsValueMap 的说明）。
// ⚠ 无条件 include：已在 Qt3 分支内；且 qjson_shim.h 自带版本守卫
//   （Qt5+ 分支为空），Qt6 侧包含它也无副作用。
#include "qjson_shim.h"
#else
#include <QSettings>
#include <QVariant>
#include <QStringList>
#include <QMap>
#endif

#ifdef QT3_BUILD
// ══ 键规范化 ══════════════════════════════════════════════════════════════
//
// ⚠ 本垫片存在期间最严重的一个坑：不规范化键，**所有根层裸键都静默丢失**。
//
// Qt3 的 QSettings 只接受 "group/leaf" 形式，且 group 与 leaf 都不可为空。
// /opt/qt338sh 实测（probe_key）：
//   writeEntry("s")     → 日志 "invalid key '/s'"，写不进也读不出
//   writeEntry("/s")    → 同上（Qt3 自己补 '/'，前导斜杠无意义）
//   writeEntry("g/s")   → OK
//   writeEntry("g/h/s") → OK（group 可多层）
//   writeEntry("g/")    → invalid（leaf 为空）
//   writeEntry("")      → invalid
// 而 Qt6 的 QSettings::value/setValue 完全接受裸键 "s"。
// stickerstore.cpp 用的正是根层裸键（"storageRoot"、"dlProgress/<hex>"），
// 于是「Qt6 跑得好好的配置，Qt3 下 writeEntry 全部报 invalid key、
// readEntry 全部返回默认值」——不报任何错，只是配置永远不生效。
//
// 规范化规则（与 Qt6 键语义对齐）：
//   1. 去掉前导与尾部的 '/'（Qt3 隐式补首 '/'，前导斜杠无意义；尾部
//      '/' 会让 leaf 变空而非法）。
//   2. 去掉后若不含 '/'，说明是**根层叶键**。Qt3 无法表达"无 group 的
//      叶键"，故注入一个合成 group "qlstik/" 来承载：
//        "storageRoot"        → "qlstik/storageRoot"
//        "dlProgress/abc123"  → "dlProgress/abc123"（本就含组，原样）
//   3. 规范化后为空（原键是 "" / "/" / "///"）→ 无法表达，返回 false，
//      各访问函数直接返回默认值（等价于"键不存在"，与 Qt6 一致）。
//
// 合成组名 "qlstik" 的取值约束：不能与用户真实 group 撞名。取了一个
// 不像业务名的串；若产品将来真要存 "qlstik/x"，它与顶层 "x" 会被当成
// 同一个键（互相覆盖）——见 qSettingsChildKeys 里前缀剥离的对应说明。
inline QString qSettingsQt3Key(const QString& key, bool* okOut)
{
    QString k = key;
    while (k.startsWith("/")) k = k.mid(1);
    while (k.endsWith("/"))  k = k.left(k.length() - 1);
    if (k.isEmpty()) {                       // 原键为 "" / "/" / "///"
        if (okOut) *okOut = false;
        return QString();
    }
    if (k.find('/') < 0) k = "qlstik/" + k;  // 根层裸键 → 合成组
    if (okOut) *okOut = true;
    return k;
}
#endif // QT3_BUILD

// ══ 读 ══════════════════════════════════════════════════════════════════
// 每种期望类型一个函数：Qt3 侧直接选对应 readXxxEntry；Qt6 侧转调原生
// value() 并让调用点自己 toXxx()。默认值语义对齐 Qt6：键不存在时返回 def。
//
// ⚠ Qt3 的 readXxxEntry 找不到键时返回 def 且 *ok=FALSE —— 与 Qt6 的
//   value(key, def) 一致（都返回默认值）。故 ok 参数统一忽略。

#ifdef QT3_BUILD
// 前向声明：qSettingsValueMap/qSettingsSetMap 在下方调用这两个函数，定义
// 在其后（紧跟 qSettingsSetMap）。
// ⚠ 必须显式声明，不能只靠「同 TU 内后面有定义」—— qSettingsValueMap 是
//   inline 函数，名字在其定义处就要解析完，那时后面的声明还看不到。
inline QVariantMap qSettingsJsonToMap(const QString& json, bool* ok);
inline QString qSettingsMapToJson(const QVariantMap& m);
#endif

// QString
inline QString qSettingsValueStr(const QString& key, const QString& def = QString())
{
#ifdef QT3_BUILD
    bool kok = false;
    const QString k = qSettingsQt3Key(key, &kok);
    return kok ? QSettings().readEntry(k, def) : def;
#else
    const QVariant v = QSettings().value(key, def);
    return v.isValid() ? v.toString() : def;
#endif
}

// int
inline int qSettingsValueInt(const QString& key, int def = 0)
{
#ifdef QT3_BUILD
    bool kok = false;
    const QString k = qSettingsQt3Key(key, &kok);
    return kok ? QSettings().readNumEntry(k, def) : def;
#else
    const QVariant v = QSettings().value(key, def);
    return v.isValid() ? v.toInt() : def;
#endif
}

// qulonglong（Qt3 侧 readNumEntry 只给 int，见下方 toULongLong 说明）
inline qulonglong qSettingsValueULongLong(const QString& key, qulonglong def = 0)
{
#ifdef QT3_BUILD
    // ⚠ Qt3 的 readNumEntry 返回 int，装不下 >2^31 的值（stickerstore 的
    //   stickergen_seed 是随机种子，64 位）。readEntry 走 QString 通道能保
    //   住全 64 位，故这里用 QString 中转再转数字。
    bool kok = false;
    const QString k = qSettingsQt3Key(key, &kok);
    if (!kok) return def;
    bool ok = false;
    const QString s = QSettings().readEntry(k, QString());
    if (s.isEmpty()) return def;
    const qulonglong v = s.toULongLong(&ok);
    return ok ? v : def;
#else
    const QVariant v = QSettings().value(key, def);
    return v.isValid() ? v.toULongLong() : def;
#endif
}

// bool
inline bool qSettingsValueBool(const QString& key, bool def = false)
{
#ifdef QT3_BUILD
    bool kok = false;
    const QString k = qSettingsQt3Key(key, &kok);
    if (!kok) return def;
    bool ok = false;
    const bool v = QSettings().readBoolEntry(k, def, &ok);
    return ok ? v : def;
#else
    const QVariant v = QSettings().value(key, def);
    return v.isValid() ? v.toBool() : def;
#endif
}

// double
inline double qSettingsValueDouble(const QString& key, double def = 0.0)
{
#ifdef QT3_BUILD
    bool kok = false;
    const QString k = qSettingsQt3Key(key, &kok);
    return kok ? QSettings().readDoubleEntry(k, def) : def;
#else
    const QVariant v = QSettings().value(key, def);
    return v.isValid() ? v.toDouble() : def;
#endif
}

// QStringList
inline QStringList qSettingsValueStringList(const QString& key)
{
#ifdef QT3_BUILD
    bool kok = false;
    const QString k = qSettingsQt3Key(key, &kok);
    if (!kok) return QStringList();
    bool ok = false;
    const QStringList l = QSettings().readListEntry(k, &ok);
    return ok ? l : QStringList();
#else
    const QVariant v = QSettings().value(key);
    return v.isValid() ? v.toStringList() : QStringList();
#endif
}

// QVariantMap
inline QVariantMap qSettingsValueMap(const QString& key)
{
#ifdef QT3_BUILD
    // ⚠ Qt3 无任何 map 型 entry，故用 JSON 文本通道存（qJsonValueToDoc 之类
    //   的现成编解码见 qjson_shim.h）。QVariantMap 的值都是标量/嵌套 map，
    //   JSON 能无损表达；本仓的用法（页面状态、下载提示）也只到标量为止。
    bool kok = false;
    const QString k = qSettingsQt3Key(key, &kok);
    if (!kok) return QVariantMap();
    bool ok = false;
    const QString s = QSettings().readEntry(k, QString());
    if (s.isEmpty()) return QVariantMap();
    QVariantMap m = qSettingsJsonToMap(s, &ok);
    return ok ? m : QVariantMap();
#else
    const QVariant v = QSettings().value(key);
    return v.isValid() ? v.toMap() : QVariantMap();
#endif
}

// ══ 写 ══════════════════════════════════════════════════════════════════

inline void qSettingsSetStr(const QString& key, const QString& v)
{
#ifdef QT3_BUILD
    bool kok = false;
    const QString k = qSettingsQt3Key(key, &kok);
    if (!kok) return;
    QSettings().writeEntry(k, v);
#else
    QSettings().setValue(key, v);
#endif
}

inline void qSettingsSetInt(const QString& key, int v)
{
#ifdef QT3_BUILD
    bool kok = false;
    const QString k = qSettingsQt3Key(key, &kok);
    if (!kok) return;
    QSettings().writeEntry(k, v);
#else
    QSettings().setValue(key, v);
#endif
}

inline void qSettingsSetULongLong(const QString& key, qulonglong v)
{
#ifdef QT3_BUILD
    bool kok = false;
    const QString k = qSettingsQt3Key(key, &kok);
    if (!kok) return;
    QSettings().writeEntry(k, QString::number(v));
#else
    QSettings().setValue(key, v);
#endif
}

inline void qSettingsSetBool(const QString& key, bool v)
{
#ifdef QT3_BUILD
    bool kok = false;
    const QString k = qSettingsQt3Key(key, &kok);
    if (!kok) return;
    QSettings().writeEntry(k, v);
#else
    QSettings().setValue(key, v);
#endif
}

inline void qSettingsSetDouble(const QString& key, double v)
{
#ifdef QT3_BUILD
    bool kok = false;
    const QString k = qSettingsQt3Key(key, &kok);
    if (!kok) return;
    QSettings().writeEntry(k, v);
#else
    QSettings().setValue(key, v);
#endif
}

inline void qSettingsSetStringList(const QString& key, const QStringList& v)
{
#ifdef QT3_BUILD
    bool kok = false;
    const QString k = qSettingsQt3Key(key, &kok);
    if (!kok) return;
    QSettings().writeEntry(k, v);
#else
    QSettings().setValue(key, v);
#endif
}

inline void qSettingsSetMap(const QString& key, const QVariantMap& v)
{
#ifdef QT3_BUILD
    bool kok = false;
    const QString k = qSettingsQt3Key(key, &kok);
    if (!kok) return;
    QSettings().writeEntry(k, qSettingsMapToJson(v));
#else
    QSettings().setValue(key, v);
#endif
}

// ── QVariantMap ⇄ JSON 文本（仅 Qt3 需要）────────────────────────────────
// Qt3 侧 QVariantMap 没有原生序列化通道（QVariant 无 toMap/fromMap，见
// qvariant_shim.h 的说明），故借 qjson_shim.h 的 QJsonDocument 落地。
//
// 为什么用 compact 且不用缩进：这些值要写进 QConf 文本，一行一条便于人工
// 排查、也避免 QSettings 自己加的转义把缩进搅乱。
//
// ⚠ 类型损失：JSON 只能表达 string/number/bool/null/array/object。QVariantMap
//   里的 QString/QStringList 会退化为 array（QStringList 是 QStringList 而非
//   JSON 原生类型），QVariant::ByteArray、Date、DateTime 等自定义类型无法
//   无损表达。本仓当前用法（页面状态、下载提示）只到标量为止，故可接受；
//   若将来要存 Date/ByteArray，须另设计通道（如逐键 type-tag 前缀）。
#ifdef QT3_BUILD

// QVariantMap → JSON 文本。
inline QString qSettingsMapToJson(const QVariantMap& m)
{
    // 惰性递归：QJsonValue 无默认构造的「容器占位」，故按实际类型逐个包。
    struct Conv {
        static QJsonValue from(const QVariant& v)
        {
            switch (v.type()) {
            case QVariant::Map:
                return QJsonValue(toObj(v.toMap()));
            case QVariant::StringList: {
                QJsonArray a;
                const QStringList l = v.toStringList();
                // ⚠ 必须用 `l[i]` 显式下标而非 `l.at(i)`：Qt3 的
                //   QStringList 继承自 QValueList<QString>，其 at() 在
                //   QString 上重载不完整 —— `l.at(i)` 会解析到返回
                //   QValueList<QString>::const_iterator 的重载，传给
                //   QJsonValue(const QString&) 时报
                //   "no matching function ... (QValueList<QString>::const_iterator)"。
                //   Qt6 的 QStringList::at() 直接返回 QString，无此问题。
                for (int i = 0; i < l.size(); ++i) a.append(QJsonValue(l[i]));
                return QJsonValue(a);
            }
            // ⚠ 顺序要紧：Qt3 的 QVariant::String 与 QString 是**不同**的
            //   type 值（QVariant::Type 枚举不同），不能只判 String。
            case QVariant::String:
                return QJsonValue(v.toString());
            case QVariant::Bool:
                return QJsonValue(v.toBool());
            case QVariant::Int:
            case QVariant::UInt:
            case QVariant::LongLong:
            case QVariant::ULongLong:
                return QJsonValue(v.toDouble());
            case QVariant::Double:
                return QJsonValue(v.toDouble());
            default:
                // 未能无损表达的类型：退化为字符串，至少不丢信息
                return QJsonValue(v.toString());
            }
        }
        static QJsonObject toObj(const QVariantMap& m)
        {
            QJsonObject o;
            QVariantMap::const_iterator it = m.begin();
            for (; it != m.end(); ++it) o.insert(it.key(), from(it.data()));
            return o;
        }
    };
    const QJsonObject o = Conv::toObj(m);
    if (o.isEmpty()) return QString();
    const QByteArray json = QJsonDocument(o).toJson(QJsonDocument::Compact);
    // ⚠ Qt3 的 QString 无 toUtf8()（那是 Qt4.1+ 的别名），只有 utf8()；
    //   且 QByteArray→QString 的显式转换即按 latin1 逐字节，而 JSON 文本
    //   是 cJSON 输出的 UTF-8 字节，故这里直接 QString(bytes) 在 Qt3 下
    //   是**错的**。必须走 fromUtf8 的 Qt3 等价。
    return qFromUtf8BA(json);
}

// JSON 文本 → QVariantMap。ok 置 false 表示解析失败或不是 object。
inline QVariantMap qSettingsJsonToMap(const QString& json, bool* ok)
{
    if (ok) *ok = false;
    if (json.isEmpty()) return QVariantMap();
    // ⚠ Qt3 的 QString 无 toUtf8()（Qt4.1+ 才加的别名），只有 utf8()。
    //   不能写 json.toUtf8()，也不能写 QString(json) —— 实测后者是 latin1
    //   逐字节（6 字节的中文会变 6 个 U+00xx 码点），只有 fromUtf8 才对。
    const QJsonDocument doc = QJsonDocument::fromJson(json.utf8());
    if (doc.isEmpty() || !doc.isObject()) return QVariantMap();
    const QJsonObject o = doc.object();
    QVariantMap m;
    QJsonObject::const_iterator it = o.constBegin();
    for (; it != o.constEnd(); ++it) {
        const QJsonValue v = it.value();
        QVariant out;
        // ⚠ 判定顺序：isObject/isArray 必须先于 isString 等，否则嵌套结构
        //   会被当成标量。cJSON 的 null 与「键缺失」都不可区分（QJsonValue
        //   用 node==nullptr 表达 Undefined，见 qjson_shim.h），故 null 存为空
        //   QVariant，读取端 v.isValid() 为假 —— 与 Qt6 读 null 的表现一致。
        if (v.isObject()) {
            struct Back {
                static QVariantMap toMap(const QJsonObject& o)
                {
                    QVariantMap m;
                    QJsonObject::const_iterator it = o.constBegin();
                    for (; it != o.constEnd(); ++it)
                        m[it.key()] = one(it.value());
                    return m;
                }
                static QVariant one(const QJsonValue& v)
                {
                    if (v.isObject()) return toMap(v.toObject());
                    if (v.isArray()) {
                        const QJsonArray a = v.toArray();
                        QStringList l;
                        for (int i = 0; i < a.size(); ++i)
                            l.append(a.at(i).toString());
                        return QVariant(l);
                    }
                    if (v.isBool())   return QVariant(v.toBool());
                    if (v.isDouble()) return QVariant(v.toDouble());
                    if (v.isString()) return QVariant(v.toString());
                    return QVariant();       // null / Undefined
                }
            };
            out = Back::toMap(v.toObject());
        } else if (v.isArray()) {
            const QJsonArray a = v.toArray();
            QStringList l;
            for (int i = 0; i < a.size(); ++i) l.append(a.at(i).toString());
            out = QVariant(l);
        } else if (v.isBool()) {
            out = QVariant(v.toBool());
        } else if (v.isDouble()) {
            out = QVariant(v.toDouble());
        } else if (v.isString()) {
            out = QVariant(v.toString());
        } else {
            out = QVariant();               // null / Undefined
        }
        m[it.key()] = out;
    }
    if (ok) *ok = true;
    return m;
}

#endif // QT3_BUILD

// ══ 删 / 列 ══════════════════════════════════════════════════════════════

#ifdef QT3_BUILD
// 递归展开 group 树收集全路径键。group 为 "." 表示根层（实测根层只有
// "." 可用，见 qSettingsChildKeys 的注释）。prefix 为已累积的路径前缀
// （根层传空串），叶键最终以 "a/b/c" 形式（无前导 '/'）加入 out，与 Qt6
// 的 childKeys() 一致。
//
// 深度不设上限，但 group 名由用户配置写入、树深度有限；若担心恶意配置
// 造成深递归，可在此加深度参数并在超过时截断。
inline void qSettingsCollectKeys(QSettings& st, const QString& group,
                                  const QString& prefix, QStringList& out)
{
    // 叶键：entryList 给本组直接键名，带前导 '/'（如 "/a"）
    QStringList leaves = st.entryList(group);
    for (int i = 0; i < leaves.size(); ++i) {
        QString k = leaves[i];
        if (k.startsWith("/")) k = k.mid(1);      // 剥前导 '/' 对齐 Qt6
        out.append(prefix.isEmpty() ? k : prefix + "/" + k);
    }
    // 子 group：subkeyList 给出子组名（本层自身也在其中，需过滤）
    QStringList subs = st.subkeyList(group);
    for (int i = 0; i < subs.size(); ++i) {
        const QString name = subs[i];
        // 根层 "." 与各层的本层名（"g"、"g/sub"）都要跳过，否则无限递归
        if (name.isEmpty() || name == "." || name == group) continue;
        // 防自指：若某层返回自身名且不等于 group（理论上不会），加个前缀判重
        const QString path = (group == "." || group.isEmpty()) ? name : group + "/" + name;
        qSettingsCollectKeys(st, path,
                             prefix.isEmpty() ? name : prefix + "/" + name, out);
    }
}
#endif // QT3_BUILD

inline void qSettingsRemove(const QString& key)
{
#ifdef QT3_BUILD
    bool kok = false;
    const QString k = qSettingsQt3Key(key, &kok);
    if (kok) QSettings().removeEntry(k);
#else
    QSettings().remove(key);
#endif
}

inline QStringList qSettingsChildKeys()
{
#ifdef QT3_BUILD
    // ⚠⚠ Qt3 分支**实际返回空列表**，这是当前垫片的已知限制，不是"没数据"
    //    时凑合的结果。原因有二，都已核实（见下）：
    //
    //  (1) Qt3 没有 childKeys()，只能靠 entryList()+subkeyList() 递归展开
    //      group 树，但 **subkeyList() 在 INI 后端根本列不出子组**。
    //      Qt 3.3 官方文档 subkeyList() 条目下的 Warning 原文：
    //        "…if QSettings is writing to an Ini file, then a call to
    //         subkeyList("/MyCompany") will return an empty list. … This
    //         call is therefore a request to list the sections in an ini
    //         file, which is not supported in this version of QSettings.
    //         This is a known issue which will be fixed in Qt-4."
    //      隔离 HOME 实测（/opt/qt338sh，写入 grp/a、grp/b、grp/h/c）：
    //        subkeyList(".")    = [.]      ← 只有自己，没有 grp
    //        subkeyList("grp")  = [grp]    ← 只有自己，没有 h
    //        entryList("grp")   = [a, b]   ← 叶键这半边是好的
    //      即递归的第二条腿断了，组树无法遍历。
    //
    //  (2) 即便去解析 INI 文件也补不回来，因为文件把层级压平了。隔离
    //      HOME 实测：写 a/b/c、a/b/d、a/e、top 落到 $HOME/.qt/arc：
    //        [General]
    //        e=3
    //        [b]
    //        c=1
    //        d=2
    //      domain = 键第 1 段（同时决定文件名 arc）、product/section = 第 2 段。
    //      所以只有 3 段以内的键能被忠实还原，4 段以上（如 a/b/c/d）在文件里
    //      已不可区分——靠解析文件做枚举注定不完整。
    //
    //  为什么保留这段递归：entryList() 那半边是对的，一旦 Qt3 的 INI 后端
    //  支持了列 section（Qt4 起已修），这段代码无需改动即可工作。
    //
    //  为什么现在不做"写操作登记表"那种替代实现：那只能列**本进程**经 shim
    //  写过的键，跨进程（典型需求：枚举上次运行落盘的 downloadedPackMeta/*）
    //  会静默漏掉——这是比"明确返回空"更坏的行为，因为调用方无法分辨
    //  "确实没有"和"只看到了本次写的"。目前产品代码尚未接入本垫片
    //  （无消费者），故按需再实现。
    //
    // ⚠ 下面的合成组前缀剥离是为"递归能跑"的假想场景预备的，保留与
    //   qSettingsQt3Key 的写入侧保持对称（若将来 subkeyList 可用即生效）。
    QStringList out;
    QSettings st;
    qSettingsCollectKeys(st, QString("."), QString(), out);
    for (int i = 0; i < out.size(); ++i) {
        if (out[i].startsWith("qlstik/")) out[i] = out[i].mid(7);
    }
    return out;
#else
    return QSettings().childKeys();
#endif
}

#endif // QLSTIK_QSETTINGS_SHIM_H
