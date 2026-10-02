// stikcommon/test_qstandardpaths_shim.cpp —— QStandardPaths 垫片契约（Qt3 单端）
//
// 被测对象：qstandardpaths_shim.h/.cpp —— 只实现 AppLocalDataLocation 一个枚举，
//   其余枚举全部返回 QString()（qstandardpaths_shim.cpp:37-39）。
//   产品 10 处引用；数据目录算错 = 缓存/设置写到别处 = 静默丢数据。
//
// 解析链（qstandardpaths_shim.cpp:36-73）：
//   1) base = $XDG_DATA_HOME，仅当它是**绝对路径**时采纳；
//      未设 / 空串 / 相对路径 → 回退 $HOME/.local/share
//   2) 追加组织名（非空才加）—— Qt3 侧 organizationName() 恒空，故这层不出现
//   3) 追加应用名（非空则用 applicationName()，否则回退可执行名）
//
// ⚠⚠ 关于第 3 层与测试写法（2026-10 实测教训）：
//   本套件的 run_tests 是**单个进程**，而 test_qclipboard_shim.cpp 会惰性建一个
//   进程级 QApplication 单例（见该文件 ensureApp()）。它在链接顺序上排在本文件
//   之前，故**全量跑时 qApp 非空**，applicationName()/可执行名会给 base 追加
//   一层应用名；而单独过滤跑本文件时 qApp 为空、不追加。
//   因此本文件的断言一律采用「**base 前缀**」而非「与 base 全等」——
//   这样无论 qApp 在不在都成立，不随执行顺序变成 flaky。
//
// ⚠ 真正有风险、必须钉死的是**第 1 层的三条回退分支**（XDG 设/不设/空/相对），
//   它们全在本文件覆盖；第 2/3 层只是可有可无的后缀。
//
// ⚠ include 顺序：qglobaltype_shim.h 必须早于本头。

#include <qstring.h>
#include <qstringlist.h>
#include <qdir.h>
#include <qfile.h>        // QFile::exists / remove（建目录端到端用例要用）
#include <stdlib.h>
#include <unistd.h>

#include "doctest/doctest.h"

#include "qglobaltype_shim.h"
#include "qstandardpaths_shim.h"

#include "../qlcomp/compatcore34.h"   // qMkdir(path, recursive)

// 环境变量作用域守卫：保存旧值，析构恢复（不存在则 unsetenv）。
class EnvScope {
public:
    explicit EnvScope(const char* name) : m_name(name)
    {
        const char* old = getenv(name);
        m_had = (old != 0);
        if (m_had) m_old = QString::fromUtf8(old);
    }
    ~EnvScope() { restore(); }
    void set(const char* value) const
    {
        if (value) setenv(m_name, value, 1);
        else      unsetenv(m_name);
    }
    void restore()
    {
        // ⚠ Qt3 的 QString 没有 toUtf8()（那是 Qt4+ 的），非 ASCII 用 utf8()。
        //   临时 QByteArray 活到本表达式结束，setenv 已复制走内容，安全。
        if (m_had) setenv(m_name, m_old.utf8().data(), 1);
        else       unsetenv(m_name);
    }
private:
    const char* m_name;
    bool m_had;
    QString m_old;
};

// 测试用固定 HOME，全程不依赖调用者真实 HOME
static const char* kFakeHome = "/tmp/qlstik_qsp_fakehome";

// base 前缀判定：结果可以多出一层应用名后缀（见文件头 qApp 说明），
// 但必须以 base 本身或 base + "/" 开头。
static bool basePrefixOk(const QString& got, const QString& base)
{
    return got == base || got.startsWith(base + QString::fromLatin1("/"));
}

TEST_CASE("QStandardPaths: 未实现的枚举一律返回空串（不得给假路径）")
{
    // ⚠ 逐个枚举验证，而不是挑一两个 —— 未实现枚举若误走通用分支，调用方会
    //   拿到一个看起来合法、实际为空的根目录去建目录。
    // ⚠ 只能列本垫片**真实定义**的枚举（qstandardpaths_shim.h:11-23）。
    // ⚠ 一律用 .isEmpty() 而不是 CHECK_EQ(..., QString())：后者在断言失败时
    //   要把 QString 交给 doctest 打印，而 null QString 会被转成 null
    //   const char*，直接把测试进程打成 SIGSEGV —— 失败现场就没了
    //   （test_qfile_shim.cpp 的 completeBaseName 用例真踩过这个坑）。
    CHECK(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation).isEmpty());
    CHECK(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation).isEmpty());
    CHECK(QStandardPaths::writableLocation(QStandardPaths::FontsLocation).isEmpty());
    CHECK(QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation).isEmpty());
    CHECK(QStandardPaths::writableLocation(QStandardPaths::MusicLocation).isEmpty());
    CHECK(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation).isEmpty());
    CHECK(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation).isEmpty());
    CHECK(QStandardPaths::writableLocation(QStandardPaths::TempLocation).isEmpty());
    CHECK(QStandardPaths::writableLocation(QStandardPaths::HomeLocation).isEmpty());
}

