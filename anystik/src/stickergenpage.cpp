#include "stickergenpage.h"
#include <QskLinearBox.h>
#include <QskTextLabel.h>
#include <QskPushButton.h>
#include <QskFontRole.h>

StickerGenPage::StickerGenPage(QQuickItem* parent)
    : Page(parent)
{
}

void StickerGenPage::onCreate(const QVariantMap&, const QVariantMap&)
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
    m_title = new QskTextLabel(tr("生成表情"), topBar);
    m_title->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    m_title->setAlignment(Qt::AlignCenter);
    topBar->addSpacer(44, 0); // 与返回键对称

    connect(backBtn, &QskAbstractButton::clicked, this, [this]() {
        finish();
    });

    layout->addStretch(1);

    // ── 占位提示 ──
    m_hint = new QskTextLabel(tr("功能开发中…"), layout);
    m_hint->setFontRole(QskFontRole::Title);
    m_hint->setAlignment(Qt::AlignCenter);

    layout->addStretch(2);
}

void StickerGenPage::retranslateUi()
{
    if (!m_title)
        return;
    m_title->setText(tr("生成表情"));
    m_hint->setText(tr("功能开发中…"));
}