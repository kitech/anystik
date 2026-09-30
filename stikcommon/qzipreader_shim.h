#ifndef QLSTIK_QZIPREADER_SHIM_H
#define QLSTIK_QZIPREADER_SHIM_H

// Qt 3.5 没有 QZipReader（Qt 5.7 才把 qzipreader 挪进 QtCore，Qt 6 在
// <QtCore/private/qzipreader_p.h>）。stickerstore.cpp:38 原本 include 的正是那个
// private 头，Qt3 下根本不存在。本头在 Qt3 下提供一个 API 同形的 QZipReader，
// 让 stickerstore 的 zip 安装链路（runInstallZip）在 Qt3 上能编能跑。
//
// 为什么必须自己实现而不能只做「zip 交给外部 unzip 命令」：
//   * stickerstore 依赖 fileInfoList() 里的 isSymLink / filePath 做 B1 安全预扫
//     （拒绝符号链接、绝对路径、父目录穿越、盘符冒号），这个判断必须在**解压前**
//     基于中央目录做出，不能先解压再检查——那正是要防的攻击面。
//   * 贴纸包要落到用户数据目录，外部命令 + 临时目录会引入路径与权限差异。
//
// ── 语义基线是 Qt 自己的 qtbase/src/corelib/io/qzip.cpp（6.7 已核对）────────
// 本实现是该文件 QZipReader 部分的 Qt3 移植，**包括 Qt 原有的两个怪癖**，
// 因为 stickerstore 在 Qt3 与 Qt6 上必须对同一个包做出相同判断：
//   1) fillFileInfo 会剥掉 filePath 前导的 '.' 与 '/'、尾部的 '/'。于是
//      '../../escape.txt' 在两版都变成 'escape.txt' —— B1 预扫看到的是
//      剥离后的名字，不能假设它能看到 '..'。
//   2) extractAll 重建目录结构时用 filePath.left(lastIndexOf('/'))；当
//      lastIndexOf 返回 -1，Qt6 的 QString::left(-1) 返回**整串**（Qt3 的
//      left(uint) 传 -1 同样返回整串），于是会给平铺文件建出同名**目录**，
//      随后写文件时打开失败、extractAll 返回 false。此怪癖一并复刻。
//
// 另有两处 Qt 行为容易踩错，已在实现处逐条注明：
//   * isReadable()/exists() 都不看 status：只要文件打得开就是 true，
//     「随便一个文件改名 .zip」也是 exists=1 isReadable=1 status=NoError，
//     只是 fileInfoList() 为空。
//   * 压缩方法取自**本地头**，而压缩/解压大小取自**中央目录**。
//
// 实现范围：只覆盖 stickerstore 实际用到的 API
//   exists() / isReadable() / close() / fileInfoList() / extractAll() / status()
//   加 QZipReader::FileInfo 的 isValid() / filePath / isDir / isFile / isSymLink
//   / permissions / size / crc。其余成员（entryInfoAt / fileData / count /
//   device）不是 stickerstore 的调用点，按「不预先造 API」原则不做对外声明。
//
// 压缩方法只支持 Stored(0) 与 Deflate(8)；其余方法与 ZipCrypto 加密条目
// 按 Qt 的做法返回空内容（Qt 6 亦然，见 qzip.cpp fileData() 的 qWarning 分支）。
// 解压用 zlib 的 raw inflate（-MAX_WBITS）：zip 的 deflate 成员是裸 deflate 流
// （无 zlib 头无 gzip 头），与 .tgs 的 gzip 成员（stickerstore 已有 gunzipTgs
// 用 inflateInit2(15+16)）不同。
//
// 本文件在 Qt3 下用到 POSIX 的 chmod/symlink/mkdir（Qt3 缺 QFile::setPermissions
// 与 QFile::link，QDir::mkpath 也不存在）。qlstik 只面向 Linux/Android，
// 与 qglobaltype_shim.h 里的 qMkdir 同一前提。
//
// Qt4+ 走 Qt 原生 private 头，本文件不参与编译（与 qlist_shim.h / qhash_shim.h
// 同一范式）。

#ifdef QT3_BUILD

#include <qcstring.h>
#include <qdatetime.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qglobal.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qvaluelist.h>

// Qt3 的 QList 是 qptrlist.h 的 #define（只能存指针），值容器 QList 模板由
// stikcommon/qlist_shim.h 提供（#undef QList 后派生自 QValueList）。本类
// fileInfoList() 要返回 QList<FileInfo>，故必须 include 它。
#include "qlist_shim.h"

// Qt3（qglobal.h:706-708）只有 Q_UINT16/Q_UINT32，没有 Qt4+ 的 quint16/quint32。
// 统一在 shim 内做映射。本头对外的 FileInfo 字段用 Qt3 原生名字，这样本实现
// 与 Qt6 侧类型名不同也不影响（FileInfo 是各自独立定义的类，无跨版本 ABI）。
#ifndef quint16
typedef Q_UINT16 quint16;
#endif
#ifndef quint32
typedef Q_UINT32 quint32;
#endif
// qint64 同样 Qt4 才有；stikcommon/qglobaltype_shim.h 已有 Qt3 的
// `typedef long long qint64`，但那个头会连带 include QDir/QFileInfo 且本头
// 只需最小集合，故本地定义（重复 typedef 同名同型，合法）。
#ifndef qint64
typedef long long qint64;
#endif

class QZipReader
{
public:
    explicit QZipReader(const QString& fileName);
    ~QZipReader();

    // 与 Qt6 一致：只看底层设备能否读，**不看 status**。所以「存在但不是
    // zip 的文件」也是 true，此时 fileInfoList() 为空。
    bool isReadable() const;
    bool exists() const;

    struct FileInfo
    {
        FileInfo()
            : isDir(false), isFile(false), isSymLink(false),
              permissions((QFileInfo::PermissionSpec)0), crc(0), size(0)
        {}

        bool isValid() const { return isDir || isFile || isSymLink; }

        QString filePath;
        uint isDir : 1;
        uint isFile : 1;
        uint isSymLink : 1;
        QFileInfo::PermissionSpec permissions;
        uint crc;
        qint64 size;
        QDateTime lastModified;
    };

    // 中央目录里全部条目（含目录项），顺序与中央目录一致。
    QList<FileInfo> fileInfoList() const;

    // 解压到 destinationDir。**不清空**目标目录（Qt 原生 extractAll 也不清），
    // 目标目录不存在时也不会替你建顶层目录——只有包里带目录项或条目路径含
    // '/' 时才会连带建出来。任一条目失败即返回 false。
    bool extractAll(const QString& destinationDir) const;

    enum Status {
        NoError,
        FileReadError,
        FileOpenError,
        FilePermissionsError,
        FileError
    };

    Status status() const;

    void close();

private:
    class QZipReaderPrivate* d;

    // QZipReaderPrivate（实现体在 .cpp）要读写 d 指向的对象。
    friend class QZipReaderPrivate;

    // 禁拷贝/赋值：持有 QZipReaderPrivate* 裸指针，拷贝会导致双 delete。
    // 不用 Q_DISABLE_COPY 宏（Qt 内部宏，Qt3 侧依赖 qglobal 的内部符号）。
    QZipReader(const QZipReader&);
    QZipReader& operator=(const QZipReader&);
};

#endif // QT3_BUILD

#endif // QLSTIK_QZIPREADER_SHIM_H
