#ifndef QLSTIK_PAGE_H
#define QLSTIK_PAGE_H

// 页基类 —— 对标 anystik/src/page.h（只读参考，不改）。
//   * 生命周期语义 1:1 照搬（Android Activity/Fragment 那套 onCreate→…→onDestroy）
//   * 手段换 Qt3 原生：QskControl → QWidget，QskStackBox 由 StackedWidget 承担
//
// 头文件规约（移植计划 §10 UI 铁律）：
//   * 只写 Qt3 原生小写头 + qlcomp compat34.h 白名单
//   * ⚠ **必须**用 `signals:` / `public slots:` 小写形式，**不能**用 `Q_SIGNALS:` /
//     `Q_SLOTS:` —— Qt3.5 的 qobjectdefs.h 里只有小写形式（已 grep 确认），
//     `Q_SIGNALS` 是 Qt4 才引入的。同理 `Q_INVOKABLE`、`qdebug.h` 在 Qt3 都不存在
//     （`qDebug` 由 qobject.h 提供）。仓库既有的双端文件（LimeScrollBar.h、chatview.h、
//     mainwindow.h）一律小写，本文件遵循。
//   * ⚠ moc 3.5.0(version 26) 实测：**不能**把 elaborated-type-specifier 当模板实参。
//     `std::function<class Page*()>` 与 `QMap<QString, class Page*>` 均报
//     "Error: syntax error"；而先单独写一行前向声明再引用则通过。
//     所以凡是「类还没定义但要进容器」的场合，一律先 `class Xxx;` 单独声明。
//     （`enum class` / `nullptr` / NSDMI / `= {}` 默认参数 / `override` / `auto`
//       moc 3.5 全部实测通过，只有这一条不行。）
//
// ⚠⚠ **Qt3 容器铁律（全部 g++ -fsyntax-only 实测，不是看头文件猜的）**：
//
//   1. `QList<QString>` 在 Qt3 解析成 **`QPtrList<QString>`**（指针语义！）。
//      `L.append("a")` 直接编译失败：`cannot convert 'const char[2]' to
//      'const QString*'`。Qt3 里存 QString 列表**必须**用 `QStringList`
//      （`qstringlist.h:61`：`class QStringList : public QValueList<QString>`）。
//      ⚠ 这不只是改名：Qt6 侧 `QList<QString>` 才是值语义，届时行为会变。
//      本文件统一用 QStringList，双端一致。
//
//   2. Qt3 `QStringList` **没有** `removeAll()`（Qt4+ 才有）、**没有** `indexOf()`
//      （用 `findIndex()`）、**没有** `takeLast()`（用 `erase()` 或倒序遍历）。
//      实测：`size`/`back`/`first`/`last`/`push_front`/`pop_back`/`prepend`
//      `/isEmpty`/`count`/`contains`/`at`/`clear`/`append`/`findIndex`
//      `/erase(begin)`/`remove` **全部可用**。
//
//   3. Qt3 `QMap` **没有 `value(key)` 方法**（整个 value 系列只有 `keys()` 和
//      `values()`）。取值一律走 `m.find(k)` 判 `!= end()` 后取迭代器值，
//      或 pagemanager.cpp 的 pmMapGet()/pmItValue() 辅助函数。实测 `m[k]` 可用
//      （会插入默认值）、`find`/`remove`/`contains`/`insert`/`begin`/`it.data()`
//      /`it.key()` 可用。
//
//   4. Qt3 `QStringList` 的 `erase()` **只收迭代器，且 iterator 不支持 `iter + int`**
//      （实测 `begin() + i` 报 "no match for operator+"）。删元素只能
//      **迭代器手动推进 + `it = list.erase(it)`**（erase 返回下一个迭代器，
//      所以不能 `++it`，否则跳过一项）。
//
//   5. Qt3 **没有** `QStringList::split()`（Qt4+ 才有）。
//
//   6. Qt3 **没有** `QWidget::forceActiveFocus()`（Qt4 引入），也没有
//      `setFocus(FocusReason)` 重载 —— 只有无参 `setFocus()`。Qt6 侧 setFocus
//      只接 FocusReason，所以要 #ifdef 分流。
//
//   7. Qt3 **没有** `qobject_cast`（Qt4 引入、Qt6 又移除），跨版本唯一可用的是
//      `dynamic_cast`（moc 保证 Q_OBJECT 类带 type_info）。**不要**用
//      `isA("Page")` 字符串法：Qt6 已删 isA，且类改名后字符串不报错、
//      静默退化成「永不匹配」。
//
//   8. Qt3 **没有** `Q_INVOKABLE`（Qt4 引入）、没有 `Q_SIGNALS`/`Q_SLOTS`
//      （只有小写 `signals:`/`slots:`）、没有 `qdebug.h` 文件
//      （qDebug/qWarning 在 qglobal.h:961-971）。
//
//   9. Qt3 `qDebug`/`qWarning` 是 **printf 风格**（只有 `const char*`/QString/
//      QCString 三种重载），**没有**流运算符 `<<`，空参 `qDebug()` 也编译不过
//      （三条实测全 NO）。Qt4+ 才是流式。跨版本日志一律走 pagemanager.cpp 的
//      pmLog()/pmWarn() 包装。
//
//   10. Qt3.5 **没有** `QVariantMap` typedef（已 grep 整个 include 目录确认），
//      Qt4+/Qt6 由 qvariant.h 提供。故本文件按 QT3_BUILD 显式补 typedef
//      （`#ifndef` 判不出来 —— 两者都不是宏）。

