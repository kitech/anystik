// stikcommon/test_qcontainers_shim.cpp —— 容器垫片契约（Qt3 单端）
//
// 被测对象（Qt 3.5 缺、本仓自建）：
//   qvector_shim.h —— QVector  ⊂ QValueVector（补 removeAll/contains/operator<<）
//   qset_shim.h    —— QSet     ⊂ QMap<Key,Key>（补一参 insert）
//   qhash_shim.h   —— QHash    ⊂ QMap<Key,T>（补 operator[]/constFind/constEnd）
//   qqueue_shim.h  —— QQueue   （std::deque 封装）
//
// 为什么值得测：这些垫片都是**基于另一种容器**拼出来的，语义差异全藏在
//   「补的方法」里，而产品点只用到其中几个方法、不看顺序：
//     · QVector::removeAll —— 头注释(qvector_shim.h:51-54)自陈「Qt3 的
//       QValueVector 无 removeAll，只能倒序 erase，正序会漏删」。这正是典型
//       的「写对了但极易被改回正序」的点，必须用重复元素钉住。
//     · QHash::operator[] —— 头注释(qhash_shim.h:20-22)自陈 Qt3 的
//       QMap::insert 返回 iterator 而非 T&，故不能直接 return insert(...)。
//       这是本仓最容易「顺手简化」成编译不过或返回错东西的地方。
//     · QSet 允许「有序但去重」，产品明确不依赖顺序（qset_shim.h:9-11）。
//
// ⚠ Qt3 陷阱：QString 无 size()（用 length()）；QByteArray 无 const char*
//   构造。本文件只用 int/QString 作元素，避开该坑。

#include <qstring.h>

#include "doctest/doctest.h"

#include "qvector_shim.h"
#include "qset_shim.h"
#include "qhash_shim.h"
#include "qqueue_shim.h"

// ═══════════════════════════════════════════════════════════════════════════
// QVector
// ═══════════════════════════════════════════════════════════════════════════

// ⚠⚠ 核心回归点：removeAll 必须删掉**所有**匹配项。头注释自陈实现是倒序
//   erase，若被改回正序，删一个后后续元素前移会漏删。用「多个 2 且被别的
//   值隔开」的布局，任何漏删都会让 size/内容对不上。
TEST_CASE("QVector: removeAll 删除全部匹配项（正序实现会漏删）")
{
    QVector<int> v;
    v << 1 << 2 << 2 << 3 << 2;          // 三个 2，中间夹 3
    CHECK_EQ(v.size(), 5);
    v.removeAll(2);
    CHECK_EQ(v.size(), 2);              // 只剩 1 和 3
    CHECK_EQ(v.at(0), 1);
    CHECK_EQ(v.at(1), 3);
}

TEST_CASE("QVector: removeAll 命中多个相同值（全部连续）")
{
    QVector<int> v;
    v << 7 << 7 << 7;
    v.removeAll(7);
    CHECK_EQ(v.size(), 0);
}

TEST_CASE("QVector: removeAll 不存在元素时不改动")
{
    QVector<int> v;
    v << 1 << 2 << 3;
    v.removeAll(99);
    CHECK_EQ(v.size(), 3);
    CHECK_EQ(v.at(0), 1);
    CHECK_EQ(v.at(2), 3);
}

TEST_CASE("QVector: contains 按值判断")
{
    QVector<int> v;
    v << 10 << 20 << 30;
    CHECK(v.contains(20));
    CHECK(v.contains(10));
    CHECK(!v.contains(40));
    CHECK(!(QVector<int>()).contains(0));   // 空容器
}

// ⚠ operator<< 必须返回 QVector& 才能链式；返回 void 会让 `v << a << b` 编不过。
TEST_CASE("QVector: operator<< 链式追加且保持顺序")
{
    QVector<int> v;
    QVector<int>& ref = (v << 1 << 2 << 3);
    CHECK(&ref == &v);                  // 返回的是自身引用（可链式）
    CHECK_EQ(v.size(), 3);
    CHECK_EQ(v.at(0), 1);
    CHECK_EQ(v.at(1), 2);
    CHECK_EQ(v.at(2), 3);
}

