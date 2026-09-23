#include "imageaiutil.h"
#include "davobfus.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkCookieJar>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QBuffer>
#include <QTimer>
#include <QDebug>

namespace {

const int kMaxHops = 8;

// 图片描述后端开关（全局变量，if(1) 风格手动切换）：
//  16 = Z.ai 智谱国际版 glm-4.6v-flash（默认；key 由 davobfus 内嵌混淆提供，邮箱注册免手机号）
//   0 = Bing（以图搜图重定向 URL 取描述，无需 key；已非默认）
//   1 = Pollinations 视觉接口（已失效：仅文本 openai-fast，勿选）
//   2 = 智谱 GLM-4.6V-Flash（需填 kZhipuApiKey，域名国内直连）
//   3 = 硅基流动 DeepSeek-OCR（需填 kSiliconFlowApiKey）
//   4 = NVIDIA NIM（需填 kNvidiaApiKey，国内直连）
//   5 = OpenRouter（需填 kOpenRouterApiKey）
//   6 = BlockRun（免 key，免费视觉模型；实测免费容量常耗尽，几乎无法使用）
//   7 = LLM7.io（需填 kLlm7ApiKey）
//   8 = Cloudflare Workers AI（需填 kCloudflareAccountId + kCloudflareApiToken）
//   9 = AI Horde 原生 interrogation（匿名 key，需本地图片，可能无在线 worker）
//  10 = 阿里云百炼 qwen3-vl-flash（限时免费/新户额度，需填 kDashScopeApiKey，中文最佳）
//  11 = OVH AI Endpoints Qwen2.5-VL-72B（免注册，但实测匿名限流严格，无 key 几乎无法使用；绑卡升级 400 req/min）
//  12 = 火山方舟豆包视觉（预置推理接入点，新用户送 token，需填 kVolcengineApiKey）
//  13 = ModelScope 国内 qwen3-vl-8b-instruct（注册送 ~2000 次/日，需填 kModelScopeApiKey）
//  14 = Google Gemini Flash 免费档（需填 kGeminiApiKey，免费额度大）
//  15 = Ollama 本地视觉（localhost:11434，零 key/零限流，需先装 Ollama + ollama pull）
//  17 = ModelScope 国际 qwen3-vl-8b-instruct（modelscope.ai，免费额度以该站为准，需填 kModelScopeIntlApiKey；实测需先在 modelscope.ai › My Settings › Account 绑定阿里云账号才能调用）
//  18 = Groq qwen3.6-27b（免费 30 RPM/8K TPM/1K RPD，Preview，需填 kGroqApiKey；实测受限地区 IP 返回 403，需海外出口访问）
//  19 = HuggingFace qwen2.5-vl-7b-instruct（Serverless 免费档额度很少，Router 按量，需填 kHuggingFaceApiKey）
// 失败不回退：所选后端失败即 emit failed，不会自动尝试其他后端。
int g_imageDescBackend = 16;

// Pollinations API key（申请：https://enter.pollinations.ai/keys）
// 注意：视觉已失效（现仅文本 openai-fast），此后端勿选
const char* const kPollinationsApiKey = "";

// 视觉模型名（gen 端点，需选支持 vision 的模型，如 openai / gemini 等）
const char* const kPollinationsVisionModel = "openai";

// 智谱 API key（申请：https://open.bigmodel.cn/ ）
// glm-4.6v-flash 永久免费（glm-4v-flash 已退役）、128K ctx，看图/视频/文件
const char* const kZhipuApiKey = "";
const char* const kZhipuVisionModel = "glm-4.6v-flash";
const int kZhipuMaxTokens = 1024;

// 硅基流动 API key（申请：https://siliconflow.cn/ ）
// DeepSeek-OCR 目前免费，偏 OCR/文档识别
const char* const kSiliconFlowApiKey = "";
const char* const kSiliconFlowVisionModel = "deepseek-ai/DeepSeek-OCR";
const int kSiliconFlowMaxTokens = 1024;

// NVIDIA NIM API key（申请：https://build.nvidia.com/ ，免费 40 RPM）
const char* const kNvidiaApiKey = "";
const char* const kNvidiaVisionModel = "meta/llama-3.2-11b-vision-instruct";
const int kNvidiaMaxTokens = 512;

// OpenRouter API key（申请：https://openrouter.ai/keys ，:free 模型 50 次/天）
// 免费名单频繁轮换：查 https://openrouter.ai/models?max_price=0
const char* const kOpenRouterApiKey = "";
const char* const kOpenRouterVisionModel = "google/gemma-4-31b-it:free";
const int kOpenRouterMaxTokens = 512;

// BlockRun 免 key（https://blockrun.ai，免费视觉模型，容量受限可能失败；实测免费容量常耗尽，几乎无法使用）
const char* const kBlockRunVisionModel = "nvidia/llama-3.2-11b-vision";
const int kBlockRunMaxTokens = 256;

// LLM7.io API token（申请：https://dash.llm7.io/ ）
// 免费 turbo 档里带视觉的仅 gemini-3.1-flash-lite
const char* const kLlm7ApiKey = "";
const char* const kLlm7VisionModel = "gemini-3.1-flash-lite";
const int kLlm7MaxTokens = 512;

// Cloudflare Workers AI（账号 ID + API Token，免费 10k neurons/天）
// 申请：https://dash.cloudflare.com/ → Workers AI
const char* const kCloudflareAccountId = "";
const char* const kCloudflareApiToken = "";
const char* const kCloudflareVisionModel =
    "@cf/meta/llama-3.2-11b-vision-instruct";
const int kCloudflareMaxTokens = 512;

// 阿里云百炼 DashScope（申请：https://bailian.console.aliyun.com/ ）
// qwen3-vl-flash 限时免费，新用户每模型系列 100万 token/90天，中文最佳
const char* const kDashScopeApiKey = "";
const char* const kDashScopeVisionModel = "qwen3-vl-flash";
const int kDashScopeMaxTokens = 512;

// OVH AI Endpoints（免注册，匿名 2 req/min/IP；实测匿名限流严格，无 key 几乎无法使用；绑卡可用 key 升级 400 req/min）
// 文档：https://endpoints.ai.cloud.ovh.net/docs
const char* const kOvhVisionModel = "Qwen2.5-VL-72B-Instruct";
const int kOvhMaxTokens = 512;

// 火山方舟豆包视觉（申请：https://console.volcengine.com/ark ）
// 预置推理接入点无需创建；新用户送 token，边缘大模型网关免费额度 200万起/可申 1000万
const char* const kVolcengineApiKey = "";
const char* const kVolcengineVisionModel = "doubao-seed-2-0-mini-260428";
const int kVolcengineMaxTokens = 512;

// Google Gemini 免费档（申请：https://aistudio.google.com/apikey ）
// gemini-2.5-flash 免费；免费档内容可能被用于改进产品；大陆直连性请自行确认
const char* const kGeminiApiKey = "";
const char* const kGeminiVisionModel = "gemini-2.5-flash";
const int kGeminiMaxTokens = 1024;

// 本地 Ollama（先安装 Ollama 并 `ollama pull qwen2.5vl:7b`；localhost 免 key）
const char* const kOllamaVisionModel = "qwen2.5vl:7b";
const int kOllamaMaxTokens = 512;

// Z.ai 智谱国际版（申请：https://z.ai ，邮箱注册免手机号；账号/Key 与 bigmodel.cn 不互通）
// 免费视觉模型永久免费（官方价格页大写 GLM-4.6V-Flash，接口用小写同为该模型），
// 免费档 1 并发 ≈1 req/s；大陆直连性需自行确认；key 由 davobfus 的 zaiApiKey() 提供
const char* const kZaiVisionModel = "glm-4.6v-flash";
const int kZaiMaxTokens = 1024;

// ModelScope 国内（申请：https://modelscope.cn ，需实名 + ms- token）
// 免费档注册即送约 2000 次/天（自然日重置，单模型动态限流）；Qwen3-VL-8B 8K ctx/4K out
const char* const kModelScopeApiKey = "";
const char* const kModelScopeVisionModel = "Qwen/Qwen3-VL-8B-Instruct";
const int kModelScopeMaxTokens = 512;

// ModelScope 国际版（申请：https://modelscope.ai ），端点 api-inference.modelscope.ai
// 免费额度政策以该站为准（token 与国内版不互通）
// 实测需先在 modelscope.ai › My Settings › Account 绑定阿里云账号才能调用
const char* const kModelScopeIntlApiKey = "";
const int kModelScopeIntlMaxTokens = 512;

// Groq（申请：https://console.groq.com/keys ，免费）
// 免费档 30 RPM / 8K TPM / 1K RPD；qwen3.6-27b 为 Preview，中文描述较好
// 实测受限地区（如大陆直连）IP 返回 403 Unsupported Region，需海外出口/代理
const char* const kGroqApiKey = "";
const char* const kGroqVisionModel = "qwen/qwen3.6-27b";
const int kGroqMaxTokens = 1024;

// HuggingFace（申请：https://huggingface.co/settings/tokens ，Inference Providers 权限）
// 路由器端点 router.huggingface.co 为按量透传（无免费包）；
// Serverless 免费档额度很少（<10B 模型约数百 req/h），有变化再考虑实际使用
const char* const kHuggingFaceApiKey = "";
const char* const kHuggingFaceVisionModel = "Qwen/Qwen2.5-VL-7B-Instruct";
const int kHuggingFaceMaxTokens = 512;

// AI Horde 原生 interrogation（匿名 key 0000000000，最低优先级）
// 端点：https://aihorde.net/api/v2/interrogate/async + status 轮询
const char* const kAiHordeApiKey = "0000000000";
const char* const kAiHordeClientAgent = "anystik:1.0:https://github.com/anystik";
const int kAiHordePollIntervalMs = 3000;
const int kAiHordeMaxPolls = 40;    // 约 120 秒超时

QNetworkRequest makeRequest(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setTransferTimeout(30000);
    // 手动跟随重定向链：Bing 经多跳后才在最后一跳 URL 的 q 参数携带图片描述
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setAttribute(QNetworkRequest::Http2DirectAttribute, false);
    req.setRawHeader("User-Agent", "anystik/1.0");
    // 描述全程约束为中文，避免 Geo 出口兜底时落到英文
    req.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.4");
    return req;
}

// 描述判据：非空，且不是镜象回显的 imgurl:xxx
bool isDescription(const QString& value)
{
    if (value.isEmpty()) {
        return false;
    }
    return !value.startsWith(QStringLiteral("imgurl:"), Qt::CaseInsensitive);
}

void logHop(const quint64 requestId, int hop, const char* tag,
            const QUrl& url, const QString& extra = QString())
{
    qInfo().noquote() << QStringLiteral("[ImageAiUtil] req=%1 hop=%2 %3 url=%4 %5")
                             .arg(requestId)
                             .arg(hop)
                             .arg(QLatin1String(tag), url.toString(), extra);
}

} // namespace

