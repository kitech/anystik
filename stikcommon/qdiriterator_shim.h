#ifndef QLSTIK_QDIRITERATOR_SHIM_H
#define QLSTIK_QDIRITERATOR_SHIM_H

// QDirIterator 垫片（Qt3 专用）。
//
// QDirIterator 随 Qt 4.2 引入，Qt3.5 无此类。stickerstore.cpp 只用一处
// （3822）：递归收集目录下的 *.eif / *.EIF，用于「zip 里再套一层 eif」的
// 兼容展开。用到的接口仅 ctor + hasNext() + next() + QDirIterator::Subdirectories。
//
// 语义说明：
//   · Qt3 的 QDir 没有 NoDotAndDotDot（qdir.h:61 的 FilterSpec 无此项），
//     且其 entryList 会列出 "." / ".."；本垫片在递归时显式跳过这两个名字。
//     调用点的 `QDir::NoDotAndDotDot` 已在 stickerstore.cpp 侧去掉——对
//     「*.eif / *.EIF」名字过滤而言，"." / ".." 本就不匹配，去掉是行为等价的
//     （Qt6 QDir::Files 也已把目录排除）。
//   · Qt6 的 QDirIterator 是惰性、按文件系统顺序；本垫片构造时一次性递归
//     收集并按 Qt3 QDir 的默认排序（Name|IgnoreCase）。本调用点收集后整体
//     处理，与顺序无关，故等价。
//   · ctor 的 filters 语义：仅当含 QDir::Files 时收集文件；Subdirectories
//     置位时递归子目录。其它 FilterSpec 位（Hidden/NoSymLinks/Readable/…）
//     透传给 Qt3 entryInfoList。
//
// ⚠ 只实现上述用到的形态。Qt6 QDirIterator 还有 fileInfo()/path()/
//   NextItem等以及两参构造，用到再补。

#include <qglobal.h>

#if QT_VERSION < 0x040200

#include <qdir.h>
#include <qfileinfo.h>
#include <qstringlist.h>

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
#endif // QLSTIK_QDIRITERATOR_SHIM_H