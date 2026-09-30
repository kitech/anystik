#ifndef QLSTIK_QJSON_SHIM_H
#define QLSTIK_QJSON_SHIM_H

// QJson 垫片（Qt3/Qt4 专用，底座 = qldox 的 cJSON 1.7.19，见 stikcommon/qldox.pri）。
// Qt5+ 走原生 QJson，本头不参与编译——只在 `#if QT_VERSION < 0x050000` 分支里被
// include，且 stikcommon.pri 用 `lessThan(QT_VERSION, 5.0.0)` 把 qjson_shim.cpp
// 排除在 Qt5+ 构建之外，两者不会同时存在。
//
// ── 所有权模型 ────────────────────────────────────────────────────────────
// cJSON 是 malloc 树、显式 cJSON_Delete；Qt 的 QJson* 是值类型 + 隐式共享。
// 两者语义对撞的地方（davbisync_baseline.cpp 就有）：
//
//     QJsonObject local;                    // 孤儿对象
//     root.insert("local", local);          // root 要持有 local 的内容
//                                          // …而 local 自己还活着
//
// 若"谁构造谁 delete"，这里必然双重释放。故：
//
//   * 引入 qjsonshim::Tree —— 引用计数的 cJSON 树，唯一 owner，refs→0 时 Delete。
//   * QJsonValue / QJsonObject / QJsonArray 全部是 (Tree*, cJSON*) 对：
//       拷贝构造 / 赋值 → retain 树（O(1)，对齐 Qt 隐式共享）
//       析构           → release 树
//       空态           → (nullptr, nullptr)
//   * 从父树里摘出来的子对象（value() / toObject() / 迭代器 value()）**同样
//     retain 父树**，因此能独立活过父对象——这正是 Qt 的行为。
//
// ── 四态映射 ──────────────────────────────────────────────────────────────
// cJSON 没有"键不存在"的概念，用 node==nullptr 表达 Undefined，与显式 null 区分：
//
//     Undefined  node == nullptr
//     Null       node->type == cJSON_NULL
//     Bool/Number/String/Object/Array   cJSON_IsXxx(node)
//
// davbisync_baseline.cpp 的"新键优先、旧单字母键回退"（size↔s）就靠这个区分。
//
// ── 与 Qt 的行为差异（已知、刻意）────────────────────────────────────────
//   * insert 用 cJSON_ReplaceItemInObject（Qt 的 insert 是覆盖语义；用
//     cJSON_AddItemToObject 会产生重复键，Qt 不允许）。
//   * 数值一律走 valuedouble。cJSON 1.7.19 的 valueint 已废弃（写它不更新
//     valuedouble 的完整精度），而 mtimeMsec 是 epoch 毫秒 ~1.7e12，
//     只有 double 能无损承载。
//   * cJSON 的对象子表是尾插链表，插入序 == Qt 的迭代序，保序成立。
//   * Compact 输出用 cJSON_PrintUnformatted；它与 Qt 的 Compact 在分隔符和
//     非 ASCII 策略上不完全等价。本模块自读自写无害，若要与服务器对拍需单独验证。

#ifdef QT3_BUILD
// Qt3.5 无 CamelCase 转发头；且 QByteArray 还没有独立头文件，它定义在
// qcstring.h 里（QCString 的基类）。
#include <qstring.h>
#include <qstringlist.h>
#include <qcstring.h>
#else
#include <QString>
#include <QStringList>
#include <QByteArray>
#endif
#include "qstring_shim.h"   // Qt3 的 QString 无 fromUtf8 之外的 QStringLiteral

#include "cJSON.h"

namespace qjsonshim {

// 引用计数的 cJSON 树。root 为空视为空树（refs 仍为 1，析构时跳过 Delete）。
struct Tree {
    cJSON* root;
    int    refs;
    explicit Tree(cJSON* r) : root(r), refs(1) {}
};

Tree* retain(Tree* t);
void  release(Tree* t);

// 递归深拷贝；n 为 0 时返回 0。用于把别处的子树挂进本树。
cJSON* dupNode(cJSON* n);

} // namespace qjsonshim

