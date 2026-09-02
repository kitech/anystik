#ifndef MAC_GIF_CONVERTER_H
#define MAC_GIF_CONVERTER_H

#include <QByteArray>
#if defined(Q_OS_MACOS)
#include <QtGui/qutimimeconverter.h>
#include <QList>
#include <QVariant>
#endif

// 纯 C++ 方案（无 ObjC/.mm）：注册常见图像 UTI ⇄ MIME 转换器，覆盖
//   com.compuserve.gif / public.gif ⇄ image/gif
//   public.png ⇄ image/png, image/apng
//   public.jpeg ⇄ image/jpeg
//   org.webmproject.webp ⇄ image/webp
// 使 mime->data("image/gif"|"image/png"…) 能取到剪贴板原始字节，
// 并使 setData(...) 写剪贴板时原样落盘对应 UTI。仅 macOS 有实现；其它平台空实现。
void ensureMacGifConverter();

#if defined(Q_OS_MACOS)
class MacGifUtiConverter : public QUtiMimeConverter
{
public:
    MacGifUtiConverter() = default;

protected:
    QString mimeForUti(const QString &uti) const override;
    QString utiForMime(const QString &mime) const override;
    QVariant convertToMime(const QString &mime, const QList<QByteArray> &data, const QString &uti) const override;
    QList<QByteArray> convertFromMime(const QString &mime, const QVariant &data, const QString &uti) const override;
};
#endif

#endif // MAC_GIF_CONVERTER_H