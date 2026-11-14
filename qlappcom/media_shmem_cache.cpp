#include "media_shmem_cache.h"
#include <cstdio>
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
    /* QCache 超限时会静默淘汰最久未用条目，没有回调；
     * 插入前记录一次“即将溢出”的汇总日志（Qt3/Qt4 API 一致） */
    if (m_rawCache.totalCost() + len > QLAPP_MAX_MEMDIA_SHMEM_CACHE_SIZE) {
        fprintf(stderr, "[cache] media_shmem evict(auto) total=%d add=%d max=%d\n",
                (int)m_rawCache.totalCost(), len,
                (int)QLAPP_MAX_MEMDIA_SHMEM_CACHE_SIZE);
    }
    m_rawCache.insert(mxcUrl, ba, len);
}

void MediaShmemCache::clear() {
    QMutexLocker lock(&m_mutex);
    m_rawCache.clear();
}
