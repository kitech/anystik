#ifndef QLSTIK_QDIR_SHIM_H
#define QLSTIK_QDIR_SHIM_H

// Qt3 的 QDir / QDirIterator 缺口垫片。Qt6 侧逐项转调原生。
//
// 本头含两部分：
//   A. 自由函数 qDirTempPath / qDirCleanPath / qDirRemoveRecursively /
//      qDirRelativeFilePath —— Qt3 的 QDir 静态与实例方法缺口。
//   B. class QDirIterator —— Qt 4.2 才有的遍历器（由原 qdiriterator_shim.h
//      并入，见文件末尾）。
//
// 命名统一 q 前缀（qDirTempPath 等），与本仓既有范式一致（qAbsPath /
// qMkpath / qFileInfoSuffix）。不用同名宏 —— QDir 的这些成员在 Qt3 上根本
// 不存在，宏替换反而会误伤无关文本；自由函数让调用点自己标明意图。
//
// ⚠ 与 qlcomp/compatcore34.h 无命名冲突（那边只有 qMkdir）。
//
// ⚠ 本仓反复踩过的一个坑，记录在此备查：Qt3 的 entryList/entryInfoList
//   **会列出 "." 与 ".."**（实测 entryInfoList(Files|Dirs|Readable) 前两项
//   即 "." 与 ".."，且 isDir() 皆为真），而 Qt3 没有 Qt4.2 才引入的
//   NoDotAndDotDot 标志。凡按名字遍历子目录，必须手工排除这两项，否则会
//   顺着 ".." 无限向上递归。

#include <qglobal.h>

#ifdef QT3_BUILD
#include <stdlib.h>     // getenv() —— qDirTempPath() 读 $TMPDIR
#include <qstring.h>
#include <qstringlist.h>
#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#else
#include <QString>
#include <QStringList>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#endif

// ── qDirTempPath()：系统临时目录（Qt 4.2 才有 QDir::tempPath）─────────────
// Qt3 完全没有这个 API（实测 /opt/qt338sh/include 全目录 grep 无任何
// tempPath / TMPDIR 符号）。语义对齐 Qt6 的 QDir::tempPath()：
//   * 优先 $TMPDIR，且**必须是绝对路径**（Qt6 源码即如此：相对或空值回退）；
//   * 否则 /tmp；
//   * 去掉结尾斜杠，避免拼出 "//anystik_xxx.gif"（Qt 亦如此）。
//
// ⚠ 这是行为近似而非逐位复刻：Qt 内部还会在 macOS 上走
//   NSTemporaryDirectory()、BSD 上走 confstr(_CS_DARWIN_USER_TEMP_DIR)，
//   并在最后做 exists() 检查。Qt3 侧本就无该 API 可对齐，唯一可选项就是
//   实现 TMPDIR→/tmp 这条 POSIX 通用规则。Windows 下 Qt3 无 TMPDIR 语义，
//   故回退 /tmp 已是该平台可用范围。
inline QString qDirTempPath()
{
#ifdef QT3_BUILD
    QString p;
    if (char* t = ::getenv("TMPDIR")) {           // getenv 需 stdlib.h
        const QString cand = QString::fromLocal8Bit(t);
        // Qt6 QDir::tempPath() 只接受绝对路径；用 isRelativePath() 取反判定
        // （Qt3 只有 isRelativePath，没有 isAbsolutePath —— 实测 qdir.h:185）。
        if (!cand.isEmpty() && !QDir::isRelativePath(cand)) p = cand;
    }
    if (p.isEmpty()) p = QString::fromLatin1("/tmp");
    while (p.length() > 1 && p.endsWith(QChar('/'))) p.truncate(p.length() - 1);
    return p;
#else
    return QDir::tempPath();
#endif
}

