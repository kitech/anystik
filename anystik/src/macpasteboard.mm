#include "macpasteboard.h"

#if defined(Q_OS_MACOS)
#import <AppKit/AppKit.h>
#include <QByteArray>

QByteArray macPasteboardData(const char* typeName)
{
    if (!typeName || !typeName[0])
        return QByteArray();

    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    NSString* type = [NSString stringWithUTF8String:typeName];
    NSData* data = [pb dataForType:type];
    if (data == nil)
        return QByteArray();

    const NSUInteger len = data.length;
    if (len == 0)
        return QByteArray();

    QByteArray out(int(len), Qt::Uninitialized);
    [data getBytes:out.data() length:len];
    return out;
}

QByteArray macPasteboardGifLike()
{
    // 枚举 generalPasteboard 全部 flavor，选疑似动画图像（GIF/WebP/PNG magic）中
    // 字节数最大者。QQ 类非标准 UTI 往往把完整动图存在 com.compuserve.gif/public.gif
    // 之外的自定义 flavor 下；Qt 桥读到的 image/gif 只含重编码单帧，此处直取原始多帧。
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    QByteArray best;
    NSUInteger bestLen = 0;
    for (NSString* t in [pb types]) {
        NSData* d = [pb dataForType:t];
        NSUInteger len = d ? d.length : 0;
        if (len <= bestLen) continue;                 // 优先更大（更可能完整动画）
        if (len < 4) continue;
        const unsigned char* b = (const unsigned char*)d.bytes;
        bool gif  = b[0] == 0x47 && b[1] == 0x49 && b[2] == 0x46 && b[3] == 0x38; // GIF8
        bool webp = b[0] == 'R' && b[1] == 'I' && b[2] == 'F' && b[3] == 'F';    // RIFF(→WEBP)
        bool png  = b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G';   // PNG
        if (!(gif || webp || png)) continue;
        QByteArray out(int(len), Qt::Uninitialized);
        [d getBytes:out.data() length:len];
        best = out;
        bestLen = len;
    }
    return best;
}

void macPasteboardEnumDump()
{
    // 诊断：打印全部 flavor 类型名、字节数、前 4 字节 magic，用于定位真实动画 flavor。
    @autoreleasepool {
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        int i = 0;
        for (NSString* t in [pb types]) {
            NSData* d = [pb dataForType:t];
            const unsigned char* b = (const unsigned char*)(d ? d.bytes : NULL);
            int m0 = -1, m1 = -1, m2 = -1, m3 = -1;
            if (d && d.length >= 4) { m0 = b[0]; m1 = b[1]; m2 = b[2]; m3 = b[3]; }
            fprintf(stderr, "[StickerPaste][pbtypes][%d] type=%s len=%lld magic=%02x%02x%02x%02x\n",
                    i++, [t UTF8String], (long long)(d ? d.length : 0),
                    m0, m1, m2, m3);
        }
    }
}

#else

QByteArray macPasteboardData(const char*)
{
    return QByteArray();
}

QByteArray macPasteboardGifLike()
{
    return QByteArray();
}

void macPasteboardEnumDump()
{
}

#endif // Q_OS_MACOS