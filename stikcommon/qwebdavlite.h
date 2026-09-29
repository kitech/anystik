#ifndef QWEBDAVLITE_H
#define QWEBDAVLITE_H

// ═══════════════════════════════════════════════════════════════════════
// QWebdavLite —— davbisync 所需的 WebDAV 窄切面
//
// 为什么不直接移植 vendor/qwebdav（2132 行）：Qt3.5 有三个**不可垫片**的
// 结构性障碍——`QByteArray`=`QMemArray<char>`（加不了 append/toHex/number/
// (const char*) 构造）、`QString` 同理（加不了 size/chop/toUpper/clear）、
// Qt3 的 `QIODevice` 不是 QObject（无 deleteLater/父子关系）。三者都只能逐
// call-site 改写，且已产生 3 处语义改动，按 §7「控制流/数据结构 0 行」阈值
// 判不可共享。详见 qlstik/移植计划.md §6.3e-1「§7 判定」。
//
// 依据：davbisync 只用 vendor QWebdav 34 个公开方法中的 13 个、1 个 signal，
// 其余（QWebdavDirParser 完整面 545 行、QWebdavItem 全套、QNaturalSort）
// 本项目不消费，故不实现。
//
// 底座不变，仍是 curl：QNetworkAccessManager（qnam_shim）→ qltox
// EventPoller → curl_multi。继承 QNAM 即白拿 401 补 Authorization 重发、
// TLS 失败→sslErrors→CA 收录/重试的完整链，以及 m_sslRetried 防重入。
//
// Qt3 约束（写新代码时主动避开，别用原生写法）：
//   QByteArray 无 append/toHex/fromHex/number/operator+、无 (const char*) 构造
//   → 用 qByteArrayAppend / qbaToHex / qbaFromHex / qNumberToByteArray，
//     字面量走 qByteArrayAppend(a, "...")
//   QString 无 size()/chop()/toUpper()/clear()/toUtf8()/split(QChar)
//   → 用 length() / qStringChop / upper() / qStringClear / utf8() /
//     split(QString(c))
//   QIODevice 无 write()，只有 writeBlock()；且不是 QObject
//
// 本类不声明 Q_OBJECT、不用 signals:/slots:：信号走 qconnect_slots.h 的垫片
// 机制（与 QNAM 垫片同一套），故**无需 moc**。
// ═══════════════════════════════════════════════════════════════════════

#include <qstring.h>
#include <qstringlist.h>
#include <qdatetime.h>

#include "qglobaltype_shim.h"   // qint64
#include "qstring_shim.h"       // qToUtf8BA / qStringChop / qStringClear
#include "qbytearray_shim.h"    // qByteArrayAppend / qNumberToByteArray
#include "qba_shim.h"           // qbaToHex / qbaFromHex
#include "qurl_shim.h"
#include "qdatetime_shim.h"     // qDateTimeToUtc / qFormatDateTime
#include "qnam_shim.h"          // QNetworkAccessManager / Request / Reply
#include "qconnect_slots.h"     // 垫片 connect/disconnect 模板

class QIODevice;

class QWebdavLite : public QNetworkAccessManager
{
public:
    enum QWebdavConnectionType { HTTP = 1, HTTPS };

    explicit QWebdavLite(QObject* parent = 0);
    ~QWebdavLite();

    // ── 连接配置（与 vendor setConnectionSettings 签名一致）──
    // sslCertDigestMd5/Sha1 是**大写 hex 串**（"XX:XX:…"），留空则不 pin。
    void setConnectionSettings(QWebdavConnectionType connectionType,
                               const QString& hostname,
                               const QString& rootPath = QString("/"),
                               const QString& username = QString(),
                               const QString& password = QString(),
                               int port = 0,
                               const QString& sslCertDigestMd5 = QString(),
                               const QString& sslCertDigestSha1 = QString());

