#include "stickergenpage.h"
#include "toastpopup.h"
#include "menuoverlay.h"
#include "myscrollarea.h"

#include <QskLinearBox.h>
#include <QskTextLabel.h>
#include <QskPushButton.h>
#include <QskComboBox.h>
#include <QskLabelData.h>
#include <QskSpinBox.h>
#include <QskCheckBox.h>
#include <QskTextField.h>
#include <QskGraphicLabel.h>
#include <QskGraphic.h>
#include <QskBoxShapeMetrics.h>
#include <QskMenu.h>
#include <QskPopup.h>
#include <QskFunctions.h>
#include <QskSeparator.h>

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QImage>
#include <QGuiApplication>
#include <QClipboard>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTimer>
#include <QDateTime>
#include <QRandomGenerator>
#include <QFontMetricsF>

#include <private/qquicktaphandler_p.h>
#include <private/qquicksinglepointhandler_p.h>

namespace {

const int kRateLimitSecs = 15;   // Pollinations 匿名层：1 次 / 15s
const int kHistoryMax = 20;
const int kImageSize = 512;      // 生成尺寸（宽高）

QString engineUrl()
{
    return QStringLiteral("https://image.pollinations.ai/prompt/");
}

// 桌面右键：只收 RightButton，回调场景坐标（与 stickerlist 同机制）
class RightClickHandler final : public QQuickSinglePointHandler
{
public:
    explicit RightClickHandler(QQuickItem* target)
        : QQuickSinglePointHandler(target)
    {
        setAcceptedButtons(Qt::RightButton);
    }

    std::function<void(const QPointF&)> onRightPress;

protected:
    void handleEventPoint(QPointerEvent* event, QEventPoint& point) override
    {
        const auto* single = dynamic_cast<const QSinglePointEvent*>(event);
        if (event->type() == QEvent::MouseButtonPress
                && single && single->button() == Qt::RightButton
                && onRightPress) {
            onRightPress(point.scenePosition());
        }
    }
};

} // namespace

StickerGenPage::StickerGenPage(QQuickItem* parent)
    : Page(parent)
{
}

