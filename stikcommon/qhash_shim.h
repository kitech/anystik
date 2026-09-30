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
        if (it == this->end()) {
            // Qt3 的 QMap::insert 返回 **iterator**（qmap.h:732），不是引用；
            // Qt4+ 的 QHash::insert 才返回 T&。所以不能 `return this->insert(...)`
            // ——那会把迭代器当 T& 返回。这里 insert 后重新 find 拿引用。
            this->insert(key, T());
            it = this->find(key);
        }
        return *it;
    }

    // constFind/constEnd 是 Qt4.1 才有的；Qt3 的 QMap 只有 const 版的
    // begin()/end()/find()（qmap.h:383-386），没有 const 作用域的 constFind/constEnd。
    // 调用点写 h.constFind(k) 拿 const 迭代器、与 Qt4+ 的 constFind 同形：
    //   * constFind 语义完全一致（找不到返回 end()，不解引用）
    //   * constEnd  = const 版的 end()，用于判空
    // 返回类型显式写 ConstIterator（Qt3 的名字；Qt4+ 里叫 const_iterator，
    // 但本类只在 Qt3 下存在，不存在跨版本名冲突问题）。
    typename QMap<Key, T>::ConstIterator constFind(const Key& key) const
    {
        return this->find(key);
    }

    typename QMap<Key, T>::ConstIterator constEnd() const
    {
        return this->end();
    }
};

#endif // QLSTIK_QHASH_SHIM_H
