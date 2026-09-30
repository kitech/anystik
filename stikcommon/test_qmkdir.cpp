// stikcommon/test_qmkdir.cpp —— qglobaltype_shim.h 契约（Qt3 单端，零网络）
//
// 被测对象为 stikcommon/qglobaltype_shim.h（纯 static inline / 模板，零链接）：
//   qAbsPath()  —— Qt3 无 QFileInfo::absolutePath()，用 QT3_SUPPORT 的 dirPath(true)
//   qMkdir()    —— Qt3 的 QDir 无 mkpath（mkdir 只建一层），逐级建目录
//   qMax/qMin/qBound —— Qt 3.5 的 qglobal.h 里没有（实测 grep 无定义）
//
// ⚠ qMkdir 会真的建目录。用 /tmp 下按 pid 命名的独立根目录，并在用例结束时
//   递归清理（RAII 守卫），避免残留与用例间互相干扰。

#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qstring.h>
#include <qstringlist.h>
#include <stdlib.h>
#include <unistd.h>

#include "doctest/doctest.h"

#include "qglobaltype_shim.h"

// 取临时目录：Qt3 的 QDir 只有 static homePath()/rootPath()，**无** tempPath()
// （实测 grep qdir.h 无该声明，Qt 4.2 才加）。按 POSIX 约定取 TMPDIR，未设则 /tmp
// —— 与 davbisync.cpp:173-186 的 qTempPath() 同一约定（那里是文件内 inline，
// 测试用不了，故此处按同样规则就地实现）。
static QString tempRootBase()
{
    const char* const td = ::getenv("TMPDIR");
    if (td && *td) {
        return QString(QCString(td));
    }
    return QString::fromLatin1("/tmp");
}

// ── 测试脚手架：每次一个独立目录根，析构时递归删除 ──────────────────
class TempRoot
{
public:
    TempRoot()
    {
        m_root = tempRootBase() + QString("/stikcommon-mkdir-")
                 + QString::number((long)getpid());
        removeTree(m_root);
        CHECK(qMkdir(m_root));
    }
    ~TempRoot() { removeTree(m_root); }

    const QString& path() const { return m_root; }
    QString sub(const QString& rel) const { return m_root + QString("/") + rel; }

    static void removeTree(const QString& path)
    {
        QDir d(path);
        if (!d.exists()) {
            return;
        }
        // ⚠⚠ Qt3 的 QDir::entryList() **会把 "." 与 ".." 也列出来**（Qt4+ 才有
        //   FilterSpec/NoDotAndDotDot 那套过滤）。必须显式跳过，否则 "." 会被
        //   QFileInfo 判成目录而无限递归，路径涨成 path/./././... 直到挂死
        //   （本文件最初就栽在这：整套 qMkdir 用例断言全过却永不退出，
        //     经 /tmp/opencode/rmprobe.cpp 隔离定位）。
        const QStringList kids = d.entryList(QDir::All | QDir::Hidden | QDir::System);
        for (int i = 0; i < kids.size(); ++i) {
            const QString name = kids[i];
            if (name == "." || name == "..") {
                continue;
            }
            const QString p = path + QString("/") + name;
            if (QFileInfo(p).isDir() && !QFileInfo(p).isSymLink()) {
                removeTree(p);
            } else {
                QFile::remove(p);
            }
        }
        QDir().rmdir(path);
    }

private:
    QString m_root;
};

// ── qAbsPath ────────────────────────────────────────────────────────

TEST_CASE("qAbsPath: 等价于 Qt4+ 的 QFileInfo::absolutePath()")
{
    // 绝对路径：取父目录，不含文件名
    CHECK(qAbsPath(QFileInfo(QString("/a/b/c.txt"))) == QString("/a/b"));
    CHECK(qAbsPath(QFileInfo(QString("/a/c.txt"))) == QString("/a"));
    // ⚠ 目录路径的实测行为：Qt3 的 dirPath(true) 对 "/a/b/"（带尾斜杠）返回
    //   **"/a"** 而非 "/a/b"。这是 dirPath 把尾斜杠后的 "b/" 视作文件名所致。
    //   此处按实测值锁定，不臆断 Qt4+ absolutePath() 的结果（未在本环境比对）。
    CHECK(qAbsPath(QFileInfo(QString("/a/b/"))) == QString("/a"));
    // 相对路径：dirPath(true) 会按当前目录补全，故结果应是绝对路径
    // Qt3 的 QFileInfo 无 isAbsolute()（grep qfileinfo.h 只有 isRelative()，:105），
    // 故用 !isRelative() 表达「是绝对路径」
    const QString rel = qAbsPath(QFileInfo(QString("relfile.txt")));
    CHECK(QFileInfo(rel).isRelative() == false);
    // 不存在的路径同样能算（absolutePath 是纯字符串操作，不碰文件系统）
    CHECK(qAbsPath(QFileInfo(QString("/no/such/dir/f.txt"))) == QString("/no/such/dir"));
}

// ── qMkdir ──────────────────────────────────────────────────────────

