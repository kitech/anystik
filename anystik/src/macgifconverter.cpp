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
                                                      const QVariant &data,
                                                      const QString &) const
{
    // 写方向：mime->setData("image/gif", bytes) + setMimeData 时，
    // Qt 经 utiForMime→com.compuserve.gif 调用本函数，把字节原样写入该 UTI。
    // 这是 mac 原生 App（微信/QQ/Keynote 等）能拿到动画 GIF 字节的关键。
    if (data.canConvert<QByteArray>()) {
        const QByteArray bytes = data.toByteArray();
        if (!bytes.isEmpty())
            return {bytes};
    }
    if (data.canConvert<QList<QByteArray>>()) {
        const QList<QByteArray> chunks = data.value<QList<QByteArray>>();
        if (!chunks.isEmpty())
            return chunks;
    }
    return QList<QByteArray>();
}

#else

void ensureMacGifConverter() {}

#endif // Q_OS_MACOS