void StickerGenPage::onCreate(const QVariantMap&, const QVariantMap&)
{
    setAutoLayoutChildren(true);
    auto* rootLayout = new QskLinearBox(Qt::Vertical, this);
    rootLayout->setPanel(true);

    // ── TopBar ──
    auto* topBar = new QskLinearBox(Qt::Horizontal, rootLayout);
    topBar->setPanel(true);
    topBar->setPreferredHeight(56);

    auto* backBtn = new QskPushButton(QString::fromUtf8("←"), topBar);
    backBtn->setPreferredSize(44, 44);
    m_title = new QskTextLabel(tr("生成表情"), topBar);
    m_title->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    m_title->setAlignment(Qt::AlignCenter);
    topBar->addSpacer(44, 0);

    connect(backBtn, &QskAbstractButton::clicked, this, [this]() {
        finish();
    });

    // ── 滚动正文（combo/输入/操作/状态/预览）──
    m_scroll = new MyScrollArea(rootLayout);
    m_scroll->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Expanding);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* layout = new QskLinearBox(Qt::Vertical, m_scroll);
    m_scroll->setItemResizable(true);
    m_scroll->setScrolledItem(layout);

    layout->addSpacer(16, 0);

    // ── 生成器选择 ──
    auto* comboRow = new QskLinearBox(Qt::Horizontal, layout);
    comboRow->setSpacing(12);
    m_engineLabel = new QskTextLabel(tr("生成器"), comboRow);
    m_engineLabel->setPreferredWidth(80);
    m_engineCombo = new QskComboBox(comboRow);
    m_engineCombo->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    m_engineCombo->addOption(QskLabelData(QStringLiteral("Pollinations")));
    m_engineCombo->setCurrentIndex(0);

    new QskSeparator(Qt::Horizontal, layout);

    // ── 提示词输入（3 行高）──
    m_promptInput = new QskTextField(layout);
    m_promptInput->setFixedHeight(76);
    m_promptInput->setWrapMode(QskTextOptions::WordWrap);
    m_promptInput->setPlaceholderText(
        tr("例如：一只戴墨镜的沙雕熊猫贴纸"));

    new QskSeparator(Qt::Horizontal, layout);
    // ── 行1：历史 / 生成（等宽撑满）──
    auto* actionRow = new QskLinearBox(Qt::Horizontal, layout);
    actionRow->setSpacing(10);

    m_historyBtn = new QskPushButton(tr("历史"), actionRow);
    m_historyBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    m_historyBtn->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    connect(m_historyBtn, &QskPushButton::clicked, this,
            [this]() { openHistoryMenu(QPointF()); });

    m_genBtn = new QskPushButton(tr("生成"), actionRow);
    m_genBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    m_genBtn->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    connect(m_genBtn, &QskPushButton::clicked, this,
            &StickerGenPage::startGenerate);

    // ── 行2：随机种子 / 贴纸风格 / 保存到表情包 ──
    auto* optionsRow = new QskLinearBox(Qt::Horizontal, layout);
    optionsRow->setSpacing(10);

    // 随机种子
    m_seedLabel = new QskTextLabel(tr("随机种子"), optionsRow);
    m_seedLabel->setPreferredWidth(80);
    m_seedSpin = new QskSpinBox(optionsRow);
    m_seedSpin->setBoundaries(0, 999999999);
    m_seedSpin->setDecimals(0);
    m_seedSpin->setStepSize(1);
    m_seedSpin->setValue(QRandomGenerator::global()->bounded(1000000000));

    // 贴纸风格
    m_styleCheck = new QskCheckBox(tr("贴纸风格"), optionsRow);
    m_styleCheck->setChecked(true);

    // 保存到表情包
    m_saveBtn = new QskPushButton(tr("保存到表情包"), optionsRow);
    m_saveBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    m_saveBtn->setEnabled(false);
    connect(m_saveBtn, &QskPushButton::clicked, this,
            &StickerGenPage::importToStore);

    // ── 预览区（右键/长按：拷贝、另存）──
    m_preview = new QskGraphicLabel(layout);
    m_preview->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Expanding);
    m_preview->setPreferredHeight(300);
    m_preview->setPanel(true);
    m_preview->setFillMode(QskGraphicLabel::PreserveAspectFit);

    // 右键菜单入口（桌面）
    m_preview->setAcceptedMouseButtons(m_preview->acceptedMouseButtons()
                                           | Qt::RightButton);
    auto* rightHandler = new RightClickHandler(m_preview);
    rightHandler->onRightPress = [this](const QPointF& scenePos) {
        if (!m_lastBytes.isEmpty()) {
            showPreviewMenu(scenePos);
        }
    };

    // 长按菜单入口（触屏）
    auto* tapHandler = new QQuickTapHandler(m_preview);
    tapHandler->setGesturePolicy(QQuickTapHandler::DragThreshold);
    connect(tapHandler, &QQuickTapHandler::longPressed, this,
            [this, tapHandler]() {
                if (m_lastBytes.isEmpty())
                    return;
                QPointF sp = tapHandler->point().scenePosition();
                if (sp.isNull()) {
                    sp = m_preview->mapToScene(QPointF(
                        m_preview->width() / 2, m_preview->height() / 2));
                }
                showPreviewMenu(sp);
            });

    // ── 底部行：状态 + 指标（预览下方并排）──
    auto* bottomRow = new QskLinearBox(Qt::Horizontal, layout);
    bottomRow->setSpacing(10);

    m_statusRow = new QskTextLabel(QString(), bottomRow);
    m_statusRow->setWrapMode(QskTextOptions::WordWrap);
    m_statusRow->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    m_metricBar = new QskTextLabel(
        tr("大小 -- · 用时 -- · 下次 --"), bottomRow);
    m_metricBar->setAlignment(Qt::AlignCenter);
    m_metricBar->setPreferredWidth(170);

    layout->addSpacer(16, 0);

    // ── 历史载入 ──
    m_history = QSettings().value(
        QStringLiteral("genPromptHistory")).toStringList();

    m_seedHistory.clear();
    const QVariantList seedVals = QSettings().value(
        QStringLiteral("genPromptHistorySeeds")).toList();
    for (const QVariant& v : seedVals)
        m_seedHistory.append(v.toULongLong());

    // 兼容旧数据：种子缺失按末尾补齐（新→旧 同序）
    while (m_seedHistory.size() < m_history.size())
        m_seedHistory.append(0);
    if (m_seedHistory.size() > m_history.size())
        m_seedHistory = m_seedHistory.mid(0, m_history.size());

    // ── 限频倒计时 ──
    m_countdownTimer = new QTimer(this);
    m_countdownTimer->setInterval(1000);
    connect(m_countdownTimer, &QTimer::timeout, this,
            &StickerGenPage::updateRateLimitBar);

    retranslateUi();
}

