#include "qjson_shim.h"
#include <string.h>   // strlen (Qt3 的 QByteArray 需显式长度)

// Qt3 的 QString 没有 toUtf8()（Qt4 改的名），它叫 utf8()。统一桥接。
#ifdef QT3_BUILD
#define SHIM_TO_UTF8(s) (s).utf8()
#else
#define SHIM_TO_UTF8(s) (s).toUtf8()
#endif

namespace qjsonshim {

Tree* retain(Tree* t)
{
    if (t) {
        ++t->refs;
    }
    return t;
}

void release(Tree* t)
{
    if (!t) {
        return;
    }
    if (--t->refs > 0) {
        return;
    }
    if (t->root) {
        cJSON_Delete(t->root);
    }
    delete t;
}

cJSON* dupNode(cJSON* n)
{
    return n ? cJSON_Duplicate(n, 1) : (cJSON*)0;
}

} // namespace qjsonshim

// ───────────────────────────── QJsonValue ─────────────────────────────

QJsonValue::QJsonValue() : m_tree(0), m_node(0) {}

QJsonValue::QJsonValue(qjsonshim::Tree* t, cJSON* n)
    : m_tree(qjsonshim::retain(t)), m_node(n) {}

void QJsonValue::adopt(cJSON* n)
{
    m_tree = n ? new qjsonshim::Tree(n) : 0;
    m_node = n;
}

void QJsonValue::reset()
{
    qjsonshim::release(m_tree);
    m_tree = 0;
    m_node = 0;
}

QJsonValue::QJsonValue(bool v)            : m_tree(0), m_node(0) { adopt(cJSON_CreateBool(v ? 1 : 0)); }
QJsonValue::QJsonValue(double v)          : m_tree(0), m_node(0) { adopt(cJSON_CreateNumber(v)); }
QJsonValue::QJsonValue(int v)             : m_tree(0), m_node(0) { adopt(cJSON_CreateNumber((double)v)); }
QJsonValue::QJsonValue(const QJsonValue& o) : m_tree(qjsonshim::retain(o.m_tree)), m_node(o.m_node) {}
QJsonValue::~QJsonValue() { reset(); }

QJsonValue& QJsonValue::operator=(const QJsonValue& o)
{
    if (this != &o) {
        qjsonshim::Tree* t = qjsonshim::retain(o.m_tree);
        reset();
        m_tree = t;
        m_node = o.m_node;
    }
    return *this;
}

// cJSON_String 在树里只存 UTF-8 裸串，valuestring 不保证 NUL 之外的信息；
// 按 strlen 取全（cJSON 打印时同样按 NUL 终止）。
QJsonValue::QJsonValue(const QString& v) : m_tree(0), m_node(0)
{
    const char* utf8 = SHIM_TO_UTF8(v).data();
    adopt(cJSON_CreateString(utf8));
}

QJsonValue::QJsonValue(const char* v) : m_tree(0), m_node(0)
{
    // 没有它，QJsonValue("…") 会把 const char* 隐式转成 bool（true）。
    // Qt 原生也有该构造（Qt5.2+ 为 QLatin1StringView）。空指针按 Qt 语义 → null。
    adopt(v ? cJSON_CreateString(v) : cJSON_CreateNull());
}

QJsonValue::QJsonValue(const QJsonObject& v) : m_tree(0), m_node(0)
{
    // 拷贝对象的子树，让本值拥有独立的一份（对齐 Qt 值类型语义）
    adopt(qjsonshim::dupNode(v.node()));
}

QJsonValue::QJsonValue(const QJsonArray& v) : m_tree(0), m_node(0)
{
    adopt(qjsonshim::dupNode(v.node()));
}

bool QJsonValue::isNull()   const { return m_node && cJSON_IsNull(m_node); }
bool QJsonValue::isBool()   const { return m_node && cJSON_IsBool(m_node); }
bool QJsonValue::isDouble() const { return m_node && cJSON_IsNumber(m_node); }
bool QJsonValue::isString() const { return m_node && cJSON_IsString(m_node); }
bool QJsonValue::isObject() const { return m_node && cJSON_IsObject(m_node); }
bool QJsonValue::isArray()  const { return m_node && cJSON_IsArray(m_node); }

bool QJsonValue::toBool(bool def) const
{
    return isBool() ? m_node->valueint != 0 : def;
}

double QJsonValue::toDouble(double def) const
{
    // 读 valuedouble：cJSON 1.7.19 起 valueint 已废弃，number 的真值在 valuedouble
    return isDouble() ? m_node->valuedouble : def;
}

int QJsonValue::toInt(int def) const
{
    return isDouble() ? (int)m_node->valuedouble : def;
}

QString QJsonValue::toString(const QString& def) const
{
    if (!isString() || !m_node->valuestring) {
        return def;
    }
    return QString::fromUtf8(m_node->valuestring);
}

QJsonObject QJsonValue::toObject() const
{
    return isObject() ? QJsonObject(m_tree, m_node) : QJsonObject();
}

