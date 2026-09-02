#ifndef MAC_PASTEBOARD_H
#define MAC_PASTEBOARD_H

#include <QByteArray>
#include <QList>
#include <QString>

// macOS：绕过 Qt 的 UTI↔MIME 映射，直接从系统 NSPasteboard 按原始类型名
// （如 com.compuserve.gif / public.gif）读取原始字节；其它平台返回空。
QByteArray macPasteboardData(const char* typeName);

// 剪贴板候选：一个 UTI flavor 的原始字节（图像），或 public.file-url 的文件引用。
struct MacPasteCandidate {
    QString type;        // UTI 类型名（诊断用）
    QByteArray data;     // 图像 flavor 的原始字节；public.file-url 时为空
    bool isFileUrl = false;
    QString filePath;    // public.file-url 解析后的本地路径
};

// macOS：枚举 generalPasteboard 全部 flavor，按魔数收集图像候选
// （GIF/WebP/PNG/TIFF/JPEG/BMP），解析 public.file-url，同内容去重。
// Qt 预定义 UTI 表不看 GIF/PNG/APNG/WebP 原始字节，此路径是权威直读。
// 其它平台返回空列表。
QList<MacPasteCandidate> macPasteboardCollect();

#endif // MAC_PASTEBOARD_H