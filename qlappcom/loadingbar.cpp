#include "loadingbar.h"

LoadingBar::LoadingBar(QWidget* parent)
    : QWidget(parent), m_dotCount(0) {
    m_label = new QLabel(this);
    QBoxLayout* layout = qNewBoxLayout(this, QBoxLayout::LeftToRight, 0, 0);
    layout->addWidget(m_label);

    m_timer = new QTimer(this);
    QObject::connect(m_timer, SIGNAL(timeout()), this, SLOT(onTimerTick()));
    m_timer->start(500);

    hide();
}

void LoadingBar::showLoading(int id, const QString& msg) {
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id == id) {
            m_items[i].msg = msg;
            updateDisplay();
            return;
        }
    }
    LoadingItem item;
    item.id = id;
    item.msg = msg;
    m_items.push_back(item);
    updateDisplay();
}

void LoadingBar::hideLoading(int id) {
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id == id) {
            m_items.erase(m_items.begin() + i);
            break;
        }
    }
    updateDisplay();
}

void LoadingBar::clearLoading() {
    m_items.clear();
    updateDisplay();
}

void LoadingBar::updateDisplay() {
    if (m_items.empty()) {
        m_label->clear();
        hide();
        return;
    }
    const LoadingItem& item = m_items.back();
    QString dots;
    for (int i = 0; i < m_dotCount; ++i)
        dots += ".";
    m_label->setText(QString::fromUtf8("\xe2\x9f\xb3 ") + item.msg + dots);
    show();
}

void LoadingBar::onTimerTick() {
    m_dotCount = (m_dotCount + 1) % 4;
    if (!m_items.empty()) {
        const LoadingItem& item = m_items.back();
        QString dots;
        for (int i = 0; i < m_dotCount; ++i)
            dots += ".";
        m_label->setText(QString::fromUtf8("\xe2\x9f\xb3 ") + item.msg + dots);
    }
}
