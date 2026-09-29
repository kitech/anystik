#ifndef QLSTIK_QHASH_SHIM_H
#define QLSTIK_QHASH_SHIM_H

// Qt 3.5 没有 QHash（只有 QValueList/QMap）。批次 1 的 eifreader 用到 QHash，
// 用到的 API：operator[] / contains / insert / size / isEmpty /
//              constFind / constEnd / value
// 全部与 QMap 同名同义，唯一差异是 QMap 无 operator[]，故派生一个只补它。
//
// 注意：QMap 按 key 有序，QHash 不保证；eifreader 只做查表/插入，不依赖顺序，
// 行为一致。仅 Qt3 引入（Qt4+ 有原生 QHash）。

#include <qmap.h>

template <class Key, class T>
class QHash : public QMap<Key, T> {
public:
    T& operator[](const Key& key) {
        typename QMap<Key, T>::iterator it = this->find(key);
        if (it == this->end())
            return this->insert(key, T());
        return *it;
    }
};

#endif // QLSTIK_QHASH_SHIM_H
