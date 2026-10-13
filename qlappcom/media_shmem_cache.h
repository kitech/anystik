#ifndef MEDIA_SHMEM_CACHE_H
#define MEDIA_SHMEM_CACHE_H

#include <qstring.h>
#ifdef QT3_BUILD
#include <qmutex.h>
#include <qcache.h>
#else
#include <QMutex>
#include <QCache>
#endif

#define QLAPP_MAX_MEMDIA_SHMEM_CACHE_SIZE (16 * 1024 * 1024) // default 64m

class MediaShmemCache {
public:
    static MediaShmemCache& inst();
    QByteArray getThumb(const QString& mxcUrl);
    void putThumb(const QString& mxcUrl, const char* data, int len);
    void clear();
private:
    MediaShmemCache() : m_rawCache(QLAPP_MAX_MEMDIA_SHMEM_CACHE_SIZE) {}
#ifdef QT3_BUILD
    QCache<QByteArray> m_rawCache;
#else
    QCache<QString, QByteArray> m_rawCache;
#endif
    QMutex m_mutex;
};

#endif