#include <qwidget.h>
#include <qstring.h>
#include <qmap.h>
#include <qvariant.h>

// ⚠ Qt3.5 的头文件里**根本没有** QVariantMap（已 grep /opt/qt338sh/include/*.h 确认），
//   Qt4+/Qt6 才由 qvariant.h 提供。本文件按版本自己补上，否则 Qt3 侧编译不过、
//   而 Qt6 侧又与 qvariant.h 的定义重复。用 #ifndef 无法区分（两者都不是宏），
//   故按 QT3_BUILD 显式分支 —— qlcomp compat34.h 是同样的做法。
#ifdef QT3_BUILD
typedef QMap<QString, QVariant> QVariantMap;
#endif

class PageManager;

// ── 页的生命周期状态，与 Android Activity/Fragment 对应 ──
// None:    初始状态，onCreate 尚未调用
// Created: onCreate 已返回，UI 已构建，页面在栈里但不可见
// Started: onStart 已返回，页面正在变为可见
// Resumed: onResume 已返回，页面完全交互、用户可操作
// Paused:  onPause 已返回，页面被部分覆盖（另一个页面即将取代前台）
// Stopped: onStop 已返回，页面完全不可见（缓存中或即将销毁）
// Destroyed: onDestroy 已返回，页面已从栈里移除
enum class PageState {
    None, Created, Started, Resumed,
    Paused, Stopped, Destroyed
};

// ── 导航模式 ──
// Standard:       每次 open 创建新实例
// SingleTop:      目标页已在栈顶时复用，调用 onNewIntent
// SingleInstance: 目标页已存在时 bringToFront + clearTop + onNewIntent
enum class LaunchMode { Standard, SingleTop, SingleInstance };

// ── 页面缓存策略 ──
// Transient:  back() 时直接销毁
// LRU:        back() 时入缓存池，超量时 LRU 驱逐
// Permanent:  永远驻留，不驱逐
enum class CachePolicy { Transient, LRU, Permanent };

class Page : public QWidget {
    Q_OBJECT
public:
    explicit Page(QWidget* parent = 0);
    virtual ~Page();

    // 当前生命周期状态
    PageState state() const { return m_state; }

    // 页的唯一标识符，由 PageManager 在注册时设置
    QString pageId() const { return m_pageId; }

    // 是否正在被关闭（用户按返回、被 replace 等）
    // onSaveInstanceState 中可查询，以决定是否保存临时状态
    bool isFinishing() const { return m_isFinishing; }

    // 获取所属 PageManager（0 表示未关联）
    PageManager* pageManager() const { return m_pageManager; }

    // ── 生命周期回调 ──
    // 所有回调默认实现为空，使用者只需 override 需要的
    // 构造 → onCreate(args, savedState) → onStart → onResume
    // 暂停 → onPause → (onSaveInstanceState) → onStop
    // 缓存恢复 → onRestart → onStart → onResume
    // 销毁 → onDestroy
    // 参数说明:
    //   launchArgs: open() 时传入的参数（类比 Intent extras）
    //   savedState: 进程死亡恢复时，配置里保存的之前的状态
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

    // 首帧门回调：窗口首批绘制完成、事件排干后，由 PageManager 调一次。
    // 默认空实现；页面可把「首帧后才该做」的重活（如列表全量加载）放这里。
    // ⚠ 不加 Q_INVOKABLE：Qt3.5 无此宏（page.h 陷阱 #8）。
    virtual void onFirstFrame() {}

    // ── 语言切换 ──
    // 语言切换时由 Lang 广播、PageManager 遍历存活页调用（见 pagemanager.cpp 的
    // retranslateAll）。⚠ 不加 Q_INVOKABLE：Qt3.5 无此宏（Qt4 才引入）；本方法靠
    // 直接调用而非元对象反射，故无宏也能工作。
    virtual void retranslateUi() {}

    // ── 返回值机制（对应 startActivityForResult） ──
    // 页面自身关闭前调用 setResult(code, data)，PageManager 在 back() 时把结果交给上一页
    void setResult(int resultCode, const QVariantMap& data = QVariantMap());
    int  resultCode() const { return m_resultCode; }
    const QVariantMap& resultData() const { return m_resultData; }

    // ── 请求关闭自身 ──
    // PageManager 收到 finishRequested 信号后执行 back()
    void finish();

signals:
    void finishRequested();

private:
    friend class PageManager;

    PageManager* m_pageManager;
    PageState    m_state;
    QString      m_pageId;
    bool         m_isFinishing;
    int          m_resultCode;  // -1 对应 Android RESULT_CANCELED
    QVariantMap  m_resultData;

    // 内部设置，仅 PageManager 使用
    void setState(PageState s) { m_state = s; }
    void setFinishing(bool f) { m_isFinishing = f; }
    void setPageManager(PageManager* pm) { m_pageManager = pm; }
    void setPageId(const QString& id) { m_pageId = id; }
};

#endif
