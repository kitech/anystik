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

#else

QByteArray macPasteboardData(const char*)
{
    return QByteArray();
}

#endif // Q_OS_MACOS