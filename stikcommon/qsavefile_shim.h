#ifndef QLSTIK_QSAVEFILE_SHIM_H
#define QLSTIK_QSAVEFILE_SHIM_H

// QSaveFile 垫片（Qt3/Qt4 专用）。
//
// ⚠ 计划更正：QSaveFile 并非"Qt4.1+ 原生"——它在 Qt **5.1** 才引入。
// 实测 /opt/qt487sh/include/QtCore 下无 qsavefile.h，Qt3/Qt4 两者皆缺，
// 故本垫片覆盖 Qt3 **与** Qt4（与 QJson / QStandardPaths 同为 Qt5 才有的类）。
//
// 语义目标：与 Qt 原生 QSaveFile 一致的"原子提交"——写同目录临时文件，commit()
// 时 rename 覆盖。中断/崩溃/写失败都不破坏原文件（davbisync_baseline 的核心
// 诉求：基线不可损毁，abort 不毁旧基线）。
//
// ── 为何 rename 走 POSIX 而非 QFile ──────────────────────────────────────
// Qt 3.5 的 QFile **没有** rename() 方法（Qt 4.0 才补上），且 Qt3/Qt4 的
// QFile::rename 在目标已存在时不会覆盖。故统一用 ::rename(2)（<cstdio>）：
//   * 两版本签名一致，无需条件编译
//   * POSIX rename(2) 原子替换目标，这正是"原子提交"要的语义
//   * 同目录 → 同一文件系统，不会 EXDEV 失败
//
// ── 为何不用 QTemporaryFile ──────────────────────────────────────────────
// 它默认落系统临时目录 → 跨文件系统 rename 必失败（EXDEV），原子性就没了；
// 且需额外把文件 move 过来。直接自建同目录临时名更可控。
//
// ── 两代 QIODevice API 差异（已在实现里逐一条件化）───────────────────────
//   Qt3.5：Q_LONG(64位) 写读接口是 writeBlock/readBlock，QIODevice 无 error()
//   Qt4.8：qint64 写读接口是 write/read，QIODevice 有 error()
//   errorString() 两版都只在 QFile 上（Qt3 注释明写 "Qt 4: move into QIODevice"）
//
// ── 已知差异（刻意）──────────────────────────────────────────────────────
//   * 原生 setDirectWriteFallback()（rename 失败退化为直写）不实现——退化直写
//     会丢原子性，与本模块用途相悖；davbisync_baseline 也未调用。
//   * 原生 fileEncoding()/setDirectWriteFallback 等不实现，均未被调用。

#include "qglobaltype_shim.h"   // Qt3 的 qint64（Qt4+ 用原生）

#ifdef QT3_BUILD
// Qt3.5 无 CamelCase 转发头；open 模式是 IO_* 宏，无 QIODevice::ReadWrite 枚举
#include <qstring.h>
#include <qiodevice.h>
#include <qfile.h>
#else
#include <QString>
#include <QIODevice>
#include <QFile>
#endif

class QSaveFile
{
public:
    explicit QSaveFile(const QString& fileName);
    ~QSaveFile();

#ifdef QT3_BUILD
    bool open(int mode = IO_ReadWrite);
#else
    bool open(QIODevice::OpenMode mode = QIODevice::ReadWrite);
#endif
    bool commit();
    void cancelWriting();

    // 转发到临时文件，使 f.write(QByteArray) 可用。
    // 返回值统一为 qint64（Qt3 下 QFile::writeBlock 返回 Q_LONG，同为 64 位有符号，
    // 转换安全），调用方 davbisync_baseline 只判 <0。
    qint64 write(const char* data, qint64 len);
    qint64 write(const QByteArray& data);
    qint64 read(char* data, qint64 maxlen);
    bool  atEnd() const;
    void  close();

    QString fileName() const { return m_fileName; }
    bool    error() const { return m_error; }
    QString errorString() const;

private:
    QSaveFile(const QSaveFile&);            // 不可拷贝（同 QFile）
    QSaveFile& operator=(const QSaveFile&);

    QString m_fileName;    // 目标路径
    QString m_tmpName;     // 临时路径（同目录）
    QFile*  m_tmpFile;
    bool    m_open;
    bool    m_error;
};

#endif // QLSTIK_QSAVEFILE_SHIM_H
