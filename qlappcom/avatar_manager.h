#ifndef AVATAR_MANAGER_H
#define AVATAR_MANAGER_H

#include "compatcore34.h"
#include <qpixmap.h>
#include <qstring.h>
#include <list>
#include <map>
#include <set>

class AvatarManager {
public:
    static AvatarManager& inst();

    QPixmap get(const QString& mxcUrl,
                const QString& senderName, int peerNumber, int size);

    bool requestDownload(const QString& mxcUrl);

    void store(const QString& mxcUrl, const QPixmap& source, int size);
    void removePending(const QString& mxcUrl);
    void clear();

    /* 诊断：缓存条目数 / 估算字节数（每像素按 depth/8 计） */
    int cacheCount() const { return (int)m_cache.size(); }
    long long cacheBytes() const { return m_cacheBytes; }
    static const int kCacheMax = 4096;

private:
    AvatarManager() = default;
    struct CacheEntry {
        QPixmap pm;
        std::list<QString>::iterator order;
    };
    static QPixmap makeThumbnail(const QPixmap& source, int size);
    static long long pixmapBytes(const QPixmap& pm);
    void cachePut(const QString& mxcUrl, const QPixmap& pm);
    std::map<QString, CacheEntry> m_cache;
    std::list<QString> m_order;      /* FIFO 淘汰顺序 */
    long long m_cacheBytes = 0;
    std::set<QString> m_pending;
};

#endif
