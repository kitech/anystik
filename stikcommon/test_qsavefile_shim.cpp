// stikcommon/test_qsavefile_shim.cpp —— QSaveFile 垫片契约（Qt3 单端）
//
// 被测对象：qsavefile_shim.h —— Qt3/Qt4 的 QSaveFile（原生是 Qt **5.1** 才
// 引入的类，本仓自建）。产品 20 处引用。
//
// 为什么这个最该测（2026-10 审计结论）：
//   该垫片存在的**唯一**理由就是原子提交 —— 头注释写明「中断/崩溃/写失败都
//   不破坏原文件（davbisync_baseline 的核心诉求：基线不可损毁，abort 不毁旧
//   基线）」。也就是说：**它坏了不会报错，只会静默毁掉用户的基线数据**。
//   这是全仓「静默丢数据」风险最高的一处，而此前零测试。
//
//   头注释还专门警告过 EXDEV（跨文件系统 rename 必失败、原子性就没了），并
//   说明为此**不用** QTemporaryFile（它默认落系统临时目录）。本文件用
//   「同目录内不留残骸」这条来钉住该设计确实生效。
//
// ⚠ QSaveFile 是本仓少数**非 inline** 的垫片（qsavefile_shim.cpp），
//   故本测试需链接它；build_tests.sh 的 $PRODUCTS 已含该 .cpp。

#include <qstring.h>
#include <qcstring.h>      // Qt3 的 QByteArray
#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qiodevice.h>
#include <qstringlist.h>
#include <unistd.h>

#include "doctest/doctest.h"

#include "qglobaltype_shim.h"   // Qt3 的 qint64
#include "qlist_shim.h"         // qDirEntryInfoList 返回 QList（Qt3 是 QPtrList）
#include "qdir_shim.h"
#include "qsavefile_shim.h"
#include "qba_shim.h"           // qbaLit
#include "qstring_shim.h"        // qFromUtf8BA —— 见下面 read() 的注释

#include "../qlcomp/compatcore34.h"   // qMkdir(path, recursive)

namespace {

class TempDir {
public:
    TempDir()
    {
        m_path = QDir(qDirTempPath()).absFilePath(
            QString::fromLatin1("qlstik_qsf_")
            + QString::number(int(getpid())) + QChar('_')
            + QString::number(++s_counter()));
        qMkdir(m_path, true);
    }
    ~TempDir() { qDirRemoveRecursively(QDir(m_path)); }
    QString path() const { return m_path; }
    QString file(const char* name) const
    { return QDir(m_path).absFilePath(QString::fromLatin1(name)); }
    bool exists(const QString& absPath) const
    { return QFile::exists(absPath); }
    bool write(const char* name, const QByteArray& d) const
    {
        QFile f(file(name));
        if (!f.open(IO_WriteOnly | IO_Truncate)) return false;
        const long n = f.writeBlock(d.data(), Q_ULONG(d.size()));
        f.close();
        return n == d.size();
    }
    QString read(const char* name) const
    {
        QFile f(file(name));
        if (!f.open(IO_ReadOnly)) return QString();
        const QByteArray b = f.readAll();
        f.close();
        // ⚠⚠ 必须用 qFromUtf8BA(b)（= fromUtf8(b.data(), b.size())），**不能**
        //   写 QString::fromUtf8(b)。Qt3 的 QByteArray 是 QMemArray<char>，
        //   fromUtf8 只有 (const char*, int len=-1) 一个重载，ba 被隐式转成
        //   const char* 后 len=-1 → strlen 语义；而 Qt3 的 readAll() 产物
        //   **不以 NUL 结尾**（实测 4 字节 "KEEP" 后面紧跟堆上残字节 ec 55，
        //   strlen 得 6），于是解出 7 个 QChar 而不是 4 个。
        //   症状：CHECK_EQ(读回, "KEEP") 报 "KEEP??U" vs "KEEP"，看着像脏内存。
        return qFromUtf8BA(b);
    }
    // 列出目录下的文件名（排序后），用于检查「临时文件有没有留下」。
    // ⚠ 必须走 qDirEntryInfoList：Qt3 的 QDir::entryInfoList 返回指向 QDir
    //   **内部成员**的指针（qdir.h:142），Qt4+ 才返回值（qdir_shim.h:248-298）。
    // ⚠ 且 Qt3 会把 "." 与 ".." 一并列出，故显式滤掉（qdir_shim.h:18-19）。
    QStringList listing() const
    {
        QStringList out;
        QDir d(m_path);
        const QList<QFileInfo> infos = qDirEntryInfoList(d, QDir::Files | QDir::Hidden);
        for (int i = 0; i < infos.count(); ++i) {
            const QString n = infos[i].fileName();
            if (n == QString::fromLatin1(".") || n == QString::fromLatin1("..")) continue;
            out.append(n);
        }
        out.sort();
        return out;
    }
private:
    static int& s_counter() { static int c = 0; return c; }
    QString m_path;
};

} // namespace

// ── 正常提交 ───────────────────────────────────────────────────────────