ImageAiUtil::ImageAiUtil(QObject* parent)
    : QObject(parent)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kAiHordePollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &ImageAiUtil::pollAiHorde);
}

ImageAiUtil* ImageAiUtil::instance()
{
    static ImageAiUtil s_inst;
    return &s_inst;
}

quint64 ImageAiUtil::fetchDescription(const QString& imageUrl,
                                      const QString& localPath)
{
    const quint64 id = m_nextId++;
    Request req;
    req.requestId = id;
    req.imageUrl = imageUrl;
    req.localPath = localPath;
    m_pending.enqueue(req);
    if (!m_busy) {
        // 延后到下一事件循环再启动：保证 descriptionReady/failed 一律晚于
        // 调用方「m_xxxReqId = fetchDescription(...)」拿到令牌之后发出，
        // 同步失败路径（格式不支持/未配置 key/过大等）不再被令牌过滤吞掉。
        QMetaObject::invokeMethod(this, &ImageAiUtil::startNext,
                                  Qt::QueuedConnection);
    }
    return id;
}

void ImageAiUtil::cancelRequest(quint64 requestId)
{
    if (requestId == 0) {
        return;
    }
    if (m_active.requestId == requestId) {
        m_active = Request();
        if (m_pollTimer) {
            m_pollTimer->stop();
        }
        m_hordeJobId.clear();
        m_hordePolls = 0;
        if (m_reply) {
            auto* reply = m_reply;
            m_reply = nullptr;
            reply->abort();
            reply->deleteLater();
        }
        finishActive();
        return;
    }
    for (int i = 0; i < m_pending.size(); ++i) {
        if (m_pending.at(i).requestId == requestId) {
            m_pending.removeAt(i);
            return;
        }
    }
}

