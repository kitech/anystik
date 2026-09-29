#ifndef QLSTIK_QVECTOR_SHIM_H
#define QLSTIK_QVECTOR_SHIM_H

// Qt 3.5 的 "QVector" 只是 qptrvector.h 里的宏（#define QVector QPtrVector），
// 缺 Qt4 的 append / removeAll / 按值 contains / begin()..end()（范围 for）。
// 这里 #undef 后补一个同名派生类，源码零改动。
//
// 注意：不重声明 count()/size() —— QGVector 里它们是虚函数且返回 uint，
//      改返回类型会被判“conflicting return type”。
//
// 所有权：QPtrVector 默认 del_item=FALSE（qptrcollection.h），
//        与 Qt4 QVector<T> 的非拥有语义一致。

#include <qptrvector.h>

#ifdef QVector
#undef QVector
#endif

template <class T>
class QVector : public QPtrVector<T> {
public:
    class const_iterator {
    public:
        const_iterator(const QPtrVector<T>* v, uint i) : m_v(v), m_i(i) {}
        T operator*() const { return *m_v->at(m_i); }
        const_iterator& operator++() { ++m_i; return *this; }
        bool operator!=(const const_iterator& o) const { return m_i != o.m_i; }
    private:
        const QPtrVector<T>* m_v;
        uint m_i;
    };

    void append(const T& item) {
        this->insert(this->size(), &item);
    }
    bool contains(const T& item) const {
        return this->containsRef(&item) > 0;
    }
    void removeAll(const T& item) {
        for (int i = (int)this->size() - 1; i >= 0; --i) {
            if (*this->at((uint)i) == item)
                this->remove((uint)i);
        }
    }
    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end()   const { return const_iterator(this, this->size()); }
};

#endif // QLSTIK_QVECTOR_SHIM_H
