#ifndef PAGE_H
#define PAGE_H

#include <QskControl.h>
#include <QVariantMap>
#include <QString>
#include <memory>

// ── 页的生命周期状态，与 Android Activity/Fragment 对应 ──
// None:    初始状态，onCreate 尚未调用
// Created: onCreate 已返回，UI 已构建，页面在 QskStackBox 中但不可见
// Started: onStart 已返回，页面正在变为可见
// Resumed: onResume 已返回，页面完全交互、用户可操作
// Paused:  onPause 已返回，页面被部分覆盖（另一个页面即将取代前台）
// Stopped: onStop 已返回，页面完全不可见（缓存中或即将销毁）
// Destroyed: onDestroy 已返回，页面已从 QskStackBox 移除
enum class PageState {
    None, Created, Started, Resumed,
    Paused, Stopped, Destroyed
};

// ── 导航模式 ──
// Standard:      每次 open 创建新实例
// SingleTop:     目标页已在栈顶时复用，调用 onNewIntent
// SingleInstance: 目标页已存在时 bringToFront+clearTop+onNewIntent
enum class LaunchMode { Standard, SingleTop, SingleInstance };

// ── 页面缓存策略 ──
// Transient:  back() 时直接销毁（QML: destroyOnPop=true + Component）
// LRU:        back() 时入缓存池，超量时 LRU 驱逐（QML: destroyOnPop=false 有限池）
// Permanent:  永远驻留，不驱逐
enum class CachePolicy { Transient, LRU, Permanent };

class PageManager;

class Page : public QskControl
{
    Q_OBJECT
public:
    explicit Page(QQuickItem* parent = nullptr);
    ~Page() override;

    // 当前生命周期状态
    PageState state() const { return m_state; }

    // 页的唯一标识符，由 PageManager 在注册时设置
    QString pageId() const { return m_pageId; }

    // 是否正在被关闭（用户按返回、被 replace 等）
    // onSaveInstanceState 中可查询，以决定是否保存临时状态
    bool isFinishing() const { return m_isFinishing; }

    // 获取所属 PageManager（nullptr 表示未关联）
    PageManager* pageManager() const { return m_pageManager; }

    // ── 生命周期回调 ──
    // 所有回调默认实现为空，使用者只需 override 需要的
    // 构造 → onCreate(args, savedState) → onStart → onResume
    // 暂停 → onPause → (onSaveInstanceState) → onStop
    // 缓存恢复 → onRestart → onStart → onResume
    // 销毁 → onDestroy
    // 参数说明:
    //   launchArgs: open() 时传入的参数（类比 Intent extras）
    //   savedState: 进程死亡恢复时，QSettings 中保存的之前的状态
    virtual void onCreate(const QVariantMap& launchArgs,
                          const QVariantMap& savedState);
    virtual void onStart();
    virtual void onResume();
    virtual void onPause();
    virtual void onStop();
    virtual void onDestroy();
    virtual void onRestart();
    virtual void onNewIntent(const QVariantMap& launchArgs);
    virtual void onSaveInstanceState(QVariantMap& outState);
    virtual void onRestoreInstanceState(const QVariantMap& savedState);

    // ── 返回值机制（对应 startActivityForResult） ──
    // 页面自身关闭前调用 setResult(code, data)，PageManager 在 back() 时将结果传递给上一页
    void setResult(int resultCode, const QVariantMap& data = {});
    int  resultCode() const { return m_resultCode; }
    const QVariantMap& resultData() const { return m_resultData; }

    // ── 请求关闭自身 ──
    // PageManager 收到 finishRequested 信号后执行 back()
    void finish();

Q_SIGNALS:
    void finishRequested();

private:
    friend class PageManager;

    PageManager* m_pageManager = nullptr;
    PageState    m_state = PageState::None;
    QString      m_pageId;
    bool         m_isFinishing = false;
    int          m_resultCode = -1;  // -1 对应 Android RESULT_CANCELED
    QVariantMap  m_resultData;

    // 内部设置，仅 PageManager 使用
    void setState(PageState s) { m_state = s; }
    void setFinishing(bool f) { m_isFinishing = f; }
    void setPageManager(PageManager* pm) { m_pageManager = pm; }
    void setPageId(const QString& id) { m_pageId = id; }
};

#endif
