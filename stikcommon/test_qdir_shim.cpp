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

// ═══════════════════════════════════════════════════════════════════════════
// qDirCleanPath —— 路径规范化（Qt3 无 QDir::cleanPath 的成员语义差异）
// ═══════════════════════════════════════════════════════════════════════════
// 头注释（qdir_shim.h:95-133）说明实现要点：保留空段以折叠 "//"、"." 直接丢、
// ".." 仅当栈顶是**真实目录名**时回退，否则原样保留。规则里最关键的一条是
// 「根之上、或相对路径前导的 .. 不折叠」——这既是安全边界（不能让 "../.."
// 在服务器侧被解析成仓库外路径），也是与 Qt6 对齐的行为。
//
// ⚠ 全部用绝对路径字面量或纯相对路径，不依赖 cwd。

TEST_CASE("qDirCleanPath: 折叠双斜杠与 './'")
{
    CHECK_EQ(qDirCleanPath(QString::fromLatin1("a//b")), QString::fromLatin1("a/b"));
    CHECK_EQ(qDirCleanPath(QString::fromLatin1("a/./b")), QString::fromLatin1("a/b"));
    CHECK_EQ(qDirCleanPath(QString::fromLatin1("a/b/./")), QString::fromLatin1("a/b"));
    CHECK_EQ(qDirCleanPath(QString::fromLatin1("/a//b/./c")), QString::fromLatin1("/a/b/c"));
}

TEST_CASE("qDirCleanPath: '..' 回退真实目录名，不跨越根或前导 ..")
{
    // 正常回退
    CHECK_EQ(qDirCleanPath(QString::fromLatin1("a/b/..")), QString::fromLatin1("a"));
    CHECK_EQ(qDirCleanPath(QString::fromLatin1("a/../b")), QString::fromLatin1("b"));
    CHECK_EQ(qDirCleanPath(QString::fromLatin1("/a/b/../c")), QString::fromLatin1("/a/c"));
}

// ⚠⚠ 关键安全/对齐点：前导 ".." 必须原样保留。头注释给了**实测 Qt6 基准**：
//   QDir::cleanPath("/../a") == "/../a"（不把根的 ".." 折叠）。
//   若这里折叠掉，Qt3 侧会把「上一级」静默变成「根」，路径语义被改写。
TEST_CASE("qDirCleanPath: 前导 .. 与根之上的 .. 原样保留（Qt6 实测基准）")
{
    CHECK_EQ(qDirCleanPath(QString::fromLatin1("../a")), QString::fromLatin1("../a"));
    CHECK_EQ(qDirCleanPath(QString::fromLatin1("../../a")), QString::fromLatin1("../../a"));
    CHECK_EQ(qDirCleanPath(QString::fromLatin1("/../a")), QString::fromLatin1("/../a"));
}

TEST_CASE("qDirCleanPath: 边界——空串、根、纯点号")
{
    CHECK(qDirCleanPath(QString()).isEmpty());
    CHECK_EQ(qDirCleanPath(QString::fromLatin1("/")), QString::fromLatin1("/"));
    CHECK_EQ(qDirCleanPath(QString::fromLatin1("/.")), QString::fromLatin1("/"));
    CHECK(qDirCleanPath(QString::fromLatin1(".")).isEmpty());
}

// ═══════════════════════════════════════════════════════════════════════════
// qDirAbsolutePath —— Qt3 absPath() / Qt6 absolutePath() 的统一入口
// ═══════════════════════════════════════════════════════════════════════════
// 回归点：Qt3 成员叫 absPath，Qt6 叫 absolutePath。调用点不该写版本分支。

TEST_CASE("qDirAbsolutePath: 绝对路径目录原样返回")
{
    const QDir d(QString::fromLatin1("/a/b/c"));
    CHECK_EQ(qDirAbsolutePath(d), QString::fromLatin1("/a/b/c"));
}

// ⚠ 相对路径目录必须被补成绝对（基于 cwd），且以 '/' 开头 —— 证明它确实做了
//   absolute 化，而不只是把入参回显。
TEST_CASE("qDirAbsolutePath: 相对路径目录被绝对化")
{
    const QDir d(QString::fromLatin1("rel/dir"));
    const QString ap = qDirAbsolutePath(d);
    CHECK(!ap.isEmpty());
    CHECK(ap.startsWith(QChar('/')));
    CHECK(ap.endsWith(QString::fromLatin1("/rel/dir")));
}

// ═══════════════════════════════════════════════════════════════════════════
// qDirRelativeFilePath —— 求 file 相对 dir 的路径
// ═══════════════════════════════════════════════════════════════════════════
// ⚠⚠ 头注释（qdir_shim.h:233-247）明确记录：两条与直觉相反的规则是靠与
//   Qt6.7.3 **12/12 对拍**才发现的，早先按直觉写错过。本组用例就是把这 12
//   条里的代表情形钉死，防止有人再「按直觉改回去」：
//     1) 无公共前缀**不是**返回 file 原样，而是照常逐级上跳；
//     2) dir 与 file 相等返回 "."（不是空串），且带尾斜杠的情形也存在。

