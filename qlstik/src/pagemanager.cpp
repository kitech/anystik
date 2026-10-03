#include "pagemanager.h"

#include "config.h"
#include "cJSON.h"

// ⚠ 不 include <qdebug.h>：Qt3.5 无此文件，qDebug 由 qobject.h 提供。
//   Qt4+ 的 <QtDebug> 已被 Qt3 门排除，故按版本分支。
#ifdef QT3_BUILD
#include <qwidgetstack.h>
#else
#include <qstackedwidget.h>
#include <qdebug.h>
#endif

// ⚠ Qt3.5 的 qDebug/qWarning 由 qglobal.h 提供（qglobal.h:961-971），随 qobject.h
//   间接引入；**没有** <qdebug.h> 那个文件（Qt4+ 才有）。
#include <qobject.h>

#include <stdlib.h>
#include <string>

// 提前声明：pmStackAdd/Remove 会调 pmStackIndexOf，但下面按「先 add 后 indexOf」
// 的可读顺序书写，故 indexOf 的定义放在 add/remove 之后，需要前置声明。
namespace {
int pmStackIndexOf(StackedWidget* s, QWidget* page);
}

// ═══════════════════════════════════════════════════════════════════
// 日志包装
// ═══════════════════════════════════════════════════════════════════
// ⚠ **Qt3 的 qDebug/qWarning 是 printf 风格**（qglobal.h:961-971 只有
//   `const char*` / QString / QCString 三种重载），**没有**流运算符
//   `qDebug() << "x"`；空参 `qDebug()` 也编译不过（实测三条都 NO）。
//   Qt4+ 才是流式且允许空参。
// 故本文件所有日志一律走下面两个包装，内部按版本分流：
//   pmLog(...)   → Qt3: qDebug(格式串, 变参)；Qt4+: qDebug() << 拼接串
//   pmWarn(...)  → 同上，走 qWarning
// 写法上仍按 Qt4+ 的流式手感写，包装负责转换。
namespace {

#ifdef QT3_BUILD
inline void pmLog(const std::string& msg)
{
    qDebug(msg.c_str());
}
inline void pmWarn(const std::string& msg)
{
    qWarning(msg.c_str());
}
#else
inline void pmLog(const std::string& msg)
{
    qDebug() << msg.c_str();
}
inline void pmWarn(const std::string& msg)
{
    qWarning() << msg.c_str();
}
#endif

// QString → std::string（走 UTF-8，Qt3 的 qDebug 收 const char* 会按本地编码解释）
inline std::string pmStr(const QString& s)
{
    const QByteArray utf8 = qToUtf8(s);
    return std::string(utf8.data(), utf8.size());
}

// int 拼进日志用
inline std::string pmNum(int v)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "%d", v);
    return std::string(buf);
}

} // namespace

