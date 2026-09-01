#ifndef MAC_GIF_CONVERTER_H
#define MAC_GIF_CONVERTER_H

#include <QByteArray>
#if defined(Q_OS_MACOS)
#include <QUtiMimeConverter>
#include <QList>
#include <QVariant>
#endif

// 纯 C++ 方案（无 ObjC/.mm）：注册 com.compuserve.gif / public.gif ⇄ image/gif
// 的 UTI↔MIME 转换器，使 mime->data("image/gif") 能取到剪贴板原始 GIF 字节。
// 仅在 macOS 有实际实现；其它平台为空实现。
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