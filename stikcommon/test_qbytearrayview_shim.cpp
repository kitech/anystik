// stikcommon/test_qbytearrayview_shim.cpp —— QByteArrayView 垫片契约（Qt3 单端）
//
// 被测对象：qbytearrayview_shim.h —— QByteArrayView 是 Qt **5.10** 才引入的
//   只读字节视图，Qt3/Qt4/Qt5.9 均无。stickerstore.cpp:3317 的 fileMd5() 用它：
//       hash.addData(QByteArrayView(buf.constData(), int(got)));
//
// 为什么值得测：它是「指针 + 长度」的薄视图，唯一容易出错的地方就是**空指针
//   与长度的配平** —— 若 (0, n) 时不把长度归零，调用方（摘要/协议解析）会拿
//   一个非空长度去解引用空指针，直接 SIGSEGV，且只在「空设备」这种少见输入下
//   触发，平时测不出来。
//
// ⚠ 只覆盖 Qt6 QByteArrayView 子集（构造 / data / size / isEmpty / constData）。
//   本垫片**刻意**不提供 slice()/removePrefix() 等，调用点一旦用到会在编译期
//   暴露，而不是静默错算。

#include <qcstring.h>
#include <qstring.h>

#include "doctest/doctest.h"

#include "qbytearrayview_shim.h"

TEST_CASE("QByteArrayView: 默认构造为空视图（指针空、长度 0、isEmpty）")
{
    const QByteArrayView v;
    CHECK(v.data() == (const char*)0);
    CHECK(v.constData() == (const char*)0);
    CHECK_EQ(v.size(), 0);
    CHECK(v.isEmpty());
}

TEST_CASE("QByteArrayView: (指针,长度) 构造保留二者")
{
    const char* buf = "hello";
    const QByteArrayView v(buf, 5);
    CHECK(v.data() == buf);                 // 不拷贝，指向原缓冲
    CHECK(v.constData() == buf);
    CHECK_EQ(v.size(), 5);
    CHECK(!v.isEmpty());
    CHECK_EQ(v.data()[0], 'h');
    CHECK_EQ(v.data()[4], 'o');
}

// ⚠⚠ 核心回归点：空指针配非零长度必须归零（qbytearrayview_shim.h:30-31 用
//   `data ? size : 0`）。这条正是防「空缓冲区 + 记录长度」的崩溃。
TEST_CASE("QByteArrayView: 空指针 + 非零长度 → 长度归零（防空指针解引用）")
{
    const QByteArrayView v((const char*)0, 5);
    CHECK(v.data() == (const char*)0);
    CHECK_EQ(v.size(), 0);                  // 关键：不是 5
    CHECK(v.isEmpty());
}

// ⚠ 边界：非空指针 + 长度 0 → 空视图（如 read 返回 0 字节时缓冲区仍有效）。
TEST_CASE("QByteArrayView: 非空指针 + 长度 0 → 空视图")
{
    const char* buf = "x";
    const QByteArrayView v(buf, 0);
    CHECK(v.data() == buf);
    CHECK_EQ(v.size(), 0);
    CHECK(v.isEmpty());
}

// ⚠ 视图不持有内存：原缓冲内容变了，视图看到的就是新内容（证明是视图而非拷贝）。
//   这条钉住「无存储期」这一设计前提，防止有人误以为是拷贝语义而存起来用。
TEST_CASE("QByteArrayView: 不拷贝，反映原缓冲的后续修改")
{
    char buf[4] = { 'a', 'b', 'c', '\0' };
    const QByteArrayView v(buf, 3);
    CHECK_EQ(v.data()[0], 'a');
    buf[0] = 'Z';
    CHECK_EQ(v.data()[0], 'Z');             // 视图看到的是改动后的内容
}
