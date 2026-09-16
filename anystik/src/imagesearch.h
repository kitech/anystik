#ifndef IMAGE_SEARCH_H
#define IMAGE_SEARCH_H

#include <QObject>
#include <QString>

class QNetworkAccessManager;

/*
 * 以图搜图的前置托管上传：把本地贴纸图变成公网直链。
 * 四 host 降级链（顺序即回退）：catbox → litterbox(1h) → mhimg.cn(国内,1h)
 * → img.scdn.io。当前 host 失败自动切换下一个，全部失败发 failed()。
 */
class ImageSearch : public QObject
{
    Q_OBJECT
public:
    explicit ImageSearch(QObject* parent = nullptr);

    void upload(const QString& filePath);

signals:
    void progressChanged(int percent, qint64 bytesSent, qint64 bytesTotal,
                         const QString& status);
    void uploaded(const QString& imageUrl);
    void failed(const QString& reason);

private:
    void startNextHost();
    void emitStatus(int hostIndex);

    QNetworkAccessManager* m_nam = nullptr;
    QString m_filePath;
    QString m_statusText;
    QString m_lastError;
    int m_hostIndex = 0;
    bool m_pending = false;
};

#endif // IMAGE_SEARCH_H