#ifndef QLSTIK_QQUEUE_SHIM_H
#define QLSTIK_QQUEUE_SHIM_H

// Qt4 引入的模板 QQueue<T>；Qt3 的 qqueue.h 只是兼容壳（指向 qptrqueue 的
// 指针队列，非模板容器）。本头仅在 Qt3 提供基于 std::deque 的等价类，
// 覆盖 imageaiutil 等共享模块的队列用法（enqueue/dequeue/at/removeAt）。
// Qt4+ 用 Qt 原生 QQueue，不做任何定义。

#ifndef QT3_BUILD
#include <QQueue>
#else
#include <deque>

template <typename T>
class QQueue
{
public:
    bool isEmpty() const { return m_q.empty(); }
    void clear() { m_q.clear(); }
    void enqueue(const T& v) { m_q.push_back(v); }
    T dequeue() { T v = m_q.front(); m_q.pop_front(); return v; }
    const T& head() const { return m_q.front(); }
    int size() const { return (int)m_q.size(); }
    const T& at(int i) const { return m_q[(size_t)i]; }
    void removeAt(int i) { m_q.erase(m_q.begin() + i); }

private:
    std::deque<T> m_q;
};
#endif

#endif // QLSTIK_QQUEUE_SHIM_H