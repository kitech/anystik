#ifndef QLSTIK_QSET_SHIM_H
#define QLSTIK_QSET_SHIM_H

// Qt 3.5 没有 QSet（只有 QValueList/QMap）。3c-2 的 davbisync 用到 QSet<QString>
// m_scannedCloudDirs，用到的 API：clear / contains / insert / size。
// clear / contains / size / begin / end 均由 QMap<Key,Key> 同名同义提供，
// 唯一差异是 QMap::insert 需要 value，故派生一个只补一参 insert。
//
// 注意：QMap 按 key 有序，QSet 不保证。davbisync 只把 m_scannedCloudDirs 当
// “这个目录列过了吗”的去重过滤，**不遍历、不依赖顺序**，故顺序差异无影响。
// 仅 Qt3 引入（Qt4+ 有原生 QSet）。

#include <qglobal.h>

#ifdef QT3_BUILD   // 仅 Qt3 需要垫片：Qt4+ 有原生 QSet

#include <qmap.h>
#include <qstring.h>

template <class Key>
class QSet : public QMap<Key, Key> {
public:
    // Qt4 的 QSet::insert 返回 iterator；此处按 Qt4 语义补齐。
    // davbisync 目前不取返回值，但保留返回以免日后误用为 void。
    typename QMap<Key, Key>::iterator insert(const Key& key) {
        return this->QMap<Key, Key>::insert(key, key);
    }
};

#else

// Qt4+ 用原生 QSet。本头仍需引入它，否则依赖方（如 qba_shim.h 的
// qByteArraySetBuildAscii）在本头被加守卫后就再也拿不到 QSet 定义，
// 报 "return type 'class QSet<QByteArray>' is incomplete"。
#include <QSet>

#endif

#endif // QLSTIK_QSET_SHIM_H