// ── qDirCleanPath()：规范化路径（Qt 4.2 才有 QDir::cleanPath）──────────────
// Qt3 无。语义对齐 Qt6 的 QDir::cleanPath()：
//   * 分隔符统一为 '/'；
//   * 折叠重复分隔符 "//" → "/"；
//   * 消除 "./"；
//   * 消除 "a/../"（**不**吞掉根之上的 ".." —— 见下方实测对拍）；
//   * 去掉结尾斜杠，但保留根 "/" 本身。
//
// 用纯字符串逐段处理而非 QDir::cd()：cd() 依赖真实文件系统存在性（无法处理
// 尚不存在的路径）且会调用 getcwd，与 Qt 原生的纯词法处理不同。
//
// 实测对拍（Qt 6.7.3 的 QDir::cleanPath 为基准，6/6 一致）：
//   "/a//b/./c/../d" → "/a/b/d"   "/" → "/"        "a/../.." → ".."
//   "/../a" → "/../a"  "a/b/" → "a/b"   "" → ""
// 注意 "/../a"：Qt6 **保留**根之上的 ".."，不折叠成 "/a"。原先按"根之上丢弃
// .."实现会得到 "/a"，与 Qt 不符，已改为原样保留。
inline QString qDirCleanPath(const QString& path)
{
#ifdef QT3_BUILD
    if (path.isEmpty()) return path;

    const QString norm = QDir::convertSeparators(path);
    const bool absolute = norm.startsWith(QChar('/'));

    // allowEmptyEntries=true：必须保留空段，才能正确折叠 "//"
    const QStringList parts = QStringList::split(QChar('/'), norm, true);
    QStringList out;
    for (int i = 0; i < (int)parts.count(); ++i) {
        const QString seg = parts[i];
        // Qt3 无 QLatin1String（实测 qstring.h），统一用 QString::fromLatin1
        // 与 QString::operator== 比较，避免依赖 Qt4 才有的轻量字符串类。
        if (seg.isEmpty() || seg == QString::fromLatin1(".")) continue;  // 折叠 // 与 ./
        if (seg == QString::fromLatin1("..")) {
            // 仅当栈顶是真实目录名（非 ".."、非根）时才回退一级。
            // 栈空或栈顶仍是 ".." 时**原样保留**该 ".."：这同时覆盖相对
            // 路径的前导 .. 与绝对路径根之上的 ..（实测 Qt6 对 "/../a"
            // 返回 "/../a"，不折叠）。
            if (!out.isEmpty() && out.last() != QString::fromLatin1("..")) {
                // Qt3 QStringList 无 removeLast()（实测 qstringlist.h），用
                // count()-1 取尾再按值 remove()。
                out.remove(out[(int)out.count() - 1]);
                continue;
            }
        }
        out.append(seg);
    }

    QString res = out.join(QString::fromLatin1("/"));
    if (absolute) res.prepend(QString::fromLatin1("/"));
    if (res.isEmpty() && absolute) res = QString::fromLatin1("/");  // 根须仍为 "/"
    return res;
#else
    return QDir::cleanPath(path);
#endif
}

// ── qDirRemoveRecursively()：递归删除目录（Qt 5.0 才有）────────────────────
// Qt3 无。本实现逐行对照 Qt 6.7.3 的 QDir::removeRecursively()
// （qtbase src/corelib/io/qdir.cpp，已联网核对原文），只做必要适配：
//
//   bool QDir::removeRecursively()
//   {
//       if (!d_ptr->exists()) return true;
//       bool success = true;
//       const QString dirPath = path();
//       QDirIterator di(dirPath, QDir::AllEntries|QDir::Hidden|QDir::System
//                               |QDir::NoDotAndDotDot);
//       while (di.hasNext()) {
//           const QFileInfo fi = di.nextFileInfo();
//           const QString &filePath = di.filePath();
//           bool ok;
//           if (fi.isDir() && !fi.isSymLink())
//               ok = QDir(filePath).removeRecursively();   // recursive
//           else {
//               ok = QFile::remove(filePath);
//               if (!ok) { /* Windows 只读文件：加 WriteUser 重试 */ }
//           }
//           if (!ok) success = false;
//       }
//       if (success) success = rmdir(absolutePath());
//       return success;
//   }
//
// 三处必要适配及原因：
//
//  1) NoDotAndDotDot → 手工跳过 "." / ".."。Qt3 的 FilterSpec（qdir.h:61-75）
//     根本没有该标志（Qt4.2 才加），且**其 entryList/entryInfoList 与 Qt6 的
//     AllEntries 一样会列出 "." 和 ".."**（已用探针实测：All 的前两项恰为
//     "." 与 ".."）。若不跳过，递归会顺着 ".." 无限向上、删到目录树之外，
//     实测直接 SIGSEGV。这是本函数最容易写错的一点。
//
//  2) Windows 只读文件重试段落省略。Qt3 连 QFile::Permissions 类型都没有
//     （实测 qfile.h 只有 static bool remove），无法移植；且该分支本就是
//     为 NTFS 只读位准备的，POSIX 下 QFile::remove 不看权限位。
//
//  3) 遍历改用 QDir::entryList()（返回 QStringList **值拷贝**）逐个取名，
//     而非 Qt6 的 QDirIterator，也**刻意避开** QDir::entryInfoList()：后者在
//     Qt3 返回指向 QDir 内部成员的指针（qdir.h:142 `const QFileInfoList*`），
//     而 Qt6 原版是"边迭代边删"，照搬就会在迭代中删除迭代器自身依赖的目录
//     条目 —— 实测两次 SIGSEGV（gdb：QFileInfo::fileName → QString::mid /
//     findRev）。entryList 的 QString 值拷贝与"边遍历边删"天然兼容。
//     代价是每项要用 QFileInfo 重新 stat 一次，性能低于 QDirIterator，
//     但删除操作本身以 fsync 级 IO 为主，这点开销可忽略。
//
// 语义要点（与 Qt6 一致）：目录不存在时返回 true；单个条目删失败不中断，
// 继续删完，最后只有 success 为真才 rmdir 自身。
// 形参按值传递而非 const QDir&：Qt6 的 QDir::removeRecursively() 是**非**
// const 成员（Qt5 起），用 const 引用无法转发（实测 Qt6 侧报 "passing
// 'const QDir' as 'this' argument discards qualifiers"）。QDir 是隐式共享
// 的浅拷贝，按值传递无额外开销。
inline bool qDirRemoveRecursively(QDir dir)
{
#ifdef QT3_BUILD
    if (!QDir(dir.absPath()).exists()) return true;   // 同 Qt6 的 d_ptr->exists()

    bool success = true;
    const QString dirPath = dir.absPath();
    // Qt3 无 AllEntries / NoDotAndDotDot：All(0x007)=Dirs|Files|Drives 与
    // AllEntries 同值，Hidden(0x100) / System(0x200) 在 Qt3 存在（qdir.h:74-75）。
    const QStringList names =
        QDir(dirPath).entryList(QString::fromLatin1("*"),
                                QDir::All | QDir::Hidden | QDir::System);
    for (int i = 0; i < (int)names.count(); ++i) {
        const QString name = names[i];
        if (name == QString::fromLatin1(".") || name == QString::fromLatin1(".."))
            continue;                       // 适配 1)
        const QString filePath = QDir(dirPath).absFilePath(name);
        const QFileInfo fi(filePath);
        bool ok;
        if (fi.isDir() && !fi.isSymLink())
            ok = qDirRemoveRecursively(QDir(filePath));      // recursive
        else
            ok = QFile::remove(filePath);                     // 适配 2)
        if (!ok) success = false;
    }
    if (success)
        success = QDir(dirPath).rmdir(dirPath);   // Qt3 rmdir 第二参 acceptAbsPath 默认 TRUE
    return success;
#else
    return dir.removeRecursively();
#endif
}

