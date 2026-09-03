#include "settingspage.h"
#include "pagemanager.h"
#include "phonemonitor.h"
#include "pushhandler.h"
#include "androidutils.h"
#include "stickerstore.h"
#include "migrationdialog.h"
#include <QskLinearBox.h>
#include <QskTextLabel.h>
#include <QskPushButton.h>
#include <QskComboBox.h>
#include <QskSwitchButton.h>
#include <QskLabelData.h>
#include <QskSeparator.h>
#include <QskStackBox.h>
#include <QskStackBoxAnimator.h>
#include <QskSetup.h>
#include <QskItem.h>
#include <QskSkinManager.h>
#include <QskSkin.h>
#include <QskTextField.h>
#include <QskBoxShapeMetrics.h>
#include <QSettings>
#include <QDebug>
#include <QTimer>
#include <QDir>
#if defined(Q_OS_ANDROID)
#include <QJniObject>
#endif

std::shared_ptr<FontSizes> SettingsPage::sharedFontSizes;
std::function<void()>      SettingsPage::applyAndroidFonts;

QPointer<SettingsPage> SettingsPage::s_instance;

SettingsPage::SettingsPage(QQuickItem* parent)
    : Page(parent)
    , m_debugBgSwitch(nullptr)
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
    auto* title = new QskTextLabel("Settings", topBar);
    title->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    title->setAlignment(Qt::AlignCenter);

    connect(backBtn, &QskAbstractButton::clicked, this, [this]() {
        finish();
    });

    layout->addSpacer(24, 0);

    // ── Row 1: Page Transition ──
    auto* row1 = new QskLinearBox(Qt::Horizontal, layout);
    row1->setSpacing(12);
    auto* transitionLabel = new QskTextLabel("Page Transition", row1);
    transitionLabel->setPreferredWidth(160);
    m_transitionCombo = new QskComboBox(row1);
    m_transitionCombo->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    m_transitionCombo->addOption(QskLabelData("Slide"));
    m_transitionCombo->addOption(QskLabelData("2D"));
    m_transitionCombo->addOption(QskLabelData("3D"));
    m_transitionCombo->addOption(QskLabelData("Perspective"));
    m_transitionCombo->setCurrentIndex(3);

    new QskSeparator(Qt::Horizontal, layout);

    // ── Row 2: Theme ──
    auto* row2 = new QskLinearBox(Qt::Horizontal, layout);
    row2->setSpacing(12);
    auto* themeLabel = new QskTextLabel("Theme", row2);
    themeLabel->setPreferredWidth(160);
    m_skinCombo = new QskComboBox(row2);
    m_skinCombo->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    m_skinCombo->addOption(QskLabelData("Fusion"));
    m_skinCombo->addOption(QskLabelData("Fluent2"));
    m_skinCombo->addOption(QskLabelData("Material3"));
    m_skinCombo->setCurrentIndex(0);

    new QskSeparator(Qt::Horizontal, layout);

    // ── Row 3: Color Scheme ──
    auto* row3 = new QskLinearBox(Qt::Horizontal, layout);
    row3->setSpacing(12);
    auto* schemeLabel = new QskTextLabel("Color Scheme", row3);
    schemeLabel->setPreferredWidth(160);
    m_darkSwitch = new QskSwitchButton(row3);
    auto* schemeVal = new QskTextLabel("Light", row3);
    schemeVal->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    connect(m_darkSwitch, &QskAbstractButton::toggled,
        [schemeVal](bool checked) {
            schemeVal->setText(checked ? "Dark" : "Light");
        });

    new QskSeparator(Qt::Horizontal, layout);

    // ── Row 4: Font Size ──
    auto* row4 = new QskLinearBox(Qt::Horizontal, layout);
    row4->setPreferredHeight(48);
    row4->setSpacing(12);
    auto* fontLabel = new QskTextLabel("Font Size", row4);
    fontLabel->setPreferredWidth(160);
    m_fontScaleCombo = new QskComboBox(row4);
    m_fontScaleCombo->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    m_fontScaleCombo->addOption(QskLabelData("Small"));
    m_fontScaleCombo->addOption(QskLabelData("Medium"));
    m_fontScaleCombo->addOption(QskLabelData("Large"));
    m_fontScaleCombo->addOption(QskLabelData("Extra Large"));
    m_fontScaleCombo->setCurrentIndex(1);

    new QskSeparator(Qt::Horizontal, layout);

    // ── Row 5: Debug Background ──
    auto* row5 = new QskLinearBox(Qt::Horizontal, layout);
    row5->setSpacing(12);
    auto* debugLabel = new QskTextLabel("Debug Background", row5);
    debugLabel->setPreferredWidth(160);
    m_debugBgSwitch = new QskSwitchButton(row5);

    new QskSeparator(Qt::Horizontal, layout);

    // ── Row 6: Phone Answer ──
    auto* row6 = new QskLinearBox(Qt::Horizontal, layout);
    row6->setSpacing(12);
    auto* phoneLabel = new QskTextLabel("Phone Answer", row6);
    phoneLabel->setPreferredWidth(160);
    m_phoneAnswerCombo = new QskComboBox(row6);
    m_phoneAnswerCombo->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    m_phoneAnswerCombo->addOption(QskLabelData("Disabled"));
    m_phoneAnswerCombo->addOption(QskLabelData("Manual"));
    m_phoneAnswerCombo->addOption(QskLabelData("Auto"));
    m_phoneAnswerCombo->setCurrentIndex(0);

    new QskSeparator(Qt::Horizontal, layout);

    // ── Row 7: Push Notification ──
    auto* row7 = new QskLinearBox(Qt::Horizontal, layout);
    row7->setSpacing(12);
    auto* pushNotifyLabel = new QskTextLabel("Push Notification", row7);
    pushNotifyLabel->setPreferredWidth(160);
    m_pushNotifySwitch = new QskSwitchButton(row7);

    new QskSeparator(Qt::Horizontal, layout);

    // ── Row 8: Push Backend (merged provider + distributor) ──
    auto* row8 = new QskLinearBox(Qt::Horizontal, layout);
    row8->setSpacing(12);
    auto* backendLabel = new QskTextLabel("Push Backend", row8);
    backendLabel->setPreferredWidth(160);
    m_backendCombo = new QskComboBox(row8);
    m_backendCombo->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    m_backendCombo->addOption(QskLabelData("Auto (system default)"));
    auto knownDists = PushHandler::knownDistributors();
    for (const auto& dist : knownDists) {
        m_knownDistPackages.append(dist.first);
        m_backendCombo->addOption(QskLabelData(dist.second + " (" + dist.first + ")"));
    }
    m_backendCombo->addOption(QskLabelData("Gotify direct (coming soon)"));
    m_backendCombo->setCurrentIndex(0);

    new QskSeparator(Qt::Horizontal, layout);

    // ── Row 9: Gotify Server (visible when Gotify direct selected) ──
    m_gotifyRow = new QskLinearBox(Qt::Horizontal, layout);
    m_gotifyRow->setSpacing(12);
    auto* gotifyUrlLabel = new QskTextLabel("Gotify URL", m_gotifyRow);
    gotifyUrlLabel->setPreferredWidth(160);
    m_gotifyUrlEdit = new QskTextField(m_gotifyRow);
    m_gotifyUrlEdit->setPlaceholderText("https://push.example.com");
    m_gotifyUrlEdit->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    auto* gsep1 = new QskSeparator(Qt::Horizontal, layout);
    m_gotifySep1 = gsep1;

    // ── Row 10: Gotify Token ──
    m_gotifyRow2 = new QskLinearBox(Qt::Horizontal, layout);
    m_gotifyRow2->setSpacing(12);
    auto* gotifyTokenLabel = new QskTextLabel("Gotify Token", m_gotifyRow2);
    gotifyTokenLabel->setPreferredWidth(160);
    m_gotifyTokenEdit = new QskTextField(m_gotifyRow2);
    m_gotifyTokenEdit->setPlaceholderText("client token");
    m_gotifyTokenEdit->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    auto* gsep2 = new QskSeparator(Qt::Horizontal, layout);
    m_gotifySep2 = gsep2;

    updateGotifyVisibility(0);

    new QskSeparator(Qt::Horizontal, layout);

    // ── Row 11: 贴纸存储位置（当前 base 目录，只读） ──
    auto* row11 = new QskLinearBox(Qt::Horizontal, layout);
    row11->setSpacing(12);
    auto* curRootLabel = new QskTextLabel(QString::fromUtf8("当前存储"), row11);
    curRootLabel->setPreferredWidth(160);
    m_currentRootValue = new QskTextLabel(QString(), row11);
    m_currentRootValue->setWrapMode(QskTextOptions::WordWrap);
    m_currentRootValue->setSizePolicy(
        QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    new QskSeparator(Qt::Horizontal, layout);

    // ── Row 12: 切换目标目录 + 迁移按钮 ──
    auto* row12 = new QskLinearBox(Qt::Horizontal, layout);
    row12->setSpacing(12);
    auto* targetRootLabel = new QskTextLabel(QString::fromUtf8("迁移到相册"), row12);
    targetRootLabel->setPreferredWidth(160);
    m_targetRootValue = new QskTextLabel(QString(), row12);
    m_targetRootValue->setWrapMode(QskTextOptions::WordWrap);
    m_targetRootValue->setSizePolicy(
        QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    auto* migrateBtn = new QskPushButton(QString::fromUtf8("迁移"), row12);
    migrateBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    m_migrateButton = migrateBtn;
    connect(migrateBtn, &QskPushButton::clicked, this,
            &SettingsPage::onMigrateStorageClicked);

    layout->addStretch(1);
}

SettingsPage::~SettingsPage()
{
    if (s_instance == this)
        s_instance = nullptr;
}

void SettingsPage::changeFontScale(int delta)
{
    QSettings s;
    int idx = qBound(0, s.value("fontScale", 1).toInt() + delta, 3);
    s.setValue("fontScale", idx);

    static const int sizes[][4] = {
        {16, 22, 14, 12}, {21, 29, 19, 16},
        {28, 39, 25, 21}, {35, 48, 32, 27},
    };
    if (sharedFontSizes) {
        sharedFontSizes->body    = sizes[idx][0];
        sharedFontSizes->title   = sizes[idx][1];
        sharedFontSizes->caption = sizes[idx][2];
        sharedFontSizes->global  = sizes[idx][3];
    }
    if (applyAndroidFonts) applyAndroidFonts();

    // Sync SettingsPage combo if open
    if (s_instance && s_instance->m_fontScaleCombo) {
        s_instance->m_fontScaleCombo->blockSignals(true);
        s_instance->m_fontScaleCombo->setCurrentIndex(idx);
        s_instance->m_fontScaleCombo->blockSignals(false);
    }
}

void SettingsPage::updateGotifyVisibility(int backendIndex)
{
    bool isGotify = (backendIndex == m_knownDistPackages.size() + 1);
    if (m_gotifyRow) m_gotifyRow->setVisible(isGotify);
    if (m_gotifySep1) m_gotifySep1->setVisible(isGotify);
    if (m_gotifyRow2) m_gotifyRow2->setVisible(isGotify);
    if (m_gotifySep2) m_gotifySep2->setVisible(isGotify);
}

void SettingsPage::rebuildBackendLabels(const QStringList& installed)
{
    m_backendCombo->blockSignals(true);
    int savedIdx = m_backendCombo->currentIndex();

    QString active;
    if (auto* ph = PushHandler::instance())
        active = ph->currentDistributor();

    QVector<QskLabelData> opts;
    opts.append(QskLabelData("Auto (system default)"));
    for (int i = 0; i < m_knownDistPackages.size(); ++i) {
        QString pkg = m_knownDistPackages[i];
        QString name = PushHandler::upDistributorDisplayName(pkg);
        bool found = installed.contains(pkg);
        QString label = name + " (" + pkg + ")";
        if (found) {
            label = (pkg == active ? "⚫ " : "✓ ") + label;
        }
        opts.append(QskLabelData(label));
    }
    opts.append(QskLabelData("Gotify direct (coming soon)"));
    m_backendCombo->setOptions(opts);

    if (savedIdx >= 0 && savedIdx < opts.size()) {
        m_backendCombo->setCurrentIndex(savedIdx);
    }
    m_backendCombo->blockSignals(false);
}

void SettingsPage::refreshStorageRows()
{
    auto* store = StickerStore::instance();
    if (!store)
        return;

    const QString current = store->currentStickerBaseDir();
    const QString target = picturesTargetPath();

    if (m_currentRootValue)
        m_currentRootValue->setText(current);
    if (m_targetRootValue)
        m_targetRootValue->setText(target);

    if (m_migrateButton) {
        const bool already = !target.isEmpty()
            && QDir::cleanPath(current) == QDir::cleanPath(target);
        m_migrateButton->setEnabled(!already);
        m_migrateButton->setText(already
            ? QString::fromUtf8("已在相册")
            : QString::fromUtf8("迁移"));
    }
}

QString SettingsPage::picturesTargetPath() const
{
    auto* store = StickerStore::instance();
    if (!store)
        return QString();
    return store->storageRootPath(
        StickerStore::StorageRoot::Pictures);
}

void SettingsPage::onMigrateStorageClicked()
{
    auto* store = StickerStore::instance();
    if (!store)
        return;

    const QString fromRoot = store->currentStickerBaseDir();
    const QString toRoot = picturesTargetPath();

    if (toRoot.isEmpty()) {
        showAndroidToast(QString::fromUtf8("无法确定相册目录"));
        return;
    }
    if (QDir::cleanPath(fromRoot) == QDir::cleanPath(toRoot)) {
        showAndroidToast(QString::fromUtf8("已在相册目录"));
        refreshStorageRows();
        return;
    }

#if defined(Q_OS_ANDROID)
    if (!androidStorageAccessGranted()) {
        requestAndroidStorageAccess();
        showAndroidToast(QString::fromUtf8(
            "请在权限页授予存储访问权限，返回后再点迁移"));
        return;
    }
#endif

    // 打开进度对话框（完成/失败自动关闭）
    MigrationDialog::show(this, fromRoot, toRoot);

    QString err;
    const bool ok = store->switchStorageRoot(
        StickerStore::StorageRoot::Pictures, &err);
    if (!ok) {
        showAndroidToast(err.isEmpty() ? QString::fromUtf8("迁移启动失败")
                                       : err);
    }
    // sync 失败（同根/建目录失败等）时马上刷新按钮状态
    QTimer::singleShot(0, this, [this]() { refreshStorageRows(); });
}

void SettingsPage::onCreate(const QVariantMap&, const QVariantMap&)
{
    s_instance = this;

    // Restore persisted values (before connecting handlers)
    QSettings settings;
    m_transitionCombo->setCurrentIndex(settings.value("transition", 3).toInt());
    m_skinCombo->setCurrentIndex(settings.value("skin", 0).toInt());
    m_darkSwitch->setChecked(settings.value("darkMode", false).toBool());
    m_fontScaleCombo->setCurrentIndex(settings.value("fontScale", 1).toInt());
    m_debugBgSwitch->setChecked(settings.value("debugBackground", false).toBool());
    m_phoneAnswerCombo->setCurrentIndex(settings.value("phoneAnswer", 0).toInt());
    m_pushNotifySwitch->setChecked(settings.value("pushNotification", true).toBool());

    refreshStorageRows();

    // ── Restore Push Backend settings ──
    m_gotifyUrlEdit->setText(settings.value("gotifyUrl").toString());
    m_gotifyTokenEdit->setText(settings.value("gotifyToken").toString());

    {
        QString savedBackend = settings.value("pushBackend").toString();
        int savedIdx = 0; // 0 = Auto
        if (!savedBackend.isEmpty()) {
            if (savedBackend == "gotify") {
                savedIdx = m_knownDistPackages.size() + 1; // last item = Gotify direct
            } else {
                for (int i = 0; i < m_knownDistPackages.size(); ++i) {
                    if (m_knownDistPackages[i] == savedBackend) {
                        savedIdx = i + 1; // +1 for Auto
                        break;
                    }
                }
            }
        }
        m_backendCombo->setCurrentIndex(savedIdx);
    }

    updateGotifyVisibility(m_backendCombo->currentIndex());

    if (m_signalsConnected) return;
    m_signalsConnected = true;

    // ── Connect signal handlers (fire on user interaction, not on restore) ──
    connect(m_transitionCombo, &QskComboBox::currentIndexChanged,
        this, [this](int index) {
            // 跳过同一类型的重复分配（例如从 Perspective 切到 Perspective）
            if (index == m_currentAnimatorIdx) return;
            m_currentAnimatorIdx = index;

            QSettings().setValue("transition", index);
            auto* sb = pageManager() ? pageManager()->stackBox() : nullptr;
            if (!sb) return;
            QskStackBoxAnimator* newAnim = nullptr;
            switch (index) {
                case 0: newAnim = new QskStackBoxAnimator1(sb); break;
                case 1: newAnim = new QskStackBoxAnimator2(sb); break;
                case 2: newAnim = new QskStackBoxAnimator3(sb); break;
                case 3: newAnim = new QskStackBoxAnimator4(sb); break;
            }
            if (newAnim) sb->setAnimator(newAnim);
        });

    connect(m_skinCombo, &QskComboBox::currentIndexChanged,
        this, [](int index) {
            QSettings().setValue("skin", index);
            static const char* names[] = {"Fusion", "Fluent2", "Material3"};
            if (index >= 0 && index < 3) {
                qskSkinManager->setSkin(names[index]);
            }
        });

    connect(m_darkSwitch, &QskAbstractButton::toggled,
        this, [](bool checked) {
            QSettings().setValue("darkMode", checked);
            auto* s = qskSkinManager->skin();
            if (s) {
                s->setColorScheme(checked
                    ? QskSkin::DarkScheme : QskSkin::LightScheme);
            }
        });

    connect(m_fontScaleCombo, &QskComboBox::currentIndexChanged,
        this, [](int index) {
            QSettings().setValue("fontScale", index);
            static const int sizes[][4] = {
                {16, 22, 14, 12},
                {21, 29, 19, 16},
                {28, 39, 25, 21},
                {35, 48, 32, 27},
            };
            if (index >= 0 && index < 4 && SettingsPage::sharedFontSizes) {
                SettingsPage::sharedFontSizes->body    = sizes[index][0];
                SettingsPage::sharedFontSizes->title   = sizes[index][1];
                SettingsPage::sharedFontSizes->caption = sizes[index][2];
                SettingsPage::sharedFontSizes->global  = sizes[index][3];
            }
            if (SettingsPage::applyAndroidFonts) {
                SettingsPage::applyAndroidFonts();
            }
        });

    // ── Row 5: Debug Background toggle ──
    connect(m_debugBgSwitch, &QskAbstractButton::toggled,
        this, [](bool checked) {
            QSettings().setValue("debugBackground", checked);
            QskSetup::setUpdateFlag(
                QskItem::DebugForceBackground, checked);
            qDebug() << "[anystik] debug background:" << checked;
        });

    // ── Row 6: Phone Answer toggle ──
    connect(m_phoneAnswerCombo, &QskComboBox::currentIndexChanged,
        this, [](int index) {
            QSettings().setValue("phoneAnswer", index);
            PhoneMonitor::setAnswerMode(index);
            qDebug() << "[anystik] phone answer mode:" << index;
#if defined(Q_OS_ANDROID)
            if (index != 0) {
                QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
                    auto ctx = QNativeInterface::QAndroidApplication::context();
                    QJniObject::callStaticMethod<void>(
                        "io/fedlet/mobutil/PermissionHelper",
                        "requestPhoneCallPermission",
                        "(Landroid/app/Activity;)V",
                        ctx.object());
                });
            }
#endif
        });

    // ── Row 7: Push Notification toggle ──
    connect(m_pushNotifySwitch, &QskAbstractButton::toggled,
        this, [](bool checked) {
            QSettings().setValue("pushNotification", checked);
            qDebug() << "[anystik] push notification:" << checked;
        });

    // ── Row 8: Push Backend selection ──
    connect(m_backendCombo, &QskComboBox::currentIndexChanged,
        this, [this](int index) {
            updateGotifyVisibility(index);
            if (index == 0) {
                QSettings().setValue("pushBackend", "");
                PushHandler::instance()->setProviderType(PushProviderType::UnifiedPush);
                qDebug() << "[anystik] push backend: auto";
            } else if (index <= m_knownDistPackages.size()) {
                QString pkg = m_knownDistPackages[index - 1];
                QString name = PushHandler::upDistributorDisplayName(pkg);
                QSettings().setValue("pushBackend", pkg);
                PushHandler::instance()->setProviderType(PushProviderType::UnifiedPush);
                PushHandler::instance()->switchDistributor(pkg);
                showAndroidToast(QString::fromUtf8("切换到 %1...").arg(name));
                qDebug() << "[anystik] push backend:" << pkg;
            } else {
                QSettings().setValue("pushBackend", "gotify");
                PushHandler::instance()->setProviderType(PushProviderType::Gotify);
                qDebug() << "[anystik] push backend: gotify direct";
            }
        });

    connect(PushHandler::instance(), &PushHandler::distributorsUpdated,
        this, [this](const QStringList& installed) {
            rebuildBackendLabels(installed);
        });
    connect(PushHandler::instance(), &PushHandler::statusChanged,
        this, [this]() {
            rebuildBackendLabels(PushHandler::installedDistributors());
        });
    PushHandler::installedDistributors();

    // ── Row 9b/10: Gotify settings (save on text change) ──
    // 注意：新版 QSkinny (a46557b) 中 textChanged() 无参（纯属性通知），
    // 带文本的用户编辑信号是 textEdited(const QString&)
    connect(m_gotifyUrlEdit, &QskTextField::textEdited,
        this, [](const QString& text) {
            QSettings().setValue("gotifyUrl", text);
        });
    connect(m_gotifyTokenEdit, &QskTextField::textEdited,
        this, [](const QString& text) {
            QSettings().setValue("gotifyToken", text);
        });

    // Sync debug background (restored value may differ from QskSetup default)
    if (m_debugBgSwitch->isChecked() != QskSetup::testUpdateFlag(QskItem::DebugForceBackground)) {
        QskSetup::setUpdateFlag(
            QskItem::DebugForceBackground, m_debugBgSwitch->isChecked());
    }

    // 迁移完成后刷新「当前存储」显示与按钮状态
    connect(StickerStore::instance(), &StickerStore::migrationFinished,
            this, [this](bool /*ok*/, const QString& /*detail*/) {
        refreshStorageRows();
    });
}