// ── XDG_DATA_HOME 采纳分支 ──────────────────────────────────────────────

TEST_CASE("QStandardPaths: XDG_DATA_HOME 为绝对路径时采纳它作为 base")
{
    EnvScope env("XDG_DATA_HOME");
    EnvScope home("HOME");
    env.set("/tmp/qlstik_qsp_xdg");
    home.set(kFakeHome);

    const QString got =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    CHECK(basePrefixOk(got, QString::fromLatin1("/tmp/qlstik_qsp_xdg")));
}

// ⚠ 尾部多一个斜杠不应该导致双斜杠：调用方会把它直接拼进路径。
//   （有 qApp 时会在其后追加 "/<app>"，故只看是否出现 "//"。）
TEST_CASE("QStandardPaths: XDG_DATA_HOME 尾部斜杠不产生双斜杠")
{
    EnvScope env("XDG_DATA_HOME");
    EnvScope home("HOME");
    env.set("/tmp/qlstik_qsp_xdg/");
    home.set(kFakeHome);

    const QString got =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    CHECK(!got.contains(QString::fromLatin1("//")));
}

// ── 三条回退分支 ───────────────────────────────────────────────────────

// ⚠⚠ 最关键的一条：XDG 未设时必须回退 $HOME/.local/share。
//   若漏了回退而返回空串，调用方 mkdir("") 失败，数据目录永远建不出来，
//   而 QDir::mkpath 对空串是**静默不报错**的。
TEST_CASE("QStandardPaths: XDG 未设 → base 回退 $HOME/.local/share")
{
    EnvScope env("XDG_DATA_HOME");
    EnvScope home("HOME");
    env.set(0);                                 // 明确置为未设
    home.set(kFakeHome);

    CHECK(basePrefixOk(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation),
        QString::fromLatin1("/tmp/qlstik_qsp_fakehome/.local/share")));
}

// ⚠ 空串 ≠ 未设：Qt5/6 实测两者都回退，故空串也必须回退而不是产出 "/xxx"。
TEST_CASE("QStandardPaths: XDG 为空串 → 同样回退（空串不是有效路径）")
{
    EnvScope env("XDG_DATA_HOME");
    EnvScope home("HOME");
    env.set("");                                // 存在但为空
    home.set(kFakeHome);

    CHECK(basePrefixOk(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation),
        QString::fromLatin1("/tmp/qlstik_qsp_fakehome/.local/share")));
}

// ⚠⚠ 相对路径必须被拒（Qt5/6 实测如此）：XDG 标准要求绝对路径。
//   若误采纳相对路径，结果会依赖**进程当前工作目录** —— 用户从别的目录启动
//   程序就会写到另一个地方，且完全静默。这是本文件最隐蔽的一个坑。
TEST_CASE("QStandardPaths: XDG 为相对路径 → 拒绝并回退，不含该相对片段")
{
    EnvScope env("XDG_DATA_HOME");
    EnvScope home("HOME");
    env.set("relative/dir");
    home.set(kFakeHome);

    const QString got =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    CHECK(basePrefixOk(got,
        QString::fromLatin1("/tmp/qlstik_qsp_fakehome/.local/share")));
    CHECK(!got.contains(QString::fromLatin1("relative")));
}

// ⚠ HOME 也空时落到当前目录下的 .local/share（Qt5/6 实测行为），而不是空串。
//   此时 base 是**相对路径**（".local/share"），可再跟一层应用名。
TEST_CASE("QStandardPaths: HOME 为空且 XDG 不可用 → base 以 .local/share 起")
{
    EnvScope env("XDG_DATA_HOME");
    EnvScope home("HOME");
    env.set(0);
    home.set("");

    const QString got =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    CHECK(!got.isEmpty());
    CHECK(got.startsWith(QString::fromLatin1(".local/share")));
}

// ⚠ 结果必须始终非空，且能直接拿去建目录。端到端验证一遍「拿来就能用」。
//   ⚠ Qt3 的 QDir **没有** mkpath 成员（那是 Qt4.1+），统一用 qMkdir(path, true)
//   —— 它就是本仓用来抹平该差异的 helper（qlcomp/compatcore34.h:57）。
TEST_CASE("QStandardPaths: 结果可直接用于建多级目录（端到端可用）")
{
    EnvScope env("XDG_DATA_HOME");
    EnvScope home("HOME");
    // ⚠ 路径必须每次运行唯一：qMkdir 对已存在目录返回 false，会让 REQUIRE
    //   在第二次运行时假失败（上次崩在中途就会留残骸）。
    const QString uniq = QString::fromLatin1("/tmp/qlstik_qsp_mk_")
                       + QString::number(int(getpid()));
    env.set(uniq.utf8().data());
    home.set(kFakeHome);

    const QString got =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString nested = got + QString::fromLatin1("/a/b");
    REQUIRE(qMkdir(nested, true));
    CHECK(QFile::exists(nested));

    // 清理（由内向外；got/a/b → got/a → got）
    QDir().rmdir(nested);
    QDir().rmdir(got + QString::fromLatin1("/a"));
    QDir().rmdir(got);
}