    // 空闲超时：连续无字节传输的毫秒数；<0 关闭。随上传/下载进度重置。
    // （vendor 在垫片面缺此 API，本类按 davbisync 调用点补齐。）
    void setTransferTimeout(int msecs) { m_transferTimeoutMsecs = msecs; }
    int transferTimeout() const { return m_transferTimeoutMsecs; }

    QString hostname() const { return m_hostname; }
    int port() const { return m_port; }
    QString rootPath() const { return m_rootPath; }
    QString username() const { return m_username; }
    QString password() const { return m_password; }
    QWebdavConnectionType connectionType() const { return m_connectionType; }
    bool isSSL() const { return m_connectionType == HTTPS; }

    // relPath 拼成 rootPath 下的绝对路径（vendor 同名语义）
    QString absolutePath(const QString& relPath);

    // 摘要 hex 互转，格式 "XX:XX:XX:…"，X ∈ [0-9A-F] 大写（与 vendor 一致）
    static QString digestToHex(const QByteArray& input);
    static QByteArray digestFromHex(const QString& input);

    // ── davbisync 实际用到的 10 个传输方法 ──
    QNetworkReply* get(const QString& path);
    QNetworkReply* get(const QString& path, QIODevice* data);
    QNetworkReply* get(const QString& path, QIODevice* data, quint64 fromRangeInBytes);
    QNetworkReply* put(const QString& path, QIODevice* data,
                       const QDateTime& dt = QDateTime());
    QNetworkReply* put(const QString& path, const QByteArray& data,
                       const QDateTime& dt = QDateTime());
    QNetworkReply* mkdir(const QString& dir);
    QNetworkReply* move(const QString& pathFrom, const QString& pathTo,
                        bool overwrite = false);
    QNetworkReply* remove(const QString& path);
    QNetworkReply* options(const QString& path);
    QNetworkReply* head(const QString& path);

    // PROPFIND：depth 0/1。davbisync 经 list() 间接用到。
    QNetworkReply* propfind(const QString& path, int depth = 0);

    // ── 垫片信号（非 moc）：errorChanged ──
    // vendor 里 davbisync 连的就是这个（davbisync.cpp:105），用于把被吞掉的
    // 认证/SSL 等内部原因暴露出来。
    void errorChanged(QString error);
    typedef std::function<void(QString)> ErrorSlot;
    std::vector<ErrorSlot>& slots_errorChanged() { return m_slotsErrorChanged; }
    void emitErrorChanged(const QString& e)
    {
        for (size_t i = 0; i < m_slotsErrorChanged.size(); ++i) {
            m_slotsErrorChanged[i](e);
        }
    }

protected:
    // 统一出口：拼 URL / 补认证头 / SSL pin 比对 / 空闲超时挂表
    QNetworkReply* createRequest(const QString& method, QNetworkRequest& req,
                                 QIODevice* outgoingData = 0);
    QNetworkReply* createRequest(const QString& method, QNetworkRequest& req,
                                 const QByteArray& outgoingData);
    QNetworkReply* sendCustomRequest(const QString& method, const QByteArray& data,
                                     QNetworkRequest& req, QIODevice* outgoingData);

    QNetworkRequest mkcolRequest(const QString& path);
    QNetworkRequest proFindRequest(const QString& path, int depth);

    QString buildUrl(const QString& path);
    void emitSslErrorsOnReply(QNetworkReply* reply);

    QWebdavConnectionType m_connectionType;
    QString m_hostname;
    int m_port;
    QString m_rootPath;
    QString m_username;
    QString m_password;
    QString m_sslCertDigestMd5;
    QString m_sslCertDigestSha1;
    int m_transferTimeoutMsecs;
    QNetworkReply* m_lastReply;
    QMap<QNetworkReply*, QIODevice*> m_inDataDevices;
    QMap<QNetworkReply*, QIODevice*> m_outDataDevices;
    QNetworkReply* m_authenticator_lastReply;
    QAuthenticator m_authenticator;
    std::vector<ErrorSlot> m_slotsErrorChanged;
};

#endif // QWEBDAVLITE_H