void StickerGenPage::onStop()
{
    if (m_reply) {
        auto* reply = m_reply;
        m_reply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    if (m_genBtn) {
        m_genBtn->setEnabled(true);
        m_genBtn->setText(tr("生成"));
    }
    if (m_statusRow)
        m_statusRow->setText(QString());
    m_countdownTimer->stop();
}

void StickerGenPage::retranslateUi()
{
    if (!m_title)
        return;
    m_title->setText(tr("生成表情"));
    m_engineLabel->setText(tr("生成器"));
    m_seedLabel->setText(tr("随机种子"));
    m_styleCheck->setText(tr("贴纸风格"));
    m_historyBtn->setText(tr("历史"));
    m_saveBtn->setText(tr("保存到表情包"));
    m_genBtn->setText(m_reply ? tr("生成中…") : tr("生成"));
    m_promptInput->setPlaceholderText(
        tr("例如：一只戴墨镜的沙雕熊猫贴纸"));
    updateRateLimitBar();
}

void StickerGenPage::startGenerate()
{
    if (m_reply)
        return;
    if (m_allowNextAt.isValid() && QDateTime::currentDateTime() < m_allowNextAt) {
        m_statusRow->setText(tr("请稍候（限频）"));
        updateRateLimitBar();
        return;
    }

    const QString prompt = m_promptInput->text().trimmed();
    if (prompt.isEmpty()) {
        m_statusRow->setText(tr("请输入提示词"));
        return;
    }

    // 提交历史
    pushHistory(prompt, static_cast<quint64>(m_seedSpin->value()));

    // 组装请求
    QString finalPrompt = prompt;
    if (m_styleCheck->isChecked()) {
        finalPrompt += QStringLiteral(", sticker, bold outline, flat design");
    }

    const QByteArray enc = QUrl::toPercentEncoding(finalPrompt);
    QUrl url(engineUrl() + QString::fromLatin1(enc));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("width"), QString::number(kImageSize));
    q.addQueryItem(QStringLiteral("height"), QString::number(kImageSize));
    q.addQueryItem(QStringLiteral("seed"),
                   QString::number(int(m_seedSpin->value())));
    q.addQueryItem(QStringLiteral("nologo"), QStringLiteral("true"));
    url.setQuery(q);

    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);

    m_lastBytes.clear();
    m_saveBtn->setEnabled(false);

    QNetworkRequest req(url);
    req.setTransferTimeout(90000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setAttribute(QNetworkRequest::Http2DirectAttribute, false);
    req.setRawHeader("User-Agent", "anystik/1.0");

    m_reply = m_nam->get(req);
    m_elapsed.start();
    m_statusRow->setText(tr("生成中…"));
    m_genBtn->setEnabled(false);
    m_genBtn->setText(tr("生成中…"));
    setMetricBar(tr("生成中… 已用时 %1 s").arg(0));

    connect(m_reply, &QNetworkReply::finished, this,
            &StickerGenPage::onReplyFinished);
}

