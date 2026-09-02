#include "macpasteboard.h"

#if defined(Q_OS_MACOS)
#import <AppKit/AppKit.h>
#import <stdio.h>
#import <string.h>
#include <QByteArray>
#include <QFileInfo>
#include <QUrl>

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

QList<MacPasteCandidate> macPasteboardCollect()
{
    // 权威读取：枚举 generalPasteboard 全部 flavor，按魔数收集图像候选并解析 file-url。
    // 非标准 UTI（如 QQ 特有 flavor）装完整动图、Qt 桥读到的 image/gif 常只是单帧重编码；
    // public.tiff 与 NeXT TIFF 等重复 flavor 按内容去重。
    QList<MacPasteCandidate> out;
    @autoreleasepool {
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        int idx = 0;
        for (NSString* t in [pb types]) {
            const char* tn = [t UTF8String];
            NSData* d = [pb dataForType:t];
            const NSUInteger len = d ? d.length : 0;
            const unsigned char* b = (const unsigned char*)(d ? d.bytes : NULL);
            // 全 flavor 诊断：长度 + 前16字节 hex（public.png/Apple PNG/org.w3.*/dyn.* 均可见）
            char hex[48];
            const int nh = int(len < 16 ? len : 16);
            for (int i = 0; i < nh; i++)
                snprintf(hex + i * 2, 3, "%02x", b ? b[i] : 0);
            hex[nh * 2] = '\0';
            fprintf(stderr, "[StickerPaste][pbtypes][%d] type=%s len=%lld head=%s%s\n",
                    idx++, tn, (long long)len, hex, len > 16 ? "..." : "");

            // public.file-url：源端文件引用（Finder 粘贴=复制原文件，动画完整）
            if (tn && strcmp(tn, "public.file-url") == 0) {
                NSString* urlStr = [pb stringForType:t];
                if (urlStr && urlStr.length > 0) {
                    const QUrl u = QUrl(QString::fromUtf8([urlStr UTF8String]));
                    if (u.isLocalFile()) {
                        MacPasteCandidate c;
                        c.type = QLatin1String(tn);
                        c.isFileUrl = true;
                        c.filePath = u.toLocalFile();
                        out << c;
                        fprintf(stderr, "            file-url file=%s\n",
                                c.filePath.toUtf8().constData());
                    }
                }
                continue;
            }

            // 承诺式文件引用：public.file-url 为空时 QQ/Chromium 可能只答应给文件
            if (tn && (strcmp(tn, "com.apple.pasteboard.promised-file-url") == 0
                    || strcmp(tn, "com.apple.pasteboard.promised-file-content-type") == 0)) {
                NSString* urlStr = [pb stringForType:t];
                if (urlStr && urlStr.length > 0) {
                    const QUrl u = QUrl(QString::fromUtf8([urlStr UTF8String]));
                    if (u.isLocalFile() && QFileInfo::exists(u.toLocalFile())) {
                        MacPasteCandidate c;
                        c.type = QLatin1String(tn);
                        c.isFileUrl = true;
                        c.filePath = u.toLocalFile();
                        out << c;
                        fprintf(stderr, "            promised-file-url file=%s\n",
                                c.filePath.toUtf8().constData());
                    }
                }
                continue;
            }

            if (len < 4) continue;
            const bool gif  = b[0] == 'G' && b[1] == 'I' && b[2] == 'F' && b[3] == '8';
            const bool webp = len >= 12 && b[0] == 'R' && b[1] == 'I' && b[2] == 'F' && b[3] == 'F'
                              && b[8] == 'W' && b[9] == 'E' && b[10] == 'B' && b[11] == 'P';
            const bool png  = b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G';
            const bool tiff = (b[0] == 'M' && b[1] == 'M' && b[2] == 0x00 && b[3] == 0x2a)
                           || (b[0] == 'I' && b[1] == 'I' && b[2] == 0x2a && b[3] == 0x00);
            const bool jpeg = b[0] == 0xFF && b[1] == 0xD8;
            const bool bmp  = b[0] == 'B' && b[1] == 'M';
            if (!(gif || webp || png || tiff || jpeg || bmp)) continue;

            QByteArray data(int(len), Qt::Uninitialized);
            [d getBytes:data.data() length:len];

            // 去重：同内容 flavor（public.tiff 与 "NeXT TIFF v4.0 pasteboard type" 同字节）
            bool dup = false;
            for (const MacPasteCandidate& c : out) {
                if (!c.isFileUrl && c.data == data) {
                    dup = true;
                    break;
                }
            }
            if (dup) {
                fprintf(stderr, "[StickerPaste][pbtypes][%d] type=%s len=%lld magic=%02x%02x%02x%02x dup=skip\n",
                        idx++, tn, (long long)len, b[0], b[1], b[2], b[3]);
                continue;
            }

            MacPasteCandidate c;
            c.type = QLatin1String(tn);
            c.data = data;
            out << c;
            fprintf(stderr, "[StickerPaste][pbtypes][%d] type=%s len=%lld magic=%02x%02x%02x%02x\n",
                    idx++, tn, (long long)len, b[0], b[1], b[2], b[3]);
        }
    }
    return out;
}

#else

QByteArray macPasteboardData(const char*)
{
    return QByteArray();
}

QList<MacPasteCandidate> macPasteboardCollect()
{
    return QList<MacPasteCandidate>();
}

#endif // Q_OS_MACOS