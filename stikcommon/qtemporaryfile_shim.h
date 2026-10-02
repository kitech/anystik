#ifndef QLSTIK_QTEMPORARYFILE_SHIM_H
#define QLSTIK_QTEMPORARYFILE_SHIM_H

// QTemporaryFile 垫片（Qt3 专用）。
//
// QTemporaryFile 随 Qt 4.3 引入，Qt3.5 无此类。它在 Qt4/5/6 的语义是：
//   * 构造传「模板名」（须含至少 6 个连续 'X'），据此在目标目录挑一个**当下不存在**
//     的唯一文件名；open() 才真正创建文件；fileName() 返回该路径；
//   * 默认 autoRemove=true，即 QFile 析构时删掉自己创建的文件；
//     setAutoRemove(false) 关掉该行为。
//   * **只有本对象挑出来的名字**才算「自己创建的」：调用方用 setFileName()
//     指定的名字（Qt3 QFile 的改名适配，原生 QTemporaryFile 无此方法）
//     归调用方所有，析构绝不删 —— 锁定该行为的是 m_ownsName 标志，
//     用例见 test_qtemporaryfile_shim.cpp:「setFileName 后 open，析构不删
//     调用方的文件」。
//
// Qt3 侧实现：手工在模板的 XXXXXX 段填入「pid + 时间戳 + 重试序号 + 随机数」，
// 用 QFile::exists() 逐个探测直到挑到不存在的名字，再交 QFile::open() 创建。
// 全程只用 Qt3 自带 API（QFile::setName / QFile::exists / QFile::remove），
// 不做裸 POSIX 调用，文件名编码转换仍走 Qt 自己的 QFile 逻辑。
//
// ⚠ 并发保证弱于 Qt 原生：Qt 原生用 open(O_CREAT|O_EXCL) 原子建文件，本垫片是
//   exists() 探测后再 open()，两步之间存在竞态窗口。批次 5 的两处调用
//   （verifyScaledResult / buildGifBytes）都是单线程临时文件，文件名带 pid +
//   时间戳 + 序号，实际撞名概率可忽略；后续 QTemporaryDir/并发场景须改回 O_EXCL。
//
// 仅 Qt3 引入（Qt4.3+ 有原生 QTemporaryFile）。本头不引用 Qt4+ 专有 API。

#if QT_VERSION < 0x040300

#include <qfile.h>
#include <qiodevice.h>
#include <qstring.h>
#include <qdatetime.h>
#include <unistd.h>
// QStringLiteral 是 Qt5 才引入的宏，Qt3/Qt4 都没有；由 qstring_shim.h 按
// QString::fromUtf8 提供（见该头 40 行注释）。
#include "qstring_shim.h"

class QTemporaryFile : public QFile
{
public:
    // 模板名须含至少 6 个连续 'X'；不符合者 open() 失败，与 Qt 原生一致。
    explicit QTemporaryFile(const QString& templateName = QString())
    {
        m_templateName = templateName;
        m_autoRemove = true;
        m_created = false;
        m_ownsName = false;
    }

    QTemporaryFile(const QString& templateName, bool autoRemoveUnused)
    {
        m_templateName = templateName;
        m_autoRemove = autoRemoveUnused;
        m_created = false;
        m_ownsName = false;
    }

    ~QTemporaryFile()
    {
        // 仅当文件确实存在才删；名字是本对象挑的，调用方在 open() 之后不会改。
        if (m_autoRemove && m_created) {
            const QString path = name();
            if (!path.isEmpty() && QFile::exists(path)) {
                QFile::remove(path);
            }
        }
    }

    void setAutoRemove(bool autoRemove) { m_autoRemove = autoRemove; }
    bool autoRemove() const { return m_autoRemove; }

    void setFileTemplate(const QString& tpl) { m_templateName = tpl; }
    QString fileTemplate() const { return m_templateName; }

    // Qt3.5 的 QIODevice 打开模式是 int + IO_* 宏（QIODevice::OpenMode 枚举类与
    // ReadWrite/WriteOnly 成员是 Qt4 才有的），故这里用宏而非 Qt4 形态。
    bool open()
    {
        return open(IO_ReadWrite);
    }