// QStringList → 日志串。⚠ 放 namespace 外是因为要用到 QStringList 完整类型，
// 而上面的 pmLog/pmWarn 块在它之前，且 QStringList 已在头文件里 include 完整。
static std::string pmStrList(const QStringList& l)
{
    std::string out;
    for (int i = 0; i < l.size(); i++) {
        if (i) { out += ", "; }
        out += pmStr(l[i]);
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════════
// StackedWidget 兼容（Qt3 QWidgetStack 与 Qt4+ QStackedWidget 方法名不同）
// ═══════════════════════════════════════════════════════════════════
// ⚠ Qt3 QWidgetStack 头文件实测（/opt/qt338sh/include/qwidgetstack.h）只有
//   addWidget / removeWidget / widget(int) / id(QWidget*) / visibleWidget()
//   / raiseWidget(int|QWidget*)，**没有** setCurrentWidget / currentWidget /
//   indexOf / count。这正是 compat34.h 里 qStackSetCurrent 存在的原因
//   （Qt3 分支是 stack->raiseWidget(page)）。下列映射集中在一处，
//   避免各调用点写 #ifdef。
// ═══════════════════════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════
// ⚠ **三处跨版本差异，全部实测确认**（不是看头文件猜的）：
//
//   1. `m.value(key)`：Qt3 的 QMap **根本没有这个方法**（value 系列只有
//      `keys()` / `values()`）；Qt4+ 有，但会在 key 不存在时**插入默认值**，
//      对 const 容器语义不对。故本文件一律不用它。
//   2. 迭代器取值的名字：Qt3 是 `it.data()`，Qt6 是 `it.value()`
//      （Qt5 两边都有）。故用 pmItValue() 包一层。
//   3. `QStringList::findIndex()`：Qt3 有（实测 OK），Qt6 删了，改名 `indexOf()`。
//      故用 pmListIndexOf() 包一层。
//
// ⚠ 顺序要求：pmItValue 必须定义在 pmMapGet **之前**（pmMapGet 的函数体里
//   要调它；C++ 模板的两阶段查找在这里不救我们，因为 pmItValue 依赖真实定义）。
//   qobject.h / qstringlist.h 由 pagemanager.h 在本文件顶部已引入。

// 迭代器取值：Qt3=it.data()，Qt6=it.value()
template <typename It>
static typename It::value_type pmItValue(It it)
{
#ifdef QT3_BUILD
    return it.data();
#else
    return it.value();
#endif
}

template <typename K, typename V>
static V pmMapGet(const QMap<K, V>& m, const K& key, const V& def = V())
{
    typename QMap<K, V>::const_iterator it = m.find(key);
    if (it == m.end()) { return def; }
    return pmItValue(it);
}

// QStringList 查找下标：Qt3=findIndex()，Qt6=indexOf()
static int pmListIndexOf(const QStringList& l, const QString& v)
{
#ifdef QT3_BUILD
    return l.findIndex(v);
#else
    return l.indexOf(v);
#endif
}

namespace {

void pmStackAdd(StackedWidget* s, QWidget* page)
{
    if (pmStackIndexOf(s, page) < 0) { s->addWidget(page); }
}

void pmStackRemove(StackedWidget* s, QWidget* page)
{
    if (pmStackIndexOf(s, page) >= 0) { s->removeWidget(page); }
}

int pmStackIndexOf(StackedWidget* s, QWidget* page)
{
    if (!s || !page) { return -1; }
#ifdef QT3_BUILD
    return s->id(page);          // Qt3: id() 不在栈里返回 -1（与 Qt4 indexOf 语义一致）
#else
    return s->indexOf(page);
#endif
}

} // namespace

// ═══════════════════════════════════════════════════════════════════
// QVariantMap ↔ JSON 编解码
// ═══════════════════════════════════════════════════════════════════
// Config 后端是 cJSON（qlstik/src/config.h），且 Config::value() 只回 QString
// （config.cpp:70-83，只认 cJSON_IsString / IsTrue / IsFalse），故把整份状态
// 序列化成**一个 JSON 文本**再以字符串形式塞进 config.json。
//
// 支持的类型子集（超出即跳过，不静默写错值）：
//   s=string  i=int  u=uint  b=bool  d=double  l=StringList  m=嵌套 Map
// 其余（QByteArray/QDate/自定义类型等）**不支持**，save 时跳过该键。
// 已知缺口：anystik 用 QSettings，其 value(key).toMap() 能原样存任意 QVariant；
// qlstik 的 cJSON 路径存不了任意 QVariant，这是存储后端换来的**能力收缩**，
// 不是移植遗漏。若将来页面需要存二进制/日期，补 "b"(base64) / "t"(ISO8601) 分支。
//
// ⚠ 「JSON 文本塞进 cJSON 字符串字段」的双重转义：Config::setValue 走
//   cJSON_AddStringToObject（config.cpp:99），它会把整段 JSON 文本当**字符串值**写入，
//   引号被转义成 \"。读回时 Config::value 用 qFromUtf8(item->valuestring)（:77），
//   拿到的是**已还原**的原始 JSON 文本，再交给 cJSON_Parse 才解出对象。
//   两级各有一次序列化/反序列化，不冲突，但别误以为 config.json 里是嵌套对象。

// 跨版本取 QVariant 的类型号：Qt3 是 type()，Qt4/5 是 userType()，Qt6 是 typeId()
static int pmVariantType(const QVariant& v)
{
#ifdef QT3_BUILD
    return int(v.type());
#elif QT_VERSION >= 0x060000
    return int(v.typeId());
#else
    return int(v.userType());
#endif
}

// 跨版本取类型枚举值：Qt6 把 QVariant::String 之类挪到了 QMetaType
enum PmType {
    PmInvalid, PmString, PmInt, PmUInt, PmBool, PmDouble, PmStringList, PmMap
};

static int pmClassify(const QVariant& v)
{
#ifdef QT3_BUILD
    const int t = int(v.type());
    if (t == QVariant::Invalid)  return PmInvalid;
    if (t == QVariant::String)  return PmString;
    if (t == QVariant::Int)     return PmInt;
    if (t == QVariant::UInt)    return PmUInt;
    if (t == QVariant::Bool)    return PmBool;
    if (t == QVariant::Double)  return PmDouble;
    if (t == QVariant::StringList) return PmStringList;
    if (t == QVariant::Map)     return PmMap;
    return PmInvalid;
#else
    const int t = pmVariantType(v);
    const int tStr = int(QMetaType::QString);
    const int tStrL = int(QMetaType::QStringList);
    const int tMap = int(QMetaType::QVariantMap);
    if (t == tStr)   return PmString;
    if (t == tStrL)  return PmStringList;
    if (t == tMap)   return PmMap;
    if (t == int(QMetaType::Int))     return PmInt;
    if (t == int(QMetaType::UInt))    return PmUInt;
    if (t == int(QMetaType::Bool))    return PmBool;
    if (t == int(QMetaType::Double))  return PmDouble;
    if (t == int(QMetaType::Void))    return PmInvalid;
    return PmInvalid;
#endif
}

static cJSON* pmEncodeMap(const QVariantMap& map);

static cJSON* pmEncodeValue(int kind, const QVariant& v)
{
    switch (kind) {
    case PmString:
        return cJSON_CreateString(qToUtf8(v.toString()).data());
    case PmInt:
        return cJSON_CreateNumber(double(v.toInt()));
    case PmUInt:
        return cJSON_CreateNumber(double(v.toUInt()));
    case PmBool:
        return cJSON_CreateBool(v.toBool() ? 1 : 0);
    case PmDouble:
        return cJSON_CreateNumber(v.toDouble());
    case PmStringList: {
        cJSON* arr = cJSON_CreateArray();
        const QStringList l = v.toStringList();
        for (int i = 0; i < l.size(); i++) {
            cJSON_AddItemToArray(arr, cJSON_CreateString(qToUtf8(l[i]).data()));
        }
        return arr;
    }
    case PmMap:
        return pmEncodeMap(v.toMap());
    default:
        return 0;
    }
}

static cJSON* pmEncodeMap(const QVariantMap& map)
{
    cJSON* obj = cJSON_CreateObject();
    if (!obj) { return 0; }
    QVariantMap::const_iterator it = map.constBegin();
    for (; it != map.constEnd(); ++it) {
        const int kind = pmClassify(pmItValue(it));
        if (kind == PmInvalid) { continue; }  // 不支持的类型：跳过，不写错值
        cJSON* val = pmEncodeValue(kind, pmItValue(it));
        if (!val) { continue; }
        // t = 类型标记，v = 值。分开存是为了区分 Int 1 与 Bool true
        // （JSON 里都是 "1"/"true"，靠标记才不丢类型）
        cJSON* box = cJSON_CreateObject();
        cJSON_AddItemToObject(box, "t", cJSON_CreateNumber(double(kind)));
        cJSON_AddItemToObject(box, "v", val);
        cJSON_AddItemToObject(obj, qToUtf8(it.key()).data(), box);
    }
    return obj;
}

static QVariantMap pmDecodeMap(cJSON* obj)
{
    QVariantMap out;
    if (!cJSON_IsObject(obj)) { return out; }
    const cJSON* item = obj->child;   // qlcomp 里的 cJSON 是新版，字段名是 child
                                    // （老版才叫 cJSON_Child）
    for (; item; item = item->next) {
        if (!item->string) { continue; }
        // ⚠ 类型标记在子对象 "t" 里（见 pmEncodeMap:278-279），不是 item 自身的
        //   valuedouble —— 那是 "v" 的数值，读它会把 String/Bool 全解成 Int。
        const cJSON* typeBox = cJSON_GetObjectItem(item, "t");
        if (!cJSON_IsNumber(typeBox)) { continue; }
        const int kind = int(typeBox->valuedouble);
        cJSON* val = cJSON_GetObjectItem(item, "v");
        switch (kind) {
        case PmString: {
            if (!cJSON_IsString(val)) { break; }
            out[qFromUtf8(item->string)] = qFromUtf8(val->valuestring);
            break;
        }
        case PmInt:
            if (cJSON_IsNumber(val)) { out[qFromUtf8(item->string)] = int(val->valuedouble); }
            break;
        case PmUInt:
            if (cJSON_IsNumber(val)) { out[qFromUtf8(item->string)] = uint(val->valuedouble); }
            break;
        case PmBool:
            if (val) { out[qFromUtf8(item->string)] = (cJSON_IsTrue(val) != 0); }
            break;
        case PmDouble:
            if (cJSON_IsNumber(val)) { out[qFromUtf8(item->string)] = val->valuedouble; }
            break;
        case PmStringList: {
            if (!cJSON_IsArray(val)) { break; }
            // ⚠ Qt3 QVariant 无 QStringList↔QStringList 的隐式互转的**赋值**路径，
            //   实测 v.toStringList() 在 Qt3 返回 QStringList（QValueList<QString>），
            //   而 QVariant 的构造重载要 QStringList 显式包一层。
            QStringList l;
            const cJSON* e = val->child;
            for (; e; e = e->next) { l.append(qFromUtf8(e->valuestring)); }
            out[qFromUtf8(item->string)] = QVariant(l);
            break;
        }
        case PmMap:
            out[qFromUtf8(item->string)] = QVariant(pmDecodeMap(val));
            break;
        default:
            break;
        }
    }
    return out;
}

// 序列化成文本（交给 Config::setValue 当字符串存）
static QString pmMapToText(const QVariantMap& map)
{
    cJSON* obj = pmEncodeMap(map);
    if (!obj) { return QString(); }
    char* txt = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!txt) { return QString(); }
    const QString out = qFromUtf8(txt);
    free(txt);
    return out;
}

static QVariantMap pmTextToMap(const QString& text)
{
    if (text.isEmpty()) { return QVariantMap(); }
    const std::string raw = qToUtf8(text).data();
    cJSON* obj = cJSON_Parse(raw.c_str());
    if (!obj) { return QVariantMap(); }   // 坏 JSON：按空状态处理，不崩
    const QVariantMap out = pmDecodeMap(obj);
    cJSON_Delete(obj);
    return out;
}

static QString pmStringListToText(const QStringList& l)
{
    cJSON* arr = cJSON_CreateArray();
    if (!arr) { return QString(); }
    for (int i = 0; i < l.size(); i++) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(qToUtf8(l[i]).data()));
    }
    char* txt = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!txt) { return QString(); }
    const QString out = qFromUtf8(txt);
    free(txt);
    return out;
}