TEST_CASE("QSaveFile: commit 后目标内容为新数据，临时文件不留残骸")
{
    TempDir td;
    QSaveFile sf(td.file("baseline.json"));
    REQUIRE(sf.open());
    REQUIRE(sf.write(qbaLit("NEW-DATA")) > 0);
    CHECK(sf.commit());
    CHECK(!sf.error());

    CHECK_EQ(td.read("baseline.json"), QString::fromLatin1("NEW-DATA"));
    // 原子性的副产品：提交完目录里应只剩目标文件本身。
    // 临时名形如 "baseline.json.tmp0"（mkTmpName 规则），不得残留。
    CHECK_EQ(td.listing(), QStringList() << QString::fromLatin1("baseline.json"));
}

// ⚠⚠ 与 qFileCopy 正好相反：QSaveFile 提交时**允许**覆盖已存在的目标
//   （Qt 原生 QSaveFile 就是 replace 语义，POSIX rename 会覆盖）。
//   这正是「原子提交」必须的能力，若哪天误改成「目标存在就失败」，
//   第二次及以后的保存会静默全部失败。
TEST_CASE("QSaveFile: 目标已存在时可覆盖（replace 语义，与 qFileCopy 相反）")
{
    TempDir td;
    REQUIRE(td.write("f.txt", qbaLit("OLD")));

    QSaveFile sf(td.file("f.txt"));
    REQUIRE(sf.open());
    sf.write(qbaLit("NEW"));
    CHECK(sf.commit());
    CHECK_EQ(td.read("f.txt"), QString::fromLatin1("NEW"));
}

// ── 放弃写入：核心诉求 ─────────────────────────────────────────────────

// ⚠⚠⚠ 本模块存在的主要理由。未 commit 就析构 = 放弃本次写入，**原文件必须
//   完好无损**。这是 davbisync_baseline「基线不可损毁」的落点。若这条挂了，
//   一次写基线失败就会把用户已有基线清空，且没有任何报错。
TEST_CASE("QSaveFile: 未 commit 就析构 → 原文件分毫不动（abort 不毁旧基线）")
{
    TempDir td;
    REQUIRE(td.write("baseline.json", qbaLit("PRECIOUS")));

    {
        QSaveFile sf(td.file("baseline.json"));
        REQUIRE(sf.open());
        sf.write(qbaLit("GARBAGE-GARBAGE"));
        // 故意不 commit
    }   // ← 析构发生在这里

    CHECK_EQ(td.read("baseline.json"), QString::fromLatin1("PRECIOUS"));
    CHECK_EQ(td.listing(), QStringList() << QString::fromLatin1("baseline.json"));
}

// ⚠ cancelWriting() 是显式放弃：语义同上，但可在析构前先看一眼状态。
//   注意它**不**置 m_error（头文件/实现里只清 m_tmpFile 与 m_open）——
//   「主动取消」不是错误，这条也钉住，免得将来有人误判成失败而报警。
TEST_CASE("QSaveFile: cancelWriting 后原文件不动，且不算错误")
{
    TempDir td;
    REQUIRE(td.write("f.bin", qbaLit("KEEP")));

    QSaveFile sf(td.file("f.bin"));
    REQUIRE(sf.open());
    sf.write(qbaLit("JUNK"));
    sf.cancelWriting();

    CHECK(!sf.error());                       // 主动取消不是错误
    CHECK_EQ(td.read("f.bin"), QString::fromLatin1("KEEP"));
    CHECK_EQ(td.listing(), QStringList() << QString::fromLatin1("f.bin"));
}

// ⚠ cancelWriting 之后再 commit 必须失败（已无临时文件可改名），
//   且不能把原文件搞坏。
TEST_CASE("QSaveFile: cancelWriting 之后 commit 失败，原文件仍在")
{
    TempDir td;
    REQUIRE(td.write("f.bin", qbaLit("KEEP")));

    QSaveFile sf(td.file("f.bin"));
    REQUIRE(sf.open());
    sf.write(qbaLit("JUNK"));
    sf.cancelWriting();

    CHECK(!sf.commit());                      // 无临时文件 → 失败
    CHECK_EQ(td.read("f.bin"), QString::fromLatin1("KEEP"));
}

// ── 状态机边界 ─────────────────────────────────────────────────────────

// ⚠ 未 open 就 commit：必须失败并置 error，而不是「碰巧成功」或崩。
TEST_CASE("QSaveFile: 未 open 就 commit → 失败并置 error")
{
    TempDir td;
    QSaveFile sf(td.file("never-opened"));
    CHECK(!sf.commit());
    CHECK(sf.error());
    CHECK(!sf.errorString().isEmpty());
    // 连目标都不该被创建
    CHECK(!td.exists(td.file("never-opened")));
}

// ⚠ 重复 open：第二次必须失败（否则会泄漏第一个临时文件句柄）。
TEST_CASE("QSaveFile: 重复 open → 第二次失败")
{
    TempDir td;
    QSaveFile sf(td.file("f.bin"));
    REQUIRE(sf.open());
    CHECK(!sf.open());
    sf.cancelWriting();
}