// ── qDirRelativeFilePath()：求 file 相对 dir 的路径（Qt 4.2 才有）─────────
// Qt3 无。语义对齐 Qt6 的 QDir::relativeFilePath()。
//
// 实测对拍（Qt 6.7.3 为基准，12/12 一致）得出规则 —— 其中两条与直觉相反，
// 早先按直觉写错了，是靠对拍才发现的：
//   1. **无公共前缀时并非返回 file 原样**，而是照常逐级上跳：
//      QDir("/x/y").relativeFilePath("/a/b") == "../../a/b"（不是 "/a/b"）。
//   2. dir 与 file 相等时返回 **"."**（不是空串）：
//      QDir("/a").relativeFilePath("/a") == "."，
//      QDir("/a/b/c").relativeFilePath("/a/b") == "../"（**带**尾斜杠）。
//
// 算法（与 Qt 行为吻合）：任一方为相对路径 → 原样返回 file；否则
// cleanPath 后取最长公共前缀 i，回退层数 = dir 在 i 之后的非空段数，
// 结果 = "../" × 层数 + file.mid(i+1)（跳过 file[i]，它在 dir 继续时必为 '/'，
// 在双方不等时必为首个分歧字符），结果为空串时用 "."。
inline QString qDirRelativeFilePath(const QDir& dir, const QString& file)
{
#ifdef QT3_BUILD
    const QString rawDir = dir.absPath();
    if (QDir::isRelativePath(rawDir) || QDir::isRelativePath(file))
        return file;                            // 同 Qt：相对路径直接原样返回

    const QString d = qDirCleanPath(QDir::convertSeparators(rawDir));
    const QString f = qDirCleanPath(QDir::convertSeparators(file));

    const int dirSize = (int)d.length();
    const int fileSize = (int)f.length();

    int i = 0;                                  // 最长公共前缀长度
    while (i < dirSize && i < fileSize && d[i] == f[i]) ++i;

    // 回退层数 = dir 在公共前缀之后的非空段数
    const QString dirTail = d.mid(i);
    int levels = 0;
    const QStringList tailParts =
        QStringList::split(QChar('/'), dirTail, false);   // 丢空段即"非空段"
    for (int j = 0; j < (int)tailParts.count(); ++j) ++levels;

    QString res;
    for (int k = 0; k < levels; ++k) res += QString::fromLatin1("../");
    // 公共前缀若正好停在 '/' 处（dir 在此继续下去），须跳过 f[i] 这个 '/'，
    // 否则会拼出 "..//b"（实测 QDir("/").relativeFilePath("/a/b") == "a/b"，
    // 而非 "/b"）。反之若 i 落在两个名字中间（'x' vs 'a'），f[i] 是分歧
    // 字符，须连它一起带上。故按"f[i] 是否为分隔符"分别处理。
    if (i < fileSize && f[i] == QChar('/')) res += f.mid(i + 1);
    else                                     res += f.mid(i);
    if (res.isEmpty()) res = QString::fromLatin1(".");
    return res;
#else
    return dir.relativeFilePath(file);
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// QDirIterator（Qt 4.2 引入，Qt3.5 无此类）
//
// 由原 qdiriterator_shim.h 并入本头：两者同属 QDir 家族、同为一处调用点
// （stickerstore.cpp），合并不引入新依赖，且能让"Qt3 QDir 的所有缺口"集中
// 在一个文件里查阅。原头注释与实现逻辑逐字保留。
//
// 用到的接口仅 ctor + hasNext() + next() + QDirIterator::Subdirectories。
//
// 语义说明：
//   · Qt3 的 QDir 没有 NoDotAndDotDot（qdir.h:61 的 FilterSpec 无此项），
//     且其 entryList 会列出 "." / ".."；本垫片在递归时显式跳过这两个名字。
//     调用点的 `QDir::NoDotAndDotDot` 已在 stickerstore.cpp 侧去掉——对
//     「*.eif / *.EIF」名字过滤而言，"." / ".." 本就不匹配（已探针实测），
//     去掉是行为等价的（Qt6 QDir::Files 也已把目录排除）。
//   · Qt6 的 QDirIterator 是惰性、按文件系统顺序；本垫片构造时一次性递归
//     收集并按 Qt3 QDir 的默认排序（Name|IgnoreCase）。本调用点收集后整体
//     处理，与顺序无关，故等价。
//   · ctor 的 filters 语义：仅当含 QDir::Files 时收集文件；Subdirectories
//     置位时递归子目录。其它 FilterSpec 位（Hidden/NoSymLinks/Readable/…）
//     透传给 Qt3 entryInfoList。
//
// ⚠ 只实现上述用到的形态。Qt6 QDirIterator 还有 fileInfo()/path()/
//   NextItem等以及两参构造，用到再补。
// ═══════════════════════════════════════════════════════════════════════════

#if QT_VERSION < 0x040200

class QDirIterator
{
public:
    enum IteratorFlag {
        NoIteratorFlags = 0x0,
        Subdirectories  = 0x1,
        FollowSymlinks  = 0x2
    };

    QDirIterator(const QString& path,
                 const QStringList& nameFilters,
                 int filters = QDir::All,
                 IteratorFlag flags = NoIteratorFlags)
        : m_idx(0)
    {
        m_nameFilters = nameFilters;
        m_filters = filters;
        collect(path, (flags & Subdirectories) != 0);
    }

    bool hasNext() const { return m_idx < (int)m_files.count(); }

    QString next()
    {
        if (m_idx >= (int)m_files.count()) return QString();
        return m_files[m_idx++];
    }

    QString filePath() const
    {
        if (m_idx >= (int)m_files.count()) return QString();
        return m_files[m_idx];
    }

    QString fileName() const { return QFileInfo(filePath()).fileName(); }

private:
    void collect(const QString& path, bool recurse)
    {
        QDir dir(path);
        if (!dir.exists()) return;

        if (m_filters & QDir::Files) {
            // Qt3 的 nameFilter 用空格或分号分隔多模式；QStringList::join 成空格。
            // 注：Qt3 的 QFileInfoList 是 QPtrList<QFileInfo>（指针列表），
            // 迭代形态与 Qt4+ 不同，故改用 entryList() 取名再自建 QFileInfo。
            const QStringList names =
                dir.entryList(m_nameFilters.join(QString(" ")), m_filters);
            for (uint i = 0; i < names.count(); ++i) {
                QFileInfo fi(dir, names[i]);
                if (fi.isFile()) m_files.append(fi.absFilePath());
            }
        }

        if (recurse) {
            // 递归用 QDir::Dirs 单列子目录名；Qt3 无 NoDotAndDotDot，显式跳过
            // "." / ".."。NoSymLinks 与调用点一致，避免跟随符号链接目录成环。
            const QStringList dirs =
                dir.entryList(QString("*"), QDir::Dirs | QDir::NoSymLinks);
            for (uint i = 0; i < dirs.count(); ++i) {
                const QString name = dirs[i];
                if (name == QString(".") || name == QString("..")) continue;
                collect(dir.absFilePath(name), true);
            }
        }
    }

    QStringList m_nameFilters;
    int         m_filters;
    QStringList m_files;
    int         m_idx;
};

#endif // QT_VERSION < 0x040200

#endif // QLSTIK_QDIR_SHIM_H