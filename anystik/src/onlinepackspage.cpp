#include "onlinepackspage.h"
#include "compatcore34.h"
#include <QskLinearBox.h>
#include <QskTextLabel.h>
#include <QskPushButton.h>
#include <QskComboBox.h>
#include <QskLabelData.h>
#include <QskFontRole.h>

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

    layout->addStretch(1);

    // ── 第一行：仅预览站点选择（占位，选中暂无行为）──
    auto* siteRow = new QskLinearBox(Qt::Horizontal, layout);
    siteRow->setSpacing(12);

    auto* siteLabel = new QskTextLabel(tr("仅预览站点"), siteRow);
    siteLabel->setPreferredWidth(120);

    auto* siteCombo = new QskComboBox(siteRow);
    siteCombo->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    siteCombo->addOption(QskLabelData(tr("去斗图 (qudoutu.com)")));
    siteCombo->addOption(QskLabelData(tr("QQ表情网 (qqbiaoqing.com)")));
    siteCombo->addOption(QskLabelData("sc.chinaz.com"));
    siteCombo->addOption(QskLabelData("616pic.com"));
    siteCombo->addOption(QskLabelData("aigei.com"));
    siteCombo->addOption(QskLabelData(tr("Koishi QFace 预览")));
    siteCombo->addOption(QskLabelData("blobs.gg"));

    // 站点主页 URL（与 combo 选项索引 0..6 一一对应）
    static const char* kSiteUrls[] = {
        "https://www.qudoutu.com/",
        "https://www.qqbiaoqing.com/",
        "https://sc.chinaz.com/",
        "https://616pic.com/",
        "https://www.aigei.com/",        // 直连 403(反爬)，仍保留该预览项
        "https://koishi.js.org/QFace/",
        "https://blobs.gg/",
    };
    const auto kSiteCount = sizeof(kSiteUrls) / sizeof(kSiteUrls[0]);

    // ── 站点操作按钮：前两个为 stub（占位，未接行为）──
    auto* stubBtn1 = new QskPushButton(QString(), siteRow);
    stubBtn1->setPreferredSize(44, 44);

    auto* stubBtn2 = new QskPushButton(QString(), siteRow);
    stubBtn2->setPreferredSize(44, 44);

    // 打开：用系统浏览器打开当前 combo 选中站点的主页（qOpenUrl）。
    // 无原生 tooltip，故以注释说明；后续如需提示可加 hover 浮层。
    auto* openBtn = new QskPushButton(QString::fromUtf8("🌐"), siteRow);
    openBtn->setPreferredSize(44, 44);
    connect(openBtn, &QskPushButton::clicked, this,
            [siteCombo]() {
        const int idx = siteCombo->currentIndex();
        if (idx >= 0 && idx < static_cast<int>(kSiteCount))
            qOpenUrl(QString::fromUtf8(kSiteUrls[idx]));
    });

    layout->addStretch(1);

    // ── 占位提示 ──
    m_hint = new QskTextLabel(tr("功能开发中…"), layout);
    m_hint->setFontRole(QskFontRole::Title);
    m_hint->setAlignment(Qt::AlignCenter);

    layout->addStretch(2);
}

void OnlinePacksPage::retranslateUi()
{
    if (!m_title)
        return;
    m_title->setText(tr("在线表情"));
    m_hint->setText(tr("功能开发中…"));
}