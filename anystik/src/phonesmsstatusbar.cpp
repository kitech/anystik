#include "phonesmsstatusbar.h"
#include "phonemonitor.h"
#include "toastpopup.h"

#include <QskBox.h>
#include <QskBoxShapeMetrics.h>
#include <QskGradient.h>
#include <QskLinearBox.h>
#include <QskPopup.h>
#include <QskPushButton.h>
#include <QskSimpleListBox.h>
#include <QskSizePolicy.h>
#include <QskTabBar.h>
#include <QskTextLabel.h>

#include <QDateTime>
#include <QQuickWindow>
#include <QTimer>
#include <QDebug>

namespace
{
    // 面板不透明度：回读当前皮肤面板填充色，仅改 alpha（保留主题配色）
    void applyPanelOpacity(QskBox* panel, qreal opacity)
    {
        QskGradient g = panel->fillGradient();
        if (!g.isValid())
            return;
        g.setAlpha(qRound(qBound(0.0, opacity, 1.0) * 255.0));
        panel->setFillGradient(g);
    }

    // 浮动框父级取窗口 content item：父坐标为窗口坐标，居中/定位不依赖宿主条位置
    QQuickItem* popupParentItem(QQuickItem* item)
    {
        if (auto* w = item ? item->window() : nullptr)
            return w->contentItem();
        return item;
    }
}

/* ── 电话/短信浮动框（私有实现：翻译串由宿主传入，保持上下文 PhoneSmsStatusBar）── */
class PhoneSmsListPopup : public QskPopup
{
public:
    PhoneSmsListPopup(const QString& tabCall, const QString& tabSms,
                      const QString& closeText, QQuickItem* parent)
        : QskPopup(parent)
    {
        setModal(false);
        setOverlay(false);
        setPopupFlag(QskPopup::DeleteOnClose, false);
        setPolishOnResize(true);
        setPolishOnParentResize(true);

        m_panel = new QskBox(this);
        m_panel->setBoxShapeHint(QskBox::Panel,
            QskBoxShapeMetrics(14, Qt::AbsoluteSize));
        applyPanelOpacity(m_panel, 0.9);   // 0.9 透明度浮动框

        m_layout = new QskLinearBox(Qt::Vertical, m_panel);
        m_layout->setMargins(16);
        m_layout->setSpacing(10);

        m_tabBar = new QskTabBar(Qt::TopEdge, m_layout);
        m_tabBar->addTab(tabCall);
        m_tabBar->addTab(tabSms);
        m_tabBar->setAutoFitTabs(true);

        m_listBox = new QskSimpleListBox(m_layout);
        m_listBox->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Expanding);

        m_closeBtn = new QskPushButton(closeText, m_layout);
        m_closeBtn->setBoxShapeHint(QskPushButton::Panel,
            QskBoxShapeMetrics(8, Qt::AbsoluteSize));
        connect(m_closeBtn, &QskAbstractButton::clicked, this, &QskPopup::close);
    }

    QskTabBar* tabBar() const { return m_tabBar; }
    QskSimpleListBox* listBox() const { return m_listBox; }

protected:
    void updateLayout() override
    {
        updateGeometry();
        m_layout->setGeometry(layoutRect());
    }

private:
    void updateGeometry()
    {
        QRectF parentRect(0, 0, 400, 520);
        if (auto* w = window())
            parentRect = QRectF(QPointF(), w->size());

        const qreal w = 340.0;
        const qreal h = qMin(parentRect.height() * 0.7, 480.0);
        QRectF r(0, 0, w, h);
        r.moveCenter(parentRect.center());
        setGeometry(r);
        m_panel->setGeometry(r.translated(-r.topLeft()));
    }

    QskBox* m_panel = nullptr;
    QskLinearBox* m_layout = nullptr;
    QskTabBar* m_tabBar = nullptr;
    QskSimpleListBox* m_listBox = nullptr;
    QskPushButton* m_closeBtn = nullptr;
};

PhoneSmsStatusBar::PhoneSmsStatusBar(QQuickItem* parent)
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

    m_listBtn = new QskPushButton(tr("列表"), this);
    m_listBtn->setPreferredWidth(64);
    m_listBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    connect(m_listBtn, &QskAbstractButton::clicked,
        this, &PhoneSmsStatusBar::openListsPopup);

    setVisible(false);

#ifdef Q_OS_ANDROID
    if (PhoneMonitor::instance())
        attachPhoneMonitor();
    else
        // 构造可能先于 PhoneMonitor::start()（main 尾部）执行，实例创建后再补连
        QTimer::singleShot(0, this, [this]() { attachPhoneMonitor(); });
