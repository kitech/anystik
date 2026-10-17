#ifndef QLSTIK_QCONCURRENT_SHIM_H
#define QLSTIK_QCONCURRENT_SHIM_H

// ══ QtConcurrent::run + QFuture<T>::then 的 Qt3 垫片（L2）══════════════════
//
// 需求（stickerstore.cpp 两处，均为「工作线程干活 → 回 GUI 线程收尾」）：
//   迁移 switchStorageRoot  :422  QtConcurrent::run(拉姆达) → :542 future.then(this, [](const MigrationResult&){...})
//   安装 runInstall         :3715 QtConcurrent::run(拉姆达) → :3718 future.then(this, [](const InstallResult&){...})
//
// QtConcurrent/QFuture 随 Qt 4.4 引入，Qt3.5 完全没有。本垫片在 Qt3 分支下
// 提供**自足的等价面**（不引入任何外部库）：
//   · namespace QtConcurrent { template<class F> QFuture<R> run(F); }   R = F 的返回类型
//   · template<class R> class QFuture {
//         template<class Ctx,class Cont> QFuture<U> then(Ctx*, Cont);  U = 续延返回类型
//         void waitForFinished();  R result();  bool isFinished();
//     }
//   · then 的续延在**调用 then 时的 context 所在线程**执行（与 Qt6 语义一致），
//     即本仓里恒为 GUI 线程——续延会碰 DB 并 emit 信号，必须在 GUI 线程。
//
// 实现分层（L2：线程池 + 异常 + 生命周期安全 + 链式）：
//   · 线程池：函数内 static 单例，固定 kPoolSize(=2) 条 QThread worker，
//     submit 入队 + wakeOne；池对象**故意泄漏不析构**（避开 static 析构序与
//     QApplication 全局态打架，同 QThreadPool::globalInstance 思路）。
//   · 结果回填：std::shared_ptr<FutureState<R>>，QMutex 保护 done/value/hasError；
//     工作线程跑完 finishState() 置 done + wakeAll（供 waitForFinished/result 唤醒）。
//   · 回投 GUI：QApplication::postEvent(Notifier*, QCustomEvent)（跨线程安全）；
//     postEvent 在**持有 state->mutex 时**投递，与 Notifier 析构清 notifier 互斥，
//     规避「context 先于任务析构」的悬垂投递（比 QGuardedPtr 更稳：其依赖
//     destroyed() 信号，非线程安全）；Notifier 析构另调 removePostedEvents(this)
//     撤回可能已入队的完成事件（Qt3 qapplication.h:164）。
//   · 异常：工作线程内 try/catch，std::exception_ptr 存进 state；result() 重抛，
//     后继 then 分支只跳过（不触发续延）。
//
// ⚠ 与 Qt6 的**有意差异**（改动时勿"顺手对齐"）：
//   1. then() **必须显式传 context**（Qt6 可省略并继承上一 context）；本仓两处
//      都传 this，无需隐式继承。
//   2. 链式 then 已支持（返回 QFuture<U>）；但每个 then 仍须各自传 context。
//   3. QFuture 析构**不阻塞**（同 Qt6，非 std::future）；未 wait 直接丢弃即可。
//   4. Qt3.5 无 QThread::idealThreadCount，池大小固定 2（本仓至多两处并发）。
//
// 线程原语选型：Qt3.5 的 QThread 不是 QObject（qthread.h:59 基类是 Qt，
//   :123 run() 纯虚），**没有 finished() 信号**（:108 的 finished() 是 `bool`
//   查询函数），故 worker 线程对象无回收负担、池化后长驻；这与旧版「每任务一条
//   detached pthread」不同，改为固定池以支撑 result()/waitForFinished() 与
//   链式 then，并避免高频短任务时线程爆炸。
//
// 说明：本头仅在 QT3_BUILD 下有内容；Qt4+ 走原生（贴纸库的 Qt6 分支已直接
//   #include <QtConcurrent/QtConcurrent>），本头对非 Qt3 为空。

#ifdef QT3_BUILD

#include <functional>
#include <memory>
#include <deque>
#include <exception>
#include <utility>

#include <qobject.h>
#include <qapplication.h>
#include <qevent.h>
#include <qmutex.h>
#include <qwaitcondition.h>
#include <qthread.h>