class QJsonObject;
class QJsonArray;

class QJsonValue {
public:
    QJsonValue();                                     // Undefined
    QJsonValue(bool v);
    QJsonValue(double v);
    QJsonValue(int v);
    QJsonValue(const QString& v);
    QJsonValue(const char* v);                        // 防 const char*→bool 隐式
    QJsonValue(const QJsonObject& v);
    QJsonValue(const QJsonArray& v);
    QJsonValue(const QJsonValue& o);
    ~QJsonValue();
    QJsonValue& operator=(const QJsonValue& o);

    bool isUndefined() const { return m_node == 0; }
    bool isNull()      const;
    bool isBool()      const;
    bool isDouble()    const;
    bool isString()    const;
    bool isObject()    const;
    bool isArray()     const;

    // 非对应类型一律返回默认值（不跨类型强转，与 Qt 一致）
    bool    toBool(bool def = false) const;
    double  toDouble(double def = 0) const;
    int     toInt(int def = 0) const;
    QString toString(const QString& def = QString()) const;
    QJsonObject toObject() const;     // 非 object → 空 QJsonObject
    QJsonArray  toArray() const;      // 非 array  → 空 QJsonArray

    qjsonshim::Tree* tree() const { return m_tree; }
    cJSON* node() const { return m_node; }

private:
    friend class QJsonObject;
    friend class QJsonArray;
    friend class QJsonDocument;
    // 借用（retain 传入的树），不夺取所有权
    QJsonValue(qjsonshim::Tree* t, cJSON* n);
    // 独占：n 已是新造的节点，构造一棵孤儿树
    void adopt(cJSON* n);
    void reset();
    qjsonshim::Tree* m_tree;
    cJSON* m_node;
};

class QJsonObject {
public:
    class const_iterator {
    public:
        const_iterator() : m_tree(0), m_cur(0) {}
        QString key() const { return QString::fromUtf8(m_cur->string); }
        QJsonValue value() const { return QJsonValue(m_tree, m_cur); }
        // 与 Qt 原生 QJsonObject::const_iterator 的 operator* 语义一致（返回值）
        QJsonValue operator*() const { return value(); }
        const_iterator& operator++() { if (m_cur) m_cur = m_cur->next; return *this; }
        const_iterator operator++(int) { const_iterator t(*this); ++*this; return t; }
        bool operator==(const const_iterator& o) const { return m_cur == o.m_cur; }
        bool operator!=(const const_iterator& o) const { return m_cur != o.m_cur; }
    private:
        friend class QJsonObject;
        const_iterator(qjsonshim::Tree* t, cJSON* c) : m_tree(t), m_cur(c) {}
        qjsonshim::Tree* m_tree;   // 迭代期间保活（元素是借用节点）
        cJSON* m_cur;
    };

    QJsonObject();
    QJsonObject(const QJsonObject& o);
    ~QJsonObject();
    QJsonObject& operator=(const QJsonObject& o);

    bool isEmpty() const;
    int  size() const;
    bool contains(const QString& key) const;
    QJsonValue value(const QString& key) const;      // 缺失 → Undefined
    void insert(const QString& key, const QJsonValue& v);
    QStringList keys() const;
    const_iterator constBegin() const;
    const_iterator constEnd() const;

    // range-for 支持（Qt5/6 原生有）。当前消费方未用，但 QJsonArray 已补，
    // 此处一并补齐以免后续模块再踩同一个编译错误。
    const_iterator begin() const { return constBegin(); }
    const_iterator end() const { return constEnd(); }

