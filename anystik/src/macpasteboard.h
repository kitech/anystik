#ifndef MAC_PASTEBOARD_H
#define MAC_PASTEBOARD_H

#include <QByteArray>

// macOS：绕过 Qt 的 UTI↔MIME 映射，直接从系统 NSPasteboard 按原始类型名
// （如 com.compuserve.gif / public.gif）读取原始字节；其它平台返回空。
QByteArray macPasteboardData(const char* typeName);

// macOS：枚举 generalPasteboard 全部 flavor，收集疑似动画图像（GIF/WebP/PNG magic）
// 中字节数最大者的原始字节（非标准 UTI 动画，如 QQ 特有 flavor，装完整动图）；
// 无命中返回空。其它平台返回空。
QByteArray macPasteboardGifLike();

// macOS：诊断——枚举 generalPasteboard 全部 flavor，逐个打印 type/长度/magic。
void macPasteboardEnumDump();

#endif // MAC_PASTEBOARD_H