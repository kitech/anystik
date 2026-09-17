#include "imagesearchpopup.h"

#include <QskLinearBox.h>
#include <QskTextLabel.h>
#include <QskProgressBar.h>
#include <QskPushButton.h>
#include <QskFontRole.h>
#include <QskBox.h>
#include <QskGradient.h>
#include <QskBoxShapeMetrics.h>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QClipboard>
#include <QTimer>
#include <QQuickItem>
#include <QQuickWindow>

namespace {

QRectF popupParentRect(QQuickItem* parent)
{
    if (!parent) {
        return {};
    }
    if (auto* w = parent->window()) {
        return QRectF(QPointF(), w->size());
    }
    return QRectF(-parent->x(), -parent->y(),
                  parent->width(), parent->height());
}

// 面板不透明度：回读当前皮肤面板填充色，仅改 alpha（保留主题配色）
void applyPanelOpacity(QskBox* panel, qreal opacity)
{
    if (!panel) {
        return;
    }
    QskGradient g = panel->fillGradient();
    if (!g.isValid()) {
        return;
    }
    g.setAlpha(qRound(qBound(0.0, opacity, 1.0) * 255.0));
    panel->setFillGradient(g);
}

QString formatBytes(qint64 bytes)
{
    if (bytes <= 0) {
        return QStringLiteral("0 B");
    }
    double v = double(bytes);
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    int i = 0;
    while (v >= 1024.0 && i < 4) {
        v /= 1024.0;
        ++i;
    }
    return i == 0
        ? QString::number(qint64(v)) + QStringLiteral(" B")
        : QString::number(v, 'f', 1) + QStringLiteral(" ")
              + QString::fromLatin1(units[i]);
}

} // namespace

ImageSearchPopup::ImageSearchPopup(QQuickItem* parent)
    : QskPopup(parent)
{
    setModal(true);
    setOverlay(true);
    setPopupFlag(QskPopup::DeleteOnClose, false);
    setPolishOnResize(true);
    setPolishOnParentResize(true);

    m_panel = new QskBox(this);
    m_panel->setBoxShapeHint(QskBox::Panel,
        QskBoxShapeMetrics(14, Qt::AbsoluteSize));
    applyPanelOpacity(m_panel, 0.9);

    m_layout = new QskLinearBox(Qt::Vertical, m_panel);
    m_layout->setSpacing(10);
    m_layout->setMargins(18);

    // ── 标题行（左：标题 / 右：关闭按钮）──
    auto* titleRow = new QskLinearBox(Qt::Horizontal, m_layout);
    titleRow->setSpacing(8);

    auto* title = new QskTextLabel(tr("搜索相似"), titleRow);
    title->setFontRole(QskFontRole::Title);
    title->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    title->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    m_cornerCloseBtn = new QskPushButton(QStringLiteral("✕"), titleRow);
    connect(m_cornerCloseBtn, &QskAbstractButton::clicked,
            this, &QskPopup::close);

    // ── 来源 + 目标引擎行 ──
    m_fileLabel = new QskTextLabel(QString(), m_layout);
    m_fileLabel->setFontRole(QskFontRole::Caption);
    m_fileLabel->setWrapMode(QskTextOptions::WrapAnywhere);
    m_fileLabel->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Constrained);

    // ── 进度条 + 百分比 + 字节统计 ──
    auto* progressRow = new QskLinearBox(Qt::Horizontal, m_layout);
    progressRow->setSpacing(8);

    m_progressBar = new QskProgressBar(0, 100, progressRow);
    m_progressBar->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    m_progressBar->setValue(0);

    m_pctLabel = new QskTextLabel(QStringLiteral("0%"), progressRow);
    m_pctLabel->setPreferredWidth(40);
    m_pctLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);

    m_bytesLabel = new QskTextLabel(QString(), progressRow);
    m_bytesLabel->setFontRole(QskFontRole::Caption);
    m_bytesLabel->setPreferredWidth(120);
    m_bytesLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);

    // ── 状态行（host / 切换 / 成功 / 失败）──
    m_statusLabel = new QskTextLabel(tr("准备上传"), m_layout);
    m_statusLabel->setFontRole(QskFontRole::Caption);
    m_statusLabel->setWrapMode(QskTextOptions::WrapAnywhere);
    m_statusLabel->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Constrained);

    // ── 成功后的图片直链 ──
    m_urlLabel = new QskTextLabel(QString(), m_layout);
    m_urlLabel->setFontRole(QskFontRole::Caption);
    m_urlLabel->setWrapMode(QskTextOptions::WrapAnywhere);
    m_urlLabel->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Constrained);
    m_urlLabel->setVisible(false);

    // ── 图片描述（Bing 以图搜图识别，上传成功后异步填充）──
    m_descLabel = new QskTextLabel(QString(), m_layout);
    m_descLabel->setFontRole(QskFontRole::Caption);
    m_descLabel->setWrapMode(QskTextOptions::WrapAnywhere);
    m_descLabel->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Constrained);
    m_descLabel->setVisible(false);

    // ── 底部按钮行：复制链接 / 关闭 ──
    auto* buttonRow = new QskLinearBox(Qt::Horizontal, m_layout);
    buttonRow->setSpacing(6);
    buttonRow->addSpacer(0, 0);

    m_copyBtn = new QskPushButton(tr("复制链接"), buttonRow);
    m_copyBtn->setEnabled(false);
    connect(m_copyBtn, &QskAbstractButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(m_urlLabel->text());
        m_statusLabel->setText(tr("已复制链接"));
    });

    m_closeBtn = new QskPushButton(tr("关闭"), buttonRow);
    connect(m_closeBtn, &QskAbstractButton::clicked, this, &QskPopup::close);

    // ESC：窗口级 eventFilter 统一吞掉 Esc（QskPopup 自身也可能处理 Esc，
    // 此处优先走本过滤器保证语义一致）
    connect(this, &QskPopup::opened, this, [this]() {
        if (m_escFilterInstalled) {
            return;
        }
        if (auto* w = window()) {
            w->installEventFilter(this);
            m_escFilterInstalled = true;
        }
    });

    QTimer::singleShot(0, this, [this]() { updateGeometry(); });
}

