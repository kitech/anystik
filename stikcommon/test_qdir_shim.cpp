// stikcommon/test_qdir_shim.cpp —— 文件系统/时间类垫片契约（Qt3 单端）
//
// 被测对象：
//   qdir_shim.h     —— qDirEntryInfoList（Qt3 返回**堆指针**、Qt6 返回值列表，
//                      且 Qt3 会把 "." / ".." 也列进来）、qDirEntryList
//                      （Qt3 的单 QString 过滤器不支持逗号分隔多通配）。
//   qfile_shim.h    —— qFileInfoAbsoluteFilePath、qFileInfoSuffix 等。
//   qdatetime_shim.h—— qDateTimeEpochSecs。
//
// 三个头都是纯头，**无需链接任何产品 .cpp**。
//
// ⚠ include 顺序约束：qdatetime_shim.h 用了 `qint64`，而 **Qt3 根本没有
//   qint64** —— 它是 stikcommon/qglobaltype_shim.h:56 自己
//   `typedef long long qint64;` 出来的。故 qglobaltype_shim.h 必须先包含。
//   这个依赖是隐式的（qdatetime_shim.h 自己没 include 它），所以本文件
//   刻意把它排在最前面，并在下面 qDirEntryInfoList 的用例里再验一次。
//
// ⚠ 用临时目录而非固定路径：测试要真的建/删文件，避免污染仓库，也避免
//   与其他用例并发冲突。Qt3 无 QTemporaryDir，故用 qDirTempPath() +
//   自造唯一名，测完用 qDirRemoveRecursively() 递归删除。
//
// ⚠ 脚手架全部走跨版本 API，不直接调 QDir 成员 —— Qt3 与 Qt6 名字差很多
//   （absFilePath/absoluteFilePath、QDir::current()/currentPath()、entryList
//   的过滤器类型…）。让测试代码自己分叉，会把「测 shim」变成「测两套 QDir」。

#include <qstring.h>
#include <qdir.h>
#include <qfileinfo.h>
#include <qfile.h>
#include <qstringlist.h>
#include <qcstring.h>

#include "doctest/doctest.h"

// ⚠ 顺序敏感：必须早于 qdatetime_shim.h（见文件头说明）
#include "qglobaltype_shim.h"

#include "qdir_shim.h"
#include "qfile_shim.h"
#include "qdatetime_shim.h"
#include "../qlcomp/compatcore34.h"   // qMkdir —— Qt3 无 QDir::mkpath()

// ── 测试脚手架：建一个含已知内容的临时目录，析构时清掉 ─────────────────

namespace {

// 用 qDirTempPath() + 进程内计数器造唯一名。
class TempDir {
public:
    TempDir()
    {
        static int counter = 0;
        m_path = QDir(qDirTempPath()).absFilePath(
            QString::fromLatin1("qlstik_shimtest_") + QString::number(++counter));
        qMkdir(m_path, true);
    }
    ~TempDir()
    {
        qDirRemoveRecursively(QDir(m_path));
    }
    QString path() const { return m_path; }
private:
    QString m_path;
};

void writeFile(const QString& dir, const QString& name, const char* data)
{
    QFile f(QDir(dir).absFilePath(name));
    if (f.open(IO_WriteOnly | IO_Truncate))
        f.writeBlock(data, Q_ULONG(qstrlen(data)));
}

} // namespace

// ── qDirEntryInfoList ──────────────────────────────────────────────────
// 回归点（三条都是实测踩出来的）：
//  1) Qt3 的 entryInfoList 返回 `const QFileInfoList*`，**由调用方 delete**；
//     旧代码在 Qt3 下既不能对它范围迭代（Qt3 QFileInfoList 是指针宏），又泄漏。
//     垫片必须拷贝成值并释放指针。
//  2) Qt3 会把 "." / ".." 也列进来（NoDotAndDotDot 在 Qt3 不存在），
//     垫片刻意不代劳过滤，调用点按名字排除。
//  3) Qt6 返回值列表，无指针、无需释放。

