#include "qzipreader_shim.h"

#ifdef QT3_BUILD

#include <zlib.h>

#include "qba_shim.h"
#include "qglobaltype_shim.h"

#include <qdir.h>

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// ── ZIP 结构常量（PKZIP APPNOTE）────────────────────────────────────────
static const quint32 kSigLocalFile     = 0x04034b50u; // 局部文件头
static const quint32 kSigCentralFile   = 0x02014b50u; // 中央目录文件头
static const quint32 kSigEndCentralDir = 0x06054b50u; // 中央目录结束记录

// Qt qzip.cpp 的 ZIP_VERSION：本实现只支持这个版本及以下
static const quint16 kZipVersion = 20;

// UnixFileAttributes（Qt qzip.cpp 同名 namespace 的取值）
enum {
    uaDir      = 0040000,
    uaFile     = 0100000,
    uaSymLink  = 0120000,
    uaTypeMask = 0170000,
    uaReadUser = 0400, uaWriteUser  = 0200, uaExeUser  = 0100,
    uaReadGroup= 0040, uaWriteGroup = 0020, uaExeGroup = 0010,
    uaReadOther= 0004, uaWriteOther = 0002, uaExeOther = 0001
};

// WindowsFileAttributes
enum {
    waDir      = 0x10,
    waFile     = 0x80,
    waTypeMask = 0x90,
    waReadOnly = 0x01
};

// HostOS 里本实现关心的几个（其余走 default 分支：条目判为 invalid）
enum {
    hostFAT  = 0,
    hostUnix = 3,
    hostHPFS = 6,
    hostNTFS = 11,
    hostVFAT = 14
};

// 通用位标志与压缩方法
enum {
    gpEncrypted    = 0x01,
    gpUtf8Names    = 0x0800,
    cmStored       = 0,
    cmDeflated     = 8
};

// 固定宽度小端读取。ZIP 是小端格式，x86/ARM 皆小端，但仍用显式移位拼装
// （不 memcpy 到整数、也不依赖 unaligned 访问），大端机上同样正确。
static inline quint16 rd16(const unsigned char* p)
{
    return (quint16)(p[0] | (p[1] << 8));
}

static inline quint32 rd32(const unsigned char* p)
{
    return (quint32)p[0] | ((quint32)p[1] << 8)
         | ((quint32)p[2] << 16) | ((quint32)p[3] << 24);
}

// MS-DOS 日期/时间（APPNOTE 4.4.6）→ QDateTime。字节布局：低 16 位是时间、
// 高 16 位是日期（Qt qzip.cpp readMSDosDate 的读法）。
static QDateTime dosDateTimeToDateTime(quint16 dostime, quint16 dosdate)
{
    const int sec  = (dostime & 0x001f) * 2;
    const int mins = (dostime >> 5) & 0x003f;
    const int hour = (dostime >> 11) & 0x001f;
    const int day  =  dosdate & 0x001f;
    const int mon  = (dosdate >> 5) & 0x000f;
    const int year = ((dosdate >> 9) & 0x007f) + 1980;
    QDate d(year, mon, day);
    QTime t(hour, mins, sec);
    if (!d.isValid() || !t.isValid()) {
        return QDateTime();
    }
    return QDateTime(d, t);
}

// Unix mode → Qt 权限位。逐条照抄 Qt qzip.cpp 的 modeToPermissions()：
// owner 的三个位要同时点亮 Owner 与 User 两个标志，这决定了
// 落盘后的 mode 也必须按同一规则反算（见 permissionsToMode）。
//
// 用 int 累加而不是直接对枚举做 |=：Qt3 的 PermissionSpec 是无作用域枚举，
// `QFileInfo::ReadOwner | QFileInfo::ReadUser` 的结果类型是 int，
// 赋回枚举变量要显式转换。
static QFileInfo::PermissionSpec modeToPermissions(quint32 mode)
{
    int ret = 0;
    if (mode & uaReadUser)   ret |= QFileInfo::ReadOwner  | QFileInfo::ReadUser;
    if (mode & uaWriteUser)  ret |= QFileInfo::WriteOwner | QFileInfo::WriteUser;
    if (mode & uaExeUser)    ret |= QFileInfo::ExeOwner   | QFileInfo::ExeUser;
    if (mode & uaReadGroup)  ret |= QFileInfo::ReadGroup;
    if (mode & uaWriteGroup) ret |= QFileInfo::WriteGroup;
    if (mode & uaExeGroup)   ret |= QFileInfo::ExeGroup;
    if (mode & uaReadOther)  ret |= QFileInfo::ReadOther;
    if (mode & uaWriteOther) ret |= QFileInfo::WriteOther;
    if (mode & uaExeOther)   ret |= QFileInfo::ExeOther;
    return (QFileInfo::PermissionSpec)ret;
}