// ⚠ 用 QString 作元素（产品里 QVector<StickerPackBrief> 等是类类型），确认不是
//   只能用于 POD。同时顺带覆盖 QValueVector<T> 基类的 append/at。
TEST_CASE("QVector: 非 POD 元素（QString）的 removeAll/contains")
{
    QVector<QString> v;
    v << QString("a") << QString("b") << QString("a");
    CHECK(v.contains(QString("a")));
    v.removeAll(QString("a"));
    CHECK_EQ(v.size(), 1);
    CHECK_EQ(v.at(0), QString("b"));
}

// ═══════════════════════════════════════════════════════════════════════════
// QSet
// ═══════════════════════════════════════════════════════════════════════════

// ⚠ 去重是集合的根本语义：重复 insert 不得让 size 增长
//   （davbisync 的 m_scannedCloudDirs 靠它做「列过了吗」过滤）。
TEST_CASE("QSet: insert 去重，size 不因重复增长")
{
    QSet<QString> s;
    CHECK(s.isEmpty());
    s.insert(QString("a"));
    s.insert(QString("b"));
    s.insert(QString("a"));              // 重复
    CHECK_EQ(s.size(), 2);
    CHECK(s.contains(QString("a")));
    CHECK(s.contains(QString("b")));
    CHECK(!s.contains(QString("c")));
}

// ⚠ insert 按 Qt4 语义返回 iterator（qset_shim.h:23-27），必须能判 end()。
TEST_CASE("QSet: insert 返回指向该元素的 iterator")
{
    QSet<QString> s;
    QSet<QString>::iterator it = s.insert(QString("k"));
    CHECK(it != s.end());
    CHECK(s.contains(QString("k")));
}

TEST_CASE("QSet: clear 清空并 isEmpty")
{
    QSet<QString> s;
    s.insert(QString("x"));
    CHECK(!s.isEmpty());
    s.clear();
    CHECK(s.isEmpty());
    CHECK_EQ(s.size(), 0);
}

// ⚠ 空集合上的 contains 必须安全返回 false（不能因为 find 到 end 就崩）。
TEST_CASE("QSet: 空集合 contains 返回 false")
{
    QSet<QString> s;
    CHECK(!s.contains(QString("anything")));
}

// ═══════════════════════════════════════════════════════════════════════════
// QHash
// ═══════════════════════════════════════════════════════════════════════════

// ⚠⚠ 核心回归点：operator[] 在 key 缺失时要**插入默认值并返回可写引用**。
//   头注释(qhash_shim.h:20-22)点明 Qt3 的 QMap::insert 返回 iterator 而非 T&，
//   若谁把它「简化」成 `return this->insert(...)`，轻则编不过，重则返回错类型。
//   本用例同时验证「首次写入」、「覆盖写」、「读缺失即插入」三种形态。
TEST_CASE("QHash: operator[] 缺失即插入默认可写引用")
{
    QHash<QString, int> h;
    h[QString("a")] = 1;                 // 缺失 → 插入后赋 1
    CHECK_EQ(h.size(), 1);
    CHECK_EQ(h[QString("a")], 1);

    h[QString("a")] = 2;                 // 已存在 → 覆盖，不新增
    CHECK_EQ(h[QString("a")], 2);
    CHECK_EQ(h.size(), 1);

    const int v = h[QString("b")];       // 读取缺失 → 插入默认(int 0)
    CHECK_EQ(v, 0);
    CHECK_EQ(h.size(), 2);
    CHECK(h.contains(QString("b")));
}

// ⚠ 返回的是**引用**：连续两次赋值必须落在同一元素上（若误返回副本，第二次
//   改动会丢失，而单次赋值看不出）。
TEST_CASE("QHash: operator[] 返回引用而非临时副本")
{
    QHash<QString, int> h;
    h[QString("k")] = 5;
    int& r = h[QString("k")];
    r = 9;                               // 通过引用改
    CHECK_EQ(h[QString("k")], 9);        // 容器内确实变了
}