void ImageAiUtil::cancelPending()
{
    m_pending.clear();
    m_active = Request();
    if (m_pollTimer) {
        m_pollTimer->stop();
    }
    m_hordeJobId.clear();
    m_hordePolls = 0;
    if (m_reply) {
        auto* reply = m_reply;
        m_reply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    finishActive();
}

void ImageAiUtil::finishActive()
{
    m_busy = false;
    startNext();
}

void ImageAiUtil::startNext()
{
    if (m_busy) {
        return;
    }
    if (m_pending.isEmpty()) {
        m_active = Request();
        return;
    }

    m_active = m_pending.dequeue();
    m_busy = true;
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
        m_nam->setCookieJar(new QNetworkCookieJar(m_nam));
    }
    m_hopCount = 0;

    if (g_imageDescBackend == 1) {
        startPollinations();
    } else if (g_imageDescBackend == 2) {
        startZhipu();
    } else if (g_imageDescBackend == 3) {
        startSiliconFlow();
    } else if (g_imageDescBackend == 4) {
        startNvidia();
    } else if (g_imageDescBackend == 5) {
        startOpenRouter();
    } else if (g_imageDescBackend == 6) {
        startBlockRun();
    } else if (g_imageDescBackend == 7) {
        startLlm7();
    } else if (g_imageDescBackend == 8) {
        startCloudflare();
    } else if (g_imageDescBackend == 9) {
        startAiHorde();
    } else if (g_imageDescBackend == 10) {
        startDashScope();
    } else if (g_imageDescBackend == 11) {
        startOvh();
    } else if (g_imageDescBackend == 12) {
        startVolcengine();
    } else if (g_imageDescBackend == 13) {
        startModelScope();
    } else if (g_imageDescBackend == 14) {
        startGemini();
    } else if (g_imageDescBackend == 15) {
        startOllama();
    } else if (g_imageDescBackend == 16) {
        startZai();
    } else if (g_imageDescBackend == 17) {
        startModelScopeIntl();
    } else if (g_imageDescBackend == 18) {
        startGroq();
    } else if (g_imageDescBackend == 19) {
        startHuggingFace();
    } else {
        startBing();
    }
}

void ImageAiUtil::startBing()
{
    const QByteArray enc = QUrl::toPercentEncoding(m_active.imageUrl);
    const QUrl url(QStringLiteral(
        "https://www.bing.com/images/searchbyimage?cbir=sbi&imgurl=")
            + QString::fromLatin1(enc));
    issueGet(url);
}

