// stikcommon/test_qglobaltype_shim.cpp —— qglobaltype_shim.h 契约（Qt3 单端）
//
// 被测对象：
//   qBufferMake() / qBufferTake()（QBuffer 跨版本绑定）
//   qBufferSetData()（Qt3 setBuffer 深拷贝 vs Qt4+ setData 隐式共享）
//   qOpenReadOnly() / qOpenWriteOnly() / qOpenWriteOnlyAppend()
//   qFileWrite() / qIoWrite()
//
// 该头是纯头，**无需链接任何产品 .cpp**。

#include <qstring.h>
#include <qbuffer.h>
#include <qcstring.h>   // Qt3 的 QByteArray 定义在此（无独立 qbytearray.h）
#include <qdir.h>
#include <qfile.h>
#include <unistd.h>

#include "doctest/doctest.h"

#include "qglobaltype_shim.h"
#include "qba_shim.h"   // qbaEqLit
#include "qdir_shim.h"  // qDirTempPath / qDirRemoveRecursively

#include "../qlcomp/compatcore34.h"   // qMkdir(path, recursive)

// ── 临时目录脚手架（沿用 test_qdir_shim.cpp 的同名惯例）────────────────
// 名字带 pid + 计数器：前者防并发两套互删，后者防同进程内计数器重置后撞名。
namespace {

class TempDir {
public:
    TempDir()
    {
        m_path = QDir(qDirTempPath()).absFilePath(
            QString::fromLatin1("qlstik_gts_")
            + QString::number(int(getpid())) + QChar('_')
            + QString::number(++s_counter()));
        qMkdir(m_path, true);
    }
    ~TempDir() { qDirRemoveRecursively(QDir(m_path)); }
    QString path() const { return m_path; }
private:
    static int& s_counter() { static int c = 0; return c; }
    QString m_path;
};

} // namespace

// ⚠ Qt3 的 QIODevice **没有** isReadOnly()（Qt4.0 才加），只有 isReadable() /
//   isWritable()（qiodevice.h:115-116，二者直接看 ioMode 位）。Qt4 isReadOnly()
//   的含义就是「可读且不可写」，故照此组合出等价判定。
//   不直接用 isWritable() 的原因：它在 Qt3 里同样反映**打开模式**而非文件权限，
//   对「以只读模式打开」这一断言正合适。
inline bool devIsReadOnly(QIODevice& dev)
{
#if QT_VERSION < 0x040000
    return dev.isReadable() && !dev.isWritable();
#else
    return dev.isReadOnly();
#endif
}

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

// ── 打开模式 / 写入转发 ──────────────────────────────────────────────
// 这一组全是「产品高频调用、但本文件此前零覆盖」的洞（2026-10 审计）：
//   qOpenReadOnly       产品 22 处   ← 最高频，却一个测试都没有
//   qOpenWriteOnlyAppend 产品 1 处    断点续传用
//   qBufferSetData      产品 8 处
//   qFileWrite/qIoWrite 产品 2 处
// 风险点在于这些函数都带 `#if QT_VERSION` 版本分支，选错分支不会编译报错，
// 只会在运行时静默拿到错误的打开模式 —— 正属于「读代码发现不了」的那类。

// ⚠ 本组要真的落盘，故建临时目录（沿用 test_qdir_shim.cpp 的 TempDir 惯例）。
//   qOpenReadOnly 的核心断言是「以只读模式打开一个存在的文件 → 成功，且
//   isReadOnly() 为真」。选错版本分支时 Qt3 会拿 IO_ReadOnly 之外的宏去开，
//   结果要么打不开、要么打开后可写 —— 后者尤其危险，静默。
TEST_CASE("qOpenReadOnly: 打开存在的文件成功，且确实是只读")
{
    TempDir td;
    const QString p = QDir(td.path()).absFilePath(QString::fromLatin1("ro.txt"));
    QFile w(p);
    REQUIRE(w.open(IO_WriteOnly | IO_Truncate));
    w.writeBlock("abc", 3);
    w.close();

    QFile r(p);
    CHECK(qOpenReadOnly(r));
    CHECK(devIsReadOnly(r));        // ← 这一条才区分「只读打开」与「随便打开成功」
    CHECK(!r.isWritable());
    char buf[8];
    CHECK_EQ(r.readBlock(buf, 8), 3);
    CHECK_EQ(qbaFromRaw(buf, 3), qbaLit("abc"));
    r.close();
}

