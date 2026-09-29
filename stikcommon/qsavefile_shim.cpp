// QSaveFile 垫片实现（Qt3/Qt4）。原子性与两代 API 差异说明见 qsavefile_shim.h。

#include "qsavefile_shim.h"
#include "qstring_shim.h"       // QLatin1Char：Qt3 由 qstring_shim 提供
#include "qglobaltype_shim.h"   // qAbsPath：Qt3/Qt4/Qt6 通用目录绝对路径

#include <cstdio>    // ::rename(2)

#ifdef QT3_BUILD
#include <qfileinfo.h>
#include <qdir.h>
#else
#include <QFileInfo>
#include <QDir>
#endif

// 在目标同目录生成一个未被占用的临时文件名（同目录 = 同文件系统，rename 不会 EXDEV）
static QString mkTmpName(const QString& target)
{
    const QFileInfo fi(target);
    const QString dir = qAbsPath(fi);
    const QString base = fi.fileName();
    for (int i = 0; i < 4096; ++i) {
        const QString cand = dir + QLatin1Char('/') + base
                             + QString(".tmp") + QString::number(i);
        if (!QFileInfo(cand).exists()) {
            return cand;
        }
    }
    return QString();
}

// ::rename(2) 原子替换。QString → 本地编码字节串（用 QFile::encodeName，
// 与 Qt3/Qt4 自己的文件 API 同一套编码规则）。
static bool atomicRename(const QString& from, const QString& to)
{
    const QByteArray f = QFile::encodeName(from);
    const QByteArray t = QFile::encodeName(to);
    if (f.isEmpty() || t.isEmpty()) {
        return false;
    }
    return ::rename(f.data(), t.data()) == 0;
}

QSaveFile::QSaveFile(const QString& fileName)
    : m_fileName(fileName), m_tmpFile(0), m_open(false), m_error(false)
{
}

QSaveFile::~QSaveFile()
{
    // 未 commit 就析构 = 放弃本次写入：删临时文件，原文件保持不动
    if (m_tmpFile) {
        if (m_open) {
            m_tmpFile->close();
        }
        m_tmpFile->remove();
        delete m_tmpFile;
        m_tmpFile = 0;
    }
}

#ifdef QT3_BUILD
bool QSaveFile::open(int mode)
{
    if (m_tmpFile) {
        return false;                        // 已打开
    }
    m_tmpName = mkTmpName(m_fileName);
    if (m_tmpName.isEmpty()) {
        m_error = true;
        return false;
    }
    m_tmpFile = new QFile(m_tmpName);
    if (!m_tmpFile->open(mode)) {            // Qt3 QFile::open 收 int
        delete m_tmpFile;
        m_tmpFile = 0;
        m_error = true;
        return false;
    }
    m_open = true;
    m_error = false;
    return true;
}
#else
bool QSaveFile::open(QIODevice::OpenMode mode)
{
    if (m_tmpFile) {
        return false;                        // 已打开
    }
    m_tmpName = mkTmpName(m_fileName);
    if (m_tmpName.isEmpty()) {
        m_error = true;
        return false;
    }
    m_tmpFile = new QFile(m_tmpName);
    if (!m_tmpFile->open(mode)) {
        delete m_tmpFile;
        m_tmpFile = 0;
        m_error = true;
        return false;
    }
    m_open = true;
    m_error = false;
    return true;
}
#endif

bool QSaveFile::commit()
{
    if (!m_tmpFile || !m_open) {
        m_error = true;
        return false;
    }
    m_tmpFile->flush();
    m_tmpFile->close();
    m_open = false;

    if (!atomicRename(m_tmpName, m_fileName)) {
        m_error = true;
        return false;                        // 临时文件留待析构清理
    }
    delete m_tmpFile;
    m_tmpFile = 0;
    return true;
}

void QSaveFile::cancelWriting()
{
    if (m_tmpFile) {
        if (m_open) {
            m_tmpFile->close();
        }
        m_tmpFile->remove();
        delete m_tmpFile;
        m_tmpFile = 0;
    }
    m_open = false;
}

qint64 QSaveFile::write(const char* data, qint64 len)
{
    if (!m_tmpFile || !m_open) {
        m_error = true;
        return -1;
    }
#ifdef QT3_BUILD
    // Qt3.5 的 QIODevice 写接口是 writeBlock
    return (qint64)m_tmpFile->writeBlock(data, (Q_ULONG)len);
#else
    return m_tmpFile->write(data, len);
#endif
}

qint64 QSaveFile::write(const QByteArray& data)
{
    return write(data.data(), data.size());
}

qint64 QSaveFile::read(char* data, qint64 maxlen)
{
    if (!m_tmpFile || !m_open) {
        m_error = true;
        return -1;
    }
#ifdef QT3_BUILD
    return (qint64)m_tmpFile->readBlock(data, (Q_ULONG)maxlen);
#else
    return m_tmpFile->read(data, maxlen);
#endif
}

bool QSaveFile::atEnd() const
{
    return m_tmpFile ? m_tmpFile->atEnd() : true;
}

void QSaveFile::close()
{
    if (m_tmpFile && m_open) {
        m_tmpFile->close();
        m_open = false;
    }
}

QString QSaveFile::errorString() const
{
    if (!m_error) {
        return QString();
    }
    if (m_tmpFile) {
        return m_tmpFile->errorString();     // Qt3/Qt4 都在 QFile 上
    }
    return QString("QSaveFile: write or commit failed");
}