TEST_CASE("qDirEntryInfoList: 返回 QList<QFileInfo> 值列表，元素可直接按索引取")
{
    TempDir td;
    writeFile(td.path(), "a.cpp", "int main(){return 0;}");
    writeFile(td.path(), "b.h", "#pragma once");
    writeFile(td.path(), "c.txt", "hello");

    QDir d(td.path());
    const QList<QFileInfo> infos =
        qDirEntryInfoList(d, QDir::Files | QDir::Readable);

    // 三个普通文件必须都在，且文件名可取（证明不是指针、拷贝到位）
    bool sawA = false, sawB = false, sawC = false;
    for (int i = 0; i < infos.count(); ++i) {
        const QString n = infos[i].fileName();
        if (n == QString("a.cpp")) sawA = true;
        if (n == QString("b.h"))   sawB = true;
        if (n == QString("c.txt")) sawC = true;
    }
    CHECK(sawA);
    CHECK(sawB);
    CHECK(sawC);
}

// ⚠ 回归点：形参类型随版本走（Qt3 是 int，Qt6 是 QDir::Filters 强枚举），
//   见 qdir_shim.h 的 qDirFilter typedef。故调用点写
//   `QDir::Files|QDir::Readable` 两边都不用改 —— 这条用例就是防那个
//   typedef 被改坏（改坏时报 invalid conversion from int to QDir::Filter）。
// ⚠ 注意本函数**只收 filterSpec，没有 nameFilter 形参**（按名过滤是
//   qDirEntryList 的活）。所以这里只能测 Files/Dirs 类别过滤，不能测
//   "*.cpp" —— 最初误以为能按名过滤，写出的断言恒假，已修正。
TEST_CASE("qDirEntryInfoList: filterSpec 类别过滤生效（Files / Dirs 互斥）")
{
    TempDir td;
    writeFile(td.path(), "a.cpp", "x");
    writeFile(td.path(), "b.h", "x");
    qMkdir(QDir(td.path()).absFilePath(QString::fromLatin1("subdir")), true);

    QDir d(td.path());
    // Files：只该有那两个文件，不该混进目录
    QList<QFileInfo> files = qDirEntryInfoList(d, QDir::Files | QDir::Readable);
    CHECK_EQ(files.count(), 2);
    for (int i = 0; i < files.count(); ++i)
        CHECK(!files[i].isDir());

    // Dirs：只该有 subdir（QDir::NoDotAndDotDot 在 Qt3 不存在，Qt3 会把
    // "." / ".." 也列进来 —— 故按名字排除，不断言确切个数）
    QList<QFileInfo> dirs = qDirEntryInfoList(d, QDir::Dirs | QDir::Readable);
    bool sawSub = false;
    for (int i = 0; i < dirs.count(); ++i) {
        const QString n = dirs[i].fileName();
        if (n == QString("subdir")) sawSub = true;
        CHECK(n != QString("a.cpp"));
    }
    CHECK(sawSub);
}

TEST_CASE("qDirEntryInfoList: 空目录不崩；不存在的目录返回空列表")
{
    TempDir td;   // 建了但不放任何文件
    QDir d(td.path());
    const QList<QFileInfo> infos =
        qDirEntryInfoList(d, QDir::Files | QDir::Readable);
    // Qt3 会额外列出 "." / ".."；本垫片不代劳过滤，故只断言「不崩、
    // 且不含我们建的任何文件」，不断言确切个数。
    for (int i = 0; i < infos.count(); ++i)
        CHECK(infos[i].fileName() != QString("a.cpp"));
    // 不存在的目录：Qt3 返回空指针，垫片必须早退而不是解引用空指针
    const QList<QFileInfo> none =
        qDirEntryInfoList(QDir(QString::fromLatin1("/nonexistent_dir_qlstik")),
                          QDir::Files | QDir::Readable);
    CHECK_EQ(none.count(), 0);
}

// ⚠⚠ 回归点：QDir 析构后结果仍有效，且**析构本身不得崩**。
// 2026-10-02 写本文件时，qdir_shim.h 的 Qt3 分支在拷贝完元素后写了
// `delete p;`，注释称「Qt3 由调用方释放」。那是错的：entryInfoList()
// 返回的是 QDir 内部缓存成员 fiList 的地址，QDir 析构时还会再
// `delete fiList`（tools/qdir.cpp:275）→ double free。
// 隔离探针实证：情形A（不 delete，直接析构）exit=0；情形B（delete p
// 后析构）exit=139 SIGSEGV，gdb 帧#0 = QDir::~QDir()。
// 下面两条断言把「QDir 先死、out 仍可用」钉住 —— 正是删掉 delete p
// 之后才成立的性质；一旦有人把 delete p 加回来，本用例会直接 SIGSEGV
// 而不是安静地退化。
TEST_CASE("qDirEntryInfoList: QDir 析构后值列表仍有效（double-free 回归）")
{
    QList<QFileInfo> held;
    {
        TempDir td;
        writeFile(td.path(), "kept.cpp", "x");
        writeFile(td.path(), "kept.h", "x");
        QDir d(td.path());
        held = qDirEntryInfoList(d, QDir::Files | QDir::Readable);
    }   // ← QDir 与临时目录在此析构。旧实现在这里 SIGSEGV
    CHECK_EQ(held.count(), 2);
    bool sawCpp = false, sawH = false;
    for (int i = 0; i < held.count(); ++i) {
        if (held[i].fileName() == QString("kept.cpp")) sawCpp = true;
        if (held[i].fileName() == QString("kept.h"))    sawH = true;
    }
    CHECK(sawCpp);
    CHECK(sawH);
}

