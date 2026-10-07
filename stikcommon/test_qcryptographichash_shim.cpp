// stikcommon/test_qcryptographichash_shim.cpp —— QCryptographicHash 垫片契约（Qt3 单端）
//
// 被测对象：qcryptographichash_shim.h —— Qt3.5 无 QCryptographicHash（Qt4.1 才
//   引入），本仓自建。stickerstore.cpp 用它生成贴纸包/文件/URL 的**稳定 ID**：
//     · fileIdFor()        Sha1(path.toUtf8()).toHex()
//     · packIdFromTitle()  Sha1(packTitle.toUtf8()).toHex().left(12)
//     · urlHex()           Md5(url.toUtf8()).toHex().left(16)
//     · fileMd5()          增量 Md5 + addData() 分块 + result()
//
// 为什么这个必须测：这些 ID 一旦算错，贴纸包会**换个 ID**——表现为用户已下载的
//   内容全部识别不出、重复下载或缓存失效，且不会有任何报错。
//
// ⚠ 本文件的期望值全是 RFC 1321 / FIPS 180-1 的**标准测试向量**，不是「跑一遍
//   记录输出」。故它是真正的客观基准：算法实现错了就会被抓，不像多数契约测试
//   只能锁当前行为。
//
// ⚠ Qt3 陷阱（已核实，勿再踩）：
//   · QByteArray 没有 const char* 构造（qcstring.h:102）→ 用 qbaLit()。
//   · Qt3HashBytes::toHex() 输出**小写**（qcryptographichash_shim.h:37-39），
//     且**不能**换成 qba_shim.h 的大写 qbaToHex()，否则 ID 全变。

#include <qcstring.h>
#include <qstring.h>

#include "doctest/doctest.h"

#include "qba_shim.h"                    // qbaLit
#include "qbytearrayview_shim.h"         // QByteArrayView
#include "qcryptographichash_shim.h"

// 标准向量（RFC 1321 / FIPS 180-1），均已用系统 sha1sum/md5sum 复核
static const char* kMd5Empty  = "d41d8cd98f00b204e9800998ecf8427e";
static const char* kMd5Abc    = "900150983cd24fb0d6963f7d28e17f72";
static const char* kMd5Fox    = "9e107d9d372bb6826bd81d3542a419d6";
static const char* kSha1Empty = "da39a3ee5e6b4b0d3255bfef95601890afd80709";
static const char* kSha1Abc   = "a9993e364706816aba3e25717850c26c9cd0d89d";
static const char* kSha1Fox   = "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12";

// ── 标准向量：算法本身对不对 ────────────────────────────────────────────

TEST_CASE("QCryptographicHash: MD5 标准向量（空/abc/fox）")
{
    CHECK_EQ(QCryptographicHash::hash(qbaLit(""), QCryptographicHash::Md5).toHex(),
             qbaLit(kMd5Empty));
    CHECK_EQ(QCryptographicHash::hash(qbaLit("abc"), QCryptographicHash::Md5).toHex(),
             qbaLit(kMd5Abc));
    CHECK_EQ(QCryptographicHash::hash(
                 qbaLit("The quick brown fox jumps over the lazy dog"),
                 QCryptographicHash::Md5).toHex(),
             qbaLit(kMd5Fox));
}

TEST_CASE("QCryptographicHash: SHA1 标准向量（空/abc/fox）")
{
    CHECK_EQ(QCryptographicHash::hash(qbaLit(""), QCryptographicHash::Sha1).toHex(),
             qbaLit(kSha1Empty));
    CHECK_EQ(QCryptographicHash::hash(qbaLit("abc"), QCryptographicHash::Sha1).toHex(),
             qbaLit(kSha1Abc));
    CHECK_EQ(QCryptographicHash::hash(
                 qbaLit("The quick brown fox jumps over the lazy dog"),
                 QCryptographicHash::Sha1).toHex(),
             qbaLit(kSha1Fox));
}

// ⚠ 固定长度：MD5=16 字节、SHA1=20 字节。resultHex 长度 = 2× 字节数。
//   长度错了说明 size 计算或 hex 展开有 off-by-one。
TEST_CASE("QCryptographicHash: 摘要字节长度固定（MD5=16 / SHA1=20）")
{
    CHECK_EQ(QCryptographicHash::hash(qbaLit("x"), QCryptographicHash::Md5).size(), 16);
    CHECK_EQ(QCryptographicHash::hash(qbaLit("x"), QCryptographicHash::Sha1).size(), 20);
    CHECK_EQ(QCryptographicHash::hash(qbaLit("x"), QCryptographicHash::Md5).toHex().size(), 32);
    CHECK_EQ(QCryptographicHash::hash(qbaLit("x"), QCryptographicHash::Sha1).toHex().size(), 40);
}