void StickerGenPage::onReplyFinished()
{
    auto* reply = m_reply;
    if (!reply)
        return;
    m_reply = nullptr;
    reply->deleteLater();

    const int elapsedMs = int(m_elapsed.elapsed());
    const double elapsedSec = elapsedMs / 1000.0;
    m_lastElapsedSec = elapsedSec;

    m_allowNextAt = QDateTime::currentDateTime().addSecs(kRateLimitSecs);

    if (reply->error() != QNetworkReply::NoError) {
        m_statusRow->setText(tr("生成失败: %1").arg(reply->errorString()));
        m_lastBytes.clear();
        setMetricBar(tr("大小 -- · 用时 %1 s").arg(elapsedSec, 0, 'f', 1));
    } else {
        m_lastBytes = reply->readAll();
        m_resultImage = QImage::fromData(m_lastBytes);
        if (m_resultImage.isNull() || m_resultImage.size().isEmpty()) {
            m_lastBytes.clear();
            m_statusRow->setText(tr("生成失败: %1").arg(tr("图片解码失败")));
            setMetricBar(tr("大小 -- · 用时 %1 s").arg(elapsedSec, 0, 'f', 1));
        } else {
            m_lastImageWidth = m_resultImage.width();
            m_lastImageHeight = m_resultImage.height();
            m_preview->setGraphic(QskGraphic::fromImage(m_resultImage));
            m_saveBtn->setEnabled(true);
            m_statusRow->setText(tr("生成完成"));
            const double kb = m_lastBytes.size() / 1024.0;
            setMetricBar(tr("大小 %1 KB · 用时 %2 s · 下次 %3 s")
                             .arg(kb, 0, 'f', 1)
                             .arg(elapsedSec, 0, 'f', 1)
                             .arg(kRateLimitSecs));
            qInfo("[StickerGen] ok seed=%d size=%dx%d bytes=%d elapsed=%dms",
                  int(m_seedSpin->value()), m_lastImageWidth, m_lastImageHeight,
                  m_lastBytes.size(), elapsedMs);
        }
    }

    m_genBtn->setEnabled(false);
    m_genBtn->setText(tr("生成中…"));
    updateRateLimitBar();
}

void StickerGenPage::updateRateLimitBar()
{
    if (!m_genBtn || !m_metricBar)
        return;

    const QDateTime now = QDateTime::currentDateTime();

    // 生成中：禁用按钮 + 显示用时
    if (m_reply) {
        m_genBtn->setEnabled(false);
        m_genBtn->setText(tr("生成中…"));
        const int sec = int(m_elapsed.elapsed() / 1000);
        setMetricBar(tr("生成中… 已用时 %1 s").arg(sec));
        m_countdownTimer->start();
        return;
    }

    if (!m_allowNextAt.isValid()) {
        m_countdownTimer->stop();
        m_genBtn->setEnabled(true);
        m_genBtn->setText(tr("生成"));
        if (m_lastBytes.isEmpty()) {
            setMetricBar(tr("大小 -- · 用时 -- · 下次 --"));
        } else {
            const double kb = m_lastBytes.size() / 1024.0;
            setMetricBar(tr("大小 %1 KB · 已存").arg(kb, 0, 'f', 1));
        }
        return;
    }

    const qint64 remainMs = now.msecsTo(m_allowNextAt);
    if (remainMs > 0) {
        const int remainSec = int((remainMs + 999) / 1000);
        m_genBtn->setEnabled(false);
        m_genBtn->setText(tr("下次 %1s").arg(remainSec));
        if (!m_lastBytes.isEmpty()) {
            const double kb = m_lastBytes.size() / 1024.0;
            setMetricBar(tr("大小 %1 KB · 用时 %2 s · 下次 %3 s")
                             .arg(kb, 0, 'f', 1)
                             .arg(m_lastElapsedSec, 0, 'f', 1)
                             .arg(remainSec));
        } else {
            setMetricBar(tr("大小 -- · 用时 %1 s · 下次 %2 s")
                             .arg(m_lastElapsedSec, 0, 'f', 1).arg(remainSec));
        }
        m_countdownTimer->start();
    } else {
        m_allowNextAt = QDateTime();
        m_countdownTimer->stop();
        m_genBtn->setEnabled(true);
        m_genBtn->setText(tr("生成"));
        const double kb = m_lastBytes.size() / 1024.0;
        setMetricBar(tr("大小 %1 KB · 已存").arg(kb, 0, 'f', 1));
    }
}

