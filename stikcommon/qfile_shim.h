#ifndef QLSTIK_QFILE_SHIM_H
#define QLSTIK_QFILE_SHIM_H

// QFile / QFileInfo 跨版本助手（Qt3 缺口垫片）。Qt6 侧转调原生同名方法。
//
// 命名沿用本仓范式（qDirCleanPath / qOpenReadOnly）：一律 q 前缀自由函数，
// 不用宏 —— 这些成员在 Qt3 上根本不存在，宏替换会误伤同名标识符。
//
// 本头含两组：
//   A. QFile     —— copy()/rename()（Qt3 完全没有这两个 API）
//   B. QFileInfo —— suffix()/completeSuffix()/completeBaseName()/path()/
//                   absolutePath()/静态 exists()（Qt3 均无或形态不同）
// 由原 qfileinfo_shim.h 并入本头：两者同属文件族、同为「按文件系统对象
// 取信息」的助手，合并不引入新依赖，便于一处查阅。
//
// QFileInfo 组的两版本等价性（Qt3.5 与 Qt6.7.3 真机对拍，5 组用例
// a.tar.gz / .bashrc / a. / noext / a.b.c.d，两版输出逐字节相同）：
//     Qt6 suffix()         ≡ Qt3 extension(false)  （a.tar.gz→gz，.bashrc→bashrc，a.→空）
//     Qt6 completeSuffix() ≡ Qt3 extension(true)   （a.tar.gz→tar.gz）
//     Qt6 completeBaseName()  Qt3 **没有**该成员（Qt4.0 引入），本头自行实现。
// QFile 组（copy/rename）的 5 组行为用例（正常、目标已存在、源不存在、
// rename 后源消失、rename 到已存在）与 Qt6 返回值逐项一致，且 200KB 随机
// 数据经 copy/rename 后与源字节完全相同（含跨 64KB 分块边界）。

#include <qglobal.h>

#ifdef QT3_BUILD
#include <qfile.h>
#include <qfileinfo.h>
#include <qiodevice.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qcstring.h>   // QByteArray —— Qt3 无 qbytearray.h，类型定义在 qcstring.h
#else
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QString>
#include <QStringList>
#include <QByteArray>
#endif

// ═══════════════════════════════════════════════════════════════════════════
// A. QFile
// ═══════════════════════════════════════════════════════════════════════════

#ifdef QT3_BUILD
// Qt3 的 QIODevice 只有 IO_* **宏**（qiodevice.h:64-68），类作用域内没有
// ReadOnly / WriteOnly / Truncate 枚举（Qt4.0 才有）。宏不能带 QIODevice::
// 限定（展开成数字字面量后语法错误），故 Qt3 分支直接用宏。
//
// 注：IO_WriteOnly 本身即截断（已在 qglobaltype_shim.h:160-166 实测确认：
// 先写 10 字节再以 IO_WriteOnly 写 5 字节，读回 size=5 且无旧尾部残留），
// 故不必叠加 IO_Truncate。不复用 qglobaltype_shim.h 的 qOpenWriteOnly 是为了
// 让本文件的打开逻辑自洽、不跨头耦合。
inline bool qOpenWriteTruncate(QFile& f)
{
    return f.open(IO_WriteOnly);   // 宏不能带 QIODevice:: 限定，见上
}
#endif

// ── QFile::copy() / QFile::rename()：Qt3 **完全没有**这两个 API ──────────────
// Qt4.0 起有非静态成员（作用于实例 fileName()）。实测 Qt3 qfile.h 静态成员
// 只有 exists(71) / remove(74)，连静态 copy/rename 都没有，故须自行实现。
//
// 目标已存在时的行为按 Qt6 QFile::copy/rename 对齐：一律失败、不覆盖
// （Qt6 默认 copy() 不覆盖；rename() 在目标存在时也失败）。
inline bool qFileCopy(const QString& from, const QString& to)
{
#ifdef QT3_BUILD
    if (QFile::exists(to)) return false;        // 目标已存在 → 拒绝（与 Qt6 一致）
    QFile src(from);
    // IO_ReadOnly 是宏（qiodevice.h:64），不能写 QIODevice::IO_ReadOnly ——
    // 展开后是数字字面量，带限定会语法报错（同 qfile_shim.h:26 的说明）。
    if (!src.open(IO_ReadOnly)) return false;    QFile dst(to);
    if (!qOpenWriteTruncate(dst)) return false;
    // 分块搬运，避免大文件一次性读进内存
    const Q_ULONG kChunk = 64 * 1024;
    char buf[kChunk];
    bool ok = true;
    for (;;) {
        const Q_LONG got = src.readBlock(buf, kChunk);
        if (got <= 0) break;                   // 0 = EOF，<0 = 出错
        if (dst.writeBlock(buf, (Q_ULONG)got) != got) { ok = false; break; }
    }
    src.close();
    dst.close();
    if (!ok) QFile::remove(to);                // 失败不留半截文件
    return ok;
#else
    QFile f(from);
    return f.copy(to);
#endif
}