#endif
}

// ── 挂接 PhoneMonitor：实例创建晚于本控件时也要能连上，避免计数永远停在 0 ──
void PhoneSmsStatusBar::attachPhoneMonitor()
{
#ifdef Q_OS_ANDROID
    auto* ph = PhoneMonitor::instance();
    if (!ph || m_attached)
        return;
    m_attached = true;   // 只连一次（构造函数 + 延迟补连只命中一次）
    connect(ph, &PhoneMonitor::countersChanged, this,
        [this]() { updateStatus(); });
    connect(ph, &PhoneMonitor::callRecorded, this, [this]() {
        refreshList();
        if (auto* p = PhoneMonitor::instance()) {
            const auto& rec = p->callRecords().constLast();
            ToastPopup::show(this, tr("电话事件：%1 (%2)")
                .arg(stateLabel(rec.state), rec.number));
        }
    });
    connect(ph, &PhoneMonitor::smsRecorded, this, [this]() {
        refreshList();
        if (auto* p = PhoneMonitor::instance()) {
            const auto& rec = p->smsRecords().constLast();
            const QString from = rec.sender.isEmpty()
                ? tr("未知") : rec.sender;
            ToastPopup::show(this, tr("新短信：%1").arg(from));
        }
    });
    updateStatus();   // 连上后立即按当前计数刷新一次
#endif
}

PhoneSmsStatusBar::~PhoneSmsStatusBar()
{
    Lang::instance().unregister(this);
}

void PhoneSmsStatusBar::retranslateUi()
{
    updateStatus();
    refreshList();
}

void PhoneSmsStatusBar::updateStatus()
{
#ifndef Q_OS_ANDROID
    setVisible(false);
    return;
#else
    auto* ph = PhoneMonitor::instance();
    const int calls = ph ? ph->callCount() : 0;
    const int sms = ph ? ph->smsCount() : 0;
    m_statusLabel->setText(tr("电话 %1 · 短信 %2").arg(calls).arg(sms));
    setVisible(true);
#endif
}

QString PhoneSmsStatusBar::stateLabel(const QString& state) const
{
    if (state == "RINGING") return tr("来电");
    if (state == "OFFHOOK") return tr("通话");
    if (state == "IDLE")    return tr("挂断");
    return state;
}

void PhoneSmsStatusBar::openListsPopup()
{
    if (m_popup) {
        m_popup->open();
        return;
    }

    if (PhoneMonitor::instance())
        PhoneMonitor::requestPermissions(true);   // 用到时再请求一次权限

    auto* popup = new PhoneSmsListPopup(tr("电话"), tr("短信"), tr("关闭"),
        popupParentItem(this));
    connect(popup->tabBar(), &QskTabBar::currentIndexChanged,
        this, [this](int) { refreshList(); });
    connect(popup, &QskPopup::closed, popup, &QObject::deleteLater);
    connect(popup, &QObject::destroyed, this, [this]() { m_popup = nullptr; });

    m_popup = popup;
    refreshList();

    QTimer::singleShot(0, popup, [popup]() {
        popup->open();
        popup->update();   // 触发 polish，走 updateGeometry 定位
    });
}

void PhoneSmsStatusBar::refreshList()
{
    if (!m_popup)
        return;

    auto* ph = PhoneMonitor::instance();
    QStringList entries;
    if (ph) {
        if (m_popup->tabBar()->currentIndex() == 1) {
            const auto& sms = ph->smsRecords();
            for (auto it = sms.crbegin(); it != sms.crend(); ++it) {
                const QString t = QDateTime::fromMSecsSinceEpoch(
                    it->timestamp).toString("HH:mm");
                const QString from = it->sender.isEmpty()
                    ? tr("未知") : it->sender;
                entries.append(tr("%1 %2：%3").arg(t, from, it->body.left(60)));
            }
        } else {
            const auto& calls = ph->callRecords();
            for (auto it = calls.crbegin(); it != calls.crend(); ++it) {
                const QString t = QDateTime::fromMSecsSinceEpoch(
                    it->timestamp).toString("HH:mm");
                const QString num = it->number.isEmpty()
                    ? tr("未知") : it->number;
                entries.append(tr("%1 [%2] %3")
                    .arg(t, stateLabel(it->state), num));
            }
        }
        if (entries.isEmpty())
            entries.append(tr("暂无记录"));
    }

    m_popup->listBox()->setEntries(entries);
}

#include "moc_phonesmsstatusbar.cpp"