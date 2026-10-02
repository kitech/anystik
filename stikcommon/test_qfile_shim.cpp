// stikcommon/test_qfile_shim.cpp —— qfile_shim.h 契约（Qt3 单端）
//
// 被测对象（qfile_shim.h 全文 API）：
//   qOpenWriteTruncate / qFileCopy / qFileRename
//   qIODeviceRead / qIODeviceReadN / qIODeviceWrite
//   qFileInfoSuffix / qFileInfoCompleteSuffix / qFileInfoCompleteBaseName
//   qFileInfoPath / qFileInfoAbsolutePath / qFileInfoAbsoluteFilePath
//   qFileInfoExists
//
// 为什么单独建文件（2026-10 审计）：本头此前只被 test_qdir_shim.cpp 顺带
// include 去测了一个 qFileInfoAbsoluteFilePath，其余全是零覆盖，而产品侧
// qFileCopy 调用 7 处、qFileRename 5 处、qIODeviceWrite 4 处、qOpenWriteTruncate
// 1 处。文件族垫片是「静默丢数据」的高发区（见下面各用例的 WHY）。
//
// ⚠ include 顺序：qglobaltype_shim.h 必须早于本头（Qt3 没有 qint64，
//   它是 qglobaltype_shim.h:56 typedef 出来的）。
//
// ⚠ 全部脚手架走跨版本 API，不直接调 QDir/QFile 成员 —— Qt3 与 Qt6 名字差很多
//   （absFilePath/absoluteFilePath、writeBlock/write），让测试自己分叉会把
//   「测 shim」变成「测两套 QFile」。

#include <qstring.h>
#include <qstringlist.h>
#include <qcstring.h>      // Qt3 的 QByteArray 定义在此
#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qiodevice.h>
#include <unistd.h>

#include "doctest/doctest.h"

#include "qglobaltype_shim.h"
#include "qdir_shim.h"     // qDirTempPath / qDirRemoveRecursively
#include "qfile_shim.h"
#include "qba_shim.h"      // qbaLit / qbaFromRaw / qbaAppend
#include "qstring_shim.h"  // qToUtf8BA（中文内容的字节形式，见下方 CJK 用例）

#include "../qlcomp/compatcore34.h"   // qMkdir(path, recursive)

namespace {

// 临时目录：名字带 pid + 进程内计数器。带 pid 防并发两套互删，
// 带计数器防同进程内计数器重置后撞名（test_qdir_shim.cpp 同款）。
class TempDir {
public:
    TempDir()
    {
        m_path = QDir(qDirTempPath()).absFilePath(
            QString::fromLatin1("qlstik_qfile_")
            + QString::number(int(getpid())) + QChar('_')
            + QString::number(++s_counter()));
        qMkdir(m_path, true);
    }
    ~TempDir() { qDirRemoveRecursively(QDir(m_path)); }

    QString path() const { return m_path; }
    QString file(const char* name) const
    {
        return QDir(m_path).absFilePath(QString::fromLatin1(name));
    }
    bool write(const char* name, const QByteArray& data) const
    {
        QFile f(file(name));
        if (!f.open(IO_WriteOnly | IO_Truncate)) return false;
        const long n = f.writeBlock(data.data(), Q_ULONG(data.size()));
        f.close();
        return n == data.size();
    }
    bool exists(const char* name) const { return QFile::exists(file(name)); }