// ⚠ constFind/constEnd 是 Qt4.1 才有的名字；Qt3 只有 const 版 find()/end()。
//   语义必须完全一致：找不到返回 constEnd、不解引用。
TEST_CASE("QHash: constFind/constEnd 与 Qt4+ 同语义")
{
    QHash<QString, int> h;
    h[QString("k")] = 7;

    CHECK(h.constFind(QString("k")) != h.constEnd());
    CHECK_EQ(*(h.constFind(QString("k"))), 7);       // 解引用给出值
    CHECK(h.constFind(QString("absent")) == h.constEnd());   // 未命中
}

TEST_CASE("QHash: contains / size / isEmpty / clear 基本契约")
{
    QHash<QString, int> h;
    CHECK(h.isEmpty());
    h[QString("a")] = 1;
    h[QString("b")] = 2;
    CHECK_EQ(h.size(), 2);
    CHECK(h.contains(QString("a")));
    CHECK(!h.contains(QString("z")));
    h.clear();
    CHECK(h.isEmpty());
    CHECK_EQ(h.size(), 0);
}

// ⚠ 值类型是非平凡类型（QString）时，operator[] 的默认构造 T() 必须是空串
//   而非未初始化内存。这条防「用 malloc 式默认值」的实现。
TEST_CASE("QHash: 值为 QString 时默认插入空串")
{
    QHash<QString, QString> h;
    h[QString("k")];                     // 仅读取缺失 key
    CHECK_EQ(h.size(), 1);
    CHECK_EQ(h[QString("k")].length(), 0);
    CHECK(h[QString("k")].isEmpty());
}

// ═══════════════════════════════════════════════════════════════════════════
// QQueue
// ═══════════════════════════════════════════════════════════════════════════

// ⚠ FIFO 是队列的根本语义，dequeue 必须取**最先**入队的（若误用 back() 会变成
//   栈，顺序反了但类型正确、编得过）。
TEST_CASE("QQueue: enqueue/dequeue 严格 FIFO")
{
    QQueue<int> q;
    CHECK(q.isEmpty());
    q.enqueue(1);
    q.enqueue(2);
    q.enqueue(3);
    CHECK_EQ(q.size(), 3);
    CHECK_EQ(q.head(), 1);               // head 是最早入队的
    CHECK_EQ(q.dequeue(), 1);            // 出队顺序 1,2,3
    CHECK_EQ(q.dequeue(), 2);
    CHECK_EQ(q.size(), 1);
    CHECK_EQ(q.dequeue(), 3);
    CHECK(q.isEmpty());
}

// ⚠ at(i) 从队首（最早入队）算起，removeAt(i) 删的是同一坐标系 —— 二者必须
//   一致，否则产品里「按索引改队列」会改错元素。
TEST_CASE("QQueue: at 与 removeAt 用同一坐标系（队首为 0）")
{
    QQueue<int> q;
    q.enqueue(10);
    q.enqueue(20);
    q.enqueue(30);
    CHECK_EQ(q.at(0), 10);
    CHECK_EQ(q.at(1), 20);
    CHECK_EQ(q.at(2), 30);
    q.removeAt(1);                       // 删中间的 20
    CHECK_EQ(q.size(), 2);
    CHECK_EQ(q.at(0), 10);
    CHECK_EQ(q.at(1), 30);               // 30 前移到下标 1
    CHECK_EQ(q.head(), 10);
}

TEST_CASE("QQueue: clear 后 isEmpty")
{
    QQueue<int> q;
    q.enqueue(1);
    q.clear();
    CHECK(q.isEmpty());
    CHECK_EQ(q.size(), 0);
}

// ⚠ 非 POD 元素（产品里队列常装 QString/信号结构）也要正确构造/析构，
//   不得出现浅拷贝导致的重复释放或内容丢失。
TEST_CASE("QQueue: QString 元素的入队出队保真")
{
    QQueue<QString> q;
    q.enqueue(QString::fromUtf8("中"));
    q.enqueue(QString("ascii"));
    CHECK_EQ(q.dequeue(), QString::fromUtf8("中"));
    CHECK_EQ(q.dequeue(), QString("ascii"));
    CHECK(q.isEmpty());
}
