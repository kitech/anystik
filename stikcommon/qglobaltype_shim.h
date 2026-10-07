#ifndef QLSTIK_QGLOBALTYPE_SHIM_H
#define QLSTIK_QGLOBALTYPE_SHIM_H

// 跨 Qt 版本的基础类型 + QFileInfo/QDir API 补齐。
// 共享源码在 anystik（Qt6/CMake）和 qlstik（Qt3/4/6）两侧编译，故不能依赖
// qlcomp；这些兜底都放在 stikcommon。
//
// ══ qint64 / quint64 ═══════════════════════════════════════════════════
// Qt 4.0 才引入 qint64 家族；Qt 3.5 只有 Q_LONG/Q_ULONG（非 LP64 平台上是 long，
// 仅 32 位）。davbisync_baseline.h 的公开 API 直接用 qint64 表达文件字节数与
// epoch 毫秒（~1.7e12），必须 64 位，故用 C++ 原生 long long 承载（qlcomp/
// qcrc64.h 用 uint64_t 是同一思路）。Qt4+ 由 #if 自动跳过，用 Qt 自己的定义。
//
// ══ QIODevice 打开模式 ═════════════════════════════════════════════════
// Qt3 的 QIODevice 用 IO_ReadOnly/IO_WriteOnly/IO_ReadWrite 宏且无枚举成员，
// Qt4+ 用 QIODevice::ReadOnly 等枚举——这是没法放进一个类型里的差异，一律走
// qOpenReadOnly() / qOpenWriteOnly() 自由函数（与 qAbsPath/qMkdir 同范式）。
//
// ══ 字节读写 ═════════════════════════════════════════════════════════
// Qt3 的 QFile/QBuffer 只有 writeBlock/readBlock（形参 Q_ULONG），Qt4+ 才有
// write(const QByteArray&) / read(char*, qint64)；且 Qt3 的 QBuffer 是
// setBuffer(QByteArray) 按值深拷贝，Qt4+ 是 setData() 隐式共享。对应自由函数：
//   qFileWrite(QFile&, QByteArray)   —— QFile 专版
//   qIoWrite(QIODevice&, QByteArray) —— 基类版，QBuffer/QFile 通吃
//   qBufferSetData(QBuffer&, QByteArray) —— Qt3 setBuffer ↔ Qt4+ setData
// 语义差异均已实测（见各函数注释），实现见文件末尾。

//
// ══ qAbsPath() / qMkdir() ══════════════════════════════════════════════
// Qt3 没有 QFileInfo::absolutePath()（用 QT3_SUPPORT 的 dirPath(true)），也没有
// QDir::mkpath()（mkdir 只建一层）。这两个免费函数对齐 Qt4/5/6 的语义：
//   qAbsPath(fi) == fi.absolutePath()
//   qMkdir("a/b/c") == QDir().mkpath("a/b/c")：已存在→true，任一失败→false

#include <qglobal.h>

#ifdef QT3_BUILD
#include <qstring.h>
#include <qdir.h>
#include <qfileinfo.h>
#include <qiodevice.h>
#include <qfile.h>
#include <qbuffer.h>
#include <qcstring.h>
#else
#include <QString>
#include <QDir>
#include <QFileInfo>
#include <QIODevice>
#include <QFile>
#include <QBuffer>
#include <QByteArray>
#endif

#if QT_VERSION < 0x040000
typedef long long qint64;
typedef unsigned long long quint64;
// qulonglong：Qt4.0 起 qglobal.h 才有此名。Qt3 侧的基础 64 位无符号整型
// 是 Q_ULLONG（qglobal.h:728 → unsigned long long），语义完全一致，故补同名
// typedef。用到它的 qsettings_shim.h（qSettingsValueULongLong/SetULongLong）
// 在 Qt3 分支本来就把值当 QString 存（Qt3 无 writeEntry(key, qulonglong)，
// 只有 writeEntry(key, const QString&)），故此 typedef 不引入语义风险。
typedef unsigned long long qulonglong;
#ifndef Q_INT64_C
#define Q_INT64_C(c)   c##LL
#define Q_UINT64_C(c)  c##ULL
#endif
// Qt3 无 qreal 与 qBound（课音：imagetmpuploader 进度百分比用）。
// qreal 语义与 Qt 相同（double on this platform），qBound 对齐 Qt4+ 模板。
typedef double qreal;
template <typename T>
inline const T& qBound(const T& val, const T& lo, const T& hi)
{
    return (val < lo) ? lo : ((hi < val) ? hi : val);
}