static QStringList pmTextToStringList(const QString& text)
{
    QStringList out;
    if (text.isEmpty()) { return out; }
    const std::string raw = qToUtf8(text).data();
    cJSON* arr = cJSON_Parse(raw.c_str());
    if (!arr) { return out; }
    const cJSON* e = arr->child;
    for (; e; e = e->next) {
        if (cJSON_IsString(e)) { out.append(qFromUtf8(e->valuestring)); }
    }
    cJSON_Delete(arr);
    return out;
}

// QStringList 去重值删除。⚠ Qt3 QStringList 没有 removeAll()（Qt4+ 才有），
//   也没有 indexOf()（用 findIndex()）。倒序遍历 + erase 才能安全边查边删。
void PageManager::removeAllFrom(QStringList& list, const QString& value)
{
    // ⚠ Qt3 QStringList 的三条删除路径实测结果（都不是 Qt4+ 写法）：
    //   1. `list.erase(list.begin() + i)` **不行** —— Qt3 iterator 不支持
    //      `iter + int`（报 "no match for operator+"），只能 erase(begin())。
    //   2. `list.remove(v)` 可行，但只删**第一个**匹配。
    //   3. **迭代器手动推进 + erase(it) 可行**，且 erase 返回下一个迭代器，
    //      所以删掉当前项后直接用返回值继续，不用 `++it`（否则会跳过一项）。
    // 用第 3 条。
    QStringList::Iterator it = list.begin();
    while (it != list.end()) {
        if (*it == value) {
            it = list.erase(it);
        } else {
            ++it;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
// PageManager
// ═══════════════════════════════════════════════════════════════════

PageManager::PageManager(StackedWidget* stackBox, QObject* parent)
    : QObject(parent)
    , m_stackBox(stackBox)
    , m_cacheMaxSize(5)
    , m_busy(false)
{
}

PageManager::~PageManager()
{
    // 不需要手动清理 —— Page 是 m_stackBox 的子 widget，Qt 父子关系会处理
}

void PageManager::registerPage(const QString& id, PageFactory factory,
                               const PageRegistration& reg)
{
    if (m_factories.contains(id)) {
        pmWarn("[PageManager] duplicate register:" + pmStr(id));
        return;
    }
    m_factories[id] = factory;
    m_registrations[id] = reg;
}

// ── 内部: 创建并初始化新页面 ──
Page* PageManager::createPage(const QString& id, const QVariantMap& args,
                              const QVariantMap& savedState)
{
    QMap<QString, PageFactory>::const_iterator fit = m_factories.find(id);
    if (fit == m_factories.end()) {
        pmWarn("[PageManager] unknown page:" + pmStr(id));
        return 0;
    }

    Page* page = pmItValue(fit)();
    if (!page) { return 0; }
    page->setPageManager(this);
    page->setPageId(id);
    page->setState(PageState::None);

    pmStackAdd(m_stackBox, page);
    m_pages[id] = page;

    // 连接 finishRequested → back()。⚠ Qt3 moc 3.5 不生成新式 connect（lambda），
    // 故统一连到本页的 onPageFinishRequested()，由 sender() 定位是哪个页。
    connect(page, SIGNAL(finishRequested()), this, SLOT(onPageFinishRequested()));

    page->onCreate(args, savedState);
    page->setState(PageState::Created);

    emit pageCreated(id);
    pmLog("[PageManager] created:" + pmStr(id));
    return page;
}

// ── 内部: 销毁页面 ──
void PageManager::destroyPage(Page* page)
{
    if (!page || page->state() == PageState::Destroyed) { return; }

    const QString id = page->pageId();
    removeAllFrom(m_cacheLRU, id);

    page->onDestroy();
    page->setState(PageState::Destroyed);
    page->setPageManager(0);

    m_pages.remove(id);
    pmStackRemove(m_stackBox, page);
    page->deleteLater();

    emit pageDestroyed(id);
    pmLog("[PageManager] destroyed:" + pmStr(id));
}

// ── 内部: 激活页面（使其成为当前项）──
void PageManager::activatePage(Page* page)
{
    if (!page) { return; }

    // ⚠ 与 anystik 的差异：anystik 在这里取消「待处理的延迟销毁/移除」
    //   （m_destroyTimer / m_pendingRemove，等动画跑完用）。本批砍掉动画，
    //   back()/replace() 里改成同步 destroyPage / removeWidget，
    //   所以这两个 pending 概念整体不存在，没有可取消的东西。

    switch (page->state()) {
    case PageState::Created:
        // 新创建: onStart → onResume
        page->onStart();
        page->setState(PageState::Started);
        break;

    case PageState::Stopped:
        // 从缓存: onRestart → onStart → onResume
        page->setFinishing(false);
        page->onRestart();
        page->onStart();
        page->setState(PageState::Started);
        break;

    case PageState::Paused:
        // 从部分覆盖恢复: 只 onResume
        break;

    default:
        pmWarn("[PageManager] activatePage invalid state:" + pmNum(int(page->state()))
               + " for " + pmStr(page->pageId()));
        return;
    }

    setStackBoxCurrent(page);
    page->onResume();
    page->setState(PageState::Resumed);

    emit pageActivated(page->pageId());
    pmLog("[PageManager] activated:" + pmStr(page->pageId()));
}

// ── 内部: 停用当前页面（onPause → onStop）──
void PageManager::deactivateCurrentPage()
{
    Page* page = currentPage();
    if (!page) { return; }

    if (page->state() == PageState::Resumed) {
        page->onPause();
        page->setState(PageState::Paused);
    }
    if (page->state() == PageState::Paused ||
        page->state() == PageState::Started) {
        page->onStop();
        page->setState(PageState::Stopped);
    }

    emit pageDeactivated(page->pageId());
    pmLog("[PageManager] deactivated:" + pmStr(page->pageId()));
}

// ── 内部: 将页面加入 LRU 缓存 ──
void PageManager::addToCache(Page* page)
{
    const QString id = page->pageId();
    removeAllFrom(m_cacheLRU, id);
    m_cacheLRU.push_front(id);

    // 超量时驱逐最久未用的
    while (m_cacheLRU.size() > m_cacheMaxSize) {
        evictLRU();
    }
}

// ── 内部: 驱逐 LRU 缓存中最久未用的页面 ──
void PageManager::evictLRU()
{
    if (m_cacheLRU.isEmpty()) { return; }

    const QString id = m_cacheLRU.back();
    m_cacheLRU.pop_back();

    Page* page = pmMapGet(m_pages, id, (Page*)0);
    if (!page) { return; }

    pmLog("[PageManager] LRU evict:" + pmStr(id));

    // 在销毁前保存状态（进程死亡后可能需要恢复）
    QVariantMap state;
    page->onSaveInstanceState(state);
    if (!state.isEmpty()) { storeSavedState(id, state); }

    // 标记 finishing = true（即将被销毁）
    page->setFinishing(true);
    destroyPage(page);
}

// ── 内部: 设置栈容器当前项 ──
void PageManager::setStackBoxCurrent(Page* page)
{
    if (!page || !m_stackBox) { return; }
    ensureInStack(page);
    if (pmStackIndexOf(m_stackBox, page) < 0) { return; }
    qStackSetCurrent(m_stackBox, page);
    // 让作用域链每层可获焦，否则键盘走不到页内控件（anystik 注释：Qt 要求
    // 每层 focus=true）。
    // ⚠ Qt3 **没有** forceActiveFocus（Qt4 才引入），也没有 setFocus(FocusReason)
    //   重载（实测 Qt3 setFocus() 无参）。Qt3 用 setFocus()，Qt4+ 用 forceActiveFocus
    //   配 ActiveWindowFocusReason —— 语义相同（激活窗口并抢焦）。
#ifdef QT3_BUILD
    page->setFocus();
#else
    page->setFocus(Qt::ActiveWindowFocusReason);
#endif
}

void PageManager::ensureInStack(Page* page)
{
    pmStackAdd(m_stackBox, page);
}

void PageManager::removeFromStack(Page* page)
{
    pmStackRemove(m_stackBox, page);
}

// ── 实例状态存取（走 Config/cJSON，不用 QSettings，见类注释偏差 a）──
QVariantMap PageManager::loadSavedState(const QString& id) const
{
    return pmTextToMap(Config::value("page_" + id + "_state"));
}

void PageManager::storeSavedState(const QString& id, const QVariantMap& state)
{
    Config::setValue("page_" + id + "_state", pmMapToText(state));
}

// ── 查询 ──
Page* PageManager::currentPage() const
{
    if (m_history.isEmpty()) { return 0; }
    return pmMapGet(m_pages, m_history.back(), (Page*)0);
}

QString PageManager::currentPageId() const
{
    Page* p = currentPage();
    return p ? p->pageId() : QString();
}

Page* PageManager::findPage(const QString& id) const
{
    return pmMapGet(m_pages, id, (Page*)0);
}

// ── 导航: open ──
void PageManager::open(const QString& id, const QVariantMap& args,
                       LaunchMode mode)
{
    if (m_busy) {
        pmWarn("[PageManager] busy, ignoring open:" + pmStr(id));
        return;
    }
    m_busy = true;

    // 检查 LaunchMode
    if (mode == LaunchMode::SingleTop && !m_history.isEmpty()) {
        if (m_history.last() == id) {
            // 已在栈顶: onNewIntent
            Page* top = currentPage();
            if (top) { top->onNewIntent(args); }
            m_busy = false;
            return;
        }
    }

    if (mode == LaunchMode::SingleInstance) {
        // 如果已存在，bringToFront
        const int existingIdx = pmListIndexOf(m_history, id);
        if (existingIdx >= 0) {
            // 先停用当前页
            deactivateCurrentPage();

            // 回退历史到目标页
            while (m_history.size() > existingIdx + 1) {
                const QString popId = m_history.last();
                Page* popPage = pmMapGet(m_pages, popId, (Page*)0);
                const PageRegistration popReg = pmMapGet(m_registrations, popId);

                if (popPage) {
                    popPage->setFinishing(popReg.policy == CachePolicy::Transient);
                    if (popReg.policy == CachePolicy::Transient) {
                        destroyPage(popPage);
                    } else {
                        if (popReg.policy == CachePolicy::LRU) {
                            addToCache(popPage);
                        }
                        // ⚠ anystik 此处是 QTimer::singleShot(250) 等动画跑完再
                        // removeItem；本批无动画，改同步移除。
                        removeFromStack(popPage);
                    }
                }
                m_history.pop_back();
            }

            // 激活目标页
            Page* target = pmMapGet(m_pages, id, (Page*)0);
            if (target) {
                target->onNewIntent(args);
                activatePage(target);
            }
            m_busy = false;
            return;
        }
    }

    // ── Standard 或 SingleInstance 未命中 ──
    // 1. 停用当前页
    deactivateCurrentPage();

    // 2. 创建或恢复目标页
    Page* target = findPage(id);
    if (!target) {
        // 全新创建。⚠ anystik 此处只在 m_history 非空（即确属恢复）时才读 QSettings，
        // 这里照搬同样条件，避免首次启动读到上次的陈旧状态。
        QVariantMap savedState;
        if (!m_history.isEmpty()) {
            savedState = loadSavedState(id);
        }
        target = createPage(id, args, savedState);
    } else {
        // 在缓存中（cacheable/permanent）
        target->onNewIntent(args);
        // 如果页面在 m_pages 中但不在栈里（之前 back 时被移除了），重新添加
        ensureInStack(target);
    }

    if (!target) {
        pmWarn("[PageManager] open: failed to create " + pmStr(id));
        m_busy = false;
        return;
    }

    // 3. 激活目标页
    activatePage(target);

    // 4. 记录历史（栈顶去重，避免重复推入导致返回栈膨胀）
    if (m_history.isEmpty() || m_history.last() != id) {
        m_history.push_back(id);
    }

    m_busy = false;
}

// ── 导航: openForResult ──
void PageManager::openForResult(const QString& id, const QVariantMap& args,
                                PageResultCallback callback,
                                LaunchMode mode)
{
    if (m_history.isEmpty()) {
        pmWarn("[PageManager] openForResult: no caller");
        return;
    }
    PendingResult pr;
    pr.callerId = m_history.last();
    pr.callback = callback;
    m_pendingResults[id] = pr;
    open(id, args, mode);
}

// ── 导航: back ──
void PageManager::back()
{
    if (m_busy) {
        pmWarn("[PageManager] busy, ignoring back");
        return;
    }

    if (m_history.size() <= 1) {
        pmLog("[PageManager] back: at root, ignored");
        return;
    }

    m_busy = true;

    const QString currentId = m_history.last();
    const QString targetId = m_history[m_history.size() - 2];
    const PageRegistration reg = pmMapGet(m_registrations, currentId);
    const CachePolicy policy = reg.policy;

    Page* current = pmMapGet(m_pages, currentId, (Page*)0);

    // 1. 停用当前页
    if (current) {
        current->setFinishing(policy == CachePolicy::Transient);
        deactivateCurrentPage();
    }

    // 2. 查找或创建目标页
    Page* target = pmMapGet(m_pages, targetId, (Page*)0);
    if (!target) {
        target = createPage(targetId, QVariantMap(), loadSavedState(targetId));
    } else {
        // 重新添加到栈里（如果之前 back 时被移除了）
        ensureInStack(target);
        // 结果回传给等待方。
        // ⚠ 键是**被关闭的那一页**（结果产出页）的 id —— 见 openForResult:760
        //   的 m_pendingResults[id]，其中 id 是被打开的页。早前误用 targetId
        //   （要返回的那一页），导致回调永远查不到、结果丢失。
        QMap<QString, PendingResult>::iterator rit = m_pendingResults.find(currentId);
        if (rit != m_pendingResults.end()) {
            if (current) {
                (pmItValue(rit).callback)(current->resultCode(), current->resultData());
            } else {
                (pmItValue(rit).callback)(-1, QVariantMap());
            }
            m_pendingResults.erase(rit);
        }
    }

    // 3. 激活目标页
    if (target) { activatePage(target); }

    // 4. 处理当前页（销毁或从栈里移除）
    // ⚠ anystik 此处是延迟 250ms（等淡入淡出动画结束，避免 animator item 悬空）。
    //   本批无动画，同步执行 —— 语义等价且少一次定时器往返。
    if (current) {
        if (policy == CachePolicy::Transient) {
            destroyPage(current);
        } else {
            if (policy == CachePolicy::LRU) { addToCache(current); }
            removeFromStack(current);
        }
    }

    // 5. 从历史中移除
    m_history.pop_back();

    m_busy = false;
}

// ── 导航: replace ──
void PageManager::replace(const QString& id, const QVariantMap& args)
{
    if (m_busy) {
        pmWarn("[PageManager] busy, ignoring replace:" + pmStr(id));
        return;
    }
    m_busy = true;

    const QString currentId = m_history.isEmpty() ? QString() : m_history.last();
    Page* current = pmMapGet(m_pages, currentId, (Page*)0);

    // 1. 停用当前页并销毁（同步，见 back() 步骤 4 的说明）
    if (current) {
        current->setFinishing(true);
        deactivateCurrentPage();
        destroyPage(current);
    }

    // 2. 替换历史最后一项（或为空时新增）
    if (!m_history.isEmpty()) {
        m_history.last() = id;
    } else {
        m_history.push_back(id);
    }

    // 3. 创建或激活目标页
    Page* target = pmMapGet(m_pages, id, (Page*)0);
    if (!target) {
        target = createPage(id, args, loadSavedState(id));
    } else {
        target->onNewIntent(args);
    }

    if (target) { activatePage(target); }

    m_busy = false;
}

// ── finishRequested 落点 ──
void PageManager::onPageFinishRequested()
{
    // ⚠ **两侧都没有 `qobject_cast`**：Qt3 完全没有（编译不过），Qt6 移除了。
    //   跨版本都可用的是 `dynamic_cast` —— moc 保证 Q_OBJECT 类带 type_info，
    //   且跨 Qt/DLL 边界 RTTI 也能工作（Page 与 PageManager 同属本程序）。
    //   ⚠ 不用 isA("Page") 那种字符串法：Qt6 已删 isA，且类改名后字符串不会报错，
    //     静默退化成"永不匹配"。dynamic_cast 改名后编译期就断。
    QObject* s = const_cast<QObject*>(sender());
    Page* p = dynamic_cast<Page*>(s);
    if (!p) { return; }
    // 守卫条件与 anystik 的 lambda 逐条等价：非重入中，且请求者正是当前页
    if (!m_busy && p == currentPage()) {
        back();
    }
}

// ── 语言切换：遍历所有存活页 ──
void PageManager::retranslateAll()
{
    QMap<QString, Page*>::const_iterator it = m_pages.constBegin();
    for (; it != m_pages.constEnd(); ++it) {
        Page* page = *it;
        if (page->state() == PageState::Destroyed) { continue; }
        page->retranslateUi();
    }
}

// ── 状态保存（进程死亡保护）──
void PageManager::saveAllStates()
{
    pmLog("[PageManager] saveAllStates");

    QMap<QString, Page*>::const_iterator it = m_pages.constBegin();
    for (; it != m_pages.constEnd(); ++it) {
        Page* page = *it;
        if (page->state() == PageState::None ||
            page->state() == PageState::Destroyed) { continue; }

        QVariantMap state;
        page->onSaveInstanceState(state);
        if (!state.isEmpty()) { storeSavedState(it.key(), state); }
    }

    // 保存栈结构
    Config::setValue("pageManager_history", pmStringListToText(m_history));
    Config::setValue("pageManager_currentId", currentPageId());

    pmLog("[PageManager] saved " + pmNum(m_pages.size()) + " pages, history:"
          + pmStrList(m_history));
}

// ── 状态恢复（进程死亡后重建）──
void PageManager::restoreAllStates()
{
    const QStringList history =
        pmTextToStringList(Config::value("pageManager_history"));
    if (history.isEmpty()) { return; }

    pmLog("[PageManager] restoreAllStates, history:" + pmStrList(history));

    m_history = history;

    // 重建所有历史中的页面
    for (int i = 0; i < m_history.size(); i++) {
        const QString id = m_history[i];
        if (m_pages.contains(id)) { continue; }

        if (!m_factories.contains(id)) {
            pmWarn("[PageManager] restore: unknown page " + pmStr(id));
            continue;
        }

        Page* page = createPage(id, QVariantMap(), loadSavedState(id));
        if (!page) { continue; }

        // 如果这是当前页，激活它
        if (i == m_history.size() - 1) {
            activatePage(page);
        }
    }

    pmLog("[PageManager] restored " + pmNum(m_pages.size()) + " pages");
}