// ── qDirEntryList ─────────────────────────────────────────────────────
// 回归点：Qt3 的 entryList 只有单 QString 过滤器，**不支持逗号分隔多通配**
// （实测 entryList("*.cpp,*.h") 命中 0 项，而 "*.cpp" 命中全部）。故垫片
// 只接受单个模式，刻意不提供 QStringList 重载，避免调用点误以为多模式可用。

TEST_CASE("qDirEntryList: 单模式命中；多模式不提供重载（防静默返回空）")
{
    TempDir td;
    writeFile(td.path(), "a.cpp", "x");
    writeFile(td.path(), "b.h", "x");
    writeFile(td.path(), "c.part", "x");

    QDir d(td.path());
    const QStringList cpp = qDirEntryList(d, QString::fromLatin1("*.cpp"),
                                          QDir::Files | QDir::Readable);
    CHECK_EQ(cpp.count(), 1);
    CHECK(cpp[0] == QString("a.cpp"));

    const QStringList part = qDirEntryList(d, QString::fromLatin1("*.part"),
                                           QDir::Files | QDir::Readable);
    CHECK_EQ(part.count(), 1);
    CHECK(part[0] == QString("c.part"));

    // 关键回归：把两个模式塞进一个串，在 Qt3 下必须是**空**（而非静默合并）。
    // 若哪天有人给垫片加了 QStringList 重载，这条会先红，提示语义变了。
    const QStringList multi = qDirEntryList(d, QString::fromLatin1("*.cpp,*.h"),
                                            QDir::Files | QDir::Readable);
    CHECK_EQ(multi.count(), 0);
}

// ── qFileInfoAbsoluteFilePath ─────────────────────────────────────────

TEST_CASE("qFileInfoAbsoluteFilePath: 相对路径被补成绝对路径")
{
    // ⚠ Qt3 的 QDir 只有 currentDirPath()（qdir.h:178），无 currentPath()；
    //   也没有 isAbsPath()，故用「QDir(p).absPath() == p」判绝对。
    const QString rel = QString::fromLatin1("some_rel_file.txt");
    const QString abs = qFileInfoAbsoluteFilePath(QFileInfo(rel));
    CHECK_EQ(QDir(abs).absPath(), abs);
    CHECK_EQ(abs, QDir(QDir::currentDirPath()).absFilePath(rel));
    // 已是绝对路径时原样返回，不重复拼接
    CHECK_EQ(qFileInfoAbsoluteFilePath(QFileInfo(abs)), abs);
}

// ── qDateTimeEpochSecs ───────────────────────────────────────────────
// 回归点：Qt5.8 才有 currentSecsSinceEpoch()，Qt3 最近的是
// QDateTime::toTime_t()（qdatetime.h:200，语义就是 Unix 纪元秒）。
// 返回 qint64 而非 time_t：32 位平台的 time_t 只有 31 位有效位，装不下
// 2038 年后的纪元秒。

TEST_CASE("qDateTimeEpochSecs: 返回合理的 Unix 纪元秒，且两次调用单调不减")
{
    const qint64 t1 = qDateTimeEpochSecs();
    // 2020-01-01 之后、2100 年之前 —— 足以证明不是 0、也不是毫秒/微秒
    CHECK(t1 > 1577836800LL);
    CHECK(t1 < 4102444800LL);
    // 不是毫秒（毫秒会是 1.5e12 量级，已被上面的上界排除）
    const qint64 t2 = qDateTimeEpochSecs();
    CHECK(t2 >= t1);
}