    // ⚠ QString 重载：给中文名用。const char* 那个走 fromLatin1，中文会变乱码，
    //   必须显式给 QString（QString::fromUtf8 构造）。传 const char* 字面量时
    //   重载解析优先选 const char* 版（精确匹配），不会歧义。
    QString file(const QString& name) const
    {
        return QDir(m_path).absFilePath(name);
    }
    bool write(const QString& name, const QByteArray& data) const
    {
        QFile f(file(name));
        if (!f.open(IO_WriteOnly | IO_Truncate)) return false;
        const long n = f.writeBlock(data.data(), Q_ULONG(data.size()));
        f.close();
        return n == data.size();
    }
    bool exists(const QString& name) const { return QFile::exists(file(name)); }
private:
    static int& s_counter() { static int c = 0; return c; }
    QString m_path;
};

QByteArray readAll(const QString& path)
{
    QFile f(path);
    if (!f.open(IO_ReadOnly)) return QByteArray();
    return f.readAll();
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// qOpenWriteTruncate
// ═══════════════════════════════════════════════════════════════════════════
// ⚠ Qt3 的 IO_WriteOnly 本身就截断（qglobaltype_shim.h:168-172 实测结论），
//   故本函数只包一层、不叠 IO_Truncate。回归点是「叠了也不会错，但叠错成
//   追加就完蛋」—— 故断言重开写之后旧尾部**没有**残留。

TEST_CASE("qOpenWriteTruncate: 重开写会截断，旧尾部不残留")
{
    TempDir td;
    REQUIRE(td.write("t.bin", qbaLit("AAAAAAAAAA")));   // 10 字节

    QFile f(td.file("t.bin"));
    REQUIRE(qOpenWriteTruncate(f));
    f.writeBlock("BBB", 3);
    f.close();

    const QByteArray got = readAll(td.file("t.bin"));
    CHECK_EQ(got.size(), 3);                 // 不是 10
    CHECK_EQ(got, qbaLit("BBB"));
}

// ⚠ 边界：写空数据也应得到 0 字节文件（截断照常发生），不是保留旧内容。
TEST_CASE("qOpenWriteTruncate: 写空数据得到 0 字节文件")
{
    TempDir td;
    REQUIRE(td.write("t.bin", qbaLit("OLD")));

    QFile f(td.file("t.bin"));
    REQUIRE(qOpenWriteTruncate(f));
    f.close();                                // 一个字节都不写

    CHECK(td.exists("t.bin"));                // 文件还在
    CHECK_EQ(readAll(td.file("t.bin")).size(), 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// qFileCopy
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("qFileCopy: 正常拷贝，字节完全一致")
{
    TempDir td;
    REQUIRE(td.write("src.bin", qbaLit("hello world")));

    CHECK(qFileCopy(td.file("src.bin"), td.file("dst.bin")));
    CHECK(td.exists("src.bin"));              // 源不被移动
    CHECK(td.exists("dst.bin"));
    CHECK_EQ(readAll(td.file("dst.bin")), qbaLit("hello world"));
}

// ⚠⚠ 核心契约：目标**已存在时必须失败且绝不覆盖**（qfile_shim.h:66-67 按 Qt6
//   对齐）。若改成覆盖，贴纸缓存会被静默改写 —— 调用方以为拷失败了去走重算
//   路径，实际旧文件已经没了。
TEST_CASE("qFileCopy: 目标已存在 → 失败，且不覆盖已有内容")
{
    TempDir td;
    REQUIRE(td.write("src.bin", qbaLit("NEW")));
    REQUIRE(td.write("dst.bin", qbaLit("OLD")));

    CHECK(!qFileCopy(td.file("src.bin"), td.file("dst.bin")));
    CHECK_EQ(readAll(td.file("dst.bin")), qbaLit("OLD"));   // 关键：没被动过
    CHECK_EQ(readAll(td.file("src.bin")), qbaLit("NEW"));
}

// ⚠ 边界：源不存在 → false，且**不得凭空创建**目标（Qt3 分支里 src.open 失败
//   会提前 return false，不该留下空文件）。
TEST_CASE("qFileCopy: 源不存在 → 失败，且不创建目标")
{
    TempDir td;
    CHECK(!qFileCopy(td.file("nope.bin"), td.file("dst.bin")));
    CHECK(!td.exists("dst.bin"));
}

// ⚠ 边界：空文件也能拷，且拷出来仍是空文件（0 字节不是「失败」）。
TEST_CASE("qFileCopy: 空文件往返")
{
    TempDir td;
    REQUIRE(td.write("empty.bin", QByteArray()));
    CHECK(qFileCopy(td.file("empty.bin"), td.file("e2.bin")));
    CHECK(td.exists("e2.bin"));
    CHECK_EQ(readAll(td.file("e2.bin")).size(), 0);
}

// ⚠⚠ 分块边界回归：qfile_shim.h:78 用 64KB 分块搬运。这条用例让数据跨过
//   分块边界（造 200KB+17 字节），任何「分块数算错 / 尾巴丢失 / 提前 break」
//   的改动都会在这里现形。头注释声称实测过，但那是手工对拍，没有回归保护。
TEST_CASE("qFileCopy: 200KB+17 字节跨 64KB 分块边界仍逐字节一致")
{
    TempDir td;
    QByteArray big;
    for (int i = 0; i < 200 * 1024 + 17; ++i)
        qbaAppend(big, char(i * 7 + (i >> 8)));      // 非周期，避免全同字节掩盖错位
    REQUIRE(td.write("big.bin", big));

    CHECK(qFileCopy(td.file("big.bin"), td.file("big2.bin")));

    const QByteArray got = readAll(td.file("big2.bin"));
    CHECK_EQ(got.size(), big.size());
    // 逐字节比：不依赖任何 operator== 的正确性
    bool same = (got.size() == big.size());
    for (int i = 0; same && i < big.size(); ++i)
        if (got[i] != big[i]) same = false;
    CHECK(same);
}

// ═══════════════════════════════════════════════════════════════════════════
// qFileRename
// ═══════════════════════════════════════════════════════════════════════════
// ⚠⚠ Qt3 的 qFileRename 是「拷贝 + 删源」实现（qfile_shim.h:96-109），不是
//   POSIX rename(2)。两个可观测后果必须钉死：
//     1) 成功后**源必须消失**、目标内容正确；
//     2) 目标已存在时**整体失败** —— 若先拷后删的顺序写错，会出现
//        「目标被写了但源还在」或更糟「源被删了但目标没写成」的中间态。

TEST_CASE("qFileRename: 成功后源消失、目标内容正确")
{
    TempDir td;
    REQUIRE(td.write("a.bin", qbaLit("payload")));

    CHECK(qFileRename(td.file("a.bin"), td.file("b.bin")));
    CHECK(!td.exists("a.bin"));               // 源必须没了
    CHECK(td.exists("b.bin"));
    CHECK_EQ(readAll(td.file("b.bin")), qbaLit("payload"));
}

TEST_CASE("qFileRename: 目标已存在 → 失败，且源与目标都原封不动")
{
    TempDir td;
    REQUIRE(td.write("a.bin", qbaLit("SRC")));
    REQUIRE(td.write("b.bin", qbaLit("DST")));

    CHECK(!qFileRename(td.file("a.bin"), td.file("b.bin")));
    // 关键：拷贝+删源的实现里，若中途顺序错了，这里会出现一边丢
    CHECK(td.exists("a.bin"));
    CHECK_EQ(readAll(td.file("a.bin")), qbaLit("SRC"));
    CHECK_EQ(readAll(td.file("b.bin")), qbaLit("DST"));
}

TEST_CASE("qFileRename: 源不存在 → 失败，且不创建目标")
{
    TempDir td;
    CHECK(!qFileRename(td.file("nope.bin"), td.file("dst.bin")));
    CHECK(!td.exists("dst.bin"));
}

// ═══════════════════════════════════════════════════════════════════════════
// qIODeviceRead / qIODeviceWrite / qIODeviceReadN
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("qIODeviceRead/Write: 往返一致，返回实际传输字节数")
{
    TempDir td;
    REQUIRE(td.write("rw.bin", qbaLit("0123456789")));

    QFile r(td.file("rw.bin"));
    REQUIRE(qOpenReadOnly(r));
    char buf[32];
    CHECK_EQ(qIODeviceRead(r, buf, 4), 4);            // 只读 4 字节
    CHECK_EQ(qIODeviceRead(r, buf, 6), 6);            // 再读 6 字节
    CHECK_EQ(qIODeviceRead(r, buf, 32), 0);           // 到底 → 0
    r.close();
    CHECK_EQ(qbaFromRaw(buf, 6), qbaLit("456789"));    // 确认是接着读的
}

// ⚠⚠ 头注释明确写「读到底后再读返回 **0**，不是 -1」（qfile_shim.h:127-128）。
//   调用点普遍写 `if (got <= 0) break;`，若哪天变成 -1 仍能过；但写
//   `if (got == 0) break;` 的调用点就会死循环。这条断言把 0 钉死。
TEST_CASE("qIODeviceRead: 读到 EOF 返回 0（不是 -1），调用点用 <=0 才安全")
{
    TempDir td;
    REQUIRE(td.write("eof.bin", qbaLit("xy")));

    QFile r(td.file("eof.bin"));
    REQUIRE(qOpenReadOnly(r));
    char buf[16];
    CHECK_EQ(qIODeviceRead(r, buf, 16), 2);
    CHECK_EQ(qIODeviceRead(r, buf, 16), 0);           // 必须恰好是 0
    CHECK_EQ(qIODeviceRead(r, buf, 16), 0);           // 再读仍是 0，不越界
    r.close();
}

// ⚠ 边界：maxlen<=0 时返回 0 且不碰缓冲区（不要求调用点先判非负）。
TEST_CASE("qIODeviceRead: maxlen<=0 返回 0")
{
    TempDir td;
    REQUIRE(td.write("z.bin", qbaLit("abc")));

    QFile r(td.file("z.bin"));
    REQUIRE(qOpenReadOnly(r));
    char buf[4] = { 'K', 'K', 'K', 'K' };
    CHECK_EQ(qIODeviceRead(r, buf, 0), 0);
    CHECK_EQ(qIODeviceRead(r, buf, -5), 0);
    CHECK_EQ(buf[0], 'K');                            // 缓冲区未被写
    r.close();
}

// ⚠⚠ qIODeviceReadN 必须与 Qt6 的 read(maxSize) 同为**流式**语义：本次只消费
//   maxSize 字节，余量留在设备里。若实现成「readAll 全量 + 截断」，第一次调用
//   就把文件读空，第二次恒返回空 —— 这条用例就是为此写的。
//   基准取自 Qt 6.7.3 实测，9 字节文件：read(8) → 8，read(100) → 1（不补齐）。
TEST_CASE("qIODeviceReadN: 请求大于剩余时返回实际长度，不补零且不吞掉后续数据")
{
    TempDir td;
    REQUIRE(td.write("n.bin", qbaLit("abcdefghi")));   // 9 字节

    QFile r(td.file("n.bin"));
    REQUIRE(qOpenReadOnly(r));
    CHECK_EQ(qIODeviceReadN(r, 8).size(), 8);           // 截断到请求上限
    CHECK_EQ(qIODeviceReadN(r, 100).size(), 1);         // 剩 1 字节，不补到 100
    CHECK_EQ(qIODeviceReadN(r, 100).size(), 0);         // 真的流式：已到 EOF
    r.close();
}

TEST_CASE("qIODeviceReadN: 空文件与 maxlen<=0 都返回空")
{
    TempDir td;
    REQUIRE(td.write("empty.bin", QByteArray()));

    QFile r(td.file("empty.bin"));
    REQUIRE(qOpenReadOnly(r));
    CHECK_EQ(qIODeviceReadN(r, 10).size(), 0);
    r.close();

    QFile r2(td.file("empty.bin"));
    REQUIRE(qOpenReadOnly(r2));
    CHECK_EQ(qIODeviceReadN(r2, 0).size(), 0);
    CHECK_EQ(qIODeviceReadN(r2, -1).size(), 0);
    r2.close();
}

// ═══════════════════════════════════════════════════════════════════════════
// QFileInfo 家族
// ═══════════════════════════════════════════════════════════════════════════
// ⚠ qfile_shim.h:16-20 声称 5 组文件名（a.tar.gz / .bashrc / a. / noext /
//   a.b.c.d）在 Qt3 与 Qt6 输出逐字节相同，completeBaseName 是 7 组 —— 但这些
//   全是手工对拍，没有测试。qFileInfoCompleteBaseName 更是**本仓自己实现的
//   规则**（Qt3 没有该成员），最需要回归保护。
//
// ⚠ 纯字符串推导的部分（suffix / completeSuffix / completeBaseName）不需要
//   文件真的存在：QFileInfo 对不存在的路径照样按名字解析。只有 path /
//   absolutePath / exists 需要真实文件系统。

TEST_CASE("qFileInfoSuffix/CompleteSuffix: 与 Qt6 对拍的 5 组基准")
{
    struct Case { const char* name; const char* suffix; const char* cSuffix; };
    const Case cases[] = {
        { "a.tar.gz", "gz",      "tar.gz"   },
        { ".bashrc",  "bashrc",  "bashrc"   },
        { "a.",       "",        ""         },
        { "noext",    "",        ""         },
        { "a.b.c.d",  "d",       "b.c.d"    },
    };
    for (int i = 0; i < 5; ++i) {
        const QFileInfo fi(QString::fromLatin1("/x/y/") + QString::fromLatin1(cases[i].name));
        CHECK_EQ(qFileInfoSuffix(fi), QString::fromLatin1(cases[i].suffix));
        CHECK_EQ(qFileInfoCompleteSuffix(fi), QString::fromLatin1(cases[i].cSuffix));
    }
}

// ⚠ completeBaseName 是本垫片**自己写的规则**（Qt3 无该成员，qfile_shim.h:211-219）。
//   最容易错的是「点在开头」的情形。基准取自 Qt 6.7.3 实测（探针记录）：
//     ".bashrc" → 空（且 isNull=0，**非** null 空串）
//     "."       → 空
//     ".a.b"    → ".a"
//     ".."      → "."   ← 不是空！纯点号与单点行为不同
//   ⚠ ".." 曾被我写成期望空串而被实测推翻：Qt6 返回的是 "."（len=1）。
TEST_CASE("qFileInfoCompleteBaseName: 本仓自实现规则的 8 组基准")
{
    struct Case { const char* name; const char* base; };
    const Case cases[] = {
        { "a.tar.gz", "a.tar"  },
        { "a.b.c.d",  "a.b.c"  },
        { "noext",    "noext"  },
        { "a.",       "a"      },
        { ".bashrc",  ""       },     // 点开头 → 空（Qt6：非 null 空串）
        { ".a.b",     ".a"     },     // 点开头但不止一个点
        { "..",       "."      },     // 纯点号：Qt6 返回 "."，不是空
        { ".",        ""       },     // 单点 → 空
    };
    for (int i = 0; i < 8; ++i) {
        const QFileInfo fi(QString::fromLatin1("/x/") + QString::fromLatin1(cases[i].name));
        CHECK_EQ(qFileInfoCompleteBaseName(fi), QString::fromLatin1(cases[i].base));
    }
}

// ⚠⚠ path() 与 absolutePath() 不可混用（qfile_shim.h:186-195 特意点出）：
//   path() 返回**字面**目录部分，absolutePath() 返回**绝对**目录。
//   传相对路径时两者分叉得最厉害，正是最容易写错的地方。
TEST_CASE("qFileInfoPath/AbsolutePath: 字面目录 vs 绝对目录，相对路径下分叉")
{
    QFileInfo abs(QString::fromLatin1("/a/b/c.png"));
    CHECK_EQ(qFileInfoPath(abs), QString::fromLatin1("/a/b"));
    CHECK_EQ(qFileInfoAbsolutePath(abs), QString::fromLatin1("/a/b"));

    // 相对路径：path 保持相对，absolutePath 补成绝对（以当前工作目录为基准）
    QFileInfo rel(QString::fromLatin1("sub/c.png"));
    CHECK_EQ(qFileInfoPath(rel), QString::fromLatin1("sub"));
    const QString ap = qFileInfoAbsolutePath(rel);
    CHECK(!ap.isEmpty());
    CHECK(ap.startsWith(QChar('/')));
    CHECK(ap.endsWith(QString::fromLatin1("/sub")));
}

// ⚠ 边界：只有文件名、没有目录部分时 path() 返回 **"."**。
//   ⚠ 此前本用例断言的是「空串」，并写着「（不是 '.'）」——那是**反的**：
//     Qt6 6.7.3 实测 QFileInfo("c.png").path() 返回 "."（len=1，非 null 非空）；
//     Qt3 侧垫片转调 dirPath(false)（qfile_shim.h:196）同样是 "."。
//     两版本一致，故按真值钉 "."；若曾按错误断言改成空串，反而会造成分叉。
TEST_CASE("qFileInfoPath: 裸文件名时返回 '.'（Qt3/Qt6 实测一致）")
{
    QFileInfo fi(QString::fromLatin1("c.png"));
    CHECK_EQ(qFileInfoPath(fi), QString::fromLatin1("."));   // 不用 CHECK_EQ(..., QString())：null QString 打印会 SIGSEGV
    CHECK(!qFileInfoPath(fi).isNull());
    // 带目录时返回目录部分，不含文件名
    CHECK_EQ(qFileInfoPath(QFileInfo(QString::fromLatin1("/a/b/c.png"))),
             QString::fromLatin1("/a/b"));
}

// ⚠ absoluteFilePath 返回**文件自身**的绝对路径，与 absolutePath 差一个文件名。
//   两者混用是本垫片注释里点名的坑（qfile_shim.h:192-195）。
TEST_CASE("qFileInfoAbsoluteFilePath: 返回文件自身路径，不是所在目录")
{
    const QFileInfo fi(QString::fromLatin1("/a/b/c.png"));
    const QString afp = qFileInfoAbsoluteFilePath(fi);
    CHECK(afp.endsWith(QString::fromLatin1("/a/b/c.png")));
    CHECK(qFileInfoAbsolutePath(fi).endsWith(QString::fromLatin1("/a/b")));
    CHECK(afp != qFileInfoAbsolutePath(fi));
}

// ⚠ qFileInfoExists 是静态版（Qt3 只有实例成员，Qt3 侧转发到 QFile::exists）。
//   三个分支都要覆盖：存在的文件 / 存在的目录 / 不存在的路径。
TEST_CASE("qFileInfoExists: 区分存在的文件、存在的目录、不存在的路径")
{
    TempDir td;
    REQUIRE(td.write("here.txt", qbaLit("x")));

    CHECK(qFileInfoExists(td.file("here.txt")));         // 文件
    CHECK(qFileInfoExists(td.path()));                   // 目录
    CHECK(!qFileInfoExists(td.file("absent.txt")));      // 不存在
    CHECK(!qFileInfoExists(QString::fromLatin1("")));     // 空路径
}

// ═══════════════════════════════════════════════════════════════════════════
// CJK（中文）文件名与内容
// ═══════════════════════════════════════════════════════════════════════════
//
// ⚠⚠ 这是文件族垫片最高危的盲区：本仓真实文件名全是中文（贴纸名、站点目录），
//   而此前用例全是 ASCII。中文名一旦在某环被 Latin-1 化，症状是「文件不存在」
//   或「复制出乱码文件」，**不报错**。
//
// ⚠ 内容往返必须用 qToUtf8BA 而不是 QString::utf8()：Qt3 的 utf8() 返回 QCString
//   且 size() **含尾 NUL**（qstring_shim.h:101 记过这条），直接把 data()/size()
//   写进文件会多写一个 0x00 字节。

TEST_CASE("qFileInfo 家族: 中文文件名的后缀/完整基名解析")
{
    // 纯推导，不需要文件真实存在
    const QFileInfo tie(QString::fromUtf8("/tmp/站点/贴纸.png"));
    CHECK_EQ(qFileInfoSuffix(tie), QString::fromLatin1("png"));
    CHECK_EQ(qFileInfoCompleteSuffix(tie), QString::fromLatin1("png"));
    CHECK_EQ(qFileInfoCompleteBaseName(tie), QString::fromUtf8("贴纸"));
    CHECK_EQ(qFileInfoPath(tie), QString::fromUtf8("/tmp/站点"));
    CHECK_EQ(qFileInfoAbsolutePath(tie), QString::fromUtf8("/tmp/站点"));

    // 多后缀 + 中文
    const QFileInfo tar(QString::fromUtf8("/tmp/站点.tar.gz"));
    CHECK_EQ(qFileInfoSuffix(tar), QString::fromLatin1("gz"));
    CHECK_EQ(qFileInfoCompleteSuffix(tar), QString::fromLatin1("tar.gz"));
    CHECK_EQ(qFileInfoCompleteBaseName(tar), QString::fromUtf8("站点.tar"));

    // 中文点开头（隐藏文件）：与 ASCII 同规则 —— 空基名
    const QFileInfo dot(QString::fromUtf8("/tmp/.站点"));
    CHECK(qFileInfoCompleteBaseName(dot).isEmpty());   // 同上，避免 null 打印崩溃

    // 码点自检（守住守门人）
    const QString tie2 = QString::fromUtf8("贴纸");
    CHECK_EQ(tie2.length(), 2);
    CHECK(tie2.at(0) == QChar(0x8D34));
    CHECK(tie2.at(1) == QChar(0x7EB8));
}

TEST_CASE("qIODeviceWrite/qIODeviceRead: 中文内容按字节往返不失真")
{
    TempDir td;
    const QString content = QString::fromUtf8("猫喵贴纸");
    const QByteArray bytes = qToUtf8BA(content);      // 12 字节，无尾 NUL
    CHECK_EQ(bytes.size(), 12);
    REQUIRE(td.write(QString::fromUtf8("中文名.txt"), bytes));

    const QByteArray back = readAll(td.file(QString::fromUtf8("中文名.txt")));
    CHECK_EQ(back.size(), bytes.size());
    CHECK(back == bytes);
    // 读回来按 UTF-8 解释必须还是原文
    CHECK_EQ(QString::fromUtf8(back.data(), (int)back.size()), content);
}

TEST_CASE("qFileCopy/qFileRename: 中文文件名与中文内容都要原样落地")
{
    TempDir td;
    const QString srcName = QString::fromUtf8("站点/贴纸.png");
    REQUIRE(qMkdir(td.file(QString::fromUtf8("站点")), true));

    const QString content = QString::fromUtf8("猫喵");
    const QByteArray bytes = qToUtf8BA(content);
    REQUIRE(td.write(srcName, bytes));

    // 复制到中文名（名字里还带空格）
    const QString dstName = QString::fromUtf8("复制 喵.png");
    REQUIRE(qFileCopy(td.file(srcName), td.file(dstName)));
    CHECK(td.exists(dstName));
    CHECK(td.exists(srcName));                          // 复制不动源
    const QByteArray copied = readAll(td.file(dstName));
    CHECK_EQ(QString::fromUtf8(copied.data(), (int)copied.size()), content);

    // 重命名到中文名：源消失、目标出现、内容不变
    const QString renName = QString::fromUtf8("重命名-表情.gif");
    REQUIRE(qFileRename(td.file(dstName), td.file(renName)));
    CHECK(!td.exists(dstName));
    CHECK(td.exists(renName));
    const QByteArray afterRename = readAll(td.file(renName));
    CHECK_EQ(QString::fromUtf8(afterRename.data(), (int)afterRename.size()), content);

    // ⚠ 目标已存在时整体失败，且源与目标都不得被动过（中文名同样如此）
    REQUIRE(td.write(QString::fromUtf8("已存在.png"), qbaLit("KEEP")));
    CHECK(!qFileRename(td.file(renName), td.file(QString::fromUtf8("已存在.png"))));
    CHECK(td.exists(renName));
    CHECK_EQ(readAll(td.file(QString::fromUtf8("已存在.png"))), qbaLit("KEEP"));
}

TEST_CASE("qFileInfoExists/qOpenReadOnly: 中文路径的判定与打开")
{
    TempDir td;
    REQUIRE(td.write(QString::fromUtf8("存在的文件.webp"), qbaLit("x")));
    CHECK(qFileInfoExists(td.file(QString::fromUtf8("存在的文件.webp"))));
    CHECK(!qFileInfoExists(td.file(QString::fromUtf8("不存在.webp"))));

    QFile f(td.file(QString::fromUtf8("存在的文件.webp")));
    REQUIRE(qOpenReadOnly(f));
    char buf[2];
    CHECK_EQ(qIODeviceRead(f, buf, 1), 1);
    f.close();

    // 打开不存在的中文名必须失败（而不是建出一个空文件）
    QFile g(td.file(QString::fromUtf8("缺失.webp")));
    CHECK(!qOpenReadOnly(g));
    CHECK(!qFileInfoExists(td.file(QString::fromUtf8("缺失.webp"))));
}
