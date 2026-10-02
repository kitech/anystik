// stikcommon/test_qglobaltype_shim.cpp —— qglobaltype_shim.h 契约（Qt3 单端）
//
// 被测对象：qBufferMake() / qBufferTake()（QBuffer 跨版本绑定）。
//
// 该头是纯头，**无需链接任何产品 .cpp**。

#include <qstring.h>
#include <qbuffer.h>
#include <qcstring.h>   // Qt3 的 QByteArray 定义在此（无独立 qbytearray.h）

#include "doctest/doctest.h"

#include "qglobaltype_shim.h"
#include "qba_shim.h"   // qbaEqLit

// ── qBuffer 垫片 ─────────────────────────────────────────────────────
// 回归点（均为实测结论）：
//  1) QBuffer 两版本都禁拷贝 —— Qt3 把拷贝构造放 private（qbuffer.h:87），
//     Qt6 是 Q_DISABLE_COPY（qbuffer.h:61）。「按值返回 QBuffer」的写法两边
//     都编不过，故 qBufferMake 只能收出参。
//  2) qBufferMake 在两版本下都**别名**到调用方那块内存，写入都回写。
//     Qt3 的 setBuffer 形参虽按值，但 QByteArray 带引用计数、共享存储，
//     实测 buffer().data() == ba.data()。⚠ 不能按「C++ 按值 = 深拷贝」
//     推断不回写 —— 那样会写出一条与事实相反的注释。
//  3) Qt3 的 QIODevice **没有** write()、也没有 WriteOnly 枚举（qiodevice.h
//     只有 writeBlock(const char*, Q_ULONG) 与 IO_WriteOnly 宏），故 open
//     /write 两步都得按版本分支 —— 这也是 qOpenWriteOnly 存在的理由。

TEST_CASE("qBufferMake/qBufferTake: 两版本都别名回写，qtBufferMake 须在 open 前调用")
{
    QByteArray ba;
    QBuffer b;
    qBufferMake(b, ba);
    CHECK(qOpenWriteOnly(b));
#if QT_VERSION < 0x040000
    CHECK_EQ(b.writeBlock("HELLO", 5), 5);
#else
    CHECK_EQ(b.write("HELLO", 5), 5);
#endif
    b.close();
    CHECK_EQ(ba.size(), 5);          // 两版本：写完外部变量立即可见
    QByteArray got = qBufferTake(b);
    CHECK_EQ(got.size(), 5);
    CHECK(qbaEqLit(got, "HELLO"));
}

TEST_CASE("qBufferMake: 别名语义 —— Qt3 下 QBuffer 与 ba 共享同一块内存")
{
    QByteArray ba = QCString("XY");
    QBuffer b;
    // 直接调 Qt3 原生 setBuffer，验证「按值形参仍共享存储」这一条
    CHECK(b.setBuffer(ba));
    b.open(IO_WriteOnly);
    b.writeBlock("HELLO", 5);
    b.close();
    // 核心断言：QBuffer 的写入**覆盖**了 ba 的旧内容 —— 即两者是同一块内存。
    // （别对 QCString 的 size 断言：它含尾 NUL，长度与 QByteArray 的直觉不同。）
    CHECK_EQ(b.buffer().size(), 5);
    CHECK(qbaEqLit(b.buffer(), "HELLO"));
    CHECK_EQ(ba.size(), 5);
    CHECK(qbaEqLit(ba, "HELLO"));
}