TEST_CASE("qDirRelativeFilePath: 同根下的常规相对化")
{
    CHECK_EQ(qDirRelativeFilePath(QDir(QString::fromLatin1("/a")),
                                  QString::fromLatin1("/a/b")),
             QString::fromLatin1("b"));
    CHECK_EQ(qDirRelativeFilePath(QDir(QString::fromLatin1("/a/b")),
                                  QString::fromLatin1("/a/b/c/d")),
             QString::fromLatin1("c/d"));
}

// ⚠ 与直觉相反之一：无公共前缀要逐级上跳，而非原样返回。
TEST_CASE("qDirRelativeFilePath: 无公共前缀时逐级上跳（非原样返回）")
{
    CHECK_EQ(qDirRelativeFilePath(QDir(QString::fromLatin1("/x/y")),
                                  QString::fromLatin1("/a/b")),
             QString::fromLatin1("../../a/b"));
}

// ⚠ 与直觉相反之二：相等返回 "."（不是空串）；/a/b/c 相对 /a/b 是 "../"（带尾斜杠）。
TEST_CASE("qDirRelativeFilePath: 相等返回 '.'，子目录回退带尾斜杠")
{
    CHECK_EQ(qDirRelativeFilePath(QDir(QString::fromLatin1("/a")),
                                  QString::fromLatin1("/a")),
             QString::fromLatin1("."));
    CHECK_EQ(qDirRelativeFilePath(QDir(QString::fromLatin1("/a/b/c")),
                                  QString::fromLatin1("/a/b")),
             QString::fromLatin1("../"));
}

// ⚠ 根目录基准：QDir("/").relativeFilePath("/a/b") == "a/b"（不得拼出 "/b"）。
//   这条专门防「公共前缀停在 '/' 处没跳过 f[i]」而多出一个斜杠。
TEST_CASE("qDirRelativeFilePath: 根目录为基准时不以 '/' 开头")
{
    CHECK_EQ(qDirRelativeFilePath(QDir(QString::fromLatin1("/")),
                                  QString::fromLatin1("/a/b")),
             QString::fromLatin1("a/b"));
}

// ⚠⚠ 「file 为相对路径 → 原样返回 file」这条成立，但**只对 file 成立**。
//   我原先写成「任一方为相对路径」，被 Qt 6.7.3 实测推翻（探针记录）：
//       QDir("rel").relativeFilePath("/a/b") → "../../../../../a/b"
//   即 dir 为相对路径时 Qt6 **不**原样返回，而是给一串随当前工作目录深度变化的
//   "../"。本仓垫片走 dir.absPath()（恒绝对）故层数比 Qt6 多一级，两边都不是
//   「原样返回」，但具体层数与 CWD 绑定，不构成可断言的契约。
//   故拆成两条：file 相对 → 原样返回（钉死）；dir 相对 → 只断言「非原样 + 结尾正确」。
TEST_CASE("qDirRelativeFilePath: file 为相对路径时原样返回 file")
{
    CHECK_EQ(qDirRelativeFilePath(QDir(QString::fromLatin1("/a")),
                                  QString::fromLatin1("rel/x")),
             QString::fromLatin1("rel/x"));
    CHECK_EQ(qDirRelativeFilePath(QDir(QString::fromLatin1("/a/b/c")),
                                  QString::fromLatin1("d/e")),
             QString::fromLatin1("d/e"));
}

TEST_CASE("qDirRelativeFilePath: dir 为相对路径时不原样返回，且结尾正确")
{
    const QString file = QString::fromLatin1("/a/b");
    const QString got =
        qDirRelativeFilePath(QDir(QString::fromLatin1("rel")), file);
    CHECK(got != file);                                        // 不原样返回
    CHECK(got.startsWith(QString::fromLatin1("..")));
    CHECK(got.endsWith(QString::fromLatin1("a/b")));          // 结尾仍是目标路径
}

// ═══════════════════════════════════════════════════════════════════════════
// CJK（中文）路径
// ═══════════════════════════════════════════════════════════════════════════
//
// ⚠⚠ 这组是本文件此前最大的盲区：所有用例都是 ASCII，而本仓真实的目录名与
//   文件名全是中文（站点目录、贴纸名）。中文一旦在某一环被当 Latin-1 处理，
//   症状是「找不到文件」或建出乱码目录，**不报任何错**。
//
// ⚠ 构造一律 QString::fromUtf8：Qt3 的 QString(const char*) 走 Latin-1
//   （中文会被展开成多个高位字符），不得用它；latin1()/toLatin1() 同禁。