    bool open(int mode)
    {
        if (name().isEmpty() && !createUniqueFileName()) {
            return false;
        }
        if (!QFile::open(mode)) {
            return false;
        }
        // 只认「名字是本对象挑出来的」那份文件。setFileName() 指定的名字属于
        // 调用方，析构不得删 —— Qt 原生 QTemporaryFile 根本没有 setFileName，
        // 不会有这种歧义，是 Qt3 QFile 改名适配带进来的（见成员说明）。
        m_created = m_ownsName;
        return true;
    }

    // 挑一个当前不存在的唯一文件名并 setName()，但不创建文件（对齐 Qt 原生
    // 「构造后 fileName() 即可拿到路径」的观感）。
    bool createUniqueFileName()
    {
        if (m_templateName.isEmpty()) return false;
        const int xPos = m_templateName.find(QStringLiteral("XXXXXX"));
        if (xPos < 0) return false;          // 无 X 段 → 无法生成唯一名

        for (int attempt = 0; attempt < 512; ++attempt) {
            const QString candidate = fillTemplate(xPos, attempt);
            if (!QFile::exists(candidate)) {
                setName(candidate);
                m_ownsName = true;          // 从此刻起这个名字归本对象管
                return true;
            }
        }
        return false;
    }

// ── Qt4/5/6 与 Qt3 的成员名适配 ────────────────────────────────────────
    // Qt4+ 的 QTemporaryFile 继承 QIODevice，故有 write()/read()/readAll()；
    // Qt3 只有 *Block。这里补出 Qt4 形态，使调用方（stickerstore.cpp:1161 的
    // `tmp.write(scaledBytes)`）方法体不改即可编译。
    //
    // 形参刻意用 Q_LONG 而非 Qt4 的 qint64：Qt3 的 writeBlock/readBlock 本就收
    // Q_LONG/Q_ULONG（非 Win64 上是 `long`），沿用同一类型可避免调用点传 int
    // 字面量时的 "invalid conversion" 报错；语义与 qFileWrite() 一致（写全部、
    // 返回实际写入字节数）。
    Q_LONG write(const QByteArray& data)
    {
        if (data.isEmpty()) return 0;
        return writeBlock(data.data(), static_cast<Q_ULONG>(data.size()));
    }
    Q_LONG write(const char* data, Q_LONG len)
    {
        return writeBlock(data, static_cast<Q_ULONG>(len));
    }
    Q_LONG read(char* data, Q_LONG maxlen)
    {
        return readBlock(data, static_cast<Q_ULONG>(maxlen));
    }
    QByteArray readAll() { return QIODevice::readAll(); }

    QString fileName() const { return name(); }

    // Qt3 QFile 的改名适配（Qt4+ 用 fileName/setFileName）。⚠ 注意：Qt 原生
    // QTemporaryFile **没有** setFileName（只有 setFileTemplate）。故这里显式
    // 把所有权标记清掉 —— 否则「setFileName 到调用方自己的文件 → open() →
    // 析构」会把那个文件删掉，而本对象从未创建过它。
    void setFileName(const QString& n) { setName(n); m_ownsName = false; }

private:
    QString fillTemplate(int xPos, int attempt) const
    {
        static uint seed = 0;
        if (seed == 0) {
            seed = uint(QTime::currentTime().msec())
                 ^ uint(uint(QDateTime::currentDateTime().toString(
                        QStringLiteral("yyyyMMddhhmmsszzz")).length()));
        }

        const int room = 6;                  // XXXXXX 段固定 6 位
        QString fill;
        fill += QString::number(uint(getpid()));
        fill += QString::number(uint(QTime::currentTime().msec()));
        fill += QString::number(attempt);
        fill += QString::number(int(seed % 9973));
        if (fill.length() > room) {
            fill = fill.right(room);         // 位数不够时截尾，保证总长不变
        }
        return m_templateName.left(xPos) + fill
             + m_templateName.mid(xPos + room);
    }

    QString m_templateName;
    bool m_autoRemove;
    bool m_created;         // 本对象创建了当前名字那份文件 → 析构可删
    bool m_ownsName;        // 当前名字是 createUniqueFileName() 挑出来的（未经 setFileName）
};

#endif // QT_VERSION < 0x040300
#endif // QLSTIK_QTEMPORARYFILE_SHIM_H