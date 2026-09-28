#include "ipinfopage.h"
#include <QskLinearBox.h>
#include <QskTextLabel.h>
#include <QskPushButton.h>
#include <QskFontRole.h>
#include <QskTextOptions.h>
#include <QskSizePolicy.h>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QVector>
#include <QColor>

#include <cstdio>
#include <cstring>
#include <vector>

// netut（touse 共享模块）：ipaddr_list.c。本页零结构体、经其官方 JSON 导出函数取数。
extern "C" {
typedef struct netut_ip_list netut_ip_list_t; // 不透明 tag，非结构体定义
netut_ip_list_t* netut_get_all_ip_addresses(void);
void netut_free_ip_list(netut_ip_list_t*);
int netut_format_ip_list_json(const netut_ip_list_t* list, char* buffer, size_t buffer_size);
}

namespace {
constexpr int kExitCount = 5;
const char* const kExitNames[kExitCount] = {
    "ifconfig.io", "api64.ipify.org", "api.ip.pe.kr",
    "curlmyip.net", "ident.me" };
const char* const kExitUrls[kExitCount] = {
    "https://ifconfig.io/ip",
    "https://api64.ipify.org/",
    "https://api.ip.pe.kr",
    "https://curlmyip.net/",
    "https://ident.me" };
}

IPInfoPage::IPInfoPage(QQuickItem* parent) : Page(parent) {}

void IPInfoPage::onCreate(const QVariantMap&, const QVariantMap&)
{
    setAutoLayoutChildren(true);
    auto* layout = new QskLinearBox(Qt::Vertical, this);
    layout->setPanel(true);

    // ── TopBar ──
    auto* topBar = new QskLinearBox(Qt::Horizontal, layout);
    topBar->setPanel(true);
    topBar->setPreferredHeight(56);

    auto* backBtn = new QskPushButton(QString::fromUtf8("←"), topBar);
    backBtn->setPreferredSize(44, 44);
    m_title = new QskTextLabel(tr("IP 信息"), topBar);
    m_title->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    m_title->setAlignment(Qt::AlignCenter);
    topBar->addSpacer(44, 0); // 与返回键对称

    connect(backBtn, &QskAbstractButton::clicked, this, [this]() {
        finish();
    });

    layout->addSpacer(8, 0);

    // ── 本机地址 ──
    auto* localTitle = new QskTextLabel(tr("本机地址"), layout);
    localTitle->setFontRole(QskFontRole::Caption);
    localTitle->setTextColor(QColor(140, 160, 190));

    m_localLabel = new QskTextLabel(QString(), layout);
    m_localLabel->setFontRole(QskFontRole::Subtitle);
    m_localLabel->setWrapMode(QskTextOptions::WrapAnywhere);
    m_localLabel->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Constrained);

    layout->addSpacer(12, 0);

    // ── 出口地址 ──
    auto* exitTitle = new QskTextLabel(tr("出口地址"), layout);
    exitTitle->setFontRole(QskFontRole::Caption);
    exitTitle->setTextColor(QColor(140, 160, 190));

    m_exitLabel = new QskTextLabel(QString(), layout);
    m_exitLabel->setFontRole(QskFontRole::Subtitle);
    m_exitLabel->setWrapMode(QskTextOptions::WrapAnywhere);
    m_exitLabel->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Constrained);

    layout->addSpacer(8, 0);

    // ── 刷新 ──
    m_refreshBtn = new QskPushButton(tr("刷新"), layout);
    m_refreshBtn->setPreferredSize(96, 44);
    connect(m_refreshBtn, &QskPushButton::clicked, this, &IPInfoPage::doRefresh);

    layout->addSpacer(8, 0);

    // ── 状态 ──
    m_statusLabel = new QskTextLabel(QString(), layout);
    m_statusLabel->setFontRole(QskFontRole::Caption);
    m_statusLabel->setWrapMode(QskTextOptions::WrapAnywhere);
    m_statusLabel->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Constrained);

    m_nam = new QNetworkAccessManager(this);
    m_nam->setTransferTimeout(15000);

    refreshLocal();
    doRefresh();
}

void IPInfoPage::refreshLocal()
{
    QStringList lines;
    netut_ip_list_t* list = netut_get_all_ip_addresses();
    if (list) {
        std::vector<char> buf(1 << 18); // 256KB
        if (netut_format_ip_list_json(list, buf.data(), buf.size()) == 0) {
            const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(buf.data()));
            const QJsonArray arr = doc.object().value(QStringLiteral("addresses")).toArray();
            for (const QJsonValue& v : arr) {
                const QJsonObject o = v.toObject();
                const QString iface = o.value(QStringLiteral("interface")).toString();
                const QString addr  = o.value(QStringLiteral("address")).toString();
                const QString fam   = o.value(QStringLiteral("family")).toString();
                const int plen      = o.value(QStringLiteral("prefix_length")).toInt();
                lines.append(iface + QStringLiteral("  ") + addr +
                    (plen > 0 ? QStringLiteral("/%1 ").arg(plen) : QStringLiteral(" ")) + fam);
            }
        }
        netut_free_ip_list(list);
    }
    m_localLabel->setText(lines.isEmpty()
        ? tr("获取失败")
        : lines.join(QStringLiteral("\n")));
}

void IPInfoPage::fetchExits()
{
    m_done = 0;
    m_exitLines.clear();
    m_exitLines.fill(QString(), kExitCount);
    m_exitLabel->setText(QString());
    m_statusLabel->setTextColor(QColor(200, 200, 210));
    m_statusLabel->setText(tr("出口地址查询中…"));

    m_replies.clear();
    for (int i = 0; i < kExitCount; ++i) {
        QNetworkRequest req(QUrl(QString::fromUtf8(kExitUrls[i])));
        req.setRawHeader("User-Agent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36");
        req.setTransferTimeout(15000);
        QNetworkReply* reply = m_nam->get(req);
        m_replies.push_back(reply);
        connect(reply, &QNetworkReply::finished, this, [this, reply, i]() {
            const bool ok = reply->error() == QNetworkReply::NoError;
            const QByteArray body = ok ? reply->readAll().trimmed() : QByteArray();
            const QString ip = body.isEmpty()
                ? tr("获取失败")
                : QString::fromLatin1(body);
            m_exitLines[i] = QString::fromUtf8(kExitNames[i])
                + QStringLiteral(": ") + ip;
            QStringList rows;
            for (const QString& line : m_exitLines)
                if (!line.isEmpty())
                    rows.append(line);
            m_exitLabel->setText(rows.join(QStringLiteral("\n")));
            reply->deleteLater();
            ++m_done;
            if (m_done >= kExitCount) {
                m_statusLabel->setTextColor(QColor(100, 180, 255));
                m_statusLabel->setText(tr("已刷新"));
                m_replies.clear();
            }
        });
    }
}

void IPInfoPage::doRefresh()
{
    refreshLocal();
    fetchExits();
}

void IPInfoPage::retranslateUi()
{
    if (!m_title)
        return;
    m_title->setText(tr("IP 信息"));
    if (m_refreshBtn)
        m_refreshBtn->setText(tr("刷新"));
}