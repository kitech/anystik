#ifndef QLSTIK_PAGEMANAGER_H
#define QLSTIK_PAGEMANAGER_H

// 页面管理器 —— 对标 anystik/src/pagemanager.h（只读参考，不改）。
//   * 注册/导航/生命周期/LRU 缓存/状态保存恢复，1:1 照搬语义
//   * 手段换 Qt3 原生：QskStackBox → compat34.h 的 StackedWidget
//     （Qt3 是 QWidgetStack，Qt4+ 是 QStackedWidget）
//
// ⚠ moc 3.5.0 限制：见 page.h 顶部说明 —— 不能把 `class Xxx` 当模板实参，
//   故下面先单独前向声明 Page 再进 std::function / QMap。
//
// ⚠ **Qt3 容器/API 铁律共 10 条，逐条 g++ -fsyntax-only 实测得出，
//   完整清单见 page.h 顶部「Qt3 容器铁律」小节。**本文件踩到的 5 条：
//   1. `QList<QString>` 在 Qt3 是 QPtrList（指针语义）→ 本文件用 QStringList。
//   2. Qt3 QStringList 无 removeAll() → PageManager::removeAllFrom() 走
//      迭代器手动推进 + erase(it)。
//   3. Qt3 QMap 无 value(key)、iterator 取值是 .data() 而非 .value() →
//      本文件用 pmMapGet() / pmItValue()。
//   4. Qt3 QStringList 无 indexOf()（有 findIndex()）→ pmListIndexOf()。
//   5. Qt3 QWidgetStack 无 setCurrentWidget/currentWidget/indexOf →
//      qStackSetCurrent()（compat34.h）+ 本文件 pmStack* 系列。
//   ⚠ 这 5 个辅助函数都定义在 .cpp 里，头文件不暴露，避免污染调用方。

#include "compat34.h"

#include <qobject.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qmap.h>
#include <qvariant.h>

#include <functional>

#include "page.h"

class Page;
class PageManager;

// 工厂函数类型。⚠ 必须在前向声明之后写 `Page*`（不能写 `class Page*`）。
typedef std::function<Page*()> PageFactory;
// resultCode: 0=OK, -1=CANCELED（同 Android RESULT_OK/RESULT_CANCELED）
typedef std::function<void(int resultCode, const QVariantMap& data)> PageResultCallback;

struct PageRegistration {
    CachePolicy policy;
    LaunchMode  defaultMode;

    PageRegistration()
        : policy(CachePolicy::Transient)
        , defaultMode(LaunchMode::Standard) {}
};

// ── PageManager ──
// 管理页面的生命周期、导航堆栈、缓存，对标 Android FragmentManager。负责：
//   1. 页面工厂管理（registerPage）
//   2. 导航（open/back/replace），驱动精确的生命周期转换
//   3. 页面缓存（LRU 池）
//   4. 进程死亡保护（saveAllStates/restoreAllStates）
//   5. 栈容器的 add/remove/raise
//
// 与 anystik 的三处**有意**偏差（移植计划 §6.3l L.2 已记）：
//   a. QSettings → Config（cJSON）。qlstik 的配置后端是 cJSON 不是 QSettings
//      （qlstik/src/config.h），且 qsettings_shim.h 尚未登记进任何 .pri。
//   b. **没有** 250ms 延迟销毁/移除（anystik 靠 m_destroyTimer 等淡入淡出动画跑完再
//      removeItem，避免 animator item 悬空）。本批砍掉动画（§6.3l L.2 表），
//      故 destroyPage / removeWidget 同步执行 —— 动画没了，延迟也没有存在理由。
//   c. anystik 的 finishRequested 用 lambda 连接；Qt3 moc 3.5 不生成新式 connect，
//      故改走单个私有 slot onPageFinishRequested() + sender() 判定，
//      守卫条件（!m_busy && 该页正是当前页）与 anystik 的 lambda 逐条等价。
class PageManager : public QObject {
    Q_OBJECT
public:
    explicit PageManager(StackedWidget* stackBox, QObject* parent = 0);
    virtual ~PageManager();

