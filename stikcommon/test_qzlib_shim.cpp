// stikcommon/test_qzlib_shim.cpp —— qzlib_shim.h 契约（Qt3 单端）
//
// 被测对象：qCompress(raw, level) —— Qt3 侧用 zlib compress2() 自实现，
// 因为 Qt3 的 QByteArray 就是 QMemArray<char>，Qt 完全不提供压缩 API。
// Qt6 侧用 QtCore 原生的全局 qCompress（qbytearray.h:701），故调用点无分支。
//
// 该头是纯 inline，**无需链接任何产品 .cpp**（zlib 由 -lz 提供，build_tests.sh
// 的链接行已有）。
//
// ⚠ 头里的 include 用的是 <qcstring.h>：Qt3 没有独立的 qbytearray.h。
//
// ── 为什么这些断言值得写 ──────────────────────────────────────────────
// 该函数唯一用途是 stickerstore.cpp 的 buildApngFromFrames —— 把「逐行
// filter=none 的原始 RGBA 扫描线」压成 PNG 的 IDAT zlib 流。所以核心契约
// 只有一条：**产出的字节流必须能被 zlib 原样解回**。长度、level 透传之类
// 都是手段，不是目的。除此之外还有两个真实的坑：
//   1. RGBA 扫描线里 0 字节极多。实现若误用 strlen/strcpy 处理 QByteArray
//      （QMemArray 可含内嵌 0），会在第一个 0 处截断，产出**看起来合法但
//      解压出错误像素**的流 —— 这类损坏不会报错，只会渲染成花屏。
//   2. level 越界时 compress2() 返回 Z_STREAM_ERROR 而非崩库。垫片的约定
//      是「失败返回空 QByteArray，由调用方走兜底分支」，得钉住。

#include <qcstring.h>
#include <zlib.h>

#include "doctest/doctest.h"

#include "qba_shim.h"    // qbaFromRaw —— Qt3 的 QByteArray 无法从 const char* 构造
#include "qzlib_shim.h"

namespace {

// ⚠ Qt3 的 QByteArray 就是 QMemArray<char>，其构造面比 Qt4+ 窄得多：
//   * 没有 QByteArray(const char*)，字符串只能走 qbaFromRaw(p[, len])；
//   * QByteArray(int size, char c) 这个「重复字符」构造是 **protected**，
//     外部取不到（qba_shim.h 的 qbaFromRaw 也是只用 QByteArray(len) 后
//     逐字节赋值来绕开）。
//   * QMemArray::count() 无参/收 QMemArray，**没有** count(char)。
// 下面两个 helper 把这些差异封起来，用例里就能写正常的意图。
inline QByteArray filled(int n, char c)
{
    QByteArray b(n);
    for (int i = 0; i < n; ++i) b[i] = c;
    return b;
}

inline int countChar(const QByteArray& ba, char c)
{
    int n = 0;
    for (int i = 0; i < ba.size(); ++i)
        if (ba.at(i) == c) ++n;
    return n;
}

// 独立用 zlib 解压，用来验「垫片产出的流合法」——不复用垫片自己的逻辑，
// 否则压缩和解压同时错也会自洽通过。
QByteArray inflateRaw(const QByteArray& comp, int expectedLen)
{
    QByteArray out;
    if (expectedLen <= 0) return out;
    out.resize(expectedLen);
    uLongf destLen = (uLongf)expectedLen;
    const int rc = uncompress((Bytef*)out.data(), &destLen,
                              (const Bytef*)comp.data(), (uLong)comp.size());
    if (rc != Z_OK) return QByteArray();
    out.resize((int)destLen);
    return out;
}

} // namespace

// ── 核心契约：压缩 → zlib 解压 → 逐字节还原 ──────────────────────────

TEST_CASE("qCompress: 产出的流能被 zlib 原样解回（核心契约）")
{
    const QByteArray raw = qbaFromRaw("hello sticker store, hello zlib, hello zlib, hello zlib");
    const QByteArray comp = qCompress(raw, 6);
    CHECK(comp.size() > 0);
    const QByteArray back = inflateRaw(comp, raw.size());
    CHECK_EQ(back.size(), raw.size());
    CHECK(back == raw);
}

TEST_CASE("qCompress: level 1..9 全部往返正确")
{
    const QByteArray raw = filled(2000, 'A');        // 高度可压
    for (int lvl = 1; lvl <= 9; ++lvl) {
        const QByteArray comp = qCompress(raw, lvl);
        CHECK(comp.size() > 0);
        const QByteArray back = inflateRaw(comp, raw.size());
        CHECK_EQ(back.size(), raw.size());
        CHECK(back == raw);
    }
}

