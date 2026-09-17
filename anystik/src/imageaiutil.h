#ifndef IMAGE_AI_UTIL_H
#define IMAGE_AI_UTIL_H

#include <QObject>
#include <QString>
#include <QQueue>

class QNetworkAccessManager;
class QNetworkReply;

/*
 * 图片描述通用工具（单例 + 顺序队列）：
 * 通过 Bing 以图搜图的重定向 URL 获取图片描述信息。
 * - fetchDescription() 每次入队并返回唯一请求令牌；同一时刻仅一个在途，其余排队。
 * - descriptionReady/failed 信号回带 requestId + imageUrl，调用方据此归属结果，
 *   支持多个调用方并存。
 * 描述返回为 trim 后的原文，业务长度约束（如 140 字）由消费方自行处理。
 */
class ImageAiUtil : public QObject
{
    Q_OBJECT
public:
    struct Request {
        quint64 requestId = 0;
        QString imageUrl;
    };

    static ImageAiUtil* instance();

    // 入队一次描述获取；返回唯一请求令牌
    quint64 fetchDescription(const QString& imageUrl);

    // 按令牌取消排队或在途请求（被取消项不回发任何信号）
    void cancelRequest(quint64 requestId);
    void cancelPending();

signals:
    void descriptionReady(quint64 requestId, const QString& imageUrl,
                          const QString& description);
    void failed(quint64 requestId, const QString& imageUrl, const QString& reason);

private:
    explicit ImageAiUtil(QObject* parent = nullptr);

    void startNext();
    void issueGet(const QUrl& url);
    void handleRedirect(QNetworkReply* reply, const QUrl& target);

    QNetworkAccessManager* m_nam = nullptr;
    QNetworkReply* m_reply = nullptr;
    QQueue<Request> m_pending;
    Request m_active;
    quint64 m_nextId = 1;
    int m_hopCount = 0;    // 当前请求已跟随的重定向跳数
};

#endif // IMAGE_AI_UTIL_H