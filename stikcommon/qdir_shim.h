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
// ⚠ 必须自己引 qlist_shim.h：本头的 qDirEntryInfoList() 返回 QList<QFileInfo>
//   并用 `out << **it` 追加，若调用方没先引 shim，Qt3 的 QList 仍是
//   qptrlist.h:189 的 `#define QList QPtrList` 指针宏，于是报
//   "no match for operator<< (QPtrList<QFileInfo>, QFileInfo)"。
//   之前该依赖是隐式的（只有 stickerstore.cpp 恰好先引了），写测试时暴露。
//   ⚠ 顺序：本头解析时 Qt 原生 QList 宏必须**先**占住，故 shim 放在
//     qdir.h 之后 —— qlist_shim.h 内部会 include qptrlist.h 占 guard 并 #undef。
#include "qlist_shim.h"
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

// ── qDirAbsolutePath()：QDir::absolutePath()（Qt4.0 才有）──────────────────// Qt3 只有 absPath()（实测 qdir.h:104），语义一致：返回绝对路径，不含冗余
// "." / ".." / 多余分隔符，但**可能含符号链接**（canonicalPath 才会解析）。
inline QString qDirAbsolutePath(const QDir& dir)
{
#ifdef QT3_BUILD
    return dir.absPath();
#else
    return dir.absolutePath();
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
// ── qDirEntryInfoList()：统一 entryInfoList 的返回类型 ─────────────────
// ⚠ 两侧返回类型不同，Qt3 侧是指针、Qt6 侧是值列表：
//   Qt3  const QFileInfoList* entryInfoList(...)   —— QFileInfoList 就是
//        typedef QPtrList<QFileInfo>（qdir.h:52），存的是指针；
//   Qt6  QFileInfoList entryInfoList(...)           —— QList<QFileInfo>，值列表。
// 直接 `const auto infoList = dir.entryInfoList(...)` 在 Qt3 下会把**指针**
// 存进 auto，于是 `for (const auto& fi : infoList)` 迭代的是指针 →
// "begin/end was not declared in this scope"。
// 故本垫片把两种形态都归一成值列表（QFileInfo 本身可值拷贝，按值持有，
// 与 Qt6 的 QList<QFileInfo> 语义一致）。
//
// ⚠⚠⚠ 所有权实测（Qt3.5 /opt/qt338sh，2026-10 隔离探针实证，**修正旧注释**）
//   那个指针**归 QDir 所有，不是调用方的**！entryInfoList() 返回的是 QDir
//   内部缓存成员 fiList 的地址，QDir 析构函数里会 `delete fiList`。
//   故对返回值调用 delete 是 **double free**，必崩：
//     探针情形A（不 delete，直接析构 QDir）→ exit=0，正常；
//     探针情形B（delete p，再析构 QDir）   → exit=139 (SIGSEGV)，
//     gdb 回溯帧 #0 = QDir::~QDir() at tools/qdir.cpp:275 `delete fiList`。
//   旧代码/旧注释写的「Qt3 由调用方 delete」是错的 —— 那是把 Qt5+ 的
//   QList 值返回语义套到了 Qt3 的指针返回上。已改掉。
//   本垫片只做**值拷贝**（QFileInfo 可值拷贝），拷贝完指针归属谁都不影响，
//   析构 QDir 后 out 依然有效 —— 这也正是必须归一成值列表的原因之一。
// ⚠ Qt3 没有 NoDotAndDotDot（qdir.h FilterSpec 无此项），Qt3 还会把 "." / ".."
//   列进来 —— 已在文件头记备查，调用点需按名字自行排除，本垫片不代劳。
// ⚠ 过滤参数的**类型**两版本不同：Qt3 的 entryList/entryInfoList 收 `int`
//   （qdir.h:138 `entryList(const QString&, int, int)`），Qt6 收 `QDir::Filters`
//   （强枚举）。调用点写的 `QDir::Files|QDir::Dirs` 在 Qt6 下是 Filters，
//   塞进 `int` 形参会报 "invalid conversion from int to QDir::Filter"。
//   故形参类型随版本走，调用点两边都不用改。
#if QT_VERSION < 0x040000
typedef int qDirFilter;
#else
typedef QDir::Filters qDirFilter;
#endif

#if QT_VERSION < 0x040000
inline QList<QFileInfo> qDirEntryInfoList(const QDir& dir, qDirFilter filterSpec)
{
    QList<QFileInfo> out;
    const QFileInfoList* p = dir.entryInfoList(filterSpec);
    if (!p) return out;
    for (QFileInfoList::ConstIterator it = p->begin(); it != p->end(); ++it) {
        if (*it) out << **it;      // QPtrList 取元素得到 QFileInfo*，解两次到值
    }
    // ⚠⚠ **绝对不要 delete p** —— 那会 double free，见上方「所有权实测」。
    return out;
}
#else
inline QList<QFileInfo> qDirEntryInfoList(const QDir& dir, qDirFilter filterSpec)
{
    return dir.entryInfoList(filterSpec);
}
#endif

// ── qDirEntryList()：统一 entryList 的过滤器参数形态 ─────────────────────
// Qt3 的 QDir 只有 entryList(const QString& nameFilter, ...) —— 单个 QString
// 过滤器（qdir.h:138），没有 Qt4.1 才引入的 QStringList 重载，也没有
// nameFilters 那套「多个通配符」概念。
// ⚠ 已实测（Qt3.5 /opt/qt338sh，目录含 120 个 .cpp）：Qt3 的单 QString 过滤器
//   **不支持逗号分隔的多通配** —— entryList("*.cpp") 命中 120 项，而
//   entryList("*.cpp,*.h") 命中 **0** 项（Qt5+ 的 QStringList 重载才会拆开）。
//   即 Qt3 下「"a,b"」被当成一个字面通配串整体去 match。故本垫片只接受
//   **单个**模式，且刻意不给 QStringList 重载，避免调用点以为多模式在
//   Qt3 下也成立（那会静默返回空列表而非报错，最难查）。多模式需求请改为
//   分别调用本函数后自行合并去重。
inline QStringList qDirEntryList(const QDir& dir, const QString& nameFilter, qDirFilter filterSpec)
{
#if QT_VERSION < 0x040000
    return dir.entryList(nameFilter, filterSpec);
#else
    // Qt6 **删掉了** 单串重载：只剩 entryList(Filters, SortFlags) 与
    // entryList(const QStringList&, Filters, SortFlags)（qdir.h:162-163）。
    // 单模式要包成单元素 QStringList 才匹配得上。
    return dir.entryList(QStringList() << nameFilter, filterSpec);
#endif
}

// ── qDirEntryInfoList 的参数在 Qt3/Qt6 上枚举类型不同（Qt3 int，Qt6 Filters），
// 调用点直接写 QDir::Files|Dirs|Readable 两边都能过，无需本垫片。
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