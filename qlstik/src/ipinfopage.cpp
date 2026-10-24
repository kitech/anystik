#include "ipinfopage.h"

#include "compat34.h"
#include "pagemanager.h"
#include "qba_shim.h"
#include "qstring_shim.h"
#include "toastwidget.h"
#ifdef QT3_BUILD
#include "qglobaltype_shim.h"
#include <qscrollview.h>
#else
#include <QScrollArea>
#endif

#include <qlabel.h>
#include <qpushbutton.h>
#include <qtextedit.h>
#include <qurl.h>
#include <qwidget.h>
#include <qframe.h>
#ifdef QT3_BUILD
#include <qcolor.h>
#else
#include <QColor>
#endif
#ifndef QT3_BUILD
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#else
#include "qnam_shim.h"
#include "qconnect_slots.h"
#endif

#include <vector>
#include <qobject.h>
#include <stdlib.h>
#include "cJSON.h"
#ifdef QT3_BUILD
#include <qcstring.h>
#else
#include <QByteArray>
#endif

extern "C" {
typedef struct netut_ip_list netut_ip_list_t;
netut_ip_list_t* netut_get_all_ip_addresses(void);
void netut_free_ip_list(netut_ip_list_t*);
int netut_format_ip_list_json(const netut_ip_list_t* list, char* buffer, size_t buffer_size);
}

namespace {
const char* const kExitNames[] = {
    "ifconfig.io", "api64.ipify.org", "api.ip.pe.kr",
    "curlmyip.net", "ident.me"
};
const char* const kExitUrls[] = {
    "https://ifconfig.io/ip",
    "https://api64.ipify.org/",
    "https://api.ip.pe.kr",
    "https://curlmyip.net/",
    "https://ident.me"
};
const int kExitCount = 5;
}

IPInfoPage::IPInfoPage(QWidget* parent)
    : Page(parent)
    , m_title(0), m_localTitle(0), m_exitTitle(0)
    , m_localEdit(0), m_exitEdit(0)
#ifdef QT3_BUILD
    , m_localScroll(0), m_exitScroll(0)
#else
    , m_localScroll(0), m_exitScroll(0)
#endif
    , m_refreshBtn(0), m_statusLabel(0)
    , m_nam(0), m_done(0)
{
    m_exitLines.clear();
    for (int i = 0; i < kExitCount; ++i)
        m_exitLines.append(QString());
    m_done = 0;
}