// Qt 权限位 → Unix mode。Qt3/4/5/6 的 PermissionSpec 位布局都是
// (mode << 12) | mode（见 qfileinfo.h:58：ReadOwner=04000 ↔ S_IRUSR=0400），
// 所以高位取 owner 三位、低 12 位取 group/other 即可还原。
// Qt3 缺 QFile::setPermissions（Qt4 才有），用 chmod 补齐同一语义。
static quint32 permissionsToMode(QFileInfo::PermissionSpec p)
{
    const quint32 v = (quint32)p;
    return (v >> 12) | (v & 0777);
}

static bool setPermissionsShim(const QString& path, QFileInfo::PermissionSpec p)
{
    const QCString b = path.local8Bit();
    return ::chmod(b.data(), (mode_t)permissionsToMode(p)) == 0;
}

// QFile::link 在 Unix 上就是建符号链接；Qt3 没有 QFile::link，用 symlink(2)。
static bool linkShim(const QString& target, const QString& linkPath)
{
    const QCString t = target.local8Bit();
    const QCString l = linkPath.local8Bit();
    return ::symlink(t.data(), l.data()) == 0;
}

// QDir::mkpath(相对路径) 的等价物。Qt3 的 QDir 只有单层 mkdir，项目
// qglobaltype_shim.h 的 qMkdir 才是递归版，但它按整路径建——这里把 base
// 拼上去即可。空/空串按 Qt 的行为返回 false（QDir::mkpath("") 报
// "Empty or null file name" 并返回 false）。
static bool mkpathIn(const QString& base, const QString& rel)
{
    if (rel.isEmpty()) {
        return false;
    }
    return qMkdir(base + "/" + rel);
}

// Qt qzip.cpp 的 inflate()：一次 inflate(Z_FINISH) 尝试，destLen 在成功时
// 被改写为实际产出长度。返回 Z_BUF_ERROR 表示输出缓冲不够，调用方需要翻倍
// 重试（Qt 就是这么做的，所以解压上限由中央目录声明的 size 决定）。
static int inflateOnce(uchar* dest, ulong* destLen,
                       const uchar* source, ulong sourceLen)
{
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.next_in   = const_cast<Bytef*>(source);
    stream.avail_in  = (uInt)sourceLen;
    stream.next_out  = dest;
    stream.avail_out = (uInt)*destLen;
    if ((uLong)stream.avail_in != sourceLen) {
        return Z_BUF_ERROR;
    }
    if ((uLong)stream.avail_out != *destLen) {
        return Z_BUF_ERROR;
    }
    const int err0 = inflateInit2(&stream, -MAX_WBITS);
    if (err0 != Z_OK) {
        return err0;
    }
    const int err = inflate(&stream, Z_FINISH);
    if (err != Z_STREAM_END) {
        inflateEnd(&stream);
        if (err == Z_NEED_DICT
            || (err == Z_BUF_ERROR && stream.avail_in == 0)) {
            return Z_DATA_ERROR;
        }
        return err;
    }
    *destLen = stream.total_out;
    return inflateEnd(&stream);
}

// ── 中央目录条目 ────────────────────────────────────────────────────────
struct ZipEntry
{
    quint16 versionMade;
    quint16 versionNeeded;
    quint16 flags;
    quint16 method;          // 中央目录里的方法；解压实际用本地头的方法
    quint16 dosTime;
    quint16 dosDate;
    quint32 extAttrs;        // external file attributes（4 字节，偏移 38..41）
    quint32 crc;
    quint32 compSize;
    quint32 uncompSize;
    quint32 localOff;
    QByteArray fileName;     // 原始名字字节
    QZipReader::FileInfo fi; // 预计算好的对外 FileInfo
};

