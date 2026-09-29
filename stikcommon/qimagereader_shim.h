#ifndef QLSTIK_QIMAGEREADER_SHIM_H
#define QLSTIK_QIMAGEREADER_SHIM_H

// Qt3 无 QImageReader（Qt4 引入）。imageaiutil 只用 format() 与
// setAutoTransform()：从设备读全部字节，按 magic 探格式。"jpg"/"jpeg"→"jpg"，
// "png"→"png"，其他→空串（对齐 Qt 的 QImageFormat 命名惯例）。
// setAutoTransform：Qt3 无 EXIF 方向处理，no-op 语义等价（探测不依赖方向）。

#if QT_VERSION < 0x040000
#include <qiodevice.h>
#include <qcstring.h>

class QImageReader
{
public:
    explicit QImageReader(QIODevice* device)
    {
        if (device) {
            // 调用方（imageaiutil）已 open 过；Qt3 QBuffer 二次 open 返回 false，
            // 故按当前设备状态读，读完仅在"本次打开"时关闭。
            const bool weOpened = !device->isOpen();
            if (weOpened) {
                device->open(IO_ReadOnly);
            }
            m_bytes = device->readAll();
            if (weOpened) {
                device->close();
            }
        }
        detect();
    }
    void setAutoTransform(bool) {}
    // 返回 QCString（Qt3 下可 .lower()/.stripWhiteSpace()，==const char* 无歧义）；
    // format() 探测结果本身已小写：png / jpg（jpeg 归一为 jpg）
    QCString format() const { return m_fmt; }

private:
    void detect()
    {
        const unsigned char* p = (const unsigned char*)m_bytes.data();
        const uint n = m_bytes.length();
        if (n >= 8 && p[0] == 0x89 && p[1] == 'P' && p[2] == 'N' && p[3] == 'G') {
            m_fmt = QCString("png");
        } else if (n >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) {
            m_fmt = QCString("jpg");
        }
    }

    QCString m_bytes;
    QCString m_fmt;
};

#endif // QT_VERSION < 0x040000

#endif // QLSTIK_QIMAGEREADER_SHIM_H