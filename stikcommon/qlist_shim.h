#ifndef QLSTIK_QLIST_SHIM_H
#define QLSTIK_QLIST_SHIM_H

// Qt 3.5 的 "QList" 只是 qptrlist.h:189 的宏（#define QList QPtrList），只能存
// 指针，没有 Qt4 的值容器语义。vendor/qwebdav 用到值类型 QList<QWebdavItem>
//（QWebdavDirParser::getList() 返回它，客户端按引用改写元素）、QList<QSslError>
//（网络管理器的 sslErrors 信号参数），Qt3 下无法表达。
// 这里 #undef QList 后补一个派生自 QValueList<T>（Qt3 原生值列表）的同名模板，
// 源码零改动；Qt4+ 走原生 QList，本头定义不参与编译。
//
// 与 QValueList 的差异（已逐一核实 /opt/qt338sh/include/qvaluelist.h）：
//   基类已有：size/append/prepend/remove/erase/insert/sort/isEmpty/first/last/
//             begin/end/constBegin/constEnd/push_back/pop_back/pop_front/
//             front/back/operator[]/operator<<
//   基类缺  ：count/at(按引用)/append(值语义,基类是指针版)/removeLast/
//             removeFirst/takeFirst/removeOne/constFirst/constLast/cbegin/cend/contains
//   at() 必须重写：Qt3 QValueList::at(size_type) 返回 const_iterator（迭代器），
//   Qt4+ 返回 const T&。qwebdavdirparser 按引用改写 getList() 结果，语义不同。
//
// 只在 QT3_BUILD 编译单元 include；此时 Qt 原生头已预处理完毕，宏替换不影响它们。

#include <qvaluelist.h>
#include <qptrlist.h>  // 仅为占住 include guard，防止后续 Qt 头再次 #define QList

#include <algorithm>
#include <vector>

#ifdef QList
#undef QList
#endif

#ifdef QListIterator
#undef QListIterator
#endif

template <class T>
class QList : public QValueList<T> {
public:
    typedef QValueList<T> base;
    typedef QValueListIterator<T> iterator;
    typedef QValueListConstIterator<T> const_iterator;

    int count() const { return (int)this->size(); }

    // Qt3 QValueList::append(T*) 是**指针**版（QValueList 本身就是指针容器），
    // Qt4+ QList::append(const T&) 是值版。此处必须遮蔽成值版，否则
    // `list.append(QSslError(...))` 会退化成往指针数组塞临时对象。
    void append(const T& x) { this->push_back(x); }

    const T& at(uint i) const { return this->operator[](i); }
    T& at(uint i) { return this->operator[](i); }

    // Qt3.5.0 的 QValueList::sort() 在 qvaluelist.h:528 被注释掉（理由是部分
    // 编译器会误实例化、强行要求 T 有 operator<），而 Qt5 的 QList::sort() 存在。
    // 这里对已解引用的元素排序：先收集成 std::vector 再回填，避开了拷贝构造的
    // 模板实例化时机问题（与被注释掉的那份实现目的相同，只是显式两段式）。
    void sort()
    {
        const int n = (int)this->size();
        if (n < 2) {
            return;
        }
        std::vector<T> tmp;
        tmp.reserve((size_t)n);
        for (int i = 0; i < n; ++i) {
            tmp.push_back(this->operator[](i));
        }
        std::sort(tmp.begin(), tmp.end());
        for (int i = 0; i < n; ++i) {
            this->operator[](i) = tmp[(size_t)i];
        }
    }

    const T& constFirst() const { return this->first(); }
    const T& constLast() const { return this->last(); }

    void removeLast() { this->pop_back(); }
    void removeFirst() { this->pop_front(); }

    T takeFirst() {
        T v = this->first();
        this->pop_front();
        return v;
    }

    T takeLast() {
        T v = this->last();
        this->pop_back();
        return v;
    }

    // Qt5 的 removeOne 只删首个匹配项；Qt3 QValueList::remove() 删全部匹配项。
    // qlstik 内 QList 的 removeOne 只用于唯一键场景（无重复项），两者等价。
    bool removeOne(const T& x) {
        if (!contains(x)) {
            return false;
        }
        for (int i = (int)this->size() - 1; i >= 0; --i) {
            if (this->operator[](i) == x) {
                iterator it = this->begin();
                for (int k = 0; k < i; ++k) {
                    ++it;
                }
                this->erase(it);
                return true;
            }
        }
        return false;
    }

    bool contains(const T& x) const {
        return this->find(x) != this->end();
    }

    const_iterator cbegin() const { return this->begin(); }
    const_iterator cend() const { return this->end(); }
};

// 移除一个元素并返回它（Qt5 QList::removeOne 无此语义，qlstik 内亦未使用）。
// 单点定义以免调用方各自造轮子。
template <class T>
T qListTakeFirst(QList<T>& list)
{
    return list.takeFirst();
}

#endif // QLSTIK_QLIST_SHIM_H
