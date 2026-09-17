#ifndef IMAGE_SEARCH_H
#define IMAGE_SEARCH_H

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

/*
 * 以图搜图的前置托管上传：把本地贴纸图变成公网直链。
 * 四 host 降级链（顺序即回退）：catbox → litterbox(1h) → mhimg.cn(国内,1h)
 * → img.scdn.io。当前 host 失败自动切换下一个，全部失败发 failed()。
 * cancel() 用于浮动层关闭时中止在途上传，避免残留 reply 回写新一次会话。
 */
class ImageSearch : public QObject
{
    Q_OBJECT
public:
    explicit ImageSearch(QObject* parent = nullptr);

    void upload(const QString& filePath);
    void cancel();

signals:
    void progressChanged(int percent, qint64 bytesSent, qint64 bytesTotal,
                         const QString& status);
    void uploaded(const QString& imageUrl);
    void failed(const QString& reason);

private:
    void startNextHost();
    void emitStatus(int hostIndex);

    QNetworkAccessManager* m_nam = nullptr;
    QNetworkReply* m_reply = nullptr;
    QString m_filePath;
    QString m_statusText;
    QString m_lastError;
    int m_hostIndex = 0;
    bool m_pending = false;
    bool m_cancelling = false;
};

#endif // IMAGE_SEARCH_H