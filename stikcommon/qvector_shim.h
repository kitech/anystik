#ifndef QLSTIK_QVECTOR_SHIM_H
#define QLSTIK_QVECTOR_SHIM_H

// Qt 3.5 的 "QVector" 只是 qptrvector.h 里的宏（#define QVector QPtrVector），
// 即**指针向量**；Qt4 的 QVector 是值向量。这里 #undef 后补一个同名派生类，
// 源码零改动。
//
// ── 为什么基类选 QValueVector<T> 而不是 QPtrVector<T> ─────────────────────
//
// 初版用 QPtrVector<T> 作基类，实测（/opt/qt338sh 真机）踩到两个硬伤：
//
//   1. 访问语义错位：QPtrVector 的 operator[](int) 返回 `type*`、
//      at(uint) 返回 `type*`（qptrvector.h:86-87）。Qt4 的 QVector 返回
//      `T&`/`T`。于是 `int ms = delayMs[i]` 报 "invalid conversion from
//      ‘int*’ to ‘int’"。可在派生类里补引用语义重载绕过。
//
//   2. **空表无法写入**（无法绕过，是选基类的决定性理由）：
//        QPtrVector<int> v;
//        v.insert(0, &p);   → "QGVector::insert: Index 0 out of range"，size 仍 0
//        v.fill(p, 1);      → 同样失败，count 仍 0
//      Qt3 的 QGVector 用 len==0 表示「空表」，而 insert 的下界检查把 0 当
//      非法下标（qgvector.h:79 的 fill 亦然）。**append 到空表必失败** ——
//      而 `QVector<T> v; v.append(x)` 是最常见的写法。
//      另注：QGVector::size() 返回 len（**已分配容量**，qgvector.h:69），
//      不是元素个数（numItems），所以不能用 size() 定位 insert 位置。
//
// QValueVector<T>（qvaluevector.h:236）则是 Qt3 自带的**值向量**，正是 Qt4
// QVector 的前身：append（:479）、operator[]、at、size()、isEmpty()、
// typedef value_type/reference/iterator（:240-250）全是值语义，与 Qt4 一致。
// 实测 append×3 + [] 读写 + 范围累加全部正常。仅缺 removeAll 与 begin/end，
// 在派生类里补齐即可（Qt4 的 QValueVector 被 QVector 取代时正是补上这些）。
//
// ⚠ 派生类里若要用基类的 at()，必须写 `QValueVector<T>::at(i)` 显式限定：
//   本类新加的 removeAll/begin 等若走 this->at()，在有重载时会选中错的那个。

#include <qvaluevector.h>

#ifdef QVector
#undef QVector
#endif

template <class T>
class QVector : public QValueVector<T> {
public:
    typedef typename QValueVector<T>::iterator iterator;
    typedef typename QValueVector<T>::const_iterator const_iterator;

    // 按值删除所有等于 item 的元素（Qt4 的 QVector::removeAll）。
    // Qt3 的 QValueVector 无 removeAll、无按索引 remove（只有 erase(iterator,
    // iterator)，resize() 内部即用它，:429），故倒序 erase 单个元素 ——
    // 正序会漏删（删除后后续元素前移）。
    void removeAll(const T& item) {
        for (int i = (int)this->size() - 1; i >= 0; --i) {
            if (this->at(i) == item)
                this->erase(this->begin() + i);
        }
    }

    // 按值 contains（Qt4 语义）。QValueVector 没有同名成员。
    bool contains(const T& item) const {
        for (int i = 0; i < (int)this->size(); ++i) {
            if (this->at(i) == item) return true;
        }
        return false;
    }

    iterator begin() { return QValueVector<T>::begin(); }
    iterator end()   { return QValueVector<T>::end(); }
    const_iterator begin() const { return QValueVector<T>::begin(); }
    const_iterator end()   const { return QValueVector<T>::end(); }

    // operator<<：Qt4 的 QVector 有 `v << x`（返回 QVector&，可链式）。
    // Qt3 的 QValueVector 只有 append(const T&)（qvaluevector.h:479），
    // 且注意 qvaluevector.h:563 那个 operator<< 是 QDataStream 的友元，
    // 与容器追加无关 —— 所以这里必须自己补一个。
    QVector<T>& operator<<(const T& x) {
        this->append(x);
        return *this;
    }
};

#endif // QLSTIK_QVECTOR_SHIM_H