void StickerGenPage::openHistoryMenu(const QPointF&)
{
    if (m_history.isEmpty()) {
        showToast(tr("暂无历史"));
        return;
    }

    auto* menu = new QskMenu(this);
    menu->setModal(true);
    menu->setPopupFlag(QskPopup::DeleteOnClose, true);

    QString fallbackW;
    for (const QString& entry : m_history) {
        QString label = entry;
        if (label.size() > 24)
            label = label.left(24) + QStringLiteral("…");
        menu->addOption(QskLabelData(label));
        if (fallbackW.size() < label.size())
            fallbackW = label;
    }

    // 定位：历史按钮场景位置；越界翻转钳制同主菜单
    QPointF origin;
    if (m_historyBtn && m_historyBtn->window()) {
        const QPointF center(
            m_historyBtn->x() + m_historyBtn->width() / 2,
            m_historyBtn->y() + m_historyBtn->height() / 2);
        origin = m_historyBtn->mapToScene(center);
    }
    {
        const QFontMetricsF fm(menu->effectiveFont(QskMenu::Text));
        const qreal pad = menu->paddingHint(QskMenu::Segment).left()
                        + menu->paddingHint(QskMenu::Segment).right();
        const qreal minW = qskHorizontalAdvance(fm, fallbackW) + pad + 24;
        menu->setStrutSizeHint(QskMenu::Panel, QSizeF(minW, 0));
    }
    if (origin.isNull()) {
        origin = m_historyBtn->mapToScene(QPointF(0, 0));
    }
    menu->setOrigin(origin);

    auto* overlay = new MenuOverlay(menu);
    connect(menu, &QObject::destroyed, overlay, &QObject::deleteLater);
    connect(menu, &QskPopup::closed, overlay, &QObject::deleteLater);

    connect(menu, &QskMenu::triggered, this,
            [this, menu](int index) {
                if (index >= 0 && index < m_history.size()) {
                    m_promptInput->setText(m_history.at(index));
                    m_seedSpin->setValue(m_seedHistory.at(index));
                    showToast(tr("已填入历史"));
                }
                menu->close();
            });

    menu->open();
}

void StickerGenPage::showPreviewMenu(const QPointF& scenePos)
{
    for (auto* old : findChildren<QskMenu*>())
        old->deleteLater();

    auto* menu = new QskMenu(this);
    menu->setModal(true);
    menu->setPopupFlag(QskPopup::DeleteOnClose, false);
    m_previewMenu = menu;

    const int idxCopy = menu->addOption(QskLabelData(tr("拷贝")));
    const int idxSave = menu->addOption(QskLabelData(tr("另存")));

    QPointF origin = scenePos;
    {
        const QFontMetricsF fm(menu->effectiveFont(QskMenu::Text));
        const qreal pad = menu->paddingHint(QskMenu::Segment).left()
                        + menu->paddingHint(QskMenu::Segment).right();
        const qreal minW = qskHorizontalAdvance(fm, QString::fromUtf8("另存"))
                         + pad + 10;
        menu->setStrutSizeHint(QskMenu::Panel, QSizeF(minW, 0));
    }
    // 越界翻转钳制
    {
        const qreal menuH = menu->sizeConstraint().height();
        if (menuH > 0 && origin.y() + menuH > height()) {
            origin.setY(qMax(0.0, origin.y() - menuH));
        }
    }
    menu->setOrigin(origin);

    auto* overlay = new MenuOverlay(menu);
    connect(menu, &QObject::destroyed, overlay, &QObject::deleteLater);
    connect(menu, &QskPopup::closed, overlay, &QObject::deleteLater);
    connect(menu, &QskPopup::closed, this, [this]() {
        m_previewMenu = nullptr;
    });

    connect(menu, &QskMenu::triggered, this,
            [this, menu, idxCopy, idxSave](int index) {
                if (index == idxCopy)
                    copyResult();
                else if (index == idxSave)
                    saveResultAs();
                menu->close();
            });

    menu->open();
}