// 统一 OpenAI 兼容视觉请求：text + image_url，Bearer 鉴权，
// 解析 choices[0].message.content / error.message。未填 key 直接失败；
// allowEmptyKey=true 时（如 BlockRun 免 key 后端）跳过 Authorization 头。
void ImageAiUtil::startOpenAiVision(const QString& backendTag, const QUrl& url,
                                    const QString& model, const QByteArray& apiKey,
                                    int maxTokens, bool allowEmptyKey)
{
    if (apiKey.isEmpty() && !allowEmptyKey) {
        const Request done = m_active;
        qWarning().noquote() << QStringLiteral(
            "[ImageAiUtil] req=%1 backend=%2 no api key")
            .arg(done.requestId).arg(backendTag);
        emit failed(done.requestId, done.imageUrl,
                    tr("未配置 %1 key").arg(backendTag));
        finishActive();
        return;
    }

    QJsonObject textPart;
    textPart.insert(QStringLiteral("type"), QStringLiteral("text"));
    textPart.insert(QStringLiteral("text"),
        QStringLiteral("请用一句中文简要描述这张图片，只输出描述本身"));

    // 本地图片优先：localPath 非空则转 base64 data-URI（各视觉后端直接读本机贴纸），
    // 否则用远程 imageUrl。只放行 JPG/PNG（GLM-4.6V 等后端的官方图片格式），
    // 格式按文件内容探测而非扩展名。
    QString imageRef = m_active.imageUrl;
    const QString localPath = m_active.localPath.trimmed();
    if (!localPath.isEmpty()) {
        QFile file(localPath);
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = file.readAll();
            if (bytes.size() > 20 * 1024 * 1024) {
                const Request done = m_active;
                qWarning().noquote() << QStringLiteral(
                    "[ImageAiUtil] req=%1 local img too large file=%2 size=%3 reason=%4")
                    .arg(done.requestId)
                    .arg(QFileInfo(localPath).fileName())
                    .arg(bytes.size())
                    .arg(tr("本地图片过大（>20MB）"));
                emit failed(done.requestId, done.imageUrl,
                            tr("本地图片过大（>20MB）"));
                finishActive();
                return;
            }
            QBuffer buf(const_cast<QByteArray*>(&bytes));
            buf.open(QIODevice::ReadOnly);
            QImageReader reader(&buf);
            reader.setAutoTransform(true);
            const QByteArray fmt = reader.format().toLower();
            const bool isJpg = (fmt == "jpg" || fmt == "jpeg");
            const bool isPng = (fmt == "png");
            if (!isJpg && !isPng) {
                const Request done = m_active;
                const QString reason = fmt.isEmpty()
                    ? tr("图片无法识别（不支持该格式）")
                    : tr("图片格式不可用（仅支持 JPG/PNG，实际：%1）")
                          .arg(QString::fromLatin1(fmt));
                qWarning().noquote() << QStringLiteral(
                    "[ImageAiUtil] req=%1 local img format rejected "
                    "file=%2 path=%3 fmt=%4 reason=%5")
                    .arg(done.requestId)
                    .arg(QFileInfo(localPath).fileName(), localPath,
                         QString::fromLatin1(fmt.isEmpty() ? "<unknown>"
                                                           : fmt), reason);
                emit failed(done.requestId, done.imageUrl, reason);
                finishActive();
                return;
            }
            imageRef = QStringLiteral("data:%1;base64,")
                           .arg(isJpg ? "image/jpeg" : "image/png")
                + QString::fromLatin1(bytes.toBase64());
        }
        // 读失败则回落原 imageUrl；两者皆空时由下方空载荷前检拦截
    }

    // 空载荷前检：本地读取失败且无远程 URL → 直接失败，不发空 url 请求
    if (imageRef.isEmpty()) {
        const Request done = m_active;
        qWarning().noquote() << QStringLiteral(
            "[ImageAiUtil] req=%1 empty image payload url=%2 local=%3 reason=%4")
            .arg(done.requestId).arg(done.imageUrl, localPath,
                                     tr("未提供可用图片（读取失败或参数为空）"));
        emit failed(done.requestId, done.imageUrl,
                    tr("未提供可用图片（读取失败或参数为空）"));
        finishActive();
        return;
    }

    QJsonObject imageUrl;
    imageUrl.insert(QStringLiteral("url"), imageRef);
    QJsonObject imagePart;
    imagePart.insert(QStringLiteral("type"), QStringLiteral("image_url"));
    imagePart.insert(QStringLiteral("image_url"), imageUrl);

    QJsonArray content;
    content.append(textPart);
    content.append(imagePart);

    QJsonObject message;
    message.insert(QStringLiteral("role"), QStringLiteral("user"));
    message.insert(QStringLiteral("content"), content);

    QJsonArray messages;
    messages.append(message);

    QJsonObject root;
    root.insert(QStringLiteral("model"), model);
    root.insert(QStringLiteral("messages"), messages);
    root.insert(QStringLiteral("max_tokens"), maxTokens);

    QNetworkRequest req(url);
    req.setTransferTimeout(60000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setAttribute(QNetworkRequest::Http2DirectAttribute, false);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
    req.setRawHeader("User-Agent", "anystik/1.0");
    req.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.4");
    if (!apiKey.isEmpty()) {
        req.setRawHeader("Authorization", "Bearer " + apiKey);
    }

    const Request active = m_active;
    qInfo().noquote() << QStringLiteral(
        "[ImageAiUtil] req=%1 backend=%2 post url=%3 img=%4")
        .arg(active.requestId).arg(backendTag, url.toString(),
                                   active.imageUrl);

    auto* reply = m_nam->post(req,
        QJsonDocument(root).toJson(QJsonDocument::Compact));
    m_reply = reply;

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, active, backendTag]() {
                reply->deleteLater();
                if (m_reply != reply) {
                    return; // 已被取消
                }
                m_reply = nullptr;

                const QByteArray data = reply->readAll();
                const QJsonObject body =
                    QJsonDocument::fromJson(data).object();

                QString desc;
                if (reply->error() == QNetworkReply::NoError) {
                    const QJsonArray choices =
                        body.value(QStringLiteral("choices")).toArray();
                    if (!choices.isEmpty()) {
                        desc = choices.at(0).toObject()
                                   .value(QStringLiteral("message")).toObject()
                                   .value(QStringLiteral("content")).toString()
                                   .trimmed();
                    }
                }
                const QString apiErr =
                    body.value(QStringLiteral("error")).toObject()
                        .value(QStringLiteral("message")).toString().trimmed();

                qInfo().noquote() << QStringLiteral(
                    "[ImageAiUtil] req=%1 backend=%2 finished "
                    "error=%3 %4 stat=%5 desc=%6 apiErr=%7")
                    .arg(active.requestId)
                    .arg(backendTag)
                    .arg(int(reply->error()))
                    .arg(reply->errorString())
                    .arg(reply->attribute(
                        QNetworkRequest::HttpStatusCodeAttribute).toInt())
                    .arg(desc, apiErr);

                if (!desc.isEmpty()
                        && desc != QStringLiteral("[object Object]")) {
                    emit descriptionReady(active.requestId,
                                          active.imageUrl, desc);
                } else {
                    QString reason;
                    if (backendTag == QStringLiteral("Ollama 本地")
                            && reply->error() != QNetworkReply::NoError) {
                        reason = tr("未检测到本地 Ollama（请先安装 Ollama 并 "
                                    "执行 `ollama pull qwen2.5vl:7b`）");
                    } else if (reply->error() != QNetworkReply::NoError)
                        reason = reply->errorString();
                    else if (!apiErr.isEmpty())
                        reason = apiErr;
                    else
                        reason = tr("未识别出图片描述");
                    qInfo().noquote() << QStringLiteral(
                        "[ImageAiUtil] req=%1 backend=%2 failed reason=%3")
                        .arg(active.requestId).arg(backendTag, reason);
                    emit failed(active.requestId, active.imageUrl, reason);
                }
                finishActive();
            });
}

