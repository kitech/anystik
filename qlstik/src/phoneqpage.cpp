#include "phoneqpage.h"

#include "compat34.h"
#include "pagemanager.h"
#include "phonedb.h"
#include "qstring_shim.h"
#include "toastwidget.h"

#ifdef QT3_BUILD
#include "qglobaltype_shim.h"
#endif

#include <qlabel.h>
#include <qlineedit.h>
#include <qpushbutton.h>
#include <qtextedit.h>
#include <qwidget.h>
#include <qframe.h>
#ifdef QT3_BUILD
#include <qregexp.h>
#include <qcolor.h>
#else
#include <QColor>
#include <QRegularExpression>
#endif

namespace {

void setLabelGray(QLabel* l)
{
    if (!l) return;
#ifdef QT3_BUILD
    l->setPaletteForegroundColor(QColor(140, 144, 180));
#else
    l->setStyleSheet(QStringLiteral("color: #8c90b4;"));
#endif
}

void setLabelColor(QLabel* l, const QColor& c)
{
    if (!l) return;
#ifdef QT3_BUILD
    l->setPaletteForegroundColor(c);
#else
    l->setStyleSheet(QStringLiteral("color: ") + c.name() + QStringLiteral(";"));
#endif
}

} // namespace

PhoneQPage::PhoneQPage(QWidget* parent)
    : Page(parent)
    , m_title(0), m_numberEdit(0), m_queryBtn(0), m_resultEdit(0), m_metaLabel(0), m_statusLabel(0)
{
}

void PhoneQPage::onCreate(const QVariantMap&, const QVariantMap&)
{
    QBoxLayout* root = qNewBoxLayout(this, QBoxLayout::TopToBottom, 0, 0);
    root->setSpacing(0);
    qSetMargins(root, 0, 0, 0, 0);

    // 顶栏：返回 + 标题居中 + 右侧占位
    QWidget* topBar = new QWidget(this);
    QBoxLayout* tb = qNewBoxLayout(topBar, QBoxLayout::LeftToRight, 0, 0);
    tb->setSpacing(8);
    qSetMargins(tb, 4, 4, 4, 4);

    QPushButton* backBtn = new QPushButton(QString::fromUtf8("\xe2\x86\x90"), topBar);
    backBtn->setFixedSize(44, 44);
    connect(backBtn, SIGNAL(clicked()), this, SLOT(onBackClicked()));

    m_title = new QLabel(QString::fromUtf8("号码归属地"), topBar);
    m_title->setAlignment(Qt::AlignCenter);

    QWidget* tbSpacer = new QWidget(topBar);
    tbSpacer->setFixedWidth(44);

    tb->addWidget(backBtn, 0);
    tb->addWidget(m_title, 1);
    tb->addWidget(tbSpacer, 0);
    root->addWidget(topBar, 0);

    root->addSpacing(8);
    root->addStretch(1);

    // 输入行
    QWidget* row = new QWidget(this);
    QBoxLayout* rl = qNewBoxLayout(row, QBoxLayout::LeftToRight, 0, 0);
    rl->setSpacing(12);
    qSetMargins(rl, 12, 12, 12, 12);

    m_numberEdit = new QLineEdit(row);
#ifdef QT3_BUILD
    // Qt3 无 setPlaceholderText，跳过
#else
    m_numberEdit->setPlaceholderText(QString::fromUtf8("如 13800138000"));
#endif
    m_numberEdit->setMinimumHeight(30);

    m_queryBtn = new QPushButton(QString::fromUtf8("查询"), row);
    m_queryBtn->setFixedSize(64, 44);
    connect(m_queryBtn, SIGNAL(clicked()), this, SLOT(doQuery()));
    connect(m_numberEdit, SIGNAL(textChanged(const QString&)),
            this, SLOT(updateStatusLabel()));

    rl->addWidget(m_numberEdit, 1);
    rl->addWidget(m_queryBtn, 0);
    root->addWidget(row, 0);

    // 结果区（只读、可选中复制）
    m_resultEdit = new QTextEdit(this);
    m_resultEdit->setReadOnly(true);
    m_resultEdit->setFrameShape(QFrame::NoFrame);
#ifndef QT3_BUILD
    m_resultEdit->setAcceptRichText(false);
#endif
#if QT_VERSION >= 0x040000
    m_resultEdit->setLineWrapMode(QTextEdit::WidgetWidth);
#endif
    m_resultEdit->setMinimumHeight(96);
    root->addWidget(m_resultEdit, 0);

    root->addStretch(1);
    root->addSpacing(4);

    // 库信息
    m_metaLabel = new QLabel(this);
#ifndef QT3_BUILD
    m_metaLabel->setWordWrap(true);
#endif
    setLabelGray(m_metaLabel);
    root->addWidget(m_metaLabel, 0);

    root->addSpacing(8);

    // 状态
    m_statusLabel = new QLabel(this);
#ifndef QT3_BUILD
    m_statusLabel->setWordWrap(true);
#endif
    setLabelGray(m_statusLabel);
    root->addWidget(m_statusLabel, 0);

    root->addSpacing(12);

    PhoneDb* db = PhoneDb::instance();
    connect(db, SIGNAL(statusChanged()), this, SLOT(onDbStatusChanged()));
    db->ensureData();
    updateStatusLabel();
    updateMetaLabel();
}