class QZipReaderPrivate
{
public:
    QZipReaderPrivate()
        : status(QZipReader::NoError), device(0), scanned(false)
    {
    }

    ~QZipReaderPrivate()
    {
        delete device;
    }

    void scanFiles();

    // Qt qzip.cpp 的 fillFileInfo()
    QZipReader::FileInfo fillFileInfo(const ZipEntry& e) const;

    // Qt qzip.cpp 的 fileData()，但传入已定位好的条目下标
    QByteArray fileDataByIndex(int index) const;

    QZipReader::Status status;

    // 始终持有打开的 QFile：exists()/isReadable() 依赖它是否打开成功
    // （Qt 也把设备留着，close() 只关设备、不清 status 也不清索引）。
    QFile* device;

    QByteArray zip;          // 整包字节；Qt 用 QIODevice 随机读，
                             // Qt3 的 QFile 无 seek()，且整读换取实现简单
    bool scanned;
    QValueList<ZipEntry> entries;
};

// Qt qzip.cpp QZipPrivate::fillFileInfo()
QZipReader::FileInfo QZipReaderPrivate::fillFileInfo(const ZipEntry& e) const
{
    QZipReader::FileInfo fi;
    quint32 mode = e.extAttrs;
    // 「external file attributes」是 4 字节（APPNOTE 偏移 38..41）。低 16 位是
    // MS-DOS 属性，高 16 位才是 Unix 模式。Qt 先看「version made by」的
    // 高字节（宿主系统）决定怎么解释这个字段——只看低 16 位会把符号链接
    // 全部漏检，而漏检意味着 stickerstore 的 B1 安全预扫放行包内符号链接。
    const quint16 hostOS = (quint16)(e.versionMade >> 8);
    switch (hostOS) {
    case hostUnix: {
        mode = (mode >> 16) & 0xffff;
        switch (mode & uaTypeMask) {
        case uaSymLink: fi.isSymLink = 1u; break;
        case uaDir:     fi.isDir = 1u;     break;
        case uaFile:
        default:        fi.isFile = 1u;    break;   // Qt 注释：just for the case
        }
        fi.permissions = modeToPermissions(mode);
        break;
    }
    case hostFAT:
    case hostNTFS:
    case hostHPFS:
    case hostVFAT: {
        int perms = 0;
        switch (mode & waTypeMask) {
        case waDir:  fi.isDir = 1u;  break;
        case waFile:
        default:     fi.isFile = 1u; break;
        }
        perms |= QFileInfo::ReadOwner | QFileInfo::ReadUser
               | QFileInfo::ReadGroup | QFileInfo::ReadOther;
        if ((mode & waReadOnly) == 0) {
            perms |= QFileInfo::WriteOwner | QFileInfo::WriteUser
                   | QFileInfo::WriteGroup | QFileInfo::WriteOther;
        }
        if (fi.isDir) {
            perms |= QFileInfo::ExeOwner | QFileInfo::ExeUser
                   | QFileInfo::ExeGroup | QFileInfo::ExeOther;
        }
        fi.permissions = (QFileInfo::PermissionSpec)perms;
        break;
    }
    default:
        // Qt：qWarning("Zip entry format at %d is not supported") 后返回
        // 三个标志全 false 的 FileInfo，即 isValid() 为 false。本实现不打印
        // 警告（stickerstore 会自行跳过 invalid 条目，无需刷屏）。
        return fi;
    }

    // 通用位标志 bit 11 置位表示文件名字段是 UTF-8
    fi.filePath = (e.flags & gpUtf8Names)
                    ? QString::fromUtf8(e.fileName.data(), e.fileName.size())
                    : QString::fromLocal8Bit(e.fileName.data(), e.fileName.size());
    fi.crc = e.crc;
    fi.size = (qint64)e.uncompSize;
    fi.lastModified = dosDateTimeToDateTime(e.dosTime, e.dosDate);

    // 修正坏掉的文件名：统一分隔符、吃掉前导的 '.' 与 '/'、尾部的 '/'。
    // QDir::fromNativeSeparators 在 Linux/Android 上是恒等变换，略过。
    //
    // ⚠ 这一点对 stickerstore 的 B1 安全预扫很关键：包内写 '../../x' 时，
    // 预扫看到的是剥离后的 'x'，**不是** '../../x'。不要以为预扫能靠
    // filePath 抓到前导穿越——Qt 两版都抓不到（真正的兜底是后面的
    // filePath.contains('/') 与符号链接检查）。
    while (!fi.filePath.isEmpty()
           && (fi.filePath[0] == QChar('.') || fi.filePath[0] == QChar('/'))) {
        fi.filePath = fi.filePath.mid(1);
    }
    while (!fi.filePath.isEmpty()
           && fi.filePath[fi.filePath.length() - 1] == QChar('/')) {
        fi.filePath = fi.filePath.left(fi.filePath.length() - 1);
    }
    return fi;
}

