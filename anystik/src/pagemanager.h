#ifndef PAGE_MANAGER_H
#define PAGE_MANAGER_H

#include <QObject>
#include <QMap>
#include <QList>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <functional>
#include "page.h"

class QskStackBox;

using PageFactory = std::function<Page*()>;
// resultCode: 0=OK, -1=CANCELED（同 Android RESULT_OK/RESULT_CANCELED）
// data: 返回的数据
using PageResultCallback = std::function<void(int resultCode, const QVariantMap& data)>;

struct PageRegistration {
    CachePolicy policy = CachePolicy::Transient;
    LaunchMode  defaultMode = LaunchMode::Standard;
};

// ── PageManager ──
// 管理页面的生命周期、导航堆栈、缓存，对标 Android FragmentManager。
// 负责:
//   1. 页面工厂管理（registerPage）
//   2. 导航（open/back/replace），驱动精确的生命周期转换
//   3. 页面缓存（LRU 池）
//   4. 进程死亡保护（saveAllStates/restoreAllStates）
//   5. QskStackBox 的 addItem/removeItem/setCurrentItem
class PageManager : public QObject
{
    Q_OBJECT
public:
    explicit PageManager(QskStackBox* stackBox, QObject* parent = nullptr);
    ~PageManager() override;

    // ── 注册页面类型 ──
    // id: 全局唯一标识
    // factory: 创建页面的工厂函数
    // reg: 缓存策略和默认导航模式
    void registerPage(const QString& id, PageFactory factory,
                      const PageRegistration& reg = {});

    // ── 导航 ──
    // 打开页面（参数类比 Intent extras）
    void open(const QString& id, const QVariantMap& args = {},
              LaunchMode mode = LaunchMode::Standard);
    // 打开页面并等待结果（对应 startActivityForResult）
    void openForResult(const QString& id, const QVariantMap& args,
                       PageResultCallback callback,
                       LaunchMode mode = LaunchMode::Standard);
    // 返回上一页（对应 onBackPressed）
    void back();
    // 替换当前页（对应 finish + startActivity）
    void replace(const QString& id, const QVariantMap& args = {});

    // ── 查询 ──
    int     depth() const { return m_history.size(); }
    Page*   currentPage() const;
    QString currentPageId() const;
    // 查找页面（活跃+缓存，不包括已销毁的）
    Page*   findPage(const QString& id) const;

    // ── 状态保存/恢复（进程死亡） ──
    // 遍历所有存活页面，调用 onSaveInstanceState 并写入 QSettings
    void saveAllStates();
    // 从 QSettings 恢复栈结构和页面状态，重新激活栈顶页
    void restoreAllStates();

    // ── 缓存配置 ──
    void setCacheMaxSize(int n) { m_cacheMaxSize = n; }
    int  cacheMaxSize() const { return m_cacheMaxSize; }

    // ── QskStackBox 访问 ──
    QskStackBox* stackBox() const { return m_stackBox; }

private:
    // 内部方法
    Page* createPage(const QString& id, const QVariantMap& args,
                     const QVariantMap& savedState);
    void  destroyPage(Page* page);
    void  activatePage(Page* page);
    void  deactivateCurrentPage();
    void  addToCache(Page* page);
    void  evictLRU();
    void  setStackBoxCurrent(Page* page);

    QskStackBox* m_stackBox;

    // 导航栈：页面 ID 有序列表，后进前出
    QList<QString> m_history;

    // 所有存活页面（活跃+缓存）
    QMap<QString, Page*> m_pages;

    // LRU 缓存队列（front=最近使用，back=最久未用）
    QList<QString> m_cacheLRU;
    int  m_cacheMaxSize = 5;

    // 工厂函数
    QMap<QString, PageFactory> m_factories;
    QMap<QString, PageRegistration> m_registrations;

    // openForResult 等待回调
    struct PendingResult {
        QString callerId;
        PageResultCallback callback;
    };
    QMap<QString, PendingResult> m_pendingResults;

    // 防止重入
    bool m_busy = false;

    // 延迟销毁（等动画完成后 removeItem，避免 animator item 引用失效）
    QTimer m_destroyTimer;
    Page* m_pendingDestroy = nullptr;

    // 延迟从 stackbox 移除（LRU/Permanent，等动画完成，可被 activatePage 取消）
    Page* m_pendingRemove = nullptr;
};

#endif