// qMax / qMin：Qt 3.5 的 qglobal.h 里**没有**（实测 grep qglobal.h 无定义，
// 只有 Qt4.0 才作为模板加入）。davbisync 的进度百分比（1305）与速率下限
// （1380）用到，语义与 Qt4+ 完全一致，故补齐。
// ⚠ 形参顺序是 Qt4+ 的 qMax(a, b) / qBound(val, lo, hi) 两套，别混用。
template <typename T>
inline const T& qMax(const T& a, const T& b)
{
    return (a < b) ? b : a;
}
template <typename T>
inline const T& qMin(const T& a, const T& b)
{
    return (b < a) ? b : a;
}
#endif

static inline QString qAbsPath(const QFileInfo& fi)
{
#ifdef QT3_BUILD
    return fi.dirPath(true);        // QT3_SUPPORT 的 absolutePath() 替身
#else
    return fi.absolutePath();
#endif
}

// 命名说明：刻意**不叫 qMkdir**。qlcomp/compatcore34.h:57 已声明
// `bool qMkdir(const QString& path, bool recursive = true)`，而 qnam_shim.h:44
// 包含了该头（本文件的调用方 stickerstore.cpp 经此间接可见），同名重载会让
// `qMkdir(toRoot)` 二义（编译器报 there are 2 candidates）。
//
// 两者语义还不同，不能简单二选一：compatcore34.cpp:334 的 Qt3 分支以
// `return QDir().mkdir(p)` 收尾，目录**已存在**时 QDir::mkdir 返回 false；
// 而 QDir::mkpath 的契约是「已存在 → true」。stickerstore.cpp 的 11 处
// `QDir().mkpath(...)` 是要按 mkpath 语义替换的，故本函数实现 mkpath 语义
// （末级 `|| QFileInfo(p).isDir()`），并在上面先做 isDir 短路。
// 已实测（/tmp/qim/mk2.cpp）：深层绝对/相对路径、重复调用、已存在目录均返回 1。
static inline bool qMkpath(const QString& path)
{
#ifdef QT3_BUILD
    // QDir 无 mkpath；逐级 mkdir（思路与 compatcore34.cpp 的 qMkdir 相同）。
    QString p = path;
    while (p.endsWith(QChar('/'))) {
        p.truncate(p.length() - 1);
    }
    if (p.isEmpty()) {
        return false;
    }
    // Qt3 的 QDir::mkdir 对"已存在"返回 false，但 QDir::mkpath 语义是"已存在→true"。
    // 本函数按注释承诺的 mkpath 语义实现，调用方（如 qcabundle 反复激活）依赖这一点。
    if (QFileInfo(p).isDir()) {
        return true;
    }
    int slashPos = 0;
    while ((slashPos = p.find('/', slashPos + 1)) != -1) {
        QDir().mkdir(p.left(slashPos));
    }
    // 末级可能已被并发创建，补一次 isDir 判定
    return QDir().mkdir(p) || QFileInfo(p).isDir();
#else
    return QDir().mkpath(path);
#endif
}

// 兼容别名：qcabundle.cpp:84 / qzipreader_shim.cpp:147,610 / qlstik/src/config.cpp:52
// / stikcommon/test_qmkdir.cpp 一律按 **单参 qMkdir** 调用建目录（历史接口名，
// 另有专门用例覆盖其 mkpath 语义）。改名 qMkpath 会打断这批既有调用点（实测：
// qcabundle.cpp:84 直接 "qMkdir was not declared"），故保留别名转调同一实现 ——
// 单点实现、单份语义，不是两份拷贝。
static inline bool qMkdir(const QString& path) { return qMkpath(path); }