TEST_CASE("qDir 系列: CJK 路径的清理/绝对化/相对化都不得改动中文字节")
{
    // cleanPath：折叠 . 与 ..，但中文段原样保留
    CHECK(qDirCleanPath(QString::fromUtf8("/tmp/站点/./子目录/../贴纸.png"))
          == QString::fromUtf8("/tmp/站点/贴纸.png"));
    // 连续斜杠 + 中文
    CHECK(qDirCleanPath(QString::fromUtf8("/tmp//站点///贴纸.png"))
          == QString::fromUtf8("/tmp/站点/贴纸.png"));
    // 绝对化：以 CWD 为基准补前缀，中文尾段不变
    const QString ap = qDirAbsolutePath(QDir(QString::fromUtf8("站点/贴纸.png")));
    CHECK(ap.startsWith(QChar('/')));
    CHECK(ap.endsWith(QString::fromUtf8("/站点/贴纸.png")));
    // 相对化：中文文件名 + 一级上跳
    CHECK(qDirRelativeFilePath(QDir(QString::fromUtf8("/tmp/站点/子目录")),
                               QString::fromUtf8("/tmp/站点/贴纸.png"))
          == QString::fromUtf8("../贴纸.png"));
    // 相对化：中文目录逐级上跳
    CHECK(qDirRelativeFilePath(QDir(QString::fromUtf8("/tmp/站点/子/孙")),
                               QString::fromUtf8("/tmp/站点/贴纸.png"))
          == QString::fromUtf8("../../贴纸.png"));
    // 码点自检：期望值里的中文确实是 2 个 QChar（守住守门人）
    const QString zhan = QString::fromUtf8("站点");
    CHECK_EQ(zhan.length(), 2);
    CHECK(zhan.at(0) == QChar(0x7AD9));
    CHECK(zhan.at(1) == QChar(0x70B9));
}

TEST_CASE("qDirEntryList/qDirEntryInfoList: 真实文件系统里的中文名不得乱码或漏项")
{
    TempDir td;
    const QString subDir = QString::fromUtf8("子目录");
    const QString cjkName = QString::fromUtf8("贴纸.png");
    const QString asciiName = QString::fromLatin1("plain.txt");

    REQUIRE(qMkdir(QDir(td.path()).absFilePath(subDir), true));
    writeFile(td.path(), cjkName, "cjk");
    writeFile(td.path(), asciiName, "ascii");
    // 中文子目录里也放一个中文名文件（entryInfoList 那段要列它）
    writeFile(QDir(td.path()).absFilePath(subDir), cjkName, "nested");

    // ⚠ Qt3 的 entryList 会把 "." 与 ".." 也列进来（NoDotAndDotDot 不存在），
    //   故这里用 qDirEntryList（垫片层）并按名字过滤，而不是按下标取。
    const QStringList files =
        qDirEntryList(QDir(td.path()), QString::fromLatin1("*"), QDir::Files);
    CHECK_EQ(files.count(), 2);
    bool sawCjk = false, sawAscii = false;
    for (int i = 0; i < files.count(); ++i) {
        if (files[i] == cjkName) sawCjk = true;
        if (files[i] == asciiName) sawAscii = true;
    }
    CHECK(sawCjk);                     // 中文名原样回来，不是乱码
    CHECK(sawAscii);

    // 中文目录名也要能被列出来（QDir::Dirs）
    const QStringList dirs =
        qDirEntryList(QDir(td.path()), QString::fromLatin1("*"), QDir::Dirs);
    bool sawSub = false;
    for (int i = 0; i < dirs.count(); ++i) {
        if (dirs[i] == subDir) sawSub = true;
    }
    CHECK(sawSub);

    // entryInfoList 路径：中文目录下的中文文件，fileName 不得丢字
    const QList<QFileInfo> infos =
        qDirEntryInfoList(QDir(QDir(td.path()).absFilePath(subDir)),
                          QDir::Files);
    CHECK_EQ(infos.count(), 1);
    CHECK(qFileInfoPath(infos[0]).endsWith(subDir));   // 所在目录名含中文
    CHECK(qFileInfoAbsoluteFilePath(infos[0]).endsWith(cjkName));
}

TEST_CASE("qMkdir: 建中文目录名，且嵌套后每层都真实存在")
{
    TempDir td;
    const QString nested = QString::fromUtf8("站点/贴纸/表情");
    REQUIRE(qMkdir(QDir(td.path()).absFilePath(nested), true));
    CHECK(QFile::exists(QDir(td.path()).absFilePath(QString::fromUtf8("站点"))));
    CHECK(QFile::exists(QDir(td.path()).absFilePath(QString::fromUtf8("站点/贴纸"))));
    CHECK(QFile::exists(QDir(td.path()).absFilePath(nested)));
}