// ⚠ 边界：打开不存在的文件必须失败且不创建它。IO_ReadOnly 若被错换成
//   IO_WriteOnly，这里就会凭空建出一个空文件 —— 一个能自愈的假阳性。
TEST_CASE("qOpenReadOnly: 打开不存在的文件失败，且不会凭空创建")
{
    TempDir td;
    const QString p = QDir(td.path()).absFilePath(QString::fromLatin1("nope.txt"));

    QFile r(p);
    CHECK(!qOpenReadOnly(r));
    CHECK(!QFile::exists(p));     // 失败后不得留下残骸
}

// ⚠ 边界：打开一个目录必须失败。Qt3 的 open(IO_ReadOnly) 对目录的返回值
//   在不同后端上不一致（有的返回 true），故只断言「不会被当作普通文件读出
//   字节」，即 atEnd 为真，不硬断言 open 的返回值。
TEST_CASE("qOpenReadOnly: 打开目录时读不出任何字节")
{
    TempDir td;
    QDir d(td.path());
    QFile r(d.absFilePath(QString::fromLatin1(".")));
    qOpenReadOnly(r);              // 不检查返回值，见上
    CHECK(r.atEnd());
    r.close();
}

// ⚠ Append 语义：断点续传（stickerstore.cpp:3546）依赖「追加写不清空旧内容」。
//   IO_Append | IO_WriteOnly 的**位或顺序**若写错成反过来，或漏了 Append，
//   结果都是旧数据被清掉 → 续传从零开始、贴纸损坏。故这里必须钉死。
TEST_CASE("qOpenWriteOnlyAppend: 追加写不清空已有内容")
{
    TempDir td;
    const QString p = QDir(td.path()).absFilePath(QString::fromLatin1("resume.bin"));
    QFile w(p);
    REQUIRE(w.open(IO_WriteOnly | IO_Truncate));
    w.writeBlock("HEAD", 4);
    w.close();

    QFile a(p);
    REQUIRE(qOpenWriteOnlyAppend(a));
    CHECK(a.isWritable());
    CHECK(!devIsReadOnly(a));
    qIoWrite(a, qbaLit("TAIL"));
    a.close();

    QFile r(p);
    REQUIRE(r.open(IO_ReadOnly));
    const QByteArray got = r.readAll();
    r.close();
    // 关键：4 + 4 = 8，且是拼接而非覆盖
    CHECK_EQ(got.size(), 8);
    CHECK_EQ(got, qbaLit("HEADTAIL"));
}

// ⚠ 对照组：qOpenWriteOnly **会**截断。这是 Append 用例的另一半 —— 若哪天
//   两个函数的行为变得一样，追加写会静默丢数据。
TEST_CASE("qOpenWriteOnly: 对照 —— 普通写会截断（故 Append 不可省）")
{
    TempDir td;
    const QString p = QDir(td.path()).absFilePath(QString::fromLatin1("trunc.bin"));
    QFile w(p);
    REQUIRE(w.open(IO_WriteOnly | IO_Truncate));
    w.writeBlock("HEAD", 4);
    w.close();

    QFile a(p);
    REQUIRE(qOpenWriteOnly(a));
    qIoWrite(a, qbaLit("T"));
    a.close();

    QFile r(p);
    REQUIRE(r.open(IO_ReadOnly));
    const QByteArray got = r.readAll();
    r.close();
    CHECK_EQ(got.size(), 1);       // 4 字节被截成 1 字节，不是 "THEAD"
    CHECK_EQ(got, qbaLit("T"));
}

