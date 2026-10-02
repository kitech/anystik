#ifndef QLSTIK_QVARIANT_SHIM_H
#define QLSTIK_QVARIANT_SHIM_H

// ══ QVariant / QVariantMap 的跨版本访问垫片 ══════════════════════════════
//
// 问题：Qt4.0 给 QMap<Q,V> 补了 value(key) / value(key, def)（qmap.h），而
// **Qt3 的 QMap 没有** —— 只有 find()/operator[]。于是 Qt6 上自然的
//     hint.value("total", -1).toLongLong()
// 在 Qt3 下报
//     'QMap<QString, QVariant>' has no member named 'value'
//
// ⚠ 为什么不用 qmap_shim.h 解决：那个垫片靠**对象式宏** `#define QMap ...`
//   改写整个 TU 的 QMap token。但 QVariantMap 本身是 typedef ——
//   Qt3 qvariant.h:87 `typedef QMap<QString, QVariant> QStringVariantMap;`
//   ——在 qvariant.h 被解析的那一刻就已经定型。qvariant.h 有 include guard，
//   宏定义得再晚也**改写不了已完成的 typedef**，所以拉 qmap_shim.h 进来
//   既修不好这些调用点，还会把整个 TU 的 QMap（含无关代码）一起换掉。
//   故用自由函数，逐个调用点显式替换。
//
// ⚠ 与 qmap_shim.h 语义差异（保持一致，未做额外放宽）：
//   Qt3 的 QMap 是**有序**的（按 key 排序），Qt6 的 QVariantMap 底层
//   QHash 无序。本垫片只提供按 key 取值，**不暴露遍历顺序**，故不引入
//   顺序差异；调用点若要遍历需自行确认不依赖顺序。

#include <qglobal.h>

#ifdef QT3_BUILD
#include <qvariant.h>
#include <qstring.h>
// Qt3 没有 QVariantMap 这个名字，只有 QStringVariantMap（qvariant.h:87）。
// 补一个同义 typedef，让本头及调用点两版本都写 QVariantMap。
// （重复 typedef 同一类型在 C++ 里合法，故与 stickerstore.h:27 的写法不冲突。）
typedef QStringVariantMap QVariantMap;
#else
#include <QVariant>
#include <QVariantMap>
#include <QString>
#endif

// ── qVariantMapValue()：单参版（缺失返回「空 QVariant」）────────────────────
// 对齐 Qt6：QMap::value(key) 缺失时返回默认构造的 V（QVariant 即 invalid）。
inline QVariant qVariantMapValue(const QVariantMap& m, const QString& key)
{
#ifdef QT3_BUILD
    QMap<QString, QVariant>::ConstIterator it = m.find(key);
    return (it == m.end()) ? QVariant() : it.data();
#else
    return m.value(key);
#endif
}

// ── qVariantMapValue()：双参版（缺失返回 def）─────────────────────────────
// 对齐 Qt6：QMap::value(key, def) 缺失时返回 def，且 **def 的类型即结果类型**
// （不会被转成 default-constructed V）—— 这一点对
// `.toLongLong()` / `.toString()` 的结果有直接影响，必须保留。
inline QVariant qVariantMapValue(const QVariantMap& m, const QString& key,
                                 const QVariant& def)
{
#ifdef QT3_BUILD
    QMap<QString, QVariant>::ConstIterator it = m.find(key);
    return (it == m.end()) ? def : it.data();
#else
    return m.value(key, def);
#endif
}

#endif // QLSTIK_QVARIANT_SHIM_H