// ── toHex 小写契约 ──────────────────────────────────────────────────────
// ⚠ 头注释专门警告过：toHex 必须小写，别复用大写版 qbaToHex()（qba_shim.h），
//   否则生成的 ID 改变、用户已有缓存全部失配。这条把大写字母钉死为不存在。
TEST_CASE("Qt3HashBytes::toHex: 输出全小写十六进制（不得换成大写版）")
{
    const Qt3HashBytes h =
        QCryptographicHash::hash(qbaLit("The quick brown fox jumps over the lazy dog"),
                                 QCryptographicHash::Md5).toHex();
    REQUIRE_EQ(h.size(), 32);
    bool hasUpper = false;
    for (int i = 0; i < h.size(); ++i) {
        const char c = h.at(i);
        if (c >= 'A' && c <= 'F') hasUpper = true;   // 大写十六进制字母
    }
    CHECK(!hasUpper);
    CHECK_EQ(h, qbaLit(kMd5Fox));                    // 且等于标准向量（本来就小写）
}

// ── left() 截断（packIdFromTitle 取 12、urlHex 取 16）────────────────────

TEST_CASE("Qt3HashBytes::left: 取前 n 字节，超长则全取")
{
    const Qt3HashBytes h =
        QCryptographicHash::hash(qbaLit("abc"), QCryptographicHash::Sha1).toHex();
    REQUIRE_EQ(h.size(), 40);                        // SHA1 hex 标准 40 字符

    CHECK_EQ(h.left(12).size(), 12);
    CHECK_EQ(h.left(12), qbaLit("a9993e364706"));    // 标准向量前缀
    CHECK_EQ(h.left(16).size(), 16);
    CHECK_EQ(h.left(0).size(), 0);                   // 边界：0
    CHECK_EQ(h.left(40), qbaLit(kSha1Abc));          // 恰好全长
    CHECK_EQ(h.left(999).size(), 40);                // 超长 → 全取，不越界
}

// ── 增量 addData：分块喂必须等于一次性喂 ────────────────────────────────
// ⚠ fileMd5() 走的就是分块：从 QIODevice 一块块读、逐块 addData。分块边界若
//   处理错（漏块/重复），大文件的 ID 会与一次性计算不同 —— 且文件越大越明显，
//   小样本测不出来。这里按素数步长 7/13 交错切分，制造非对齐边界。
TEST_CASE("QCryptographicHash: 增量 addData 分块 == 一次性（含非对齐边界）")
{
    // 造一段比单个卷积块(64B)更长的数据，避免只在短输入上碰巧正确
    QByteArray data;
    for (int i = 0; i < 1000; ++i)
        qbaAppend(data, char((i * 31 + 7) & 0xFF));

    QCryptographicHash inc(QCryptographicHash::Sha1);
    int pos = 0;
    int step = 7;
    while (pos < data.size()) {
        int n = step;
        if (pos + n > data.size()) n = data.size() - pos;
        inc.addData(qbaFromRaw(data.data() + pos, n));
        pos += n;
        step = (step == 7) ? 13 : 7;                 // 交替步长，制造非对齐
    }

    const Qt3HashBytes chunked = inc.result();
    const Qt3HashBytes oneShot =
        QCryptographicHash::hash(data, QCryptographicHash::Sha1);
    CHECK_EQ(chunked, oneShot);
    CHECK_EQ(chunked.toHex(), oneShot.toHex());
}

// ⚠ addData(QByteArrayView) 是 stickerstore.cpp:3317 走的形态
//   （QByteArrayView(buf.constData(), int(got))）。两条入口必须同结果。
TEST_CASE("QCryptographicHash: addData(QByteArrayView) 与 addData(QByteArray) 同结果")
{
    QByteArray data = qbaLit("payload-for-view");
    QCryptographicHash a(QCryptographicHash::Sha1);
    QCryptographicHash b(QCryptographicHash::Sha1);
    a.addData(data);
    b.addData(QByteArrayView(data.data(), data.size()));
    CHECK_EQ(a.result(), b.result());
    // 两条入口都必须等于一次性 hash()，才算真正同结果（而非「一起错」）
    CHECK_EQ(Qt3HashBytes(a.result()).toHex(),
             QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
}

// ⚠ 空 addData 是 no-op（qcryptographichash_shim.h:111/124 提前 return），
//   不得改变状态。故「只喂空串」== 「什么都不喂」。
TEST_CASE("QCryptographicHash: addData(空) 不改变状态")
{
    QCryptographicHash h(QCryptographicHash::Md5);
    h.addData(QByteArray());                         // 空 QByteArray
    h.addData(QByteArrayView());                     // 空 view
    CHECK_EQ(Qt3HashBytes(h.result()).toHex(), qbaLit(kMd5Empty));  // == MD5("")
}

// ⚠ result() 必须**非破坏性**（qcryptographichash_shim.h:134-136 拷贝 ctx 再
//   Final），这样可以在不 reset 的情况下多次取摘要、或继续 addData。若实现
//   改成就地 Final，第二次 result() 会返回垃圾，而单次调用点看不出来。
TEST_CASE("QCryptographicHash: result() 可重复取且不破坏后续 addData")
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(qbaLit("abc"));
    const Qt3HashBytes first = h.result();
    const Qt3HashBytes second = h.result();          // 再取一次
    CHECK_EQ(Qt3HashBytes(first).toHex(), qbaLit(kSha1Abc));
    CHECK_EQ(Qt3HashBytes(second).toHex(), qbaLit(kSha1Abc));  // 未被第一次污染

    // 继续增量：abc + fox 的一次性值
    h.addData(qbaLit("The quick brown fox jumps over the lazy dog"));
    // SHA1("abcThe quick...") 无公开向量，但与一次性构造比对即证增量正确
    QCryptographicHash ref(QCryptographicHash::Sha1);
    ref.addData(qbaLit("abcThe quick brown fox jumps over the lazy dog"));
    CHECK_EQ(h.result(), ref.result());
}