void PhoneQPage::onDbStatusChanged()
{
    updateStatusLabel();
    updateMetaLabel();
}

void PhoneQPage::doQuery()
{
    PhoneDb* db = PhoneDb::instance();
    if (!db || !db->ready()) {
        if (m_resultEdit)
            m_resultEdit->clear();
        if (m_statusLabel) {
            setLabelColor(m_statusLabel, QColor(255, 91, 91));
            m_statusLabel->setText(QString::fromUtf8("归属地库尚未就绪，请稍候"));
        }
        if (db)
            db->ensureData();
        return;
    }

    QString raw = m_numberEdit ? m_numberEdit->text() : QString();
    raw = qTrimmed(raw);
    QString digits = raw;
#ifdef QT3_BUILD
    digits.replace(QRegExp(QStringLiteral("\\D")), QString());
#else
    digits.remove(QRegularExpression(QStringLiteral("\\D")));
#endif
    if (digits.length() != 11) {
        if (m_resultEdit)
            m_resultEdit->clear();
        if (m_statusLabel) {
            setLabelColor(m_statusLabel, QColor(240, 190, 90));
            m_statusLabel->setText(QString::fromUtf8("请输入 11 位手机号"));
        }
        return;
    }

    const PhoneDb::Result r = db->lookup(digits);
    if (!r.ok) {
        if (m_resultEdit)
            m_resultEdit->clear();
        if (m_statusLabel) {
            setLabelColor(m_statusLabel, QColor(255, 91, 91));
            m_statusLabel->setText(QString::fromUtf8("未收录该号段"));
        }
        return;
    }

    if (m_statusLabel) {
        setLabelColor(m_statusLabel, QColor(100, 180, 255));
        m_statusLabel->setText(QString::fromUtf8("已按离线库查询"));
    }
    if (m_resultEdit) {
        m_resultEdit->setText(QString::fromUtf8("归属地：%1 %2（%3）")
                              .arg(r.province, r.city, r.operatorName));
    }
}

void PhoneQPage::updateStatusLabel()
{
    PhoneDb* db = PhoneDb::instance();
    if (!m_statusLabel || !db)
        return;
    setLabelColor(m_statusLabel, QColor(200, 200, 210));
    m_statusLabel->setText(db->statusText());
}

void PhoneQPage::updateMetaLabel()
{
    PhoneDb* db = PhoneDb::instance();
    if (!m_metaLabel || !db)
        return;
    const QString meta = db->metaText();
    m_metaLabel->setText(meta);
#ifdef QT3_BUILD
    if (meta.isEmpty())
        m_metaLabel->hide();
    else
        m_metaLabel->show();
#else
    m_metaLabel->setVisible(!meta.isEmpty());
#endif
}

void PhoneQPage::onBackClicked()
{
    finish();
}
