#include "media_shmem_cache.h"
#include <cstring>

#ifdef QT3_BUILD
#include <qmutex.h>
#else
#include <QMutex>
#endif

MediaShmemCache& MediaShmemCache::inst() {
    static MediaShmemCache s;
    return s;
}

QByteArray MediaShmemCache::getThumb(const QString& mxcUrl) {
    QMutexLocker lock(&m_mutex);
#ifdef QT3_BUILD
    if (auto* data = m_rawCache.find(mxcUrl)) {
#else
    if (auto* data = m_rawCache.object(mxcUrl)) {
#endif
        return *data;
    }
    return QByteArray();
}

void MediaShmemCache::putThumb(const QString& mxcUrl, const char* data, int len) {
    QByteArray* ba = new QByteArray();
    ba->resize(len);
    if (len > 0 && data) {
        ::memcpy(ba->data(), data, len);
    }
    QMutexLocker lock(&m_mutex);
    m_rawCache.insert(mxcUrl, ba, len);
}

void MediaShmemCache::clear() {
    QMutexLocker lock(&m_mutex);
    m_rawCache.clear();
}