// ⚠ 重复 commit：第一次成功后临时文件已改名，第二次必须失败。
TEST_CASE("QSaveFile: 重复 commit → 第二次失败")
{
    TempDir td;
    QSaveFile sf(td.file("f.bin"));
    REQUIRE(sf.open());
    sf.write(qbaLit("A"));
    CHECK(sf.commit());
    CHECK(!sf.commit());                      // 已无临时文件
    CHECK_EQ(td.read("f.bin"), QString::fromLatin1("A"));   // 内容不受影响
}

// ⚠ open 到一个所在目录不存在的路径：必须失败置 error，不能崩、也不能
//   在别处凭空造出文件。
TEST_CASE("QSaveFile: 目录不存在时 open 失败并置 error")
{
    TempDir td;
    QSaveFile sf(td.file("no-such-dir/f.bin"));
    CHECK(!sf.open());
    CHECK(sf.error());
    CHECK(!sf.errorString().isEmpty());
    CHECK_EQ(td.listing(), QStringList());    // 目录里仍是空的
}

// ⚠ fileName() 返回构造时传入的**目标**路径，不是临时路径 —— 调用方要靠它
//   记日志/做相对路径解析。
TEST_CASE("QSaveFile: fileName 返回目标路径而非临时路径")
{
    TempDir td;
    QSaveFile sf(td.file("named.bin"));
    CHECK_EQ(sf.fileName(), td.file("named.bin"));
}

// ⚠ write 返回实际写入字节数；空写入返回 0（不是负数）。
//   ⚠ 此前本用例写完 "hello" 后断言文件为**空**，自相矛盾（这条一直没跑到：
//   全量套件在更早的用例里 SIGSEGV 就死了，它藏在没执行的那一半里）。
//   现按真实语义拆成两段：先验证「空写入返回 0 且不破坏已有内容」，
//   再用独立文件验证「只做空写入 → 0 字节文件」。
TEST_CASE("QSaveFile: write 返回字节数；空写入返回 0 且不破坏已有内容")
{
    TempDir td;
    QSaveFile sf(td.file("e.bin"));
    REQUIRE(sf.open());
    CHECK_EQ(sf.write(qbaLit("hello")), 5);
    CHECK_EQ(sf.write(QByteArray()), 0);       // 空写入不是错误，也不该清掉前面的内容
    CHECK(sf.commit());
    CHECK(td.exists(td.file("e.bin")));
    CHECK_EQ(td.read("e.bin"), QString::fromLatin1("hello"));
}

TEST_CASE("QSaveFile: 全程只做空写入 → 目标落成 0 字节文件")
{
    TempDir td;
    QSaveFile sf(td.file("zero.bin"));
    REQUIRE(sf.open());
    CHECK_EQ(sf.write(QByteArray()), 0);
    CHECK(sf.commit());
    CHECK(td.exists(td.file("zero.bin")));
    CHECK(td.read("zero.bin").isEmpty());      // 不用 CHECK_EQ(..., QString())：null QString 打印会 SIGSEGV
}

// ⚠⚠ 临时文件必须落在**目标同目录**：这是 rename(2) 能原子生效的前提。
//   若哪天改成落系统临时目录（同 QTemporaryFile 的默认行为），跨文件系统时
//   rename 会以 EXDEV 失败 —— 而 commit() 只返回 false，调用方未必察觉，
//   基线就再也存不进去了。这条用例在提交前检查同目录出现了新文件。
TEST_CASE("QSaveFile: 临时文件落在目标同目录（rename 不跨文件系统）")
{
    TempDir td;
    QSaveFile sf(td.file("atomic.bin"));
    REQUIRE(sf.open());
    sf.write(qbaLit("data"));

    // 未 commit 时，目录里应多出一个「临时文件」，且它必须与目标同目录
    const QStringList mid = td.listing();
    CHECK_EQ(mid.count(), 1);
    CHECK(mid[0] != QString::fromLatin1("atomic.bin"));      // 不是目标本身
    CHECK(mid[0].startsWith(QString::fromLatin1("atomic.bin")));  // 形如 atomic.bin.tmp0

    CHECK(sf.commit());
    CHECK_EQ(td.listing(), QStringList() << QString::fromLatin1("atomic.bin"));
}

// ⚠ 两个 QSaveFile 写同一目标：临时名不撞（mkTmpName 逐个试 .tmp0/.tmp1…），
//   且**先提交的那个赢**、后提交的覆盖它，不出现残骸堆积。
TEST_CASE("QSaveFile: 两个实例写同一目标，临时名不撞、提交后无残骸")
{
    TempDir td;
    QSaveFile a(td.file("same.bin"));
    QSaveFile b(td.file("same.bin"));
    REQUIRE(a.open());
    REQUIRE(b.open());                          // ← 关键：两次都成功=临时名没撞
    a.write(qbaLit("AAA"));
    b.write(qbaLit("BBB"));

    CHECK(a.commit());
    CHECK(b.commit());
    CHECK_EQ(td.listing(), QStringList() << QString::fromLatin1("same.bin"));
    CHECK_EQ(td.read("same.bin"), QString::fromLatin1("BBB"));   // 后提交者赢
}