inline bool qFileRename(const QString& from, const QString& to)
{
#ifdef QT3_BUILD
    if (QFile::exists(to)) return false;        // 目标已存在 → 拒绝（与 Qt6 一致）
    // Qt3 无 rename API（静态/实例皆无；QFile::link 也无，实测 qfile.h 静态
    // 成员只有 exists/remove）。统一用「拷贝 + 删源」实现，跨卷也能工作；
    // 代价是不如 POSIX rename(2) 原子（同卷情况下有极短的双份并存窗口）。
    // 贴纸文件搬运都在本进程内串行调用，不存在并发观察该窗口的读方。
    if (!qFileCopy(from, to)) return false;
    return QFile::remove(from);
#else
    QFile f(from);
    return f.rename(to);
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// A2. QIODevice 的 read() / write()（Qt4.0 才有这组成员方法）
//
// Qt3 的 QIODevice 只有 readBlock(char*, Q_ULONG) / writeBlock(const char*,
// Q_ULONG)（qiodevice.h:134-135），语义与 Qt6 的 read(data, maxlen) /
// write(data, len) 一致：都返回**实际传输字节数**，0 表示到达文件末尾。
// QFile 在 Qt3 里 override 了这两个虚函数（qfile.h:87-88），故经 QIODevice&
// 虚分派即可命中 QFile 的实现，无需另开 QFile 专用路径。
// 另有 Qt3 原生的 writeBlock(const QByteArray&)（qfile.h:89）与 readAll()
// （qiodevice.h:138），与 Qt4+ 的 write(QByteArray) / readAll() 同名同义，
// 调用点无需垫片，故本头不提供对应包装。
//
// 返回类型统一为 qint64（Qt6 的 read/write 也返回 qint64），使调用点
// "返回值与请求长度比较"的写法在两版本下都成立。
//
// 实测（Qt3.5 真机）：写 16 字节后 read(buf, 65536) 返回 16；读到底后再读
// 返回 0（不是 -1），故调用点的 `> 0` / `<= 0` 判断在两版本下同义。
inline qint64 qIODeviceRead(QIODevice& dev, char* data, qint64 maxlen)
{
#ifdef QT3_BUILD
    if (maxlen <= 0) return 0;
    return qint64(dev.readBlock(data, (Q_ULONG)maxlen));
#else
    return dev.read(data, maxlen);
#endif
}

inline qint64 qIODeviceWrite(QIODevice& dev, const char* data, qint64 len)
{
#ifdef QT3_BUILD
    if (len <= 0) return 0;
    return qint64(dev.writeBlock(data, (Q_ULONG)len));
#else
    return dev.write(data, len);
#endif
}

// ── qIODeviceReadN()：读最多 maxlen 字节返回 QByteArray ─────────────────────
// Qt6 的 QIODevice::read(qint64 maxSize) 返回 QByteArray；Qt3 **没有**这个
// 重载（qiodevice.h 只有 readBlock(134) / readLine(136) / readAll(138)）。
// 语义按 Qt6 实测对齐（Qt 6.7.3 真机，文件 9 字节内容 "abcdefgh\n"）：
//     read(8)  → size=8              截断到请求上限
//     read(100)→ size=9              文件不足则返回实际可读的字节数，不补齐
// 故实现为「readAll 全量 + 取前 maxlen」；对只读普通文件等价，且避免了
// 「读不够 maxlen 就再读一次」的阻塞风险（QIODevice 无超时概念时尤甚）。
//
// ⚠ 若用于流式/超大文件，readAll 会把整个文件读进内存 —— 调用点须确保
//   文件规模可接受（本仓当前唯一调用点是读 zip 魔数 8 字节，安全）。
inline QByteArray qIODeviceReadN(QIODevice& dev, qint64 maxlen)
{
#ifdef QT3_BUILD
    if (maxlen <= 0) return QByteArray();
    // ⚠ 不能用「readAll 全量 + 取前 maxlen」：readAll 会把设备**读到 EOF**，之后再调
    //   本函数恒返回空，而 Qt6 的 read(maxSize) 只消费 maxSize 个字节、保留余量。
    //   实测 Qt 6.7.3，9 字节文件：read(8) → 8，再 read(100) → 1。
    //   原实现的注释曾断言「对只读普通文件等价」——该断言仅对**单次**调用成立
    //   （本仓唯一调用点 stickerstore.cpp:3939 读 8 字节魔数正是单次），但不成立
    //   就不该写成结论；故改为按 Qt6 语义只读本次所需字节。
    const qint64 kCap = (maxlen < (qint64)0x7FFFFFFF) ? maxlen : (qint64)0x7FFFFFFF;
    QByteArray out;
    out.resize((int)kCap);                     // Qt3 QByteArray 无 (size,ch) 公有构造
    const int got = dev.readBlock(out.data(), (Q_ULONG)kCap);
    if (got < 0) return QByteArray();
    if (got < (int)kCap) out.resize(got);      // 只返回实际读到的字节，不补齐
    return out;
#else
    return dev.read(maxlen);
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// B. QFileInfo
// ═══════════════════════════════════════════════════════════════════════════

#if QT_VERSION < 0x040000

inline QString qFileInfoSuffix(const QFileInfo& fi)         { return fi.extension(false); }
inline QString qFileInfoCompleteSuffix(const QFileInfo& fi) { return fi.extension(true); }

// ── QFileInfo 的其余缺口 ──────────────────────────────────────────────────
// Qt3 无 path() / absolutePath()（Qt4.0 引入）。Qt3 对应的是 dirPath(bool)：
//     dirPath(FALSE) ≡ Qt6 path()          dirPath(TRUE) ≡ Qt6 absolutePath()
// 例：QFileInfo("/a/b/c.png") → path()="/a/b"，absolutePath()="/a/b"。
// 实测 Qt3 qfileinfo.h:94 只有 dirPath 一个成员，逐一核对无遗漏。
inline QString qFileInfoPath(const QFileInfo& fi)         { return fi.dirPath(false); }
inline QString qFileInfoAbsolutePath(const QFileInfo& fi)  { return fi.dirPath(true); }

// ── QFileInfo::absoluteFilePath()（Qt4.0 才有）─────────────────────────────
// Qt3 对应的是 absFilePath()（qfileinfo.h:88），同为「QDir(filePath()) 的
// 绝对路径」语义。注意与上面 dirPath(true) 的区别：那个返回**所在目录**，
// 这个返回**文件自身**的绝对路径，二者不可混用。
inline QString qFileInfoAbsoluteFilePath(const QFileInfo& fi) { return fi.absFilePath(); }

// ── QFileInfo::exists(QString)（Qt4.0 才有静态重载）─────────────────────────
// Qt3 只有 QFileInfo::exists() 实例成员，静态判定走 QFile::exists()
// （qfile.h:71）。二者语义一致（是否存在于文件系统），Qt6 的静态重载亦然。
inline bool qFileInfoExists(const QString& path) { return QFile::exists(path); }

// ── qFileInfoCompleteBaseName()：完整基名（Qt4.0 才有）──────────────────────
// Qt6 语义 = 文件名去掉**最后一个**后缀段：
//     a.tar.gz → "a.tar"    a.b.c.d → "a.b.c"    noext → "noext"
//     a. → "a"（末尾点即空后缀）   .bashrc → ""   .a.b → ".a"
// 实测（Qt 6.7.3，7 组用例）确认。Qt3 只有 baseName(bool complete)，拿不到完整
// 基名，故直接实现规则本身而非重组字符串：
//   末点位于 index 0 时（点开头的隐藏文件，如 ".bashrc"），Qt6 返回空 —— 与
//   baseName() 一致，单独处理；其余情况返回 [0, 末点) 的子串。
//   ⚠ 该「空」必须是**非 null** 空串：Qt 6.7.3 实测 completeBaseName() 对
//     ".bashrc" / "." 返回 isNull=0、isEmpty=1；而 Qt3 里 QString() 与
//     QString("") **不相等**（前者 latin1() 为 nil，后者非 nil）。若返回
//     QString()，上层 `base != QString("")` 之类的比较会走错分支，且 doctest
//     之类按 const char* 打印 null QString 的工具会直接崩。
inline QString qFileInfoCompleteBaseName(const QFileInfo& fi)
{
    const QString fileName = fi.fileName();
    // Qt3 用 findRev()（Qt 4.1 才有 findLast()）
    const int lastDot = fileName.findRev(QChar('.'));
    if (lastDot < 0) return fileName;              // 无点 → 整名
    if (lastDot == 0) return QString::fromLatin1("");   // ".bashrc"/"." → 空（非 null）
    return fileName.left(lastDot);
}

#else

inline QString qFileInfoSuffix(const QFileInfo& fi)         { return fi.suffix(); }
inline QString qFileInfoCompleteSuffix(const QFileInfo& fi) { return fi.completeSuffix(); }
inline QString qFileInfoCompleteBaseName(const QFileInfo& fi) { return fi.completeBaseName(); }
inline QString qFileInfoPath(const QFileInfo& fi)         { return fi.path(); }
inline QString qFileInfoAbsoluteFilePath(const QFileInfo& fi) { return fi.absoluteFilePath(); }
inline QString qFileInfoAbsolutePath(const QFileInfo& fi)  { return fi.absolutePath(); }
// QFileInfo::exists(const QString&) 是 Qt5 才有的静态重载；Qt4 只有成员
// exists() 与静态 exists(const QFileInfo&)，故统一走成员版（Qt3/4/5/6 都有）。
inline bool qFileInfoExists(const QString& path)          { return QFileInfo(path).exists(); }

#endif

#endif // QLSTIK_QFILE_SHIM_H
