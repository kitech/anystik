#include "macgifconverter.h"

#if defined(Q_OS_MACOS)
#include <QGuiApplication>

static MacGifUtiConverter* s_converter = nullptr;

void ensureMacGifConverter()
{
    // QUtiMimeConverter 须在 QGuiApplication 构建后实例化；堆分配由 Qt 接管生命周期。
    if (!s_converter && QGuiApplication::instance())
        s_converter = new MacGifUtiConverter();
}

QString MacGifUtiConverter::mimeForUti(const QString &uti) const
{
    if (uti == QLatin1String("com.compuserve.gif") || uti == QLatin1String("public.gif"))
        return QLatin1String("image/gif");
    return QString();
}

QString MacGifUtiConverter::utiForMime(const QString &mime) const
{
    if (mime == QLatin1String("com.compuserve.gif") || mime == QLatin1String("public.gif"))
        return mime;                 // 允许 B 循环按原始 UTI 名直读
    if (mime == QLatin1String("image/gif"))
        return QLatin1String("com.compuserve.gif");
    return QString();
}

QVariant MacGifUtiConverter::convertToMime(const QString &mime,
                                           const QList<QByteArray> &data,
                                           const QString &uti) const
{
    if (mime == QLatin1String("image/gif")
        || mime == QLatin1String("com.compuserve.gif")
        || mime == QLatin1String("public.gif")) {
        QByteArray out;
        for (const QByteArray &chunk : data)
            out += chunk;
        if (!out.isEmpty())
            return QVariant(out);
    }
    return QVariant();
}

QList<QByteArray> MacGifUtiConverter::convertFromMime(const QString &,
                                                      const QVariant &,
                                                      const QString &) const
{
    return QList<QByteArray>(); // 只读，从不写入剪贴板
}

#else

void ensureMacGifConverter() {}

#endif // Q_OS_MACOS