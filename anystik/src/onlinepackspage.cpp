#include "onlinepackspage.h"
#include "searchresultgrid.h"
#include "imagesearchclient.h"
#include "compatcore34.h"
#include <QSettings>
#include <QUrl>
#include <QskLinearBox.h>
#include <QskTextLabel.h>
#include <QskPushButton.h>
#include <QskComboBox.h>
#include <QskTextField.h>
#include <QskLabelData.h>
#include <QskFontRole.h>

namespace {

// combo 选项索引：0..6 仅预览站点，7/8/9 搜索引擎
constexpr int kPreviewCount = 7;
constexpr int kGoogleIdx = 7;
constexpr int kBingIdx   = 8;
constexpr int kYandexIdx = 9;
constexpr int kComboCount = 10;

const char* const kDefaultKeyword = "斗图表情最新最热";

// 仅预览站点主页（与 combo 0..6 一一对应）
const char* const kSiteUrls[] = {
    "https://www.qudoutu.com/",
    "https://www.qqbiaoqing.com/",
    "https://sc.chinaz.com/",
    "https://616pic.com/",
    "https://www.aigei.com/",        // 直连 403(反爬)，仍保留该预览项
    "https://koishi.js.org/QFace/",
    "https://blobs.gg/",
};

QString encodeKw(const QString& kw)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(kw));
}

} // namespace

OnlinePacksPage::OnlinePacksPage(QQuickItem* parent)
    : Page(parent)
{
}

void OnlinePacksPage::onCreate(const QVariantMap&, const QVariantMap&)
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
    m_title = new QskTextLabel(tr("在线表情"), topBar);
    m_title->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    m_title->setAlignment(Qt::AlignCenter);
    topBar->addSpacer(44, 0); // 与返回键对称

    connect(backBtn, &QskAbstractButton::clicked, this, [this]() {
        finish();
    });

    layout->addSpacer(8, 0);

    // ── 第一行：选择 combo（预览站 0..6 + 搜索引擎 7..9）+ 浏览器打开 ──
    auto* siteRow = new QskLinearBox(Qt::Horizontal, layout);
    siteRow->setSpacing(12);

    auto* siteLabel = new QskTextLabel(tr("仅预览站点"), siteRow);
    siteLabel->setPreferredWidth(120);

    m_siteCombo = new QskComboBox(siteRow);
    m_siteCombo->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    m_siteCombo->addOption(QskLabelData(tr("去斗图 (qudoutu.com)")));
    m_siteCombo->addOption(QskLabelData(tr("QQ表情网 (qqbiaoqing.com)")));
    m_siteCombo->addOption(QskLabelData("sc.chinaz.com"));
    m_siteCombo->addOption(QskLabelData("616pic.com"));
    m_siteCombo->addOption(QskLabelData("aigei.com"));
    m_siteCombo->addOption(QskLabelData(tr("Koishi QFace 预览")));
    m_siteCombo->addOption(QskLabelData("blobs.gg"));
    m_siteCombo->addOption(QskLabelData(tr("Google图片")));
    m_siteCombo->addOption(QskLabelData(tr("Bing图片")));
    m_siteCombo->addOption(QskLabelData(tr("Yandex图片")));

    // 打开：站点 → 主页；搜索引擎 → 带关键词的图片搜索页（系统浏览器 qOpenUrl）
    auto* openBtn = new QskPushButton(QString::fromUtf8("🌐"), siteRow);
    openBtn->setPreferredSize(44, 44);
    connect(openBtn, &QskPushButton::clicked, this,
            &OnlinePacksPage::openCurrentInBrowser);

    // ── 第二行：关键词 + 搜索 ──
    auto* kwRow = new QskLinearBox(Qt::Horizontal, layout);
    kwRow->setSpacing(12);

    auto* kwLabel = new QskTextLabel(tr("关键词"), kwRow);
    kwLabel->setPreferredWidth(120);

    m_keywordEdit = new QskTextField(kwRow);
    m_keywordEdit->setPlaceholderText(QString::fromUtf8(kDefaultKeyword));
    m_keywordEdit->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    m_keywordEdit->setFixedHeight(30);
    // 恢复上次关键词
    const QString kw = QSettings().value("onlinepacks_kw",
        QString::fromUtf8(kDefaultKeyword)).toString();
    m_keywordEdit->setText(kw);

    m_searchBtn = new QskPushButton(tr("搜索"), kwRow);
    m_searchBtn->setPreferredSize(64, 44);
    connect(m_searchBtn, &QskPushButton::clicked, this,
            &OnlinePacksPage::doSearch);

    // ── 状态行 ──
    m_statusLabel = new QskTextLabel(tr("输入关键词点搜索：Bing/Yandex 应用内展示，Google 打开浏览器"), layout);
    m_statusLabel->setFontRole(QskFontRole::Caption);
    m_statusLabel->setWrapMode(QskTextOptions::WrapAnywhere);
    m_statusLabel->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Constrained);

    // ── 结果网格 ──
    m_resultGrid = new SearchResultGrid(layout);
    m_resultGrid->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Expanding);

    // ── 后台图片搜索客户端（Bing/Yandex 应用内）──
    m_searchClient = new ImageSearchClient(this);
    connect(m_searchClient, &ImageSearchClient::resultsReady, this,
        [this](ImageSearchClient::Engine, const QStringList& urls) {
            m_resultGrid->setResults(urls);
            m_statusLabel->setTextColor(QColor(200, 200, 210));
            m_statusLabel->setText(tr("%1 个结果").arg(urls.size()));
        });
    connect(m_searchClient, &ImageSearchClient::errorOccurred, this,
        [this](ImageSearchClient::Engine, const QString& message) {
            m_resultGrid->clear();
            m_statusLabel->setTextColor(QColor(255, 91, 91));
            m_statusLabel->setText(message);
        });

    // ── 点击结果 → 页内预览 ──
    connect(m_resultGrid, &SearchResultGrid::resultClicked, this,
        [this](const QString& url) {
            if (!m_preview) {
                m_preview = new RemoteImagePreview(this);
                connect(m_preview, &QskPopup::closed, this, [this]() {
                    if (m_preview) {
                        m_preview->deleteLater();
                        m_preview = nullptr;
                    }
                });
            }
            m_preview->showImage(url);
        });

    // ── 恢复「在线表情」当前选中项 ──
    // 键: onlinepacks_site（int, 0..9, 默认 0），onStop 保存；越界回退到 0。
    const int idx = qBound(0, QSettings().value("onlinepacks_site", 0).toInt(),
                           kComboCount - 1);
    m_siteCombo->setCurrentIndex(idx);
}