ImageSearchPopup::~ImageSearchPopup()
{
    if (m_escFilterInstalled) {
        if (auto* w = window()) {
            w->removeEventFilter(this);
        }
        m_escFilterInstalled = false;
    }
}

bool ImageSearchPopup::eventFilter(QObject*, QEvent* ev)
{
    if (ev->type() == QEvent::KeyPress
        && static_cast<QKeyEvent*>(ev)->key() == Qt::Key_Escape) {
        close();
        return true;
    }
    return false;
}

void ImageSearchPopup::resetForRun(const QString& fileName, const QString& engineName)
{
    m_fileLabel->setVisible(!fileName.isEmpty());
    m_fileLabel->setText(QStringLiteral("%1 → %2").arg(fileName, engineName));

    m_progressBar->setValue(0);
    m_pctLabel->setText(QStringLiteral("0%"));
    m_bytesLabel->setText(QString());

    m_statusLabel->setTextColor(QColor(100, 180, 255));
    m_statusLabel->setText(tr("准备上传"));

    m_urlLabel->setText(QString());
    m_urlLabel->setVisible(false);
    m_copyBtn->setEnabled(false);

    m_descLabel->setText(QString());
    m_descLabel->setVisible(false);
}

void ImageSearchPopup::setProgress(int percent, qint64 bytesSent, qint64 bytesTotal,
                                   const QString& status)
{
    percent = qBound(0, percent, 100);
    m_progressBar->setValue(percent);
    m_pctLabel->setText(QStringLiteral("%1%").arg(percent));

    if (bytesTotal > 0) {
        m_bytesLabel->setText(QStringLiteral("%1 / %2")
            .arg(formatBytes(bytesSent), formatBytes(bytesTotal)));
    } else if (bytesSent > 0) {
        m_bytesLabel->setText(formatBytes(bytesSent));
    }

    if (!status.isEmpty()) {
        m_statusLabel->setText(status);
    }
}

void ImageSearchPopup::setUploadedUrl(const QString& url)
{
    m_urlLabel->setText(url);
    m_urlLabel->setVisible(true);
    m_copyBtn->setEnabled(true);
    m_statusLabel->setTextColor(QColor(120, 210, 140));
    m_statusLabel->setText(tr("上传成功，已打开浏览器"));
    m_progressBar->setValue(100);
    m_pctLabel->setText(QStringLiteral("100%"));
}

void ImageSearchPopup::setFailed(const QString& reason)
{
    m_copyBtn->setEnabled(false);
    m_urlLabel->setVisible(false);
    m_progressBar->setValue(0);
    m_pctLabel->setText(QStringLiteral("0%"));
    m_bytesLabel->setText(QString());
    m_statusLabel->setTextColor(QColor(255, 91, 91));
    m_statusLabel->setText(reason.isEmpty() ? tr("上传失败") : reason);
}

void ImageSearchPopup::setDescription(const QString& desc)
{
    const QString text = desc.trimmed();
    if (text.isEmpty()) {
        m_descLabel->setText(QString());
        m_descLabel->setVisible(false);
        return;
    }
    m_descLabel->setTextColor(QColor(100, 180, 255));
    m_descLabel->setText(tr("AI 描述：%1").arg(text));
    m_descLabel->setVisible(true);
}

void ImageSearchPopup::setDescriptionFailed(const QString& reason)
{
    const QString text = reason.trimmed();
    m_descLabel->setTextColor(QColor(180, 180, 185));
    m_descLabel->setText(tr("AI 描述：%1")
        .arg(text.isEmpty() ? tr("未获取到图片描述") : text));
    m_descLabel->setVisible(true);
}

void ImageSearchPopup::updateLayout()
{
    updateGeometry();
    m_layout->setGeometry(layoutRect());
}

void ImageSearchPopup::updateGeometry()
{
    const auto parentRect = popupParentRect(parentItem());
    if (parentRect.isEmpty()) {
        return;
    }

    // 2/3 高度浮层，宽 0.9 父宽下限，水平垂直居中
    const qreal maxW = 0.9 * parentRect.width();
    const qreal maxH = parentRect.height() * 2.0 / 3.0;

    const auto hint = m_layout->effectiveSizeHint(Qt::PreferredSize, QSizeF());
    const qreal panelW = qMin(qMax(300.0, hint.width() + 36), maxW);
    const qreal panelH = qMin(qMax(240.0, hint.height() + 36), maxH);

    QRectF r(0, 0, panelW, panelH);
    r.moveCenter(parentRect.center());
    setGeometry(r);
    m_panel->setGeometry(r.translated(-r.topLeft()));
}