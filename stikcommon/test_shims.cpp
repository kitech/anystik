// stikcommon/test_shims.cpp —— 容器垫片契约（Qt3 单端，零网络）
//
// 被测对象：
//   qlist_shim.h —— Qt3 的 QList 只是 #define QList QPtrList（只能存指针），
//                   垫片 #undef 后补一个派生自 QValueList<T> 的同名模板，
//                   补齐 Qt4+ 值容器语义。
//   qmap_shim.h  —— Qt3 的 QMap 缺 Qt4+ 的 value()/constFind()/cbegin()/cend()
//                   与「erase(iterator) 返回下一个」的 Qt5 语义。
//
// ⚠ 本文件只管**容器**垫片。其它 shim 各有专属测试文件（一个 shim 一个
//   test_<name>_shim.cpp），别再往这里堆 —— 2026-10 曾把 qBuffer/
//   QVariantMap/qString/qba 四组塞进来，导致这个文件的 include 顺序成了
//   隐式雷区（qmap_shim.h 末尾的 QMap 宏会改写后续所有 QMap token）。
//   现已拆为：
//     test_qglobaltype_shim.cpp  qBufferMake/qBufferTake
//     test_qvariant_shim.cpp     qVariantMapValue
//     test_qstring_shim.cpp      qStringSplitSkipEmpty/qStringRefAt/…
//     test_qba_shim.cpp          qbaToLongLong
//     test_qdir_shim.cpp         qDirEntryInfoList/qDirEntryList/…
//
// 两个容器头都是纯模板，**无需链接任何产品 .cpp**。
//
// ⚠ include 顺序：qmap_shim.h 末尾是**对象式**宏
//   `#define QMap qlstik_qt3::QMapShim`，会重写本 TU 里所有 `QMap` token
//   （连 `::QMap` 也会被重写），故必须放在 doctest 与其它 Qt 头之后。
// ⚠ 本文件不测 `::QMap`（Qt3 原生 QMap）：如上，宏定义后该写法不可用。
// ⚠ qvariant_shim.h / qstring_shim.h 的 include 顺序同理受此约束。

#include <qstring.h>
#include <qvaluelist.h>
#include <qbuffer.h>

#include "doctest/doctest.h"

#include "qglobaltype_shim.h"
#include "qba_shim.h"
// ⚠ 必须在 qmap_shim.h 之前：那个头末尾的 `#define QMap` 会改写本 TU 后续
//   所有 QMap token。qvariant_shim.h 的 Qt3 分支要写原生 QMap 的
//   ConstIterator，所以要抢在宏生效前解析（与生产侧一致 —— stickerstore.cpp
//   根本不引 qmap_shim.h）。
#include "qvariant_shim.h"
#include "qstring_shim.h"
#include "qlist_shim.h"
#include "qmap_shim.h"

// ── QList 垫片 ───────────────────────────────────────────────────────

TEST_CASE("QList 垫片: 本体能实例化值类型（Qt3 原生 QList 只存指针，装不下）")
{
    // 光是 QList<QString> 能编译，就已证明用的是垫片而非 QPtrList 宏：
    // QPtrList<QString> 只能存 QString*。
    QList<QString> l;
    l.append(QString("b"));
    l.append(QString("a"));
    l.append(QString("c"));
    CHECK_EQ(l.count(), 3);
    // 顺序按插入顺序，不自动排序
    CHECK(l.at(0) == QString("b"));
    CHECK(l.at(1) == QString("a"));
    CHECK(l.at(2) == QString("c"));
    // 非 QString 的值类型同样可用
    QList<int> n;
    n.append(1);
    n.append(2);
    CHECK_EQ(n.count(), 2);
    CHECK_EQ(n.at(1), 2);
}

TEST_CASE("QList 垫片: at() 返回可写引用（qwebdavdirparser 按引用改写 getList 结果）")
{
    QList<QString> l;
    l.append(QString("x"));
    l.append(QString("y"));
    // 这是垫片重写 at() 的唯一理由：Qt3 QValueList::at() 返回 const_iterator
    // 而不是 const T&，qwebdavdirparser 需要按引用改写元素。
    l.at(0) = QString("modified");
    CHECK(l.at(0) == QString("modified"));
    CHECK(l.at(1) == QString("y"));

    // 整型同样可写
    QList<int> n;
    n.append(10);
    n.at(0) = 99;
    CHECK_EQ(n.at(0), 99);

    // const 容器上的 at() 返回 const 引用：只读
    QList<QString> src;
    src.append(QString("ro"));
    const QList<QString>& cl = src;
    CHECK(cl.at(0) == QString("ro"));
}

TEST_CASE("QList 垫片: append 是值语义（不能退化成往指针数组塞临时对象）")
{
    QList<QString> l;
    l.append(QString("tmp"));          // 临时对象
    CHECK_EQ(l.count(), 1);
    CHECK(l.at(0) == QString("tmp"));  // 若走指针版，这里会是悬垂地址
    // 改元素不影响原变量，证明是拷贝而非存地址
    QString src = QString("orig");
    l.append(src);
    l.at(1) = QString("changed");
    CHECK(src == QString("orig"));
}