// ── qOpenReadOnly()：QIODevice 打开模式（Qt3 宏 / Qt4+ 枚举）──────────────
// Qt3 的 QIODevice 只有 IO_ReadOnly/IO_WriteOnly/IO_ReadRead 宏、类作用域里
// **没有** ReadOnly 枚举；Qt4+ 才有 QIODevice::ReadOnly。这差异没法塞进类型，
// 故按本文件 qAbsPath/qMkdir 的同款范式给自由函数（见上"QIODevice 打开模式"）。
//
// ⚠ 边界是 < 0x040000 而非 < 0x050000：IO_ReadOnly/IO_WriteOnly 宏只在 **Qt3**
//   qiodevice.h 存在（Qt4 起改为 QIODevice::ReadOnly/WriteOnly 枚举；Qt3 的
//   QIODevice 没有枚举）。注释原文（"#if < 0x050000 覆盖 Qt4"）没有实测过 Qt4，
//   buildqt4 全量编译实锤 Qt4 无此宏，故收窄。
inline bool qOpenReadOnly(QIODevice& dev)
{
#if QT_VERSION < 0x040000
    return dev.open(IO_ReadOnly);
#else
    return dev.open(QIODevice::ReadOnly);
#endif
}

// ── qOpenWriteOnly()：QIODevice 写模式（Qt3 宏 / Qt4+ 枚举）────────────────
// 同 qOpenReadOnly，Qt3 用 IO_WriteOnly 宏，Qt4+ 用 QIODevice::WriteOnly 枚举。
//
// ⚠ 截断语义已实测确认（Qt3.5 + /opt/qt338sh 真机）：Qt3 的 IO_WriteOnly
//   **本身就截断**——对同一文件先写 10 字节、再以 IO_WriteOnly 写 5 字节，
//   读回 size=5 且内容纯为新数据，无旧尾部残留。故此处无需（也不应）再加
//   IO_Truncate，与 Qt4+ 的 QIODevice::WriteOnly 语义一致。
//   （该宏值 0x0002，IO_Truncate 另有 0x0008，见 qiodevice.h:65,68。）
inline bool qOpenWriteOnly(QIODevice& dev)
{
#if QT_VERSION < 0x040000
    return dev.open(IO_WriteOnly);
#else
    return dev.open(QIODevice::WriteOnly);
#endif
}

// ── qOpenWriteOnlyAppend()：追加写模式（Qt3 宏 / Qt4+ 枚举位或）────────────
// stickerstore.cpp:3546 用 `QIODevice::Append | QIODevice::WriteOnly` 做断点
// 续传。Qt3 无 Append 枚举，需用 IO_Append|IO_WriteOnly（qiodevice.h:64-68）。
inline bool qOpenWriteOnlyAppend(QIODevice& dev)
{
#if QT_VERSION < 0x040000
    return dev.open(IO_Append | IO_WriteOnly);
#else
    return dev.open(QIODevice::WriteOnly | QIODevice::Append);
#endif
}

// ── qFileWrite()：QFile 写字节（Qt3 是 writeBlock，无 write）───────────────
// Qt3 的 QFile 只有 Q_LONG writeBlock(const char*, Q_ULONG)（qfile.h:88），
// 整个 QFile 都没有 Qt4+ 的 write(const QByteArray&)。语义一致（写全部、
// 返回实际写入字节数），故按 QIODevice/qMkdir 同范式给自由函数。
inline long qFileWrite(QFile& f, const QByteArray& data)
{
#if QT_VERSION < 0x040000
    // Qt3 的 writeBlock 只吃裸指针 + 长度，不能直接传 QByteArray。
    return (long)f.writeBlock(data.data(), (Q_ULONG)data.size());
#else
    return (long)f.write(data);
#endif
}

// ── qIoWrite()：任意 QIODevice 写字节（QFile 与 QBuffer 通吃）──────────────
// qFileWrite() 的形参写死 QFile&，但 Qt4 的 QIODevice::write(const QByteArray&)
// 是基类虚函数，QBuffer / QFile / QTemporaryFile 都能调——stickerstore.cpp 的
// 写缓冲正是 QBuffer（如 QBuffer wb; wb.open(WriteOnly); ... wb.write(...)）。
// Qt3 侧统一转发到 QIODevice::writeBlock。
inline long qIoWrite(QIODevice& dev, const QByteArray& data)
{
#if QT_VERSION < 0x040000
    return (long)dev.writeBlock(data.data(), (Q_ULONG)data.size());
#else
    return (long)dev.write(data);
#endif
}