void ImageAiUtil::startPollinations()
{
    startOpenAiVision(QStringLiteral("pollinations"),
                      QUrl(QStringLiteral(
                          "https://gen.pollinations.ai/v1/chat/completions")),
                      QString::fromLatin1(kPollinationsVisionModel),
                      QByteArray(kPollinationsApiKey).trimmed(), 200);
}

void ImageAiUtil::startZhipu()
{
    startOpenAiVision(QStringLiteral("智谱"),
                      QUrl(QStringLiteral("https://open.bigmodel.cn/api/paas/v4"
                                          "/chat/completions")),
                      QString::fromLatin1(kZhipuVisionModel),
                      QByteArray(kZhipuApiKey).trimmed(), kZhipuMaxTokens);
}

void ImageAiUtil::startSiliconFlow()
{
    startOpenAiVision(QStringLiteral("硅基流动"),
                      QUrl(QStringLiteral(
                          "https://api.siliconflow.cn/v1/chat/completions")),
                      QString::fromLatin1(kSiliconFlowVisionModel),
                      QByteArray(kSiliconFlowApiKey).trimmed(),
                      kSiliconFlowMaxTokens);
}

void ImageAiUtil::startNvidia()
{
    startOpenAiVision(QStringLiteral("NVIDIA NIM"),
                      QUrl(QStringLiteral(
                          "https://integrate.api.nvidia.com/v1/chat/completions")),
                      QString::fromLatin1(kNvidiaVisionModel),
                      QByteArray(kNvidiaApiKey).trimmed(), kNvidiaMaxTokens);
}

void ImageAiUtil::startOpenRouter()
{
    startOpenAiVision(QStringLiteral("OpenRouter"),
                      QUrl(QStringLiteral(
                          "https://openrouter.ai/api/v1/chat/completions")),
                      QString::fromLatin1(kOpenRouterVisionModel),
                      QByteArray(kOpenRouterApiKey).trimmed(),
                      kOpenRouterMaxTokens);
}