TEST_CASE("qCompress: level=0（不压缩）与 level=-1（默认）都能往返")
{
    const QByteArray raw = qbaFromRaw("abcabcabc", 9);
    const QByteArray c0 = qCompress(raw, 0);
    CHECK(c0.size() > 0);
    CHECK(inflateRaw(c0, raw.size()) == raw);

    const QByteArray cdef = qCompress(raw, -1);
    CHECK(cdef.size() > 0);
    CHECK(inflateRaw(cdef, raw.size()) == raw);
}

// ⚠ 回归点：QByteArray 是 QMemArray，可含内嵌 0 字节，**不能**用
// strlen 类逻辑处理。本用例的原始数据 1/4 是 0 字节，且第一个 0 出现在
// 偏移 0 —— 若实现按 C 字符串处理，产出的流会被截成空，解压必然失败或
// 解出短数据。这是 RGBA 扫描线的真实形态。
TEST_CASE("qCompress: 含大量内嵌 0 字节的二进制不被截断")
{
    QByteArray raw;
    raw.resize(1024);
    for (int i = 0; i < 1024; ++i)
        raw[i] = (char)((i & 3) ? 0 : 'A');   // 每 4 字节一个非 0，其余为 0

    const QByteArray comp = qCompress(raw, 6);
    CHECK(comp.size() > 0);
    const QByteArray back = inflateRaw(comp, raw.size());
    CHECK_EQ(back.size(), raw.size());
    CHECK(back == raw);
    // 反证：原始数据确实以 0 字节为主（i%4!=0 的位置共 768 个），
    // 且 index 1 就是 0 —— 即「第一个 0 紧跟在有效字节之后」，
    // 任何按 C 字符串处理（strlen）的实现都会在这里把流截断。
    CHECK_EQ(raw.at(0), 'A');
    CHECK_EQ(raw.at(1), '\0');
    CHECK_EQ(countChar(raw, '\0'), 768);
}

// ── 边界与失败契约 ───────────────────────────────────────────────────

TEST_CASE("qCompress: 空输入 → 空输出（不崩、不返回垃圾）")
{
    CHECK_EQ(qCompress(QByteArray(), 6).size(), 0);
    CHECK_EQ(qCompress(QByteArray(), 0).size(), 0);
    CHECK_EQ(qCompress(QByteArray(), -1).size(), 0);
}

TEST_CASE("qCompress: level 越界 → 返回空 QByteArray（不抛异常、不 abort）")
{
    // compress2 对 level>9 返回 Z_STREAM_ERROR。垫片约定是失败返回空串，
    // 由调用方走兜底分支。zlib.h 的注释只说「level 必须在 0..9 或 -1」，
    // 实测确实不崩，故这条是契约而非猜测。
    const QByteArray raw = qbaFromRaw("some data to compress");
    CHECK_EQ(qCompress(raw, 10).size(), 0);
    CHECK_EQ(qCompress(raw, 99).size(), 0);
    CHECK_EQ(qCompress(raw, -2).size(), 0);
}

// ── 压缩率：确认 level 真的透传了，不是恒定输出 ──────────────────────
// 回归点：垫片按 compressBound() 上界一次性分配再 resize 截断。若
// compress2 失败忘了 resize，或截断用了上界而非 destLen，产出的流会
// 带上尾部垃圾 —— 解压仍可能「成功」但内容错。往返用例已覆盖，这里补
// 压缩率只是佐证 level 真的起作用。

TEST_CASE("qCompress: 高重复数据确实被压小，且尺寸随 level 单调不增")
{
    QByteArray raw;
    raw.resize(8192);
    for (int i = 0; i < 8192; ++i)
        raw[i] = (char)('a' + (i % 4));

    const QByteArray c1 = qCompress(raw, 1);
    const QByteArray c9 = qCompress(raw, 9);
    CHECK(c1.size() > 0);
    CHECK(c9.size() > 0);
    CHECK(c9.size() < raw.size() / 4);          // 4 字符循环，zlib 压得动
    CHECK(c9.size() <= c1.size());              // 9 比 1 压得更紧或相等
    CHECK(inflateRaw(c9, raw.size()) == raw);
    CHECK(inflateRaw(c1, raw.size()) == raw);
}
