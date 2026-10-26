#include "avatar_manager.h"
#include "identicon.h"
#include "storage.h"
#include "cache_db.h"
#include <qpainter.h>
#ifdef QT3_BUILD
#include <qbitmap.h>
#include <qimage.h>
#include <qbuffer.h>
#else
#include <qpainterpath.h>
#include <QBuffer>
#endif

AvatarManager& AvatarManager::inst() {
    static AvatarManager mgr;
    return mgr;
}

static QString avatarSeed(const QString& name, int peer) {
    if (!name.isEmpty())
        return name;
    return QString::number(peer);
}

QPixmap AvatarManager::makeThumbnail(const QPixmap& source, int size) {
#ifdef QT3_BUILD
    QPixmap scaled(source.convertToImage().smoothScale(size, size));
    QBitmap mask(scaled.size(), true);
    QPainter mp(&mask);
    mp.setBrush(Qt::color1);
    mp.setPen(Qt::NoPen);
    mp.drawEllipse(0, 0, size, size);
    mp.end();
    scaled.setMask(mask);
    return scaled;
#else
    // 居中方裁：非方源先裁正方形（不拉伸），再统一缩放；圆=裁剪后全尺寸。
    // 旧写法 KeepAspectRatio 出非方图 + 圆按 size 钳在 (0,0) → 头像变形/缺角。
    const int sw = source.width(), sh = source.height();
    const int side = qMin(sw, sh);
    const QRectF crop((sw - side) / 2.0, (sh - side) / 2.0, side, side);

    QPixmap square(side, side);
    square.fill(Qt::transparent);
    {
        QPainter sp(&square);
        sp.drawPixmap(QRectF(0, 0, side, side), source, crop);
    }

    QPixmap scaled = square.scaled(size, size, Qt::IgnoreAspectRatio,
                                   Qt::SmoothTransformation);
    QPixmap circle(scaled.size());
    circle.fill(Qt::transparent);
    QPainter p(&circle);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addEllipse(0, 0, scaled.width(), scaled.height());
    p.setClipPath(path);
    p.drawPixmap(0, 0, scaled);
    p.end();
    return circle;
#endif
}

QPixmap AvatarManager::get(const QString& mxcUrl,
                           const QString& senderName, int peerNumber, int size) {
    TimePoint _t0 = timeNow();
    if (!mxcUrl.isEmpty()) {
        auto it = m_cache.find(mxcUrl);
        if (it != m_cache.end()) {
            long long _el = elapsedMs(_t0); if (_el >= 50) qWarning("SLOW [hangui] AvatarManager::get(cache) took %lldms", _el);
            return it->second;
        }

        auto dbData = Storage::instance().cacheDb()->get(
            mediaCacheKey("avatar", mxcUrl).c_str());
        QPixmap raw;
        if (!dbData.empty() && raw.loadFromData(dbData.data(), dbData.size())) {
            QPixmap scaled = makeThumbnail(raw, size);
            m_cache[mxcUrl] = scaled;
            long long _el = elapsedMs(_t0); if (_el >= 50) qWarning("SLOW [hangui] AvatarManager::get(db) took %lldms", _el);
            return scaled;
        }
    }
    long long _el = elapsedMs(_t0); if (_el >= 50) qWarning("SLOW [hangui] AvatarManager::get(identicon) took %lldms", _el);
    return generateIdenticon(avatarSeed(senderName, peerNumber), size);
}

bool AvatarManager::requestDownload(const QString& mxcUrl) {
    if (mxcUrl.isEmpty()) return false;
    if (m_cache.count(mxcUrl)) return false;
    if (m_pending.count(mxcUrl)) return false;
    m_pending.insert(mxcUrl);
    return true;
}

void AvatarManager::store(const QString& mxcUrl, const QPixmap& source, int size) {
    m_pending.erase(mxcUrl);
    if (source.isNull()) {
        return;
    }

    QPixmap scaled = makeThumbnail(source, size);
    m_cache[mxcUrl] = scaled;

    QByteArray pngBytes;
#ifdef QT3_BUILD
    QBuffer buf(pngBytes);
    buf.open(IO_WriteOnly);
#else
    QBuffer buf(&pngBytes);
    buf.open(QIODevice::WriteOnly);
#endif
    source.save(&buf, "PNG");
    buf.close();
#ifdef QT3_BUILD
    pngBytes = buf.buffer();
#endif
    {
        std::string key = mediaCacheKey("avatar", mxcUrl);
        const auto* raw = reinterpret_cast<const uint8_t*>(pngBytes.data());
        std::vector<uint8_t> data(raw, raw + pngBytes.size());
        Storage::instance().cacheDbAsync()->storeMedia(
            std::move(key), std::move(data), "image/png", 1, nullptr);
    }
}

void AvatarManager::removePending(const QString& mxcUrl) {
    m_pending.erase(mxcUrl);
}

void AvatarManager::clear() {
    m_cache.clear();
    m_pending.clear();
}