void IPInfoPage::onCreate(const QVariantMap&, const QVariantMap&)
{
    QBoxLayout* root = qNewBoxLayout(this, QBoxLayout::TopToBottom, 0, 0);
    root->setSpacing(0);
    qSetMargins(root, 0, 0, 0, 0);

    // 顶栏
    QWidget* topBar = new QWidget(this);
    QBoxLayout* tb = qNewBoxLayout(topBar, QBoxLayout::LeftToRight, 0, 0);
    tb->setSpacing(8);
    qSetMargins(tb, 4, 4, 4, 4);

    QPushButton* backBtn = new QPushButton(QString::fromUtf8("\xe2\x86\x90"), topBar);
    backBtn->setFixedSize(44, 44);
    connect(backBtn, SIGNAL(clicked()), this, SLOT(onBackClicked()));

    m_title = new QLabel(QString::fromUtf8("IP 信息"), topBar);
    m_title->setAlignment(Qt::AlignCenter);

    QWidget* tbSpacer = new QWidget(topBar);
    tbSpacer->setFixedWidth(44);

    tb->addWidget(backBtn, 0);
    tb->addWidget(m_title, 1);
    tb->addWidget(tbSpacer, 0);
    root->addWidget(topBar, 0);

    root->addSpacing(8);
    root->addStretch(1);

    // 本机地址
    m_localTitle = new QLabel(QString::fromUtf8("本机地址"), this);
#ifdef QT3_BUILD
    m_localTitle->setPaletteForegroundColor(QColor(140, 144, 180));
#else
    m_localTitle->setStyleSheet(QStringLiteral("color: #8c90b4;"));
#endif
    root->addWidget(m_localTitle, 0);
    root->addSpacing(4);

    m_localEdit = new QTextEdit(this);
    m_localEdit->setReadOnly(true);
    m_localEdit->setFrameShape(QFrame::NoFrame);
#ifndef QT3_BUILD
    m_localEdit->setAcceptRichText(false);
#endif
#if QT_VERSION >= 0x040000
    m_localEdit->setLineWrapMode(QTextEdit::WidgetWidth);
#endif
#ifdef QT3_BUILD
    m_localScroll = new QScrollView(this);
    m_localScroll->setResizePolicy(QScrollView::Manual);
    m_localScroll->setHScrollBarMode(QScrollView::AlwaysOff);
    m_localScroll->addChild(m_localEdit);
#else
    m_localScroll = new QScrollArea(this);
    m_localScroll->setWidgetResizable(true);
    m_localScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_localScroll->setWidget(m_localEdit);
#endif
    m_localScroll->setMinimumHeight(160);
    root->addWidget(m_localScroll, 0);

    root->addSpacing(12);

    // 出口地址
    m_exitTitle = new QLabel(QString::fromUtf8("出口地址"), this);
#ifdef QT3_BUILD
    m_exitTitle->setPaletteForegroundColor(QColor(140, 144, 180));
#else
    m_exitTitle->setStyleSheet(QStringLiteral("color: #8c90b4;"));
#endif
    root->addWidget(m_exitTitle, 0);
    root->addSpacing(4);

    m_exitEdit = new QTextEdit(this);
    m_exitEdit->setReadOnly(true);
    m_exitEdit->setFrameShape(QFrame::NoFrame);
#ifndef QT3_BUILD
    m_exitEdit->setAcceptRichText(false);
#endif
#if QT_VERSION >= 0x040000
    m_exitEdit->setLineWrapMode(QTextEdit::WidgetWidth);
#endif
#ifdef QT3_BUILD
    m_exitScroll = new QScrollView(this);
    m_exitScroll->setResizePolicy(QScrollView::Manual);
    m_exitScroll->setHScrollBarMode(QScrollView::AlwaysOff);
    m_exitScroll->addChild(m_exitEdit);
#else
    m_exitScroll = new QScrollArea(this);
    m_exitScroll->setWidgetResizable(true);
    m_exitScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_exitScroll->setWidget(m_exitEdit);
#endif
    m_exitScroll->setMinimumHeight(180);
    root->addWidget(m_exitScroll, 0);

    root->addSpacing(8);

    m_refreshBtn = new QPushButton(QString::fromUtf8("刷新"), this);
    m_refreshBtn->setFixedSize(96, 44);
    connect(m_refreshBtn, SIGNAL(clicked()), this, SLOT(doRefresh()));
    root->addWidget(m_refreshBtn, 0, Qt::AlignHCenter);

    root->addSpacing(8);

    m_statusLabel = new QLabel(this);
#ifndef QT3_BUILD
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #c8c8d2;"));
#endif
    root->addWidget(m_statusLabel, 0);

    root->addStretch(1);

    m_nam = new QNetworkAccessManager(this);
#ifndef QT3_BUILD
#if QT_VERSION >= 0x050000
    m_nam->setTransferTimeout(15000);
#endif
#endif

    refreshLocal();
    doRefresh();
}