void StickerGenPage::importToStore()
{
    if (m_lastBytes.isEmpty())
        return;
    QString err;
    if (StickerStore::instance()->importImageBytes(m_lastBytes, &err)) {
        showToast(tr("已保存"));
    } else {
        showToast(err.isEmpty() ? tr("保存失败") : err);
    }
}

void StickerGenPage::copyResult()
{
    if (m_resultImage.isNull()) {
        showToast(tr("拷贝失败"));
        return;
    }
    QGuiApplication::clipboard()->setImage(m_resultImage);
    showToast(tr("已复制"));
}

void StickerGenPage::saveResultAs()
{
    if (m_lastBytes.isEmpty()) {
        showToast(tr("保存失败"));
        return;
    }
    QString dir = StickerStore::instance()->storageRootPath(
        StickerStore::StorageRoot::Pictures);
    if (dir.isEmpty()) {
        showToast(tr("保存失败"));
        return;
    }
    QDir().mkpath(dir);

    // 按内容判定扩展名（JPEG 魔数 → .jpg，其余按 PNG）
    const bool jpeg = m_lastBytes.size() > 3
        && uchar(m_lastBytes[0]) == 0xFF && uchar(m_lastBytes[1]) == 0xD8;
    const QString ext = jpeg ? QStringLiteral(".jpg") : QStringLiteral(".png");
    const QString path = dir + QStringLiteral("/anystik_gen_")
        + QDateTime::currentDateTime().toString(
            QStringLiteral("yyyyMMdd_hhmmss_zzz")) + ext;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)
            || file.write(m_lastBytes) != m_lastBytes.size()) {
        showToast(tr("保存失败"));
        return;
    }
    showToast(tr("已保存: %1").arg(path));
}

void StickerGenPage::pushHistory(const QString& prompt, quint64 seed)
{
    const QString p = prompt.trimmed();
    if (p.isEmpty())
        return;
    QSet<QString> seen;
    QStringList fresh;
    QList<quint64> seedFresh;
    fresh.append(p);
    seedFresh.append(seed);
    seen.insert(p);
    for (int i = 0; i < m_history.size(); ++i) {
        const QString& e = m_history.at(i);
        if (seen.contains(e))
            continue;
        fresh.append(e);
        seedFresh.append(m_seedHistory.value(i, 0));
        seen.insert(e);
        if (fresh.size() >= kHistoryMax)
            break;
    }
    m_history = fresh;
    m_seedHistory = seedFresh;
    QSettings s;
    s.setValue(QStringLiteral("genPromptHistory"), m_history);
    QVariantList seedVals;
    for (quint64 v : m_seedHistory)
        seedVals.append(QVariant::fromValue<quint64>(v));
    s.setValue(QStringLiteral("genPromptHistorySeeds"), seedVals);
    s.sync();
}

void StickerGenPage::setMetricBar(const QString& text)
{
    if (m_metricBar)
        m_metricBar->setText(text);
}

void StickerGenPage::showToast(const QString& text)
{
    ToastPopup::show(this, text);
    qDebug() << "[StickerGenPage]" << text;
}

#include "moc_stickergenpage.cpp"