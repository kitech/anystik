#include "migrationdialog.h"
#include "stickerstore.h"

#include <QskBox.h>
#include <QskBoxShapeMetrics.h>
#include <QskLinearBox.h>
#include <QskTextLabel.h>
#include <QskTextOptions.h>
#include <QskPushButton.h>
#include <QskProgressBar.h>
#include <QQuickItem>
#include <QQuickWindow>
#include <QDebug>
#include <QFileInfo>
#include <QtGlobal>

namespace {
QRectF dialogParentRect(QQuickItem* parent)
{
    if (!parent)
        return {};
    if (auto* w = parent->window())
        return QRectF(QPointF(), w->size());
    return QRectF(-parent->x(), -parent->y(),
                  parent->width(), parent->height());
}
}

MigrationDialog* MigrationDialog::show(QQuickItem* parent,
                                       const QString& fromRoot,
                                       const QString& toRoot)
{
    return new MigrationDialog(fromRoot, toRoot, parent);
}

MigrationDialog::MigrationDialog(const QString& fromRoot, const QString& toRoot,
                                 QQuickItem* parent)
    : QskPopup(parent)
{
    setModal(true);
    setOverlay(true);
    setPopupFlag(QskPopup::DeleteOnClose, true);   // finished 后 deleteLater
    setPolishOnResize(true);
    setPolishOnParentResize(true);

    m_panel = new QskBox(this);
    m_panel->setBoxShapeHint(QskBox::Panel,
        QskBoxShapeMetrics(14, Qt::AbsoluteSize));

    m_layout = new QskLinearBox(Qt::Vertical, m_panel);
    m_layout->setMargins(18);
    m_layout->setSpacing(12);
    m_layout->setSizePolicy(
        QskSizePolicy::MinimumExpanding, QskSizePolicy::Constrained);

    m_titleLabel = new QskTextLabel(QString::fromUtf8("迁移存储位置"), m_layout);
    m_titleLabel->setWrapMode(QskTextOptions::WordWrap);
    m_titleLabel->setAlignment(Qt::AlignCenter);

    m_fromLabel = new QskTextLabel(
        QString::fromUtf8("从：") + fromRoot, m_layout);
    m_fromLabel->setWrapMode(QskTextOptions::WordWrap);

    m_toLabel = new QskTextLabel(
        QString::fromUtf8("到：") + toRoot, m_layout);
    m_toLabel->setWrapMode(QskTextOptions::WordWrap);

    // 进度按「文件数」计算（done/total）；字节数另行列示、不参与进度
    m_progress = new QskProgressBar(m_layout);
    m_progress->setMaximum(1.0);
    m_progress->setValue(0.0);

    m_bytesLabel = new QskTextLabel(QString::fromUtf8("已拷贝：0 字节"), m_layout);
    m_bytesLabel->setWrapMode(QskTextOptions::WordWrap);
    m_bytesLabel->setAlignment(Qt::AlignCenter);

    auto* buttons = new QskLinearBox(Qt::Horizontal, m_layout);
    buttons->setSpacing(14);

    m_pauseButton = new QskPushButton(QString::fromUtf8("暂停"), buttons);
    m_cancelButton = new QskPushButton(QString::fromUtf8("取消"), buttons);
    m_pauseButton->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    m_cancelButton->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));

    // 暂停/取消：本次仅占位为空实现，点击只打日志，不做实际暂停/取消
    connect(m_pauseButton, &QskPushButton::clicked, this, [this]() {
        qDebug() << "[StickerStore] 迁移「暂停」占位（暂未实现）";
    });
    connect(m_cancelButton, &QskPushButton::clicked, this, [this]() {
        qDebug() << "[StickerStore] 迁移「取消」占位（暂未实现）";
    });

    auto* store = StickerStore::instance();
    connect(store, &StickerStore::migrationProgress,
            this, &MigrationDialog::onProgress);
    connect(store, &StickerStore::migrationFinished,
            this, &MigrationDialog::onFinished);

    open();
}

void MigrationDialog::updateLayout()
{
    const auto parentRect = dialogParentRect(parentItem());
    if (parentRect.isEmpty())
        return;

    const auto panelHint = m_layout->effectiveSizeHint(
        Qt::PreferredSize, QSizeF());

    const qreal maxW = qMin(0.92 * parentRect.width(), 460.0);
    const qreal maxH = 0.9 * parentRect.height();
    const qreal panelW = qBound(320.0, panelHint.width() + 36, maxW);
    const qreal panelH = qMin(panelHint.height() + 36.0, maxH);

    QRectF popupRect(0, 0, panelW, panelH);
    popupRect.moveCenter(parentRect.center());
    setGeometry(popupRect);

    m_panel->setGeometry(popupRect.translated(-popupRect.topLeft()));
    m_layout->setGeometry(layoutRect());
}

void MigrationDialog::onProgress(int done, int total, qint64 copiedBytes,
                                 const QString& /*current*/)
{
    if (total > 0)
        m_progress->setValue(qBound<qreal>(0.0, qreal(done) / qreal(total), 1.0));
    else
        m_progress->setValue(0.0);

    const double mb = double(copiedBytes) / (1024.0 * 1024.0);
    m_bytesLabel->setText(
        QString::fromUtf8("已拷贝：%1 MB (%2/%3) 个文件")
            .arg(mb, 0, 'f', 1)
            .arg(done).arg(total));
}

void MigrationDialog::onFinished(bool ok, const QString& detail)
{
    if (!ok && !detail.isEmpty())
        qDebug() << "[StickerStore] 迁移失败:" << detail;
    // 完成即关闭（DeleteOnClose 负责销毁）
    close();
}