// Qt qzip.cpp QZipReaderPrivate::scanFiles()
void QZipReaderPrivate::scanFiles()
{
    if (scanned) {
        return;   // 对应 Qt 的 dirtyFileTree 标志：只扫一次
    }
    scanned = true;

    const int len = zip.size();
    if (len < 4) {
        return;
    }
    if (rd32((const unsigned char*)zip.data()) != kSigLocalFile) {
        return;   // Qt：qWarning("not a zip file!")，status 保持不变
    }

    // 从尾部回找 EOCD（尾部可能有 comment，最多 65535 字节）
    int cdOffset = 0;
    int numEntries = 0;
    bool foundEocd = false;
    for (int i = 0; i <= 65535; ++i) {
        const int pos = len - 22 - i;
        if (pos < 0) {
            break;   // Qt：qWarning("EndOfDirectory not found")
        }
        const unsigned char* e = (const unsigned char*)zip.data() + pos;
        if (rd32(e) == kSigEndCentralDir) {
            numEntries = (int)rd16(e + 10);
            cdOffset = (int)rd32(e + 16);
            foundEocd = true;
            break;
        }
    }
    if (!foundEocd) {
        return;
    }

    int p = cdOffset;
    for (int n = 0; n < numEntries; ++n) {
        // Qt 读固定长中央目录头，读不满就 break（索引不完整）
        if (p < 0 || p + 46 > len) {
            break;
        }
        const unsigned char* c = (const unsigned char*)zip.data() + p;
        if (rd32(c) != kSigCentralFile) {
            break;   // Qt：invalid header signature
        }
        ZipEntry e;
        e.versionMade   = rd16(c + 4);
        e.versionNeeded = rd16(c + 6);
        e.flags         = rd16(c + 8);
        e.method        = rd16(c + 10);
        const quint32 mod = rd32(c + 12);   // 低 16 位时间、高 16 位日期
        e.dosTime = (quint16)(mod & 0xffff);
        e.dosDate = (quint16)((mod >> 16) & 0xffff);
        e.crc        = rd32(c + 16);
        e.compSize   = rd32(c + 20);
        e.uncompSize = rd32(c + 24);
        const quint16 nameLen    = rd16(c + 28);
        const quint16 extraLen   = rd16(c + 30);
        const quint16 commentLen = rd16(c + 32);
        e.extAttrs  = rd32(c + 38);
        e.localOff  = rd32(c + 42);

        // 名字/extra/comment 三段必须都读得下，否则 Qt 会 break，
        // fileInfoList() 的条数就会比这里少
        if (p + 46 + nameLen + extraLen + commentLen > len) {
            break;
        }
        e.fileName = QByteArray((int)nameLen);
        memcpy(e.fileName.data(), (const char*)c + 46, nameLen);

        e.fi = fillFileInfo(e);
        entries.append(e);
        p += 46 + nameLen + extraLen + commentLen;
    }
}