// ⚠ qBufferSetData 的 Qt3 分支是 setBuffer（**快照深拷贝**），Qt4+ 是 setData
//   （隐式共享）。头注释明确说：产品那 8 处「setData 后只读」两种语义等价，
//   但若出现「setData 后改源、期望 buffer 跟着变」的用法，Qt3 就不成立。
//   这条用例把「改源不影响 buffer」钉成显式事实，将来真要用别名语义时它会红。
TEST_CASE("qBufferSetData: Qt3 下是快照深拷贝 —— 改源不影响 buffer")
{
    QByteArray src = qbaLit("ORIG");
    QBuffer b;
    REQUIRE(qBufferSetData(b, src));
    CHECK_EQ(qBufferTake(b), qbaLit("ORIG"));

    src = qbaLit("CHANGED");
    CHECK_EQ(b.size(), 4);
    CHECK_EQ(qBufferTake(b), qbaLit("ORIG"));   // 仍是旧内容
}

// ⚠ 边界：空 QByteArray 也要能 set 成功且长度为 0，不能崩、不能返回 false。
TEST_CASE("qBufferSetData: 空数据也成功，长度为 0")
{
    QByteArray empty;
    QBuffer b;
    CHECK(qBufferSetData(b, empty));
    CHECK_EQ(qBufferTake(b).size(), 0);
}

// ⚠ qFileWrite / qIoWrite 都返回**实际写入字节数**，调用点靠它判成败。
//   边界：len<=0 时返回 0（不是 -1），否则调用点 `> 0` 的判断会误判。
//   这里对 QFile 走 qFileWrite、对 QBuffer 走 qIoWrite，各验一遍。
TEST_CASE("qFileWrite/qIoWrite: 返回实际写入字节数；空写入返回 0 而非负数")
{
    TempDir td;
    const QString p = QDir(td.path()).absFilePath(QString::fromLatin1("w.bin"));
    QFile f(p);
    REQUIRE(f.open(IO_WriteOnly | IO_Truncate));

    CHECK_EQ(qFileWrite(f, qbaLit("hello")), 5);
    CHECK_EQ(qFileWrite(f, QByteArray()), 0);      // 空写 → 0，不是 -1
    CHECK_EQ(qFileWrite(f, qbaLit("")), 0);
    f.close();

    QByteArray ba;
    QBuffer b;
    qBufferMake(b, ba);
    REQUIRE(qOpenWriteOnly(b));
    CHECK_EQ(qIoWrite(b, qbaLit("xy")), 2);
    CHECK_EQ(qIoWrite(b, QByteArray()), 0);
    b.close();
}

// ⚠ 内嵌 NUL：Qt3 的 writeBlock 传的是裸指针+显式长度，若某天有人图省事
//   改成 strlen 语义，这里会暴露截断。本仓已被 QCString::size 的尾 NUL
//   咬过一次（qclipboard 测试注释），同类陷阱值得留个钉子。
TEST_CASE("qIoWrite: 含内嵌 NUL 的数据按显式长度写入，不被截断")
{
    QByteArray ba;
    QBuffer b;
    qBufferMake(b, ba);
    REQUIRE(qOpenWriteOnly(b));

    QByteArray bin = qbaLit("AB");
    bin[1] = '\0';
    qbaAppend(bin, "CD", 2);   // Qt3 无 operator+=(const char*)
    CHECK_EQ(qIoWrite(b, bin), 4);

    b.close();
    CHECK_EQ(ba.size(), 4);
    CHECK_EQ(ba[0], 'A');
    CHECK_EQ(ba[1], '\0');
    CHECK_EQ(ba[2], 'C');
    CHECK_EQ(ba[3], 'D');
}