// ── qBufferSetData()：QBuffer::setData（Qt3 只有 setBuffer）──────────────────
// Qt3.5 的 QBuffer 是 setBuffer(QByteArray)（qbuffer.h:58，按值传），Qt4+ 才是
// setData(const QByteArray&) 且是隐式共享引用。
//
// ⚠ 语义差异（已实测，Qt3.5 + /opt/qt338sh）：Qt3 的 setBuffer 是**快照深拷贝**——
//   设 buffer 后改源 QByteArray，buffer() 内容不变；Qt4+ 的 setData 是隐式共享
//   （改源会影响 buffer）。故 Qt3 版对「setData 后只读」的用法（stickerstore 的
//   7 处 `QBuffer probe; probe.setData(bytes);` 全部如此）**行为等价**；
//   若日后出现「setData 后改源、期望 buffer 跟着变」的用法则不成立，须改写该处。
// ⚠ Qt3 的 QByteArray 是 QMemArray<char>（qcstring.h:98），**无隐式共享**，
//   故拷贝是 Qt3 的固有行为，无法也不必对齐 Qt4+。
inline bool qBufferSetData(QBuffer& buf, const QByteArray& data)
{
#if QT_VERSION < 0x040000
    return buf.setBuffer(data);
#else
    buf.setData(data);
    return true;
#endif
}

// ── qBufferMake()：让 QBuffer 绑定到一块外部 QByteArray ───────────────────
// ⚠ 形参用**出参**而非返回值：QBuffer 两版本都禁拷贝
//   —— Qt3 把拷贝构造放在 private（qbuffer.h:87 `QBuffer(const QBuffer&)`），
//      Qt6 更是直接 `Q_DISABLE_COPY`（qbuffer.h:61）。
//   故 `return QBuffer(ba);` 那种写法两版本都编不过，只能就地绑定。
//
// ⚠ 绑定 API 两版本同名不同参，但**语义一致**（都别名到调用方那块内存）：
//   Qt3 `bool setBuffer(QByteArray)`（qbuffer.h:58）形参虽**按值**，但
//      Qt3 的 QByteArray 是 QMemArray<char>、带引用计数，按值传参共享存储。
//      实测 setBuffer(ba) 后 `b.buffer().data() == ba.data()` 为真，写进去
//      的内容 ba 立即可见（QBuffer 覆写了 ba 的旧内容 "XY"）。
//      ⚠ 别按「C++ 按值传参 = 深拷贝」推断这里不回写 —— 实测是回写的。
//   Qt6 `void setBuffer(QByteArray*)`（qbuffer.h:33）直接用那块内存，
//      Qt 官方文档明写 "write something into the QBuffer, byteArray
//      **will be modified**"。
//   ⚠ 别用 Qt6 的 `setData(const QByteArray&)`（qbuffer.h:35）：文档是
//     "Sets the **contents** ... same as assigning data to buffer()"，即
//     赋值语义，与按指针绑定不是一回事。
//   ⚠ 两版本的 setBuffer 在 buffer 已 open 时都只发 qWarning 且**什么都不做**
//     （Qt6 qbuffer.cpp:200-203），故必须在 open 之前调用。
inline void qBufferMake(QBuffer& buf, QByteArray& ba)
{
#if QT_VERSION < 0x040000
    buf.setBuffer(ba);
#else
    buf.setBuffer(&ba);
#endif
}

// 取回缓冲内容。两版本下 qBufferMake 都已经让 ba 可见，故本函数**不是**
// 正确性所必需（不是「Qt3 必须显式取回」）；保留它是让调用点写法在两版本
// 统一、且不依赖上面那条别名语义。返回值都是拷贝。
inline QByteArray qBufferTake(const QBuffer& buf)
{
#if QT_VERSION < 0x040000
    return buf.buffer();
#else
    return QByteArray(buf.data(), static_cast<int>(buf.size()));
#endif
}

#endif // QLSTIK_QGLOBALTYPE_SHIM_H