TEST_CASE("QList 垫片: sort / first / last / constFirst / constLast")
{
    QList<QString> l;
    l.append(QString("b"));
    l.append(QString("a"));
    l.append(QString("c"));
    l.sort();
    CHECK(l.at(0) == QString("a"));
    CHECK(l.at(1) == QString("b"));
    CHECK(l.at(2) == QString("c"));
    CHECK(l.first() == QString("a"));
    CHECK(l.last() == QString("c"));
    const QList<QString> cl = l;
    CHECK(cl.constFirst() == QString("a"));
    CHECK(cl.constLast() == QString("c"));
    // 少于 2 个元素时 sort 不得崩（垫片里 n < 2 直接 return）
    QList<QString> one;
    one.append(QString("only"));
    one.sort();
    CHECK_EQ(one.count(), 1);
    QList<QString> none;
    none.sort();
    CHECK_EQ(none.count(), 0);
}

TEST_CASE("QList 垫片: takeFirst/takeLast 返回被移除元素，removeFirst/removeLast 只删")
{
    QList<QString> l;
    l.append(QString("a"));
    l.append(QString("b"));
    l.append(QString("c"));

    CHECK(l.takeFirst() == QString("a"));
    CHECK_EQ(l.count(), 2);
    CHECK(l.first() == QString("b"));

    CHECK(l.takeLast() == QString("c"));
    CHECK_EQ(l.count(), 1);

    l.append(QString("d"));
    l.removeFirst();
    CHECK_EQ(l.count(), 1);
    CHECK(l.first() == QString("d"));
    l.removeLast();
    CHECK_EQ(l.count(), 0);

    // 自由函数版
    QList<QString> l2;
    l2.append(QString("x"));
    l2.append(QString("y"));
    CHECK(qListTakeFirst(l2) == QString("x"));
    CHECK_EQ(l2.count(), 1);
}

TEST_CASE("QList 垫片: removeOne 只删首个匹配项（Qt3 基类 remove() 删全部）")
{
    QList<QString> l;
    l.append(QString("a"));
    l.append(QString("dup"));
    l.append(QString("dup"));
    l.append(QString("b"));
    CHECK(l.contains(QString("dup")));
    CHECK(l.removeOne(QString("dup")));
    // 关键差异：还有一个 dup 留着
    CHECK_EQ(l.count(), 3);
    CHECK(l.contains(QString("dup")));
    // 删掉最后一个
    CHECK(l.removeOne(QString("dup")));
    CHECK(!l.contains(QString("dup")));
    // 删不存在的返回 false
    CHECK(l.removeOne(QString("nope")) == false);
    CHECK_EQ(l.count(), 2);
}

TEST_CASE("QList 垫片: contains / cbegin-cend / 基类面未被遮蔽")
{
    QList<int> l;
    l.append(2);
    l.append(1);
    CHECK(l.contains(2));
    CHECK(l.contains(1));
    CHECK(l.contains(9) == false);

    // cbegin/cend 遍历
    int sum = 0;
    for (QList<int>::const_iterator it = l.cbegin(); it != l.cend(); ++it) {
        sum += *it;
    }
    CHECK_EQ(sum, 3);

    // 基类 QValueList 的面仍可用（垫片只做补齐，不改语义）
    l.prepend(0);
    CHECK_EQ(l.count(), 3);
    CHECK_EQ(l.first(), 0);
    CHECK(l.isEmpty() == false);
    l.push_back(9);
    CHECK_EQ(l.last(), 9);
    l.pop_back();
    CHECK_EQ(l.count(), 3);
    CHECK_EQ(l.size(), l.count());
}

// ── qListReserve：必须 no-op，绝不能 resize ──────────────────────────────
// 回归点：Qt3 的 QValueList/QPtrList 无 capacity 概念，也没有 Qt4 的
// reserve()。早期有人想用 resize(n) 顶替，那会把 size 真的变成 n，后续
// append 从 n 起追加 —— 贴纸缩略图列表会带一串前导空元素。size 不变是
// 唯一可观察契约，必须锁死。
TEST_CASE("qListReserve: Qt3 侧是 no-op，size 与内容都不变")
{
    QList<QString> l;
    l.append(QString("a"));
    l.append(QString("b"));
    qListReserve(l, 100);
    CHECK_EQ(l.count(), 2);
    CHECK(l[0] == QString("a"));
    CHECK(l[1] == QString("b"));
    // reserve 后 append 仍接在尾部，不产生前导空元素
    l.append(QString("c"));
    CHECK_EQ(l.count(), 3);
    CHECK(l[2] == QString("c"));
    // 空容器上 reserve 也不能凭空造出 n 个元素
    QList<int> e;
    qListReserve(e, 5);
    CHECK_EQ(e.count(), 0);
}

// ── QMap 垫片 ────────────────────────────────────────────────────────