void ImageAiUtil::startBlockRun()
{
    // 免 key 后端：allowEmptyKey=true，不发送 Authorization 头
    startOpenAiVision(QStringLiteral("BlockRun"),
                      QUrl(QStringLiteral(
                          "https://blockrun.ai/api/v1/chat/completions")),
                      QString::fromLatin1(kBlockRunVisionModel),
                      QByteArray(), kBlockRunMaxTokens, true);
}

void ImageAiUtil::startLlm7()
{
    startOpenAiVision(QStringLiteral("LLM7"),
                      QUrl(QStringLiteral(
                          "https://api.llm7.io/v1/chat/completions")),
                      QString::fromLatin1(kLlm7VisionModel),
                      QByteArray(kLlm7ApiKey).trimmed(), kLlm7MaxTokens);
}

void ImageAiUtil::startCloudflare()
{
    const QByteArray accountId =
        QByteArray(kCloudflareAccountId).trimmed();
    const QByteArray token =
        QByteArray(kCloudflareApiToken).trimmed();
    if (accountId.isEmpty() || token.isEmpty()) {
        const Request done = m_active;
        qWarning().noquote() << QStringLiteral(
            "[ImageAiUtil] req=%1 backend=Cloudflare no account/token")
            .arg(done.requestId);
        emit failed(done.requestId, done.imageUrl,
                    tr("未配置 Cloudflare 账号或 Token"));
        finishActive();
        return;
    }
    startOpenAiVision(QStringLiteral("Cloudflare"),
                      QUrl(QStringLiteral(
                          "https://api.cloudflare.com/client/v4/accounts/%1"
                          "/ai/v1/chat/completions")
                               .arg(QString::fromLatin1(accountId))),
                      QString::fromLatin1(kCloudflareVisionModel),
                      token, kCloudflareMaxTokens);
}

void ImageAiUtil::startDashScope()
{
    startOpenAiVision(QStringLiteral("百炼"),
                      QUrl(QStringLiteral(
                          "https://dashscope.aliyuncs.com/compatible-mode/v1"
                          "/chat/completions")),
                      QString::fromLatin1(kDashScopeVisionModel),
                      QByteArray(kDashScopeApiKey).trimmed(),
                      kDashScopeMaxTokens);
}

void ImageAiUtil::startOvh()
{
    // 免 key 后端：allowEmptyKey=true，不发送 Authorization 头
    startOpenAiVision(QStringLiteral("OVH"),
                      QUrl(QStringLiteral(
                          "https://oai.endpoints.kepler.ai.cloud.ovh.net/v1"
                          "/chat/completions")),
                      QString::fromLatin1(kOvhVisionModel),
                      QByteArray(), kOvhMaxTokens, true);
}

void ImageAiUtil::startVolcengine()
{
    startOpenAiVision(QStringLiteral("豆包"),
                      QUrl(QStringLiteral(
                          "https://ark.cn-beijing.volces.com/api/v3"
                          "/chat/completions")),
                      QString::fromLatin1(kVolcengineVisionModel),
                      QByteArray(kVolcengineApiKey).trimmed(),
                      kVolcengineMaxTokens);
}

void ImageAiUtil::startModelScope()
{
    startOpenAiVision(QStringLiteral("ModelScope(国内)"),
                      QUrl(QStringLiteral(
                          "https://api-inference.modelscope.cn/v1"
                          "/chat/completions")),
                      QString::fromLatin1(kModelScopeVisionModel),
                      QByteArray(kModelScopeApiKey).trimmed(),
                      kModelScopeMaxTokens);
}

void ImageAiUtil::startModelScopeIntl()
{
    startOpenAiVision(QStringLiteral("ModelScope(国际)"),
                      QUrl(QStringLiteral(
                          "https://api-inference.modelscope.ai/v1"
                          "/chat/completions")),
                      QString::fromLatin1(kModelScopeVisionModel),
                      QByteArray(kModelScopeIntlApiKey).trimmed(),
                      kModelScopeIntlMaxTokens);
}

void ImageAiUtil::startGroq()
{
    startOpenAiVision(QStringLiteral("Groq"),
                      QUrl(QStringLiteral(
                          "https://api.groq.com/openai/v1/chat/completions")),
                      QString::fromLatin1(kGroqVisionModel),
                      QByteArray(kGroqApiKey).trimmed(), kGroqMaxTokens);
}

void ImageAiUtil::startHuggingFace()
{
    startOpenAiVision(QStringLiteral("HuggingFace"),
                      QUrl(QStringLiteral(
                          "https://router.huggingface.co/v1/chat/completions")),
                      QString::fromLatin1(kHuggingFaceVisionModel),
                      QByteArray(kHuggingFaceApiKey).trimmed(),
                      kHuggingFaceMaxTokens);
}

void ImageAiUtil::startGemini()
{
    startOpenAiVision(QStringLiteral("Gemini"),
                      QUrl(QStringLiteral(
                          "https://generativelanguage.googleapis.com/v1beta"
                          "/openai/chat/completions")),
                      QString::fromLatin1(kGeminiVisionModel),
                      QByteArray(kGeminiApiKey).trimmed(),
                      kGeminiMaxTokens);
}

