#include "phoneqpage.h"
#include "phonedb.h"
#include <QskLinearBox.h>
#include <QskTextLabel.h>
#include <QskPushButton.h>
#include <QskTextField.h>
#include <QskFontRole.h>
#include <QskTextOptions.h>
#include <QskSizePolicy.h>
#include <QColor>
#include <QRegularExpression>

PhoneQPage::PhoneQPage(QQuickItem* parent)
    : Page(parent)
{
}

void PhoneQPage::onCreate(const QVariantMap&, const QVariantMap&)
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
    m_title = new QskTextLabel(tr("号码归属地"), topBar);
    m_title->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    m_title->setAlignment(Qt::AlignCenter);
    topBar->addSpacer(44, 0); // 与返回键对称

    connect(backBtn, &QskAbstractButton::clicked, this, [this]() {
        finish();
    });

    layout->addSpacer(8, 0);

    // ── 输入行 ──
    layout->addStretch(1);   // 上方弹性填充
    auto* row = new QskLinearBox(Qt::Horizontal, layout);
    row->setSpacing(12);

    m_numberEdit = new QskTextField(row);
    m_numberEdit->setPlaceholderText(QString::fromUtf8("如 13800138000"));
    m_numberEdit->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    m_numberEdit->setFixedHeight(30);

    m_queryBtn = new QskPushButton(tr("查询"), row);
    m_queryBtn->setPreferredSize(64, 44);
    connect(m_queryBtn, &QskPushButton::clicked, this, &PhoneQPage::doQuery);
    connect(m_numberEdit, &QskTextField::textChanged, this, [this]() {
        updateStatusLabel();
    });

    // ── 结果 ──
    m_resultLabel = new MultiLineTextEdit(layout);
    m_resultLabel->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    m_resultLabel->setPreferredHeight(96);
    m_resultLabel->setReadOnly(true);   // 结果区只读：可选中/复制，禁编辑
    m_resultLabel->setText(QString());

    layout->addStretch(1);   // 结果与状态之间弹性填充
    layout->addSpacer(4, 0);

    // ── 库元信息 ──
    m_metaLabel = new QskTextLabel(QString(), layout);
    m_metaLabel->setFontRole(QskFontRole::Caption);
    m_metaLabel->setTextColor(QColor(140, 160, 190));
    m_metaLabel->setWrapMode(QskTextOptions::WrapAnywhere);
    m_metaLabel->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Constrained);

    layout->addSpacer(8, 0);

    // ── 状态 ──
    m_statusLabel = new QskTextLabel(QString(), layout);
    m_statusLabel->setFontRole(QskFontRole::Caption);
    m_statusLabel->setWrapMode(QskTextOptions::WrapAnywhere);
    m_statusLabel->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Constrained);

    // 数据库状态联动
    auto* db = PhoneDb::instance();
    connect(db, &PhoneDb::statusChanged, this, [this]() {
        updateStatusLabel();
        updateMetaLabel();
    });
    db->ensureData();
    updateStatusLabel();
    updateMetaLabel();
}

void PhoneQPage::doQuery()
{
    auto* db = PhoneDb::instance();
    if (!db->ready()) {
        m_resultLabel->setText(QString());
        m_statusLabel->setTextColor(QColor(255, 91, 91));
        m_statusLabel->setText(tr("归属地库尚未就绪，请稍候"));
        db->ensureData();
        return;
    }

    const QString raw = m_numberEdit->text().trimmed();
    QString digits = raw;
    digits.remove(QRegularExpression(QString::fromUtf8("\\D")));
    if (digits.size() != 11) {
        m_resultLabel->setText(QString());
        m_statusLabel->setTextColor(QColor(240, 190, 90));
        m_statusLabel->setText(tr("请输入 11 位手机号"));
        return;
    }

    const auto r = db->lookup(digits);
    if (!r.ok) {
        m_resultLabel->setText(QString());
        m_statusLabel->setTextColor(QColor(255, 91, 91));
        m_statusLabel->setText(tr("未收录该号段"));
        return;
    }
    m_statusLabel->setTextColor(QColor(100, 180, 255));
    m_statusLabel->setText(tr("已按离线库查询"));
    m_resultLabel->setText(tr("归属地：%1 %2（%3）").arg(
        r.province, r.city, r.operatorName));
}

void PhoneQPage::updateStatusLabel()
{
    const auto st = PhoneDb::instance()->statusText();
    m_statusLabel->setTextColor(QColor(200, 200, 210));
    m_statusLabel->setText(st);
}

void PhoneQPage::updateMetaLabel()
{
    const QString meta = PhoneDb::instance()->metaText();
    m_metaLabel->setText(meta);
    m_metaLabel->setVisible(!meta.isEmpty());
}

void PhoneQPage::retranslateUi()
{
    if (!m_title)
        return;
    m_title->setText(tr("号码归属地"));
    if (m_queryBtn)
        m_queryBtn->setText(tr("查询"));
    updateMetaLabel();
}