TEST_CASE("qMkdir: 逐级建目录（Qt3 QDir::mkdir 只能建一层）")
{
    TempRoot t;
    const QString deep = t.sub(QString("a/b/c/d"));
    CHECK(qMkdir(deep));
    // 每一级都得真的存在
    CHECK(QFileInfo(t.sub(QString("a"))).isDir());
    CHECK(QFileInfo(t.sub(QString("a/b"))).isDir());
    CHECK(QFileInfo(t.sub(QString("a/b/c"))).isDir());
    CHECK(QFileInfo(t.sub(QString("a/b/c/d"))).isDir());
}

TEST_CASE("qMkdir: 已存在返回 true（mkpath 语义，与 Qt3 QDir::mkdir 相反）")
{
    TempRoot t;
    const QString deep = t.sub(QString("x/y"));
    CHECK(qMkdir(deep));
    // 关键差异：Qt3 的 QDir::mkdir 对「已存在」返回 false，本函数承诺 true。
    // qcabundle 反复激活时依赖这一点。
    CHECK(QDir().mkdir(t.sub(QString("x/y"))) == false);   // 基类语义：false
    CHECK(qMkdir(deep));                                   // 垫片语义：true
    // 单级已存在也 true
    CHECK(qMkdir(t.sub(QString("x"))));
}

TEST_CASE("qMkdir: 尾斜杠被剥掉，不影响结果")
{
    TempRoot t;
    CHECK(qMkdir(t.sub(QString("p/q/"))));
    CHECK(QFileInfo(t.sub(QString("p/q"))).isDir());
    // 多个尾斜杠
    CHECK(qMkdir(t.sub(QString("r/s///"))));
    CHECK(QFileInfo(t.sub(QString("r/s"))).isDir());
}

TEST_CASE("qMkdir: 空路径与全斜杠返回 false")
{
    CHECK(qMkdir(QString("")) == false);
    CHECK(qMkdir(QString("/")) == false);
    CHECK(qMkdir(QString("///")) == false);
}

TEST_CASE("qMkdir: 父级是普通文件时返回 false（不抛异常、不错删文件）")
{
    TempRoot t;
    // 造一个普通文件当父级
    const QString file = t.sub(QString("plain.txt"));
    QFile f(file);
    // ⚠ Qt3 的打开模式不是作用域枚举，而是 qiodevice.h:64-69 的 IO_* 宏；
    //   写入只有 writeBlock()（qiodevice.h:135/137），无 Qt4+ 的 write()。
    CHECK(f.open(IO_WriteOnly));
    f.writeBlock("x", 1);
    f.close();
    // 在文件底下建子目录必然失败
    CHECK(qMkdir(file + QString("/child")) == false);
    // 原文件不得被删掉
    CHECK(QFileInfo(file).isFile());
    CHECK_EQ(QFileInfo(file).size(), 1);
}

// ── qMax / qMin / qBound ────────────────────────────────────────────

TEST_CASE("qMax/qMin: 语义与 Qt4+ 一致，注意形参顺序")
{
    CHECK_EQ(qMax(1, 2), 2);
    CHECK_EQ(qMax(2, 1), 2);
    CHECK_EQ(qMax(3, 3), 3);
    CHECK_EQ(qMin(1, 2), 1);
    CHECK_EQ(qMin(2, 1), 1);
    CHECK_EQ(qMin(3, 3), 3);
    // 负数
    CHECK_EQ(qMax(-1, -2), -1);
    CHECK_EQ(qMin(-1, -2), -2);
    // 返回 const 引用，可直接绑定
    const int r = qMax(4, 9);
    CHECK_EQ(r, 9);
}

TEST_CASE("qBound: 把值夹在 [lo, hi]，形参顺序是 (val, lo, hi)")
{
    CHECK_EQ(qBound(5, 1, 10), 5);      // 区间内
    CHECK_EQ(qBound(0, 1, 10), 1);      // 低于下界
    CHECK_EQ(qBound(99, 1, 10), 10);    // 高于上界
    CHECK_EQ(qBound(1, 1, 10), 1);      // 等于下界
    CHECK_EQ(qBound(10, 1, 10), 10);    // 等于上界
    // 负区间
    CHECK_EQ(qBound(-5, -3, -1), -3);
    // ⚠ lo > hi 的退化输入：实现是 `(val < lo) ? lo : ((hi < val) ? hi : val)`，
    //   5 < 10 成立便直接返回 lo=10，**根本走不到 hi 分支**。
    //   故此处只锁定实测行为；它与 Qt4+ qBound 对 max<min 的处理是否一致，
    //   本环境无 Qt4+ 可比对，不下结论。
    CHECK_EQ(qBound(5, 10, 1), 10);
}

TEST_CASE("qreal: Qt3 垫片补的类型别名可用（imagetmpuploader 进度百分比用）")
{
    const qreal half = 0.5;
    CHECK(half > 0.4);
    CHECK(half < 0.6);
    // 可与 double 互转
    const double d = half;
    CHECK(d > 0.4);
    CHECK_EQ((int)(half * 100), 50);
}