    // ── 注册页面类型 ──
    // id: 全局唯一标识
    // factory: 创建页面的工厂函数
    // reg: 缓存策略和默认导航模式
    void registerPage(const QString& id, PageFactory factory,
                      const PageRegistration& reg = PageRegistration());

    // ── 导航 ──
    // 打开页面（参数类比 Intent extras）
    void open(const QString& id, const QVariantMap& args = QVariantMap(),
              LaunchMode mode = LaunchMode::Standard);
    // 打开页面并等待结果（对应 startActivityForResult）
    void openForResult(const QString& id, const QVariantMap& args,
                       PageResultCallback callback,
                       LaunchMode mode = LaunchMode::Standard);
    // 返回上一页（对应 onBackPressed）
    void back();
    // 替换当前页（对应 finish + startActivity）
    void replace(const QString& id, const QVariantMap& args = QVariantMap());

    // ── 查询 ──
    int     depth() const { return m_history.size(); }
    Page*   currentPage() const;
    QString currentPageId() const;
    // 查找页面（活跃+缓存，不包括已销毁的）
    Page*   findPage(const QString& id) const;

    // 首帧门：主窗首批绘制后调用一次，把 onFirstFrame 转发给当前页。
    void notifyFirstFrame();

    // ── 状态保存/恢复（进程死亡） ──
    // 遍历所有存活页面，调用 onSaveInstanceState 并写入 Config
    void saveAllStates();
    // 从 Config 恢复栈结构和页面状态，重新激活栈顶页
    void restoreAllStates();

    // ── 缓存配置 ──
    void setCacheMaxSize(int n) { m_cacheMaxSize = n; }
    int  cacheMaxSize() const { return m_cacheMaxSize; }

    // ── 栈容器访问 ──
    StackedWidget* stackBox() const { return m_stackBox; }

signals:
    void pageCreated(const QString& id);
    void pageDestroyed(const QString& id);
    void pageActivated(const QString& id);
    void pageDeactivated(const QString& id);

public slots:
    // Page::finishRequested → back() 的落点（Qt3 moc 不生成 lambda connect）
    void onPageFinishRequested();
    // 语言切换：遍历所有存活页调 retranslateUi()
    void retranslateAll();

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
    void  ensureInStack(Page* page);
    void  removeFromStack(Page* page);
    // QStringList 去掉所有等于 value 的项。Qt3 无 removeAll()，Qt4+ 有。
    static void removeAllFrom(QStringList& list, const QString& value);
    // 从 Config 取某页上次保存的实例状态
    QVariantMap loadSavedState(const QString& id) const;
    void        storeSavedState(const QString& id, const QVariantMap& state);

    StackedWidget* m_stackBox;

    // 导航栈：页面 ID 有序列表，后进先出
    // ⚠ 用 QStringList 而非 QList<QString>：Qt3 里后者是 QPtrList<QString>
    //   （指针语义），详见 page.h 顶部「Qt3 容器铁律」第 1 条。
    QStringList m_history;

    // 所有存活页面（活跃+缓存）
    QMap<QString, Page*> m_pages;

    // LRU 缓存队列（front=最近使用，back=最久未用）
    QStringList m_cacheLRU;
    int  m_cacheMaxSize;

    // 工厂函数
    QMap<QString, PageFactory> m_factories;
    QMap<QString, PageRegistration> m_registrations;

    // openForResult 等待回调
    struct PendingResult {
        QString callerId;
        PageResultCallback callback;
        PendingResult() {}
    };
    QMap<QString, PendingResult> m_pendingResults;

    // 防止重入
    bool m_busy;
    bool m_firstFrameNotified;   // notifyFirstFrame 只生效一次
};

#endif