    qjsonshim::Tree* tree() const { return m_tree; }
    cJSON* node() const { return m_node; }

private:
    friend class QJsonValue;
    friend class QJsonDocument;
    QJsonObject(qjsonshim::Tree* t, cJSON* n);
    void ensureTree();     // 孤儿对象首次 insert 时惰性建树
    void reset();
    qjsonshim::Tree* m_tree;
    cJSON* m_node;
};

class QJsonArray {
public:
    class const_iterator {
    public:
        const_iterator() : m_tree(0), m_cur(0) {}
        QJsonValue value() const { return QJsonValue(m_tree, m_cur); }
        // range-for（`for (const auto& v : arr)`）需要 operator*；Qt5/6 原生的
        // QJsonArray::const_iterator 都有。sitelistclient.cpp 的 parseQFace
        // 就靠它遍历 QFace 索引数组，缺了直接编不过。
        QJsonValue operator*() const { return QJsonValue(m_tree, m_cur); }
        const_iterator& operator++() { if (m_cur) m_cur = m_cur->next; return *this; }
        const_iterator operator++(int) { const_iterator t(*this); ++*this; return t; }
        bool operator==(const const_iterator& o) const { return m_cur == o.m_cur; }
        bool operator!=(const const_iterator& o) const { return m_cur != o.m_cur; }
    private:
        friend class QJsonArray;
        const_iterator(qjsonshim::Tree* t, cJSON* c) : m_tree(t), m_cur(c) {}
        qjsonshim::Tree* m_tree;
        cJSON* m_cur;
    };

    QJsonArray();
    QJsonArray(const QJsonArray& o);
    ~QJsonArray();
    QJsonArray& operator=(const QJsonArray& o);

    bool isEmpty() const;
    int  size() const;
    QJsonValue at(int i) const;              // 越界 → Undefined
    QJsonValue first() const { return at(0); }  // 等价 at(0)，空数组 → Undefined
    void append(const QJsonValue& v);
    void removeAt(int i);
    const_iterator constBegin() const;
    const_iterator constEnd() const;

    // range-for（`for (const auto& v : arr)`）需要 begin()/end()，Qt5/6 的
    // QJsonArray 原生就有。sitelistclient.cpp 的 parseQFace 用了
    // `for (const auto& v : doc.array())`，只给 constBegin/constEnd 编不过
    // （实测报 "'begin' was not declared in this scope"）。
    const_iterator begin() const { return constBegin(); }
    const_iterator end() const { return constEnd(); }

    qjsonshim::Tree* tree() const { return m_tree; }
    cJSON* node() const { return m_node; }

private:
    friend class QJsonValue;
    friend class QJsonDocument;
    QJsonArray(qjsonshim::Tree* t, cJSON* n);
    void ensureTree();
    void reset();
    qjsonshim::Tree* m_tree;
    cJSON* m_node;
};

class QJsonDocument {
public:
    enum Formatting { Indented, Compact };

    QJsonDocument();
    QJsonDocument(const QJsonObject& o);
    QJsonDocument(const QJsonArray& a);
    QJsonDocument(const QJsonDocument& o);
    ~QJsonDocument();
    QJsonDocument& operator=(const QJsonDocument& o);

    // 解析失败（或空输入）→ 空文档（isEmpty() 为真）。会剥掉 UTF-8 BOM
    // （Qt 原生版容忍 BOM，cJSON 1.7.19 不容忍）。
    static QJsonDocument fromJson(const QByteArray& json);

    bool isEmpty() const;
    bool isObject() const;
    bool isArray() const;
    QJsonObject object() const;     // 非 object → 空 QJsonObject
    QJsonArray  array() const;      // 非 array  → 空 QJsonArray
    QByteArray toJson(Formatting style = Indented) const;

    qjsonshim::Tree* tree() const { return m_tree; }
    cJSON* node() const { return m_node; }

private:
    void reset();
    qjsonshim::Tree* m_tree;
    cJSON* m_node;
};

#endif // QLSTIK_QJSON_SHIM_H