void ImageAiUtil::startOllama()
{
    // 本地服务（localhost:11434），免 key；连不上时给出安装提示
    startOpenAiVision(QStringLiteral("Ollama 本地"),
                      QUrl(QStringLiteral(
                          "http://localhost:11434/v1/chat/completions")),
                      QString::fromLatin1(kOllamaVisionModel),
                      QByteArray(), kOllamaMaxTokens, true);
}

void ImageAiUtil::startZai()
{
    // 国际版独立端点；key 由 davobfus 内嵌混淆提供（zaiApiKey()）
    startOpenAiVision(QStringLiteral("Z.ai(国际)"),
                      QUrl(QStringLiteral(
                          "https://api.z.ai/api/paas/v4/chat/completions")),
                      QString::fromLatin1(kZaiVisionModel),
                      QByteArray(zaiApiKey().toUtf8()).trimmed(),
                      kZaiMaxTokens);
}

// AI Horde 原生 interrogation：本地图片 base64 提交 → 定时轮询状态。
// 免 key（匿名 0000000000），但依赖在线识别 worker，可能超时失败。
void ImageAiUtil::startAiHorde()
{
    if (m_active.localPath.isEmpty()) {
        const Request done = m_active;
        emit failed(done.requestId, done.imageUrl,
                    tr("AI Horde 需要本地图片"));
        finishActive();
        return;
    }
    QFile file(m_active.localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        const Request done = m_active;
        emit failed(done.requestId, done.imageUrl,
                    tr("读取本地图片失败"));
        finishActive();
        return;
    }
    const QByteArray b64 = file.readAll().toBase64();
    file.close();

    QJsonObject form;
    form.insert(QStringLiteral("name"), QStringLiteral("caption"));
    QJsonArray forms;
    forms.append(form);
    QJsonObject root;
    root.insert(QStringLiteral("forms"), forms);
    root.insert(QStringLiteral("source_image"), QString::fromLatin1(b64));

    QNetworkRequest req(QUrl(QStringLiteral(
        "https://aihorde.net/api/v2/interrogate/async")));
    req.setTransferTimeout(60000);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
    req.setRawHeader("apikey", kAiHordeApiKey);
    req.setRawHeader("Client-Agent", kAiHordeClientAgent);
    req.setRawHeader("User-Agent", "anystik/1.0");
    req.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.4");

    const Request active = m_active;
    qInfo().noquote() << QStringLiteral(
        "[ImageAiUtil] req=%1 backend=AI Horde submit img=%2")
        .arg(active.requestId).arg(active.imageUrl);

    auto* reply = m_nam->post(req,
        QJsonDocument(root).toJson(QJsonDocument::Compact));
    m_reply = reply;

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, active]() {
                reply->deleteLater();
                if (m_reply != reply) {
                    return; // 已被取消
                }
                m_reply = nullptr;

                const QByteArray data = reply->readAll();
                const QJsonObject body =
                    QJsonDocument::fromJson(data).object();
                const QString jobId = body.value(QStringLiteral("id")).toString();
                const QString apiErr =
                    body.value(QStringLiteral("message")).toString().trimmed();

                qInfo().noquote() << QStringLiteral(
                    "[ImageAiUtil] req=%1 backend=AI Horde submitted "
                    "job=%2 error=%3 %4 stat=%5 apiErr=%6")
                    .arg(active.requestId).arg(jobId)
                    .arg(int(reply->error()))
                    .arg(reply->errorString())
                    .arg(reply->attribute(
                        QNetworkRequest::HttpStatusCodeAttribute).toInt())
                    .arg(apiErr);

                if (reply->error() != QNetworkReply::NoError
                        || jobId.isEmpty()) {
                    QString reason;
                    if (reply->error() != QNetworkReply::NoError)
                        reason = reply->errorString();
                    else if (!apiErr.isEmpty())
                        reason = apiErr;
                    else
                        reason = tr("AI Horde 提交失败");
                    emit failed(active.requestId, active.imageUrl, reason);
                    finishActive();
                    return;
                }
                m_hordeJobId = jobId;
                m_hordePolls = 0;
                m_pollTimer->start();
            });
}

