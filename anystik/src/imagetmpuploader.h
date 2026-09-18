#ifndef IMAGE_TMP_UPLOADER_H
#define IMAGE_TMP_UPLOADER_H

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

/*
 * 临时图床托管上传（零 UI 依赖，可独立复用）：把本地图片变成公网直链。
 * 四 host 降级链（顺序即回退）：catbox → litterbox(1h) → mhimg.cn(国内,1h)
 * → img.scdn.io。当前 host 失败自动切换下一个，全部失败发 failed()。
 * 每次上传用独立实例即可并发；cancel() 用于中止在途上传，
 * 避免残留 reply 回写新一次会话。
 */
class ImageTmpUploader : public QObject
{
    Q_OBJECT
public:
    explicit ImageTmpUploader(QObject* parent = nullptr);

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

#endif // IMAGE_TMP_UPLOADER_H
