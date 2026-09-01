#ifndef MAC_PASTEBOARD_H
#define MAC_PASTEBOARD_H

#include <QByteArray>

// macOS：绕过 Qt 的 UTI↔MIME 映射，直接从系统 NSPasteboard 按原始类型名
// （如 com.compuserve.gif / public.gif）读取原始字节；其它平台返回空。
QByteArray macPasteboardData(const char* typeName);

#endif // MAC_PASTEBOARD_H