QJsonArray QJsonValue::toArray() const
{
    return isArray() ? QJsonArray(m_tree, m_node) : QJsonArray();
}

// ───────────────────────────── QJsonObject ────────────────────────────

QJsonObject::QJsonObject() : m_tree(0), m_node(0) {}

QJsonObject::QJsonObject(qjsonshim::Tree* t, cJSON* n)
    : m_tree(qjsonshim::retain(t)), m_node(n) {}

QJsonObject::QJsonObject(const QJsonObject& o)
    : m_tree(qjsonshim::retain(o.m_tree)), m_node(o.m_node) {}

QJsonObject::~QJsonObject() { reset(); }

QJsonObject& QJsonObject::operator=(const QJsonObject& o)
{
    if (this != &o) {
        qjsonshim::Tree* t = qjsonshim::retain(o.m_tree);
        reset();
        m_tree = t;
        m_node = o.m_node;
    }
    return *this;
}

void QJsonObject::reset()
{
    qjsonshim::release(m_tree);
    m_tree = 0;
    m_node = 0;
}

void QJsonObject::ensureTree()
{
    if (m_node && m_tree) {
        return;                 // 已有归属树
    }
    // 孤儿：造一棵新的 object 树。若本对象是"借用"来的（无自有树），
    // 借来的节点仍被原树持有，不在此动它 —— 直接另起一棵树。
    cJSON* o = cJSON_CreateObject();
    m_tree = new qjsonshim::Tree(o);
    m_node = o;
}

bool QJsonObject::isEmpty() const { return m_node == 0 || m_node->child == 0; }
int  QJsonObject::size() const    { return isEmpty() ? 0 : cJSON_GetArraySize(m_node); }

bool QJsonObject::contains(const QString& key) const
{
    if (!m_node) {
        return false;
    }
    return cJSON_GetObjectItemCaseSensitive(m_node, SHIM_TO_UTF8(key).data()) != 0;
}

QJsonValue QJsonObject::value(const QString& key) const
{
    if (!m_node) {
        return QJsonValue();    // Undefined
    }
    cJSON* n = cJSON_GetObjectItemCaseSensitive(m_node, SHIM_TO_UTF8(key).data());
    return QJsonValue(m_tree, n);   // n 为 0 → Undefined
}

void QJsonObject::insert(const QString& key, const QJsonValue& v)
{
    ensureTree();
    // Qt 的 insert 是 upsert 语义。cJSON 的 ReplaceItemInObjectCaseSensitive 在
    // 键不存在时静默失败（返回 false 且不加挂）；AddItemToObject 在键已存在时
    // 仅追加（产生重复键，cJSON_GetObjectItem* 取第一个，值就被遮蔽）。
    // 于是自己二选一：
    //   * 键存在 → cJSON_ReplaceItemViaPointer 直接交换指针（覆盖旧值，旧值
    //     归旧树，不 Delete，避免树外删除）
    //   * 键不存在 → AddItemToObject 追加
    cJSON* dup = v.node() ? qjsonshim::dupNode(v.node()) : cJSON_CreateNull();
    if (!dup) {
        return;
    }
    const char* k = SHIM_TO_UTF8(key).data();
    cJSON* old = cJSON_GetObjectItemCaseSensitive(m_node, k);
    if (old) {
        // ReplaceItemInObjectCaseSensitive 会替 replacement 重设 string=key，
        // detach 旧节点并 Delete。旧节点已在链上，删除安全；replacement 由
        // dupNode 深拷而来，独立无主。
        cJSON_ReplaceItemInObjectCaseSensitive(m_node, k, dup);
    } else {
        cJSON_AddItemToObject(m_node, k, dup);
    }
}

QStringList QJsonObject::keys() const
{
    QStringList out;
    if (!m_node) {
        return out;
    }
    for (cJSON* c = m_node->child; c; c = c->next) {
        if (c->string) {
            out.append(QString::fromUtf8(c->string));
        }
    }
    return out;
}

QJsonObject::const_iterator QJsonObject::constBegin() const
{
    return const_iterator(m_tree, m_node ? m_node->child : 0);
}

QJsonObject::const_iterator QJsonObject::constEnd() const
{
    return const_iterator(m_tree, 0);
}

// ───────────────────────────── QJsonArray ─────────────────────────────

QJsonArray::QJsonArray() : m_tree(0), m_node(0) {}

QJsonArray::QJsonArray(qjsonshim::Tree* t, cJSON* n)
    : m_tree(qjsonshim::retain(t)), m_node(n) {}

QJsonArray::QJsonArray(const QJsonArray& o)
    : m_tree(qjsonshim::retain(o.m_tree)), m_node(o.m_node) {}

QJsonArray::~QJsonArray() { reset(); }