namespace qlstik_qt3_concurrent {

// 续延投递用的事件类型（只在本垫片内使用，取一个不与他处冲突的 User 偏移）。
inline int completionEventType()
{
    static const int t = QEvent::User + 0x5c;
    return t;
}

// 线程池大小（Qt3.5 无 QThread::idealThreadCount；本仓至多两处并发）。
enum { kPoolSize = 2 };

// void 的替身：FutureState<void> 需要可默认构造的 value 成员。
struct Void {};

// 取「存储值类型」：R 原样，void 映射为 Void。
template <typename R> struct ValueOf      { typedef R    type; };
template <>          struct ValueOf<void> { typedef Void type; };

// 存储求值：R 有值则赋给状态，R=void 则只求值（保证副作用发生）。
template <typename R> struct Store {
    template <typename Dst, typename Fn>
    static void eval(Dst* dst, Fn& fn) { dst->value = fn(); }
};
template <> struct Store<void> {
    template <typename Dst, typename Fn>
    static void eval(Dst* dst, Fn& fn) { (void)dst; fn(); }
};

// 调用续延：R 有值则 cont(value)；R=void 则 cont()。
template <typename R> struct Invoke {
    template <typename Cont, typename V>
    static auto call(Cont& c, V& v) -> decltype(c(v)) { return c(v); }
};
template <> struct Invoke<void> {
    template <typename Cont, typename V>
    static auto call(Cont& c, V&) -> decltype(c()) { return c(); }
};

template <typename R> class Notifier;   // 前置声明（FutureState 仅持其指针）

// 工作线程与续延之间共享的完成状态。
template <typename R>
struct FutureState {
    QMutex        mutex;
    QWaitCondition cond;
    bool          done;
    bool          hasError;
    typename ValueOf<R>::type value;
    std::exception_ptr error;
    Notifier<R>*  notifier;
    FutureState() : done(false), hasError(false), notifier(0) {}
    void setError(const std::exception_ptr& e) { hasError = true; error = e; }
};

// 在 context 线程触发一次回调的事件接收器。created 于 context 线程，
// 由 QApplication::postEvent 从工作线程投递 QCustomEvent 触发。
template <typename R>
class Notifier : public QObject
{
public:
    Notifier(QObject* parent, const std::shared_ptr<FutureState<R> >& st)
        : QObject(parent), m_state(st) {}
    ~Notifier()
    {
        // context 先于任务析构：锁内置空登记，工作线程此后不再投递；
        // 并撤回可能已入队的完成事件，避免投递到已析构对象。
        if (m_state) {
            {
                QMutexLocker locker(&m_state->mutex);
                if (m_state->notifier == this)
                    m_state->notifier = 0;
            }
            QApplication::removePostedEvents(this);
        }
    }
    std::function<void()> fn;
protected:
    void customEvent(QCustomEvent*)
    {
        std::function<void()> f;
        f.swap(fn);
        if (f) f();
        deleteLater();
    }
private:
    std::shared_ptr<FutureState<R> > m_state;
};

// 工作线程跑完后回填 done 并唤醒等待者；若已登记续延则在锁内投递到 context 线程。
template <typename R>
void finishState(FutureState<R>* st)
{
    Notifier<R>* n = 0;
    {
        QMutexLocker locker(&st->mutex);
        st->done = true;
        st->cond.wakeAll();
        n = st->notifier;
        st->notifier = 0;
        if (n)
            QApplication::postEvent(n, new QCustomEvent(completionEventType()));
    }
}

// 读取结果：R 有值返回 value，void 只检查异常（借 R=void 时 `return void_expr;` 合法）。
template <typename R> struct ResultGet {
    static R get(FutureState<R>* st)
    {
        QMutexLocker locker(&st->mutex);
        if (st->hasError) std::rethrow_exception(st->error);
        return st->value;
    }
};
template <> struct ResultGet<void> {
    static void get(FutureState<void>* st)
    {
        QMutexLocker locker(&st->mutex);
        if (st->hasError) std::rethrow_exception(st->error);
    }
};

// 类型擦除的任务体。
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

// 固定大小线程池（函数内 static 单例，故意泄漏）。
class Pool
{
public:
    static Pool& instance()
    {
        static Pool* p = new Pool(kPoolSize);   // 泄漏：避开静态析构序
        return *p;
    }
    void submit(JobBase* j)
    {
        QMutexLocker locker(&m_mutex);
        m_queue.push_back(j);
        m_cond.wakeOne();
    }
private:
    class Worker : public QThread
    {
    public:
        explicit Worker(Pool* pool) : m_pool(pool) {}
    protected:
        void run()
        {
            for (;;) {
                JobBase* j = m_pool->take();
                if (!j)
                    break;
                j->exec();
                delete j;
            }
        }
    private:
        Pool* m_pool;
    };
    explicit Pool(int n) : m_stopping(false)
    {
        for (int i = 0; i < n; ++i) {
            Worker* w = new Worker(this);   // 泄漏：随进程存活
            w->start();
        }
    }
    ~Pool() {}
    JobBase* take()
    {
        QMutexLocker locker(&m_mutex);
        while (m_queue.empty() && !m_stopping)
            m_cond.wait(&m_mutex);
        if (m_queue.empty())
            return 0;
        JobBase* j = m_queue.front();
        m_queue.pop_front();
        return j;
    }
    QMutex               m_mutex;
    QWaitCondition       m_cond;
    std::deque<JobBase*> m_queue;
    bool                 m_stopping;
};

} // namespace qlstik_qt3_concurrent

