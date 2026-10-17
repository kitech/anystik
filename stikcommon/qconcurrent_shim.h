#ifndef QLSTIK_QCONCURRENT_SHIM_H
#define QLSTIK_QCONCURRENT_SHIM_H

// ══ QtConcurrent::run + QFuture<T>::then 的 Qt3 垫片 ═══════════════════════
//
// 需求（stickerstore.cpp 两处，均为「工作线程干活 → 回 GUI 线程收尾」）：
//   迁移 switchStorageRoot  :422  QtConcurrent::run(拉姆达) → :542 future.then(this, [](const MigrationResult&){...})
//   安装 runInstall         :3715 QtConcurrent::run(拉姆达) → :3718 future.then(this, [](const InstallResult&){...})
//
// QtConcurrent/QFuture 随 Qt 4.4 引入，Qt3.5 完全没有。本垫片在 Qt3 分支下
// 提供**最小等价面**，只覆盖上述两处用到的形态：
//   · namespace QtConcurrent { template<class F> QFuture<R> run(F); }   R = F 的返回类型
//   · template<class T> class QFuture { template<class Ctx, class Cont> void then(Ctx*, Cont); }
//   · then 的续延在**调用 then 时的 context 所在线程**执行（与 Qt6 语义一致），
//     即本仓里恒为 GUI 线程——续延会碰 DB 并 emit 信号，必须在 GUI 线程。
//
// ⚠ 与 Qt6 的**有意差异**（改动时勿"顺手对齐"）：
//   1. QFuture 不提供 waitForFinished()/result()/progress 等；本仓两处都不需要。
//   2. then() 返回 void（Qt6 返回 QFuture<U>）。两处调用都是语句形式、丢弃返回，
//      故无需链式 U。若将来要链式，须补返回类型。
//   3. Qt6 的 QtConcurrent::run 会在全局线程池排队；本垫片**每任务一条 detached
//      pthread**（一次性线程）。对本仓「下载/迁移偶发、单任务」足够；若将来有
//      高频短任务，应改为线程池。
//
// 线程原语选型（为何用 pthread 而非 QThread）：Qt3.5 的 QThread 不是 QObject
//   （qthread.h:59 基类是 Qt，:123 run() 纯虚），**没有 finished() 信号**（:108
//   的 finished() 是 `bool` 查询函数），故无法用「finished→deleteLater」回收线程
//   对象；而 detached pthread 天然无对象生命周期负担，最干净。
//   线程返回值经 std::shared_ptr<FutureState> 回填；续延经
//   QApplication::postEvent(QCustomEvent) 投递到 context 线程的事件循环。
//
// 说明：本头仅在 QT3_BUILD 下有内容；Qt4+ 走原生（贴纸库的 Qt6 分支已直接
//   #include <QtConcurrent/QtConcurrent>），本头对非 Qt3 为空。

#ifdef QT3_BUILD

#include <functional>
#include <memory>

#include <qobject.h>
#include <qapplication.h>
#include <qevent.h>
#include <qmutex.h>

#include <pthread.h>

namespace qlstik_qt3_concurrent {

// 续延投递用的事件类型（只在本垫片内使用，取一个不与他处冲突的 User 偏移）。
inline int completionEventType()
{
    static const int t = QEvent::User + 0x5c;
    return t;
}

// 在 receiver 线程触发一次回调的事件接收器。created 于 context 线程，
// 由 QApplication::postEvent 从工作线程投递 QCustomEvent 触发。
class Notifier : public QObject
{
public:
    explicit Notifier(QObject* parent) : QObject(parent) {}
    std::function<void()> fn;
protected:
    void customEvent(QCustomEvent*)
    {
        std::function<void()> f;
        f.swap(fn);
        if (f) f();
        deleteLater();
    }
};

// 工作线程与续延之间共享的完成状态。
template <typename T>
struct FutureState {
    QMutex  mutex;
    bool    done;
    T       value;
    Notifier* notifier;
    FutureState() : done(false), notifier(0) {}
};

// 工作线程跑完后回填结果；若已登记续延则唤醒之（投递到 context 线程）。
template <typename T>
void finish(FutureState<T>* st, const T& v)
{
    Notifier* n = 0;
    {
        QMutexLocker locker(&st->mutex);
        st->value = v;
        st->done = true;
        n = st->notifier;
        st->notifier = 0;
    }
    if (n)
        QApplication::postEvent(n, new QCustomEvent(completionEventType()));
}

// 类型擦除的可调用体，交给 pthread 入口执行。
struct JobBase {
    virtual ~JobBase() {}
    virtual void exec() = 0;
};

template <typename F>
struct Job : JobBase {
    F fn;
    explicit Job(const F& f) : fn(f) {}
    void exec() { fn(); }
};

inline void* jobTrampoline(void* p)
{
    JobBase* j = static_cast<JobBase*>(p);
    j->exec();
    delete j;
    return 0;
}

} // namespace qlstik_qt3_concurrent

template <typename T>
class QFuture
{
public:
    QFuture()
        : m_state(std::make_shared<qlstik_qt3_concurrent::FutureState<T> >()) {}

    qlstik_qt3_concurrent::FutureState<T>* state() const { return m_state.get(); }

    // 登记续延：工作线程完成时（或已完成则立即）在 context 线程执行 cont(结果)。
    template <typename Ctx, typename Cont>
    void then(Ctx* context, Cont cont)
    {
        std::shared_ptr<qlstik_qt3_concurrent::FutureState<T> > st = m_state;
        qlstik_qt3_concurrent::Notifier* n =
            new qlstik_qt3_concurrent::Notifier(context);
        n->fn = [st, cont]() { cont(st->value); };

        bool finished = false;
        {
            QMutexLocker locker(&st->mutex);
            if (st->done)
                finished = true;              // 已完成：then() 在 context 线程，直接投递
            else
                st->notifier = n;             // 未完成：登记，由工作线程唤醒
        }
        if (finished)
            QApplication::postEvent(
                n, new QCustomEvent(qlstik_qt3_concurrent::completionEventType()));
    }

private:
    std::shared_ptr<qlstik_qt3_concurrent::FutureState<T> > m_state;
};

namespace QtConcurrent {

template <typename F>
auto run(F f) -> QFuture<decltype(f())>
{
    typedef decltype(f()) R;
    QFuture<R> fut;
    qlstik_qt3_concurrent::FutureState<R>* st = fut.state();

    // 包装：跑 f 得到结果，回填并唤醒续延。
    auto wrapper = [f, st]() mutable {
        const R v = f();
        qlstik_qt3_concurrent::finish<R>(st, v);
    };
    qlstik_qt3_concurrent::JobBase* job =
        new qlstik_qt3_concurrent::Job<decltype(wrapper)>(wrapper);

    pthread_t tid;
    if (pthread_create(&tid, 0, qlstik_qt3_concurrent::jobTrampoline, job) == 0) {
        pthread_detach(tid);
    } else {
        // 线程创建失败：同步跑（保证续延一定触发），并回收 job。
        job->exec();
        delete job;
    }
    return fut;
}

} // namespace QtConcurrent

#endif // QT3_BUILD

#endif // QLSTIK_QCONCURRENT_SHIM_H