// ⚠ reset() 回到初始（空输入）状态：feed 后 reset，结果应等于空向量。
TEST_CASE("QCryptographicHash: reset() 回到初始状态")
{
    QCryptographicHash h(QCryptographicHash::Md5);
    h.addData(qbaLit("abc"));
    CHECK_EQ(Qt3HashBytes(h.result()).toHex(), qbaLit(kMd5Abc));
    h.reset();
    CHECK_EQ(Qt3HashBytes(h.result()).toHex(), qbaLit(kMd5Empty));
}

// ⚠ 不同输入必须给出不同摘要（防「常数返回」这类实现错误）。同时校验 md5≠sha1
//   对同一输入不同 —— 防两种算法被接到同一个 ctx。
TEST_CASE("QCryptographicHash: 不同输入/不同算法给出不同摘要")
{
    const Qt3HashBytes a = QCryptographicHash::hash(qbaLit("a"), QCryptographicHash::Md5);
    const Qt3HashBytes b = QCryptographicHash::hash(qbaLit("b"), QCryptographicHash::Md5);
    CHECK(!(a == b));

    const Qt3HashBytes m = QCryptographicHash::hash(qbaLit("same"), QCryptographicHash::Md5);
    const Qt3HashBytes s = QCryptographicHash::hash(qbaLit("same"), QCryptographicHash::Sha1);
    CHECK(!(m == s));
}

// ⚠ 二进制安全：摘要必须按字节算，遇到 0x00 不得当字符串结束。
//   这条容易被「顺手用 data() 当 char*」的实现写坏，而 stickerstore 的
//   fileMd5 正是对二进制缓冲区求摘要。
TEST_CASE("QCryptographicHash: 二进制安全，内嵌 NUL 参与摘要")
{
    // "a\0b" 与 "a" 必须不同（若把 NUL 当结尾就会相同）
    const QByteArray withNul = qbaFromRaw("a\0b", 3);
    const Qt3HashBytes hn = QCryptographicHash::hash(withNul, QCryptographicHash::Sha1);
    const Qt3HashBytes ha = QCryptographicHash::hash(qbaLit("a"), QCryptographicHash::Sha1);
    CHECK(!(hn == ha));
    CHECK_EQ(hn.toHex().size(), 40);

    // 与一次性喂入 commit 的标准短向量不冲突：单独校验 MD5("a") 长度
    CHECK_EQ(QCryptographicHash::hash(qbaLit("a"), QCryptographicHash::Md5).toHex().size(), 32);
}

// ⚠ 回归：SHA1 不得改写调用者输入缓冲区（2026-10-07 粘贴 JPEG 落盘损坏根因）。
//   sha1.c 未定义 SHA1HANDSOFF 时 SHA1_Transform 把 const 输入强转就地改写：
//   前 64B 经内部 buffer 拷贝幸免，[64, len-尾块) 被字节序交换/消息扩展覆盖
//   （日志实测 firstdiff=64、lastdiff=1652415=1652416-1），摘要仍正确但缓冲区
//   已毁 → 之后落盘的是垃圾字节。构建必须带 -DSHA1HANDSOFF（qlstik.pro /
//   stikcommon.pri / build_tests.sh）；缺宏时本用例必红。输入用 FIPS 180-1
//   标准向量 "a"×1000000（sha1sum 已复核），走多块路径才暴露此问题。
TEST_CASE("QCryptographicHash: SHA1 不改写输入缓冲区（>64B 多块）")
{
    QByteArray data;
    data.resize(1000000);
    data.fill('a');
    const QByteArray before = data.copy();   // Qt3 QMemArray::copy() 深拷贝

    const Qt3HashBytes h =
        QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex();
    CHECK_EQ(h, qbaLit("34aa973cd4c4daa4f61eeb2bdbad27316534016f"));
    CHECK(data == before);                   // 输入一字节未动（CHECK 避免失败时打印 1MB）
}
