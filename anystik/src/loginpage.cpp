#include "loginpage.h"
#include "pagemanager.h"
#include <QskLinearBox.h>
#include <QskPushButton.h>
#include <QskTextLabel.h>

LoginPage::LoginPage(QQuickItem* parent)
    : Page(parent)
{
}

void LoginPage::onCreate(const QVariantMap&, const QVariantMap&)
{
    setAutoLayoutChildren(true);
    auto* layout = new QskLinearBox(Qt::Vertical, this);
    layout->setPanel(true);

    layout->addStretch(1);

    auto* title = new QskTextLabel("anystik", layout);
    title->setAlignment(Qt::AlignCenter);
    title->setPreferredHeight(48);

    layout->addSpacer(40, 0);

    struct { const char* label; const char* url; } entries[] = {
        {"localhost:8181",          "http://localhost:8181"},
        {"192.168.43.157:4004",    "http://192.168.43.157:4004"},
        {"192.168.49.136:4004",    "http://192.168.49.136:4004"},
    };

    for (size_t i = 0; i < 3; i++) {
        if (i > 0)
            layout->addSpacer(12, 0);

        auto* btn = new QskPushButton(entries[i].label, layout);
        btn->setPreferredWidth(340);
        btn->setPreferredHeight(52);
        const QString url(entries[i].url);
        connect(btn, &QskAbstractButton::clicked, this, [this, url]() {
            Q_UNUSED(url)
            pageManager()->replace("stickerhome");
        });
    }

    layout->addSpacer(20, 0);
    layout->addStretch(1);
}
