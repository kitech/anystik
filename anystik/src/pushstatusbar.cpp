#include "pushstatusbar.h"
#include "pushhandler.h"
#include <QskTextLabel.h>
#include <QDebug>

PushStatusBar::PushStatusBar(QQuickItem* parent)
    : QskLinearBox(Qt::Horizontal, parent)
{
    Lang::instance().registerRetranslatable(this);

    setPreferredHeight(28);
    setPanel(true);
    setSpacing(4);

    m_statusLabel = new QskTextLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setSizePolicy(
        QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    setVisible(false);

#ifdef Q_OS_ANDROID
    if (auto* ph = PushHandler::instance()) {
        connect(ph, &PushHandler::statusChanged,
                this, &PushStatusBar::updateStatus);
    }
    updateStatus();
#endif
}

PushStatusBar::~PushStatusBar()
{
    Lang::instance().unregister(this);
}

void PushStatusBar::retranslateUi()
{
    updateStatus();
}

void PushStatusBar::updateStatus()
{
#ifndef Q_OS_ANDROID
    setVisible(false);
    return;
#else
    QString backend = PushHandler::instance() ? PushHandler::instance()->currentDistributorDisplayName() : QString();
    if (backend.isEmpty()) {
        backend = PushHandler::isNtfyInstalled()
            ? QStringLiteral("ntfy") : QStringLiteral("push");
    }

    QString status;
    if (PushHandler::isConnected()) {
        status = tr("Push: %1 (已连接)").arg(backend);
    } else if (PushHandler::isRegistering()) {
        status = tr("Push: %1 (等待中)").arg(backend);
    } else {
        status = tr("Push: 未连接");
    }

    m_statusLabel->setText(status);
    setVisible(true);
#endif
}