TEST_CASE("QMap 垫片: 迭代器有 value()（Qt3 只有 data()）与 key/first/second/*")
{
    QMap<QString, int> m;
    m[QString("x")] = 1;
    m[QString("y")] = 2;

    // QMap 按键有序遍历
    QString seen;
    for (QMap<QString, int>::iterator it = m.begin(); it != m.end(); ++it) {
        seen += it.key();
        CHECK_EQ(it.value(), it.data());   // value() 与 data() 等价
        CHECK_EQ(it.key(), it.first());    // first() 兼容 std::pair 写法
        CHECK_EQ(it.value(), it.second());
        CHECK_EQ(it.value(), *it);
    }
    CHECK(seen == QString("xy"));          // 有序：x 在 y 前

    // 可写迭代器能改值
    for (QMap<QString, int>::iterator it = m.begin(); it != m.end(); ++it) {
        if (it.key() == QString("x")) {
            it.value() = 100;
        }
    }
    CHECK_EQ(m.value(QString("x")), 100);
}

TEST_CASE("QMap 垫片: const 迭代器 + cbegin/cend + constFind")
{
    QMap<QString, int> m;
    m[QString("x")] = 1;
    m[QString("y")] = 2;
    const QMap<QString, int>& cm = m;

    int sum = 0;
    for (QMap<QString, int>::const_iterator it = cm.cbegin(); it != cm.cend(); ++it) {
        sum += it.value();
    }
    CHECK_EQ(sum, 3);

    // constFind 命中/落空
    QMap<QString, int>::const_iterator hit = cm.constFind(QString("y"));
    CHECK(hit != cm.cend());
    CHECK_EQ(hit.value(), 2);
    CHECK(cm.constFind(QString("zz")) == cm.cend());
}

TEST_CASE("QMap 垫片: value(key) 缺失返回 V()，value(key, def) 返回默认值")
{
    QMap<QString, int> m;
    m[QString("x")] = 1;
    const QMap<QString, int>& cm = m;
    CHECK_EQ(cm.value(QString("x")), 1);
    CHECK_EQ(cm.value(QString("zz")), 0);        // V() 即 int 的零值
    CHECK_EQ(cm.value(QString("zz"), -1), -1);   // 带默认值
    CHECK_EQ(cm.value(QString("x"), -1), 1);    // 命中时默认值被忽略
    // QString 的 V() 是空串，不是 "null"
    QMap<QString, QString> ms;
    ms[QString("k")] = QString("v");
    CHECK(ms.value(QString("nope")).isEmpty());
    CHECK(ms.value(QString("nope"), QString("dflt")) == QString("dflt"));
}

TEST_CASE("QMap 垫片: erase(iterator) 返回下一个（Qt5 语义，Qt3 基类返回 void）")
{
    QMap<int, int> m;
    m[1] = 1;
    m[2] = 2;
    m[3] = 3;

    // it = m.erase(it) 的标准擦除循环
    for (QMap<int, int>::iterator it = m.begin(); it != m.end(); ) {
        if (it.key() == 2) {
            it = m.erase(it);
        } else {
            ++it;
        }
    }
    CHECK_EQ(m.size(), 2);
    CHECK(m.contains(1));
    CHECK(m.contains(2) == false);
    CHECK(m.contains(3));

    // 删首元素时返回的是「原第二个」；删末元素时返回 end()
    QMap<int, int> m2;
    m2[10] = 10;
    m2[20] = 20;
    QMap<int, int>::iterator it = m2.begin();
    QMap<int, int>::iterator next = m2.erase(it);
    CHECK(next != m2.end());
    CHECK_EQ(next.key(), 20);
    // 此刻 m2 = {20}，next 指向末元素；擦除它应返回 end()，容器随之变空
    CHECK(m2.erase(next) == m2.end());
    CHECK_EQ(m2.size(), 0);

    // ⚠ 端点语义备忘（Qt3 QMap 特有，实测所得）：QMap::end() **不是** node==0，
    //   而是指向共享结构里的一个真实哨兵节点；++ 走完末元素即等于 end()。
    //   哨兵在容器被清空后依然有效（地址不变，仍与新的 m.end() 相等）。
    //   因此擦除末元素返回预置的「后继」是安全的。
    // ⚠ 但绝不能 erase(end())：那是拿哨兵当真实节点删，属 UB（实测 SIGSEGV）。
    //   上面刻意让 m2 在被擦之前仍非空，就是为了不踩这条。
}

TEST_CASE("QMap 垫片: const 与非 const 迭代器可互比")
{
    QMap<QString, int> m;
    m[QString("x")] = 1;
    m[QString("y")] = 2;
    // 非 const 容器上取到的 begin 与 const 视图比较
    QMap<QString, int>::iterator it = m.begin();
    const QMap<QString, int>& cm = m;
    CHECK(it == cm.cbegin());
    CHECK(it != cm.cend());
    CHECK(cm.cbegin() == m.begin());
    CHECK(cm.cend() != m.begin());
}