void ImageAiUtil::pollAiHorde()
{
    if (m_hordeJobId.isEmpty() || m_reply) {
        return;
    }
    if (++m_hordePolls > kAiHordeMaxPolls) {
        m_pollTimer->stop();
        const Request done = m_active;
        emit failed(done.requestId, done.imageUrl,
                    tr("AI Horde 超时（当前可能无在线识别 worker）"));
        finishActive();
        return;
    }

    QNetworkRequest req(QUrl(QStringLiteral(
        "https://aihorde.net/api/v2/interrogate/status/%1")
            .arg(m_hordeJobId)));
    req.setTransferTimeout(30000);
    req.setRawHeader("apikey", kAiHordeApiKey);
    req.setRawHeader("Client-Agent", kAiHordeClientAgent);
    req.setRawHeader("User-Agent", "anystik/1.0");

    const Request active = m_active;
    auto* reply = m_nam->get(req);
    m_reply = reply;

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, active]() {
                reply->deleteLater();
                if (m_reply != reply) {
                    return; // 已被取消
                }
                m_reply = nullptr;

                const QByteArray data = reply->readAll();
                const QJsonObject body =
                    QJsonDocument::fromJson(data).object();
                const bool done =
                    body.value(QStringLiteral("done")).toBool();

                QString caption;
                const QJsonArray forms =
                    body.value(QStringLiteral("forms")).toArray();
                if (!forms.isEmpty()) {
                    caption = forms.at(0).toObject()
                                  .value(QStringLiteral("result")).toObject()
                                  .value(QStringLiteral("caption")).toString()
                                  .trimmed();
                }

                if (reply->error() != QNetworkReply::NoError) {
                    m_pollTimer->stop();
                    emit failed(active.requestId, active.imageUrl,
                                reply->errorString());
                    finishActive();
                    return;
                }
                if (done) {
                    m_pollTimer->stop();
                    m_hordeJobId.clear();
                    if (!caption.isEmpty()) {
                        emit descriptionReady(active.requestId,
                                              active.imageUrl, caption);
                    } else {
                        emit failed(active.requestId, active.imageUrl,
                                    tr("未识别出图片描述"));
                    }
                    finishActive();
                }
                // 未完成：保持 busy，等待下一次轮询
            });
}

void ImageAiUtil::issueGet(const QUrl& url)
{
    logHop(m_active.requestId, m_hopCount, "get", url);
    auto* reply = m_nam->get(makeRequest(url));
    m_reply = reply;

    connect(reply, &QNetworkReply::redirected, this,
            [this, reply](const QUrl& target) {
                logHop(m_active.requestId, m_hopCount, "redirected",
                       reply->url(),
                       QStringLiteral("→ %1").arg(target.toString()));
                handleRedirect(reply, target);
            });

    connect(reply, &QNetworkReply::finished, this,
            [this, reply]() {
                reply->deleteLater();
                if (m_reply != reply) {
                    return; // 已被取消 / redirect 路径接手
                }
                m_reply = nullptr;
                const Request done = m_active;
                const bool redirected = reply->attribute(
                    QNetworkRequest::RedirectionTargetAttribute).isValid();

                qInfo().noquote() << QStringLiteral(
                    "[ImageAiUtil] req=%1 hop=%2 finished error=%3 %4 "
                    "stat=%5 redirectAttr=%6 final=%7")
                    .arg(done.requestId)
                    .arg(m_hopCount)
                    .arg(int(reply->error()))
                    .arg(reply->errorString())
                    .arg(reply->attribute(
                        QNetworkRequest::HttpStatusCodeAttribute).toInt())
                    .arg(redirected)
                    .arg(reply->url().toString());

                if (reply->error() == QNetworkReply::NoError && redirected) {
                    // redirected 信号缺失时的兜底：按属性手动跟随一次
                    QUrl target = reply->attribute(
                        QNetworkRequest::RedirectionTargetAttribute).toUrl();
                    if (target.isRelative()) {
                        target = reply->url().resolved(target);
                    }
                    if (isDescription(QUrlQuery(target).queryItemValue(
                            QStringLiteral("q")).trimmed())) {
                        m_reply = nullptr;
                        emit descriptionReady(done.requestId, done.imageUrl,
                            QUrlQuery(target).queryItemValue(
                                QStringLiteral("q")).trimmed());
                        finishActive();
                        return;
                    }
                    if (++m_hopCount > kMaxHops) {
                        emit failed(done.requestId, done.imageUrl,
                                    tr("Bing 重定向过多"));
                        finishActive();
                        return;
                    }
                    m_reply = nullptr;
                    issueGet(target);
                    return;
                }

                const QString reason = reply->error() == QNetworkReply::NoError
                    ? tr("未识别出图片描述")
                    : reply->errorString();
                emit failed(done.requestId, done.imageUrl, reason);
                finishActive();
            });
}

void ImageAiUtil::handleRedirect(QNetworkReply* reply, const QUrl& target)
{
    if (m_reply != reply) {
        return; // 已被取消或已切到下一跳
    }
    const Request done = m_active;
    QUrl resolved = target;
    if (resolved.isRelative()) {
        resolved = reply->url().resolved(resolved);
    }
    QString desc;
    if (!resolved.isEmpty()) {
        desc = QUrlQuery(resolved).queryItemValue(
            QStringLiteral("q")).trimmed();
    }
    logHop(done.requestId, m_hopCount, "redirect-parse",
           resolved, QStringLiteral("q=%1 desc=%2")
               .arg(desc, isDescription(desc) ? "yes" : "no"));

    if (isDescription(desc)) {
        // 拿到描述 → 结束本次
        m_reply = nullptr;
        reply->abort();
        emit descriptionReady(done.requestId, done.imageUrl, desc);
        finishActive();
        return;
    }
    // 非描述跳（地域跳转 / imgurl 回显）→ 继续跟随
    if (++m_hopCount > kMaxHops) {
        m_reply = nullptr;
        reply->abort();
        emit failed(done.requestId, done.imageUrl,
                    tr("Bing 重定向过多"));
        finishActive();
        return;
    }
    m_reply = nullptr;
    reply->abort();
    issueGet(resolved);
}