// Qt qzip.cpp QZipReader::fileData()
//
// 注意 Qt 的取材方式：压缩方法取**本地头**的，而压缩/解压大小取**中央目录**
// 的；条目按 QString::fromLocal8Bit(原始名字) 匹配（不是按剥离后的
// filePath，也不是按 UTF-8 标志解码）——后者在非 UTF-8 名字上会匹配不上，
// Qt 就是这个行为，照抄。
QByteArray QZipReaderPrivate::fileDataByIndex(int index) const
{
    if (index < 0 || index >= (int)entries.size()) {
        return QByteArray();
    }
    const ZipEntry& e = entries[(uint)index];
    if (e.versionNeeded > kZipVersion) {
        return QByteArray();   // Qt：qWarning 后返回空
    }
    if (e.flags & gpEncrypted) {
        return QByteArray();   // Qt：qWarning(Unsupported encryption) 后返回空
    }

    // 定位数据区：跳到本地头，跳过 name + extra
    const int start = (int)e.localOff;
    if (start < 0 || start + 30 > zip.size()) {
        return QByteArray();
    }
    const unsigned char* lh = (const unsigned char*)zip.data() + start;
    if (rd32(lh) != kSigLocalFile) {
        return QByteArray();
    }
    const quint16 method    = rd16(lh + 8);   // 本地头的压缩方法
    const quint16 lnameLen  = rd16(lh + 26);
    const quint16 lextraLen = rd16(lh + 28);
    const int dataOff = start + 30 + lnameLen + lextraLen;
    if (dataOff < 0 || dataOff > zip.size()) {
        return QByteArray();
    }
    // 实际可读字节数（中央目录声明可能被伪造或文件被截断）
    const int avail = zip.size() - dataOff;
    const quint32 want = e.compSize;
    const quint32 srcLen = (want > (quint32)avail) ? (quint32)avail : want;

    // Qt 读 compressed_size 字节后对 Stored 做 truncate(uncompressed_size)
    QByteArray compressed((int)srcLen);
    if (srcLen > 0) {
        memcpy(compressed.data(), zip.data() + dataOff, srcLen);
    }

    if (method == cmStored) {
        if (e.uncompSize < (quint32)srcLen) {
            // Qt：compressed.truncate(uncompressed_size)
            QByteArray out((int)e.uncompSize);
            if (e.uncompSize > 0) {
                memcpy(out.data(), compressed.data(), e.uncompSize);
            }
            return out;
        }
        return compressed;
    }
    if (method != cmDeflated) {
        return QByteArray();   // Qt：qWarning(Unsupported compression method)
    }

    // Deflate：raw inflate（-MAX_WBITS）——zip 成员是裸 deflate 流，
    // 无 zlib 头也无 gzip 头（与 .tgs 的 gzip 成员不同，那里用 15+16）。
    // 初值取中央目录声明的解压大小，与 Qt 相同；Z_BUF_ERROR 时翻倍重试，
    // 所以声明值偏小也能解出来（代价是内存）。
    QByteArray ba;
    ulong len = (ulong)((int)e.uncompSize > 1 ? (int)e.uncompSize : 1);
    int res;
    do {
        ba.resize(len);
        res = inflateOnce((uchar*)ba.data(), &len,
                          (const uchar*)compressed.data(), (ulong)srcLen);
        if (res == Z_OK) {
            if ((ulong)ba.size() != len) {
                ba.resize(len);
            }
            break;
        }
        if (res != Z_BUF_ERROR) {
            // Z_MEM_ERROR / Z_DATA_ERROR / Z_STREAM_END：Qt 只是 qWarning，
            // 循环条件 res==Z_BUF_ERROR 不成立，照样把 ba 交出去
            break;
        }
        len *= 2;
    } while (res == Z_BUF_ERROR);
    return ba;
}

QZipReader::QZipReader(const QString& fileName)
{
    d = new QZipReaderPrivate();
    QFile* f = new QFile(fileName);
    d->device = f;
    // 与 Qt 构造器一致：只有「打不开」才算错误。一个存在但不是 zip 的文件
    // 照样是 NoError（于是 exists()/isReadable() 都为 true），
    // 真正「不是 zip」要到 scanFiles 里才被静默发现。
    if (!f->open(IO_ReadOnly)) {
        d->status = FileOpenError;
        return;
    }
    const Q_LONG total = f->size();
    if (total < 0) {
        d->status = FileError;
        return;
    }
    QByteArray buf((int)total);
    Q_LONG got = 0;
    while (got < total) {
        const Q_LONG n = f->readBlock(buf.data() + got, (Q_ULONG)(total - got));
        if (n <= 0) {
            break;
        }
        got += n;
    }
    // 读不满不置错误状态：Qt 这时要等 scanFiles 里的短读才会发现，那时
    // 也只是 break + qWarning，status 依旧不变。这里同样保持 NoError。
    d->zip = buf;
    d->scanFiles();
}

