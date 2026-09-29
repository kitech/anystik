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
// Qt4+ 用 QIODevice::ReadOnly 等枚举——这是没法放进一个类型里的差异，交给
// davbisync_baseline.cpp 顶部的"Qt3 API 适配区"集中映射，本头不管。
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
#else
#include <QString>
#include <QDir>
#include <QFileInfo>
#endif

#if QT_VERSION < 0x040000
typedef long long qint64;
typedef unsigned long long quint64;
#ifndef Q_INT64_C
#define Q_INT64_C(c)   c##LL
#define Q_UINT64_C(c)  c##ULL
#endif
#endif

static inline QString qAbsPath(const QFileInfo& fi)
{
#ifdef QT3_BUILD
    return fi.dirPath(true);        // QT3_SUPPORT 的 absolutePath() 替身
#else
    return fi.absolutePath();
#endif
}

static inline bool qMkdir(const QString& path)
{
#ifdef QT3_BUILD
    // QDir 无 mkpath；逐级 mkdir。与 qlcomp/compatcore34.cpp 的 qMkdir 同思路。
    QString p = path;
    while (p.endsWith(QChar('/'))) {
        p.truncate(p.length() - 1);
    }
    if (p.isEmpty()) {
        return false;
    }
    int slashPos = 0;
    while ((slashPos = p.find('/', slashPos + 1)) != -1) {
        QDir().mkdir(p.left(slashPos));
    }
    return QDir().mkdir(p);
#else
    return QDir().mkpath(path);
#endif
}

#endif // QLSTIK_QGLOBALTYPE_SHIM_H