void OnlinePacksPage::doSearch()
{
    const int idx = m_siteCombo->currentIndex();
    const QString kw = m_keywordEdit->text().trimmed();

    if (kw.isEmpty()) {
        m_statusLabel->setTextColor(QColor(240, 190, 90));
        m_statusLabel->setText(tr("请输入关键词"));
        return;
    }

    if (idx == kGoogleIdx) {
        // Google 反爬强，应用内抓取通常命中校验码 → 浏览器打开（不降级回退，直接开）
        qOpenUrl(QStringLiteral("https://www.google.com/search?tbm=isch&q=")
                 + encodeKw(kw));
        m_resultGrid->clear();
        m_statusLabel->setTextColor(QColor(200, 200, 210));
        m_statusLabel->setText(tr("已在浏览器打开 Google 图片搜索"));
        return;
    }

    if (idx == kBingIdx || idx == kYandexIdx) {
        m_resultGrid->clear();
        m_statusLabel->setTextColor(QColor(100, 180, 255));
        m_statusLabel->setText(tr("搜索中…"));
        const auto engine = (idx == kBingIdx)
            ? ImageSearchClient::Engine::Bing
            : ImageSearchClient::Engine::Yandex;
        m_searchClient->search(engine, kw, 20);
        return;
    }

    m_statusLabel->setTextColor(QColor(240, 190, 90));
    m_statusLabel->setText(tr("当前为仅预览站点，请选择 Google / Bing / Yandex 图片搜索"));
}

void OnlinePacksPage::openCurrentInBrowser()
{
    const int idx = m_siteCombo->currentIndex();
    const QString kw = m_keywordEdit->text().trimmed();

    const char* url = nullptr;
    switch (idx) {
    case kGoogleIdx:
        qOpenUrl(QStringLiteral("https://www.google.com/search?tbm=isch&q=")
                 + encodeKw(kw));
        return;
    case kBingIdx:
        qOpenUrl(QStringLiteral("https://www.bing.com/images/search?q=")
                 + encodeKw(kw));
        return;
    case kYandexIdx:
        qOpenUrl(QStringLiteral("https://yandex.com/images/search?text=")
                 + encodeKw(kw));
        return;
    default:
        if (idx >= 0 && idx < kPreviewCount)
            url = kSiteUrls[idx];
        break;
    }
    if (url) {
        qOpenUrl(QString::fromUtf8(url));
    }
}

void OnlinePacksPage::onStop()
{
    // ── 保存「在线表情」当前选中项与关键词 ──
    if (m_siteCombo)
        QSettings().setValue("onlinepacks_site", m_siteCombo->currentIndex());
    if (m_keywordEdit)
        QSettings().setValue("onlinepacks_kw", m_keywordEdit->text().trimmed());
    if (m_searchClient)
        m_searchClient->abortAll();
    Page::onStop();
}

void OnlinePacksPage::retranslateUi()
{
    if (!m_title)
        return;
    m_title->setText(tr("在线表情"));
    if (m_searchBtn)
        m_searchBtn->setText(tr("搜索"));
}