QZipReader::~QZipReader()
{
    delete d;
}

bool QZipReader::isReadable() const
{
    return d->device && d->device->isReadable();
}

bool QZipReader::exists() const
{
    return d->device && d->device->exists();
}

QList<QZipReader::FileInfo> QZipReader::fileInfoList() const
{
    d->scanFiles();
    QList<FileInfo> files;
    const int n = d->entries.size();
    for (int i = 0; i < n; ++i) {
        files.append(d->entries[(uint)i].fi);
    }
    return files;
}

QZipReader::Status QZipReader::status() const
{
    return d->status;
}

void QZipReader::close()
{
    // 与 Qt 一致：只关设备，不清 status、不清索引（所以 close() 之后
    // fileInfoList() 仍返回已扫到的条目）
    if (d->device) {
        d->device->close();
    }
}

bool QZipReader::extractAll(const QString& destinationDir) const
{
    const QList<FileInfo> allFiles = fileInfoList();
    const QChar sep = QDir::separator();

    // 第一趟：建目录条目，并判断「包内是否带目录结构」
    bool foundDirs = false;
    bool hasDirs = false;
    for (int i = 0; i < (int)allFiles.size(); ++i) {
        const FileInfo& fi = allFiles[(uint)i];
        if (fi.isDir) {
            const QString absPath = destinationDir + sep + fi.filePath;
            foundDirs = true;
            if (!mkpathIn(destinationDir, fi.filePath)) {
                return false;
            }
            if (!setPermissionsShim(absPath, fi.permissions)) {
                return false;
            }
        } else if (!hasDirs && fi.filePath.contains(QChar('/'))) {
            // filePath 已被剥掉前导/尾部 '/'，所以这里出现 '/' 就说明
            // 路径确实带目录层级
            hasDirs = true;
        }
    }

    // 有些包只给文件条目、不给目录条目，得按路径重建目录结构
    if (hasDirs && !foundDirs) {
        for (int i = 0; i < (int)allFiles.size(); ++i) {
            const FileInfo& fi = allFiles[(uint)i];
            // ⚠ Qt 怪癖（已核对 Qt 6.7 行为）：lastIndexOf 返回 -1 时
            // QString::left(-1) 返回**整串**，于是会给平铺文件建出同名目录，
            // 随后第三趟写文件时打不开、extractAll 返回 false。
            // Qt3 的 left(uint) 传 -1 同样返回整串，行为一致，照抄。
            const int idx = fi.filePath.findRev(QChar('/'));
            const QString dirPath = fi.filePath.left((uint)idx);
            if (!mkpathIn(destinationDir, dirPath)) {
                return false;
            }
        }
    }

    // 第二趟：建符号链接
    for (int i = 0; i < (int)allFiles.size(); ++i) {
        const FileInfo& fi = allFiles[(uint)i];
        if (!fi.isSymLink) {
            continue;
        }
        const QString absPath = destinationDir + sep + fi.filePath;
        const QByteArray raw = d->fileDataByIndex(i);
        QCString cs(raw.data());
        const QString target = QFile::decodeName(cs);
        if (target.isEmpty()) {
            return false;
        }
        // 目标父目录不存在就建出来（Qt 用 QDir::root().mkpath(绝对路径)）
        const int slash = absPath.findRev(sep);
        if (slash > 0) {
            qMkdir(absPath.left(slash));
        }
        if (!linkShim(target, absPath)) {
            return false;
        }
    }

    // 第三趟：写普通文件
    for (int i = 0; i < (int)allFiles.size(); ++i) {
        const FileInfo& fi = allFiles[(uint)i];
        if (!fi.isFile) {
            continue;
        }
        const QString absPath = destinationDir + sep + fi.filePath;
        QFile f(absPath);
        if (!qOpenWriteOnly(f)) {
            return false;
        }
        const QByteArray data = d->fileDataByIndex(i);
        if (data.size() > 0) {
            f.writeBlock(data.data(), (Q_ULONG)data.size());
        }
        setPermissionsShim(absPath, fi.permissions);
        f.close();
    }

    return true;
}

#endif // QT3_BUILD