void IPInfoPage::refreshLocal()
{
    QStringList lines;
    netut_ip_list_t* list = netut_get_all_ip_addresses();
    if (list) {
        // 纯 C 段：malloc 缓冲 + cJSON 直解析，不过任何 Qt/STL 容器
        char* const buf = (char*)malloc(256 * 1024);
        if (buf) {
            if (netut_format_ip_list_json(list, buf, 256 * 1024) == 0) {
                cJSON* const root = cJSON_Parse(buf);
                if (root) {
                    cJSON* const arr = cJSON_GetObjectItem(root, "addresses");
                    if (arr && cJSON_IsArray(arr)) {
                        for (cJSON* it = arr->child; it; it = it->next) {
                            const cJSON* jiface = cJSON_GetObjectItem(it, "interface");
                            const cJSON* jaddr  = cJSON_GetObjectItem(it, "address");
                            const cJSON* jfam   = cJSON_GetObjectItem(it, "family");
                            const cJSON* jplen  = cJSON_GetObjectItem(it, "prefix_length");
                            if (jiface && jiface->valuestring && jaddr
                                && jaddr->valuestring && jfam && jfam->valuestring) {
                                QString line = QString::fromUtf8(jiface->valuestring)
                                    + QStringLiteral("  ")
                                    + QString::fromUtf8(jaddr->valuestring);
                                if (jplen && jplen->valueint > 0)
                                    line += QStringLiteral("/")
                                        + QString::number(jplen->valueint);
                                line += QStringLiteral(" ")
                                    + QString::fromUtf8(jfam->valuestring);
                                lines.append(line);
                            }
                        }
                    }
                    cJSON_Delete(root);
                }
            }
            free(buf);
        }
        netut_free_ip_list(list);
    }
    if (m_localEdit)
        m_localEdit->setText(lines.isEmpty() ? QString::fromUtf8("获取失败") : lines.join(QStringLiteral("\n")));
}

void IPInfoPage::fetchExits()
{
#ifdef QT3_BUILD
    using ::connect;
#endif
    m_done = 0;
    for (int i = 0; i < kExitCount; ++i)
        m_exitLines[i] = QString();
    if (m_exitEdit)
        m_exitEdit->clear();
    if (m_statusLabel) {
#ifndef QT3_BUILD
        m_statusLabel->setStyleSheet(QStringLiteral("color: #c8c8d2;"));
#endif
        m_statusLabel->setText(QString::fromUtf8("出口地址查询中…"));
    }

    for (size_t k = 0; k < m_replies.size(); ++k)
        m_replies[k]->deleteLater();
    m_replies.clear();

    for (int i = 0; i < kExitCount; ++i) {
        QNetworkRequest req(QUrl(QString::fromUtf8(kExitUrls[i])));
        req.setRawHeader("User-Agent", "Mozilla/5.0 (X11; Linux x86_64)");
#ifndef QT3_BUILD
#if QT_VERSION >= 0x050000
        req.setTransferTimeout(15000);
#endif
#endif
        QNetworkReply* reply = m_nam->get(req);
        m_replies.push_back(reply);
        // Qt3 无动态属性、moc 不认垫片信号；用函数指针 + lambda 捕住
        // (reply, i)，范式同 stickerstore.cpp:3422
        connect(reply, &QNetworkReply::finished, this,
            [this, reply, i] { onExitReplyFinished(reply, i); });
    }
}

void IPInfoPage::onExitReplyFinished(QNetworkReply* reply, int i)
{
    const bool ok = (reply->error() == QNetworkReply::NoError);
    QByteArray body;
    if (ok)
        body = qTrimmed(reply->readAll());
    QString ip = body.isEmpty() ? QString::fromUtf8("获取失败") : QString(body);
    if (i >= 0 && i < kExitCount) {
        m_exitLines[i] = QString::fromUtf8(kExitNames[i]) + QStringLiteral(": ") + ip;
    }
    updateExitText();
    reply->deleteLater();
    for (std::vector<QNetworkReply*>::iterator it = m_replies.begin();
         it != m_replies.end(); ++it) {
        if (*it == reply) {
            m_replies.erase(it);
            break;
        }
    }
    ++m_done;
    if (m_done >= kExitCount && m_statusLabel) {
#ifndef QT3_BUILD
        m_statusLabel->setStyleSheet(QStringLiteral("color: #64b4ff;"));
#endif
        m_statusLabel->setText(QString::fromUtf8("已刷新"));
    }
}

void IPInfoPage::updateExitText()
{
    QStringList rows;
    for (int i = 0; i < kExitCount; ++i) {
        if (!m_exitLines[i].isEmpty())
            rows.append(m_exitLines[i]);
    }
    if (m_exitEdit)
        m_exitEdit->setText(rows.join(QStringLiteral("\n")));
}

void IPInfoPage::doRefresh()
{
    refreshLocal();
    fetchExits();
}

void IPInfoPage::onBackClicked()
{
    finish();
}
