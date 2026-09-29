#ifndef QLSTIK_QMAP_SHIM_H
#define QLSTIK_QMAP_SHIM_H

// Qt 3.5 的 QMap 缺 Qt4+ 的四个语义，vendor/qwebdav 与 davbisync 都在用：
//   1) 迭代器只有 data()，没有 value()        （qwebdavdirparser 遍历属性集）
//   2) 没有 constFind() / cbegin() / cend()
//   3) 没有带默认值的 value(key, default)
//   4) erase(iterator) 返回 void，Qt5 语义是返回下一个迭代器（it = m.erase(it)）
//
// Qt3 的 QMap<K,V> 全部成员方法都是非虚、且只操作同一个私有 sh 指针，因此可以
// 安全派生：基类方法（insert/find/operator[]/remove/erase…）在派生对象上行为不变，
// 这里只补 Qt4+ 的缺失面。迭代器用包装类转发到 QMapIterator/QMapConstIterator
//（qmap.h:613-614 的 typedef），包装类可从对方构造（qmap.h:246），
// 两者都提供 key()/data()。
//
// 只在 QT3_BUILD 编译单元 include；Qt4+ 走原生 QMap。

#include <qmap.h>

// Qt3 的 QMap 是真类（不是宏），无法 #undef 后同名重定义：同名模板类在同一
// 作用域会与 Qt3 的 QMap 冲突。做法是先在 detail 命名空间里继承 Qt3 的 QMap
// 补齐 Qt4+ 语义，再用宏把源码里的 QMap 指向它：
//   qlstik_qt3::QMapShim —— 补 value/constFind/cbegin/cend/erase/迭代器 value()
//   宏 QMap = qlstik_qt3::QMapShim
// 向上转型到 Qt3 QMap 自动可用，基类原方法（insert/remove/operator[]/find…）
// 行为不变；Qt3 原生 QMap 仍可通过 ::QMap 使用。

namespace qlstik_qt3 {

template <class K, class V>
class QMapShim : public ::QMap<K, V> {
public:
    typedef ::QMap<K, V> base;
    typedef QMapIterator<K, V> base_iterator;
    typedef QMapConstIterator<K, V> base_const_iterator;

    class const_iterator;

    class iterator {
    public:
        iterator() : m_it() {}
        iterator(const base_iterator& it) : m_it(it) {}

        const K& key() const { return m_it.key(); }
        V& value() { return m_it.data(); }
        V& data() { return m_it.data(); }
        const K& first() const { return m_it.key(); }
        V& second() { return m_it.data(); }
        V& operator*() { return m_it.data(); }

        iterator& operator++() { ++m_it; return *this; }
        bool operator==(const iterator& o) const { return m_it == o.m_it; }
        bool operator!=(const iterator& o) const { return m_it != o.m_it; }
        // 与 const_iterator 互比（Qt4+ 里 const_iterator 是 iterator 的只读视图，
        // 两者可比较）。qlstik 的遍历写法在 const/非 const 容器间切换时需要。
        // const_iterator 此时尚未定义完，故这四个函数放在类外定义。
        bool operator==(const const_iterator& o) const;
        bool operator!=(const const_iterator& o) const;

        base_iterator& baseIt() { return m_it; }
        const base_iterator& baseIt() const { return m_it; }

    private:
        base_iterator m_it;
    };

    class const_iterator {
    public:
        const_iterator() : m_it() {}
        const_iterator(const base_const_iterator& it) : m_it(it) {}

        const K& key() const { return m_it.key(); }
        const V& value() const { return m_it.data(); }
        const V& data() const { return m_it.data(); }
        const K& first() const { return m_it.key(); }
        const V& second() const { return m_it.data(); }
        const V& operator*() const { return m_it.data(); }

        const_iterator& operator++() { ++m_it; return *this; }
        bool operator==(const const_iterator& o) const { return m_it == o.m_it; }
        bool operator!=(const const_iterator& o) const { return m_it != o.m_it; }
        bool operator==(const iterator& o) const;
        bool operator!=(const iterator& o) const;

        const base_const_iterator& baseIt() const { return m_it; }

    private:
        base_const_iterator m_it;
    };

    // 跨 const/非 const 迭代器比较：靠 baseIt() 比较底层 QMapIterator 节点。
    // 成员声明处 const_iterator/iterator 尚不完整，实现在类外（见文件末尾）。

    iterator begin() { return iterator(base::begin()); }
    iterator end() { return iterator(base::end()); }
    const_iterator begin() const { return const_iterator(base::begin()); }
    const_iterator end() const { return const_iterator(base::end()); }
    const_iterator cbegin() const { return const_iterator(base::constBegin()); }
    const_iterator cend() const { return const_iterator(base::constEnd()); }

    // Qt3 无 constFind()，const 版 find() 返回 QMapConstIterator（qmap.h:706）。
    const_iterator constFind(const K& key) const {
        const base& b = *this;
        return const_iterator(b.find(key));
    }
    iterator find(const K& key) {
        return iterator(base::find(key));
    }

    // Qt4+ 有单参 value(key)（缺失时返回 V()）。Qt3 的 QMap 无。
    V value(const K& key) const {
        const base& b = *this;
        if (b.find(key) == b.end()) {
            return V();
        }
        return b.operator[](key);
    }

    V value(const K& key, const V& defaultValue) const {
        const base& b = *this;
        if (b.find(key) == b.end()) {
            return defaultValue;
        }
        return b.operator[](key);
    }

    // Qt5 语义：删除并返回被删位置的下一个迭代器。Qt3 QMap::erase(iterator) 无返回值，
    // 所以先取下一个再删。
    iterator erase(const iterator& it) {
        iterator next = it;
        ++next;
        base::erase(it.baseIt());
        return next;
    }
};

} // namespace qlstik_qt3

// 跨 const/非 const 迭代器比较的实体定义（类内只能声明：两个嵌套类互相引用时
// 至少有一个不完整）。参数类型用完整限定名是因为 QMapShim 是类模板。
template <class K, class V>
inline bool qlstik_qt3::QMapShim<K, V>::iterator::operator==(
    const qlstik_qt3::QMapShim<K, V>::const_iterator& o) const
{
    return m_it == o.baseIt();
}

template <class K, class V>
inline bool qlstik_qt3::QMapShim<K, V>::iterator::operator!=(
    const qlstik_qt3::QMapShim<K, V>::const_iterator& o) const
{
    return m_it != o.baseIt();
}

template <class K, class V>
inline bool qlstik_qt3::QMapShim<K, V>::const_iterator::operator==(
    const qlstik_qt3::QMapShim<K, V>::iterator& o) const
{
    return m_it == o.baseIt();
}

template <class K, class V>
inline bool qlstik_qt3::QMapShim<K, V>::const_iterator::operator!=(
    const qlstik_qt3::QMapShim<K, V>::iterator& o) const
{
    return m_it != o.baseIt();
}

#define QMap qlstik_qt3::QMapShim

#endif // QLSTIK_QMAP_SHIM_H