QJsonArray& QJsonArray::operator=(const QJsonArray& o)
{
    if (this != &o) {
        qjsonshim::Tree* t = qjsonshim::retain(o.m_tree);
        reset();
        m_tree = t;
        m_node = o.m_node;
    }
    return *this;
}

void QJsonArray::reset()
{
    qjsonshim::release(m_tree);
    m_tree = 0;
    m_node = 0;
}

void QJsonArray::ensureTree()
{
    if (m_node && m_tree) {
        return;
    }
    cJSON* a = cJSON_CreateArray();
    m_tree = new qjsonshim::Tree(a);
    m_node = a;
}

bool QJsonArray::isEmpty() const { return m_node == 0 || m_node->child == 0; }
int  QJsonArray::size() const    { return isEmpty() ? 0 : cJSON_GetArraySize(m_node); }

QJsonValue QJsonArray::at(int i) const
{
    if (!m_node || i < 0) {
        return QJsonValue();
    }
    return QJsonValue(m_tree, cJSON_GetArrayItem(m_node, i));   // 越界 → Undefined
}

void QJsonArray::append(const QJsonValue& v)
{
    ensureTree();
    cJSON* dup = v.node() ? qjsonshim::dupNode(v.node()) : cJSON_CreateNull();
    if (dup) {
        cJSON_AddItemToArray(m_node, dup);
    }
}

void QJsonArray::removeAt(int i)
{
    if (m_node && i >= 0) {
        cJSON_DeleteItemFromArray(m_node, i);
    }
}

QJsonArray::const_iterator QJsonArray::constBegin() const
{
    return const_iterator(m_tree, m_node ? m_node->child : 0);
}

QJsonArray::const_iterator QJsonArray::constEnd() const
{
    return const_iterator(m_tree, 0);
}

// ──────────────────────────── QJsonDocument ───────────────────────────

QJsonDocument::QJsonDocument() : m_tree(0), m_node(0) {}

QJsonDocument::QJsonDocument(const QJsonObject& o)
    : m_tree(qjsonshim::retain(o.tree())), m_node(o.node()) {}

QJsonDocument::QJsonDocument(const QJsonArray& a)
    : m_tree(qjsonshim::retain(a.tree())), m_node(a.node()) {}

QJsonDocument::QJsonDocument(const QJsonDocument& o)
    : m_tree(qjsonshim::retain(o.m_tree)), m_node(o.m_node) {}

QJsonDocument::~QJsonDocument() { reset(); }

QJsonDocument& QJsonDocument::operator=(const QJsonDocument& o)
{
    if (this != &o) {
        qjsonshim::Tree* t = qjsonshim::retain(o.m_tree);
        reset();
        m_tree = t;
        m_node = o.m_node;
    }
    return *this;
}

void QJsonDocument::reset()
{
    qjsonshim::release(m_tree);
    m_tree = 0;
    m_node = 0;
}

QJsonDocument QJsonDocument::fromJson(const QByteArray& json)
{
    QJsonDocument doc;
    const char* p = json.data();
    uint n = json.size();
    if (!p || n == 0) {
        return doc;              // 空输入 → 空文档
    }
    // Qt 原生 fromJson 容忍 UTF-8 BOM，cJSON 1.7.19 不容忍，先剥 3 字节
    if (n >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB
        && (unsigned char)p[2] == 0xBF) {
        p += 3;
        n -= 3;
    }
    if (n == 0) {
        return doc;
    }
    cJSON* root = cJSON_ParseWithLength(p, (size_t)n);
    if (!root) {
        return doc;              // 解析失败 → 空文档
    }
    doc.m_tree = new qjsonshim::Tree(root);
    doc.m_node = root;
    return doc;
}

bool QJsonDocument::isEmpty()  const { return m_node == 0; }
bool QJsonDocument::isObject() const { return m_node && cJSON_IsObject(m_node); }
bool QJsonDocument::isArray()  const { return m_node && cJSON_IsArray(m_node); }

QJsonObject QJsonDocument::object() const
{
    return isObject() ? QJsonObject(m_tree, m_node) : QJsonObject();
}

QJsonArray QJsonDocument::array() const
{
    return isArray() ? QJsonArray(m_tree, m_node) : QJsonArray();
}

QByteArray QJsonDocument::toJson(Formatting style) const
{
    if (!m_node) {
        return QByteArray();
    }
    char* s = (style == Compact) ? cJSON_PrintUnformatted(m_node) : cJSON_Print(m_node);
    if (!s) {
        return QByteArray();
    }
    // cJSON_Print* 返回 cJSON 自己的 malloc 缓冲，拷进 QByteArray 后必须
    // 交回给 cJSON 的 free_fn（cJSON_free），不能直接 delete。
#ifdef QT3_BUILD
    // Qt3 的 QByteArray 是 QMemArray<char>，无 (const char*) 构造，用 QCString
    //（QByteArray 子类）承载；返回时隐式转回基类。
    const QCString out(s);
#else
    const QByteArray out(s);
#endif
    cJSON_free(s);
    return out;
}