template <typename R>
class QFuture
{
    template <typename> friend class QFuture;   // then 需访问别实例的私有 m_state
public:
    QFuture()
        : m_state(std::make_shared<qlstik_qt3_concurrent::FutureState<R> >()) {}

    qlstik_qt3_concurrent::FutureState<R>* state() const { return m_state.get(); }
    const std::shared_ptr<qlstik_qt3_concurrent::FutureState<R> >& statePtr() const
    { return m_state; }

    // 登记续延：工作线程完成时（或已完成则立即）在 context 线程执行 cont(结果)；
    // 返回承载「续延返回值」的新 future，可继续 .then。
    template <typename Ctx, typename Cont>
    auto then(Ctx* context, Cont cont)
        -> QFuture<decltype(qlstik_qt3_concurrent::Invoke<R>::call(
                std::declval<Cont&>(),
                std::declval<typename qlstik_qt3_concurrent::ValueOf<R>::type&>()))>
    {
        typedef decltype(qlstik_qt3_concurrent::Invoke<R>::call(
                    std::declval<Cont&>(),
                    std::declval<typename qlstik_qt3_concurrent::ValueOf<R>::type&>())) U;
        typedef qlstik_qt3_concurrent::FutureState<R> SrcState;
        typedef qlstik_qt3_concurrent::FutureState<U> DstState;

        QFuture<U> next;
        std::shared_ptr<SrcState> src = m_state;
        std::shared_ptr<DstState> dst = next.m_state;

        qlstik_qt3_concurrent::Notifier<R>* n =
            new qlstik_qt3_concurrent::Notifier<R>(context, src);
        n->fn = [src, dst, cont]() mutable {
            if (src->hasError) {
                dst->setError(src->error);         // 异常沿链传播
            } else {
                typename qlstik_qt3_concurrent::ValueOf<R>::type& v = src->value;
                auto thunk = [&cont, &v]() -> decltype(
                        qlstik_qt3_concurrent::Invoke<R>::call(cont, v)) {
                    return qlstik_qt3_concurrent::Invoke<R>::call(cont, v);
                };
                try {
                    qlstik_qt3_concurrent::Store<U>::eval(dst.get(), thunk);
                } catch (...) {
                    dst->setError(std::current_exception());
                }
            }
            qlstik_qt3_concurrent::finishState<U>(dst.get());
        };

        bool finished = false;
        {
            QMutexLocker locker(&src->mutex);
            if (src->done)
                finished = true;               // 已完成：then() 在 context 线程，直接投递
            else
                src->notifier = n;             // 未完成：登记，由工作线程唤醒
        }
        if (finished)
            QApplication::postEvent(
                n, new QCustomEvent(qlstik_qt3_concurrent::completionEventType()));
        return next;
    }

    void waitForFinished() const
    {
        qlstik_qt3_concurrent::FutureState<R>* st = m_state.get();
        QMutexLocker locker(&st->mutex);
        while (!st->done)
            st->cond.wait(&st->mutex);
    }

    bool isFinished() const
    {
        qlstik_qt3_concurrent::FutureState<R>* st = m_state.get();
        QMutexLocker locker(&st->mutex);
        return st->done;
    }

    R result() const
    {
        waitForFinished();
        return qlstik_qt3_concurrent::ResultGet<R>::get(m_state.get());
    }

private:
    std::shared_ptr<qlstik_qt3_concurrent::FutureState<R> > m_state;
};

namespace QtConcurrent {

template <typename F>
auto run(F f) -> QFuture<decltype(f())>
{
    typedef decltype(f()) R;
    QFuture<R> fut;
    std::shared_ptr<qlstik_qt3_concurrent::FutureState<R> > st = fut.statePtr();

    // 包装：跑 f 得到结果（或捕获异常），回填并唤醒续延。
    auto wrapper = [f, st]() mutable {
        try {
            qlstik_qt3_concurrent::Store<R>::eval(st.get(), f);
        } catch (...) {
            st->setError(std::current_exception());
        }
        qlstik_qt3_concurrent::finishState<R>(st.get());
    };
    qlstik_qt3_concurrent::JobBase* job =
        new qlstik_qt3_concurrent::Job<decltype(wrapper)>(wrapper);
    qlstik_qt3_concurrent::Pool::instance().submit(job);
    return fut;
}

} // namespace QtConcurrent

#endif // QT3_BUILD

#endif // QLSTIK_QCONCURRENT_SHIM_H
