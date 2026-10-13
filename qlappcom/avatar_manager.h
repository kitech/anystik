#ifndef AVATAR_MANAGER_H
#define AVATAR_MANAGER_H

#include "compat34.h"
#include <qpixmap.h>
#include <qstring.h>
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

private:
    AvatarManager() = default;
    static QPixmap makeThumbnail(const QPixmap& source, int size);
    std::map<QString, QPixmap> m_cache;
    std::set<QString> m_pending;
};

#endif
