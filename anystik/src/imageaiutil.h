#ifndef IMAGE_AI_UTIL_H
#define IMAGE_AI_UTIL_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QQueue>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

/*
 * 图片描述通用工具（单例 + 顺序队列）：
 * 支持多种后端，由 imageaiutil.cpp 内的全局开关 g_imageDescBackend 切换：
 *  16 = Z.ai 智谱国际版 glm-4.6v-flash（默认；key 由 davobfus 内嵌混淆提供）
 *   0 = Bing 以图搜图的重定向 URL（无需 key，已非默认）
 *   1 = Pollinations 视觉接口（需填 kPollinationsApiKey）
 *   2 = 智谱 GLM-4.6V-Flash（需填 kZhipuApiKey）
 *   3 = 硅基流动 DeepSeek-OCR（需填 kSiliconFlowApiKey）
 *   4 = NVIDIA NIM（需填 kNvidiaApiKey）
 *   5 = OpenRouter（需填 kOpenRouterApiKey）
 *   6 = BlockRun（免 key，免费视觉模型）
 *   7 = LLM7.io（需填 kLlm7ApiKey）
 *   8 = Cloudflare Workers AI（需填 kCloudflareAccountId + kCloudflareApiToken）
 *   9 = AI Horde 原生 interrogation（匿名 key 0000000000，需本地图片）
 *  10 = 阿里云百炼 qwen3-vl-flash（限时免费/新户额度，需填 kDashScopeApiKey）
 *  11 = OVH AI Endpoints Qwen2.5-VL-72B（免注册免 key，匿名 2 req/min/IP）
 *  12 = 火山方舟豆包视觉（预置推理接入点，新用户送 token，需填 kVolcengineApiKey）
 *  14 = Google Gemini Flash 免费档（需填 kGeminiApiKey，免费额度大）
 *  15 = Ollama 本地视觉（localhost:11434，零 key/零限流，需先装 Ollama）
 *  16 = Z.ai 智谱国际版 glm-4.6v-flash（默认；key 由 davobfus 内嵌混淆提供）
 * 1~8/10~12/14~16 走 OpenAI 兼容 chat/completions；9 走 AI Horde 异步提交+轮询；
 * 0 走 Bing 重定向解析。后端失败不回退。
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
        QString localPath;    // 本地图片路径（AI Horde 上传、OpenAI 兼容后端本地图 base64）
    };

    static ImageAiUtil* instance();

    // 入队一次描述获取；返回唯一请求令牌。
    // localPath 供需要本地上传/本地图的后端使用（AI Horde、OpenAI 兼容后端 base64
    // data-URI），远程图后端忽略。
    quint64 fetchDescription(const QString& imageUrl,
                             const QString& localPath = QString());

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
    void startBing();
    void startPollinations();
    void startZhipu();
    void startSiliconFlow();
    void startNvidia();
    void startOpenRouter();
    void startBlockRun();
    void startLlm7();
    void startCloudflare();
    void startAiHorde();
    void startDashScope();
    void startOvh();
    void startVolcengine();
    void startGemini();
    void startOllama();
    void startZai();
    void pollAiHorde();
    void startOpenAiVision(const QString& backendTag, const QUrl& url,
                           const QString& model, const QByteArray& apiKey,
                           int maxTokens, bool allowEmptyKey = false);
    void issueGet(const QUrl& url);
    void handleRedirect(QNetworkReply* reply, const QUrl& target);

    // 结束当前占位并推进队列（顺序队列的 busy 标志在这一处清）
    void finishActive();

    QNetworkAccessManager* m_nam = nullptr;
    QNetworkReply* m_reply = nullptr;
    QQueue<Request> m_pending;
    Request m_active;
    quint64 m_nextId = 1;
    int m_hopCount = 0;    // 当前请求已跟随的重定向跳数
    bool m_busy = false;   // 顺序队列占用标志（AI Horde 轮询期间保持占用）
    QTimer* m_pollTimer = nullptr;    // AI Horde 状态轮询
    QString m_hordeJobId;
    int m_hordePolls = 0;
};

#endif // IMAGE_AI_UTIL_H