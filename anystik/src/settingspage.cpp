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
#include "myi18n.h"
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
    m_title = new QskTextLabel(tr("Settings"), topBar);
    m_title->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    m_title->setAlignment(Qt::AlignCenter);

    connect(backBtn, &QskAbstractButton::clicked, this, [this]() {
        finish();
    });

    layout->addSpacer(24, 0);

    // ── Row 0: Language ──
    auto* row0 = new QskLinearBox(Qt::Horizontal, layout);
    row0->setSpacing(12);
    m_langLabel = new QskTextLabel(tr("Language"), row0);
    m_langLabel->setPreferredWidth(160);
    m_langCombo = new QskComboBox(row0);
    m_langCombo->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    m_langCombo->addOption(QskLabelData(tr("简体中文")));
    m_langCombo->addOption(QskLabelData("English"));
    m_langCombo->addOption(QskLabelData("繁體中文"));
    m_langCombo->setCurrentIndex(
        Lang::instance().code() == "zh-TW" ? 2
        : Lang::instance().code() == "en"  ? 1
                                           : 0);
    connect(m_langCombo, &QskComboBox::currentIndexChanged, this,
        [](int index) {
            Lang::instance().setLanguage(
                index == 2 ? "zh-TW" : index == 1 ? "en" : "zh-CN");
        });

    new QskSeparator(Qt::Horizontal, layout);

    // ── Row 1: Page Transition ──
    auto* row1 = new QskLinearBox(Qt::Horizontal, layout);
    row1->setSpacing(12);
    m_transitionLabel = new QskTextLabel(tr("Page Transition"), row1);
    m_transitionLabel->setPreferredWidth(160);
    m_transitionCombo = new QskComboBox(row1);
    m_transitionCombo->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    m_transitionCombo->addOption(QskLabelData(tr("Slide")));
    m_transitionCombo->addOption(QskLabelData(tr("2D")));
    m_transitionCombo->addOption(QskLabelData(tr("3D")));
    m_transitionCombo->addOption(QskLabelData("Perspective"));
    m_transitionCombo->setCurrentIndex(3);

    new QskSeparator(Qt::Horizontal, layout);

    // ── Row 2: Theme ──
    auto* row2 = new QskLinearBox(Qt::Horizontal, layout);
    row2->setSpacing(12);
    m_themeLabel = new QskTextLabel(tr("Theme"), row2);
    m_themeLabel->setPreferredWidth(160);
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
    m_schemeLabel = new QskTextLabel(tr("Color Scheme"), row3);
    m_schemeLabel->setPreferredWidth(160);
    m_darkSwitch = new QskSwitchButton(row3);
    m_schemeVal = new QskTextLabel(tr("Light"), row3);
    m_schemeVal->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    connect(m_darkSwitch, &QskAbstractButton::toggled,
        [this](bool checked) {
            m_schemeVal->setText(checked ? tr("Dark") : tr("Light"));
        });

    new QskSeparator(Qt::Horizontal, layout);

    // ── Row 4: Font Size ──
    auto* row4 = new QskLinearBox(Qt::Horizontal, layout);
    row4->setPreferredHeight(48);
    row4->setSpacing(12);
    m_fontLabel = new QskTextLabel(tr("Font Size"), row4);
    m_fontLabel->setPreferredWidth(160);
    m_fontScaleCombo = new QskComboBox(row4);
    m_fontScaleCombo->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    m_fontScaleCombo->addOption(QskLabelData(tr("Small")));
    m_fontScaleCombo->addOption(QskLabelData(tr("Medium")));
    m_fontScaleCombo->addOption(QskLabelData(tr("Large")));
    m_fontScaleCombo->addOption(QskLabelData(tr("Extra Large")));
    m_fontScaleCombo->setCurrentIndex(1);

    new QskSeparator(Qt::Horizontal, layout);

    // ── Row 5: Debug Background ──
    auto* row5 = new QskLinearBox(Qt::Horizontal, layout);
    row5->setSpacing(12);
    m_debugLabel = new QskTextLabel(tr("Debug Background"), row5);
    m_debugLabel->setPreferredWidth(160);
    m_debugBgSwitch = new QskSwitchButton(row5);

    new QskSeparator(Qt::Horizontal, layout);

    // ── Row 6: Phone Answer ──
    auto* row6 = new QskLinearBox(Qt::Horizontal, layout);
    row6->setSpacing(12);
    m_phoneLabel = new QskTextLabel(tr("Phone Answer"), row6);
    m_phoneLabel->setPreferredWidth(160);
    m_phoneAnswerCombo = new QskComboBox(row6);
    m_phoneAnswerCombo->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    m_phoneAnswerCombo->addOption(QskLabelData(tr("Disabled")));
    m_phoneAnswerCombo->addOption(QskLabelData(tr("Manual")));
    m_phoneAnswerCombo->addOption(QskLabelData("Auto"));
    m_phoneAnswerCombo->setCurrentIndex(0);

    new QskSeparator(Qt::Horizontal, layout);

    // ── Row 7: Push Notification ──
    auto* row7 = new QskLinearBox(Qt::Horizontal, layout);
    row7->setSpacing(12);
    m_pushNotifyLabel = new QskTextLabel(tr("Push Notification"), row7);
    m_pushNotifyLabel->setPreferredWidth(160);
    m_pushNotifySwitch = new QskSwitchButton(row7);

    new QskSeparator(Qt::Horizontal, layout);

    // ── Row 8: Push Backend (merged provider + distributor) ──
    auto* row8 = new QskLinearBox(Qt::Horizontal, layout);
    row8->setSpacing(12);
    m_backendLabel = new QskTextLabel(tr("Push Backend"), row8);
    m_backendLabel->setPreferredWidth(160);
    m_backendCombo = new QskComboBox(row8);
    m_backendCombo->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    m_backendCombo->addOption(QskLabelData(tr("Auto (system default)")));
    auto knownDists = PushHandler::knownDistributors();
    for (const auto& dist : knownDists) {
        m_knownDistPackages.append(dist.first);
        m_backendCombo->addOption(QskLabelData(dist.second + " (" + dist.first + ")"));
    }
    m_backendCombo->addOption(QskLabelData(tr("Gotify direct (coming soon)")));
    m_backendCombo->setCurrentIndex(0);

    new QskSeparator(Qt::Horizontal, layout);

    // ── Row 9: Gotify Server (visible when Gotify direct selected) ──
    m_gotifyRow = new QskLinearBox(Qt::Horizontal, layout);
    m_gotifyRow->setSpacing(12);
    m_gotifyUrlLabel = new QskTextLabel(tr("Gotify URL"), m_gotifyRow);
    m_gotifyUrlLabel->setPreferredWidth(160);
    m_gotifyUrlEdit = new QskTextField(m_gotifyRow);
    m_gotifyUrlEdit->setPlaceholderText("https://push.example.com");
    m_gotifyUrlEdit->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    auto* gsep1 = new QskSeparator(Qt::Horizontal, layout);
    m_gotifySep1 = gsep1;

    // ── Row 10: Gotify Token ──
    m_gotifyRow2 = new QskLinearBox(Qt::Horizontal, layout);
    m_gotifyRow2->setSpacing(12);
    m_gotifyTokenLabel = new QskTextLabel(tr("Gotify Token"), m_gotifyRow2);
    m_gotifyTokenLabel->setPreferredWidth(160);
    m_gotifyTokenEdit = new QskTextField(m_gotifyRow2);
    m_gotifyTokenEdit->setPlaceholderText(tr("client token"));
    m_gotifyTokenEdit->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    auto* gsep2 = new QskSeparator(Qt::Horizontal, layout);
    m_gotifySep2 = gsep2;

    updateGotifyVisibility(0);

    new QskSeparator(Qt::Horizontal, layout);

    // ── Row 11: 贴纸存储位置（当前 base 目录，只读） ──
    auto* row11 = new QskLinearBox(Qt::Horizontal, layout);
    row11->setSpacing(12);
    m_curRootLabel = new QskTextLabel(tr("当前存储"), row11);
    m_curRootLabel->setPreferredWidth(160);
    m_currentRootValue = new QskTextLabel(QString(), row11);
    m_currentRootValue->setWrapMode(QskTextOptions::WordWrap);
    m_currentRootValue->setSizePolicy(
        QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    new QskSeparator(Qt::Horizontal, layout);

    // ── Row 12: 迁移到相册（目标目录 + 迁移按钮） ──
    auto* row12 = new QskLinearBox(Qt::Horizontal, layout);
    row12->setSpacing(12);
    m_targetRootLabel = new QskTextLabel(tr("迁移到相册"), row12);
    m_targetRootLabel->setPreferredWidth(160);
    m_targetPicsValue = new QskTextLabel(QString(), row12);
    m_targetPicsValue->setWrapMode(QskTextOptions::WordWrap);
    m_targetPicsValue->setSizePolicy(
        QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    auto* migrateBtn = new QskPushButton(tr("迁移"), row12);
    migrateBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    m_migratePicsButton = migrateBtn;
    connect(migrateBtn, &QskPushButton::clicked, this,
            [this]() { onMigrateStorageClicked(StickerStore::StorageRoot::Pictures); });

    new QskSeparator(Qt::Horizontal, layout);

    // ── Row 13: 迁移回私用（目标 AppLocalDataLocation + 迁移按钮） ──
    auto* row13 = new QskLinearBox(Qt::Horizontal, layout);
    row13->setSpacing(12);
    m_targetPrivateLabel = new QskTextLabel(tr("迁移回私用"), row13);
    m_targetPrivateLabel->setPreferredWidth(160);
    m_targetPrivateValue = new QskTextLabel(QString(), row13);
    m_targetPrivateValue->setWrapMode(QskTextOptions::WordWrap);
    m_targetPrivateValue->setSizePolicy(
        QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    auto* migratePrivateBtn = new QskPushButton(tr("迁移"), row13);
    migratePrivateBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    m_migratePrivateButton = migratePrivateBtn;
    connect(migratePrivateBtn, &QskPushButton::clicked, this,
            [this]() { onMigrateStorageClicked(StickerStore::StorageRoot::AppPrivate); });

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

void SettingsPage::rebuildTranslatedCombos()
{
    const auto rebuild = [](QskComboBox* combo, const QVector<QskLabelData>& opts) {
        const int saved = combo->currentIndex();
        combo->blockSignals(true);
        combo->clear();
        for (const auto& o : opts)
            combo->addOption(o);
        combo->setCurrentIndex(qMin(saved, opts.size() - 1));
        combo->blockSignals(false);
    };

    rebuild(m_langCombo, { QskLabelData(tr("简体中文")),
                           QskLabelData("English"),
                           QskLabelData("繁體中文") });
    rebuild(m_transitionCombo, { QskLabelData(tr("Slide")),
                                 QskLabelData(tr("2D")),
                                 QskLabelData(tr("3D")),
                                 QskLabelData("Perspective") });
    rebuild(m_fontScaleCombo, { QskLabelData(tr("Small")),
                                QskLabelData(tr("Medium")),
                                QskLabelData(tr("Large")),
                                QskLabelData(tr("Extra Large")) });
    rebuild(m_phoneAnswerCombo, { QskLabelData(tr("Disabled")),
                                  QskLabelData(tr("Manual")),
                                  QskLabelData("Auto") });
    // m_skinCombo：Fusion/Fluent2/Material3 为产品名，不翻译
}

void SettingsPage::retranslateUi()
{
    if (!m_title)
        return;

    m_title->setText(tr("Settings"));
    m_langLabel->setText(tr("Language"));
    m_transitionLabel->setText(tr("Page Transition"));
    m_themeLabel->setText(tr("Theme"));
    m_schemeLabel->setText(tr("Color Scheme"));
    m_fontLabel->setText(tr("Font Size"));
    m_debugLabel->setText(tr("Debug Background"));
    m_phoneLabel->setText(tr("Phone Answer"));
    m_pushNotifyLabel->setText(tr("Push Notification"));
    m_backendLabel->setText(tr("Push Backend"));
    m_gotifyUrlLabel->setText(tr("Gotify URL"));
    m_gotifyTokenLabel->setText(tr("Gotify Token"));
    m_curRootLabel->setText(tr("当前存储"));
    m_targetRootLabel->setText(tr("迁移到相册"));
    m_targetPrivateLabel->setText(tr("迁移回私用"));

    m_schemeVal->setText(m_darkSwitch->isChecked() ? tr("Dark") : tr("Light"));

    rebuildTranslatedCombos();
    m_gotifyTokenEdit->setPlaceholderText(tr("client token"));
    rebuildBackendLabels(PushHandler::installedDistributors());
    refreshStorageRows();
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
    opts.append(QskLabelData(tr("Auto (system default)")));
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
    opts.append(QskLabelData(tr("Gotify direct (coming soon)")));
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
    const QString pics = targetPath(StickerStore::StorageRoot::Pictures);
    const QString priv = targetPath(StickerStore::StorageRoot::AppPrivate);
    const bool inPics = store->isCurrentStoragePictures();

    if (m_currentRootValue)
        m_currentRootValue->setText(current);
    if (m_targetPicsValue)
        m_targetPicsValue->setText(pics);
    if (m_targetPrivateValue)
        m_targetPrivateValue->setText(priv);

    if (m_migratePicsButton) {
        m_migratePicsButton->setEnabled(!inPics);
        m_migratePicsButton->setText(inPics
            ? tr("已在相册")
            : tr("迁移"));
    }
    if (m_migratePrivateButton) {
        m_migratePrivateButton->setEnabled(inPics);
        m_migratePrivateButton->setText(inPics
            ? tr("迁移")
            : tr("已在私用"));
    }
}

QString SettingsPage::targetPath(StickerStore::StorageRoot r) const
{
    auto* store = StickerStore::instance();
    if (!store)
        return QString();
    return store->storageRootPath(r);
}

void SettingsPage::onMigrateStorageClicked(StickerStore::StorageRoot target)
{
    auto* store = StickerStore::instance();
    if (!store)
        return;

    const QString fromRoot = store->currentStickerBaseDir();
    const QString toRoot = targetPath(target);

    if (toRoot.isEmpty()) {
        showAndroidToast(tr("无法确定目标目录"));
        return;
    }
    if (QDir::cleanPath(fromRoot) == QDir::cleanPath(toRoot)) {
        showAndroidToast(target == StickerStore::StorageRoot::Pictures
            ? tr("已在相册目录")
            : tr("已在私用目录"));
        refreshStorageRows();
        return;
    }

#if defined(Q_OS_ANDROID)
    if (target == StickerStore::StorageRoot::Pictures
        && !androidStorageAccessGranted()) {
        requestAndroidStorageAccess();
        showAndroidToast(tr("请在权限页授予存储访问权限，返回后再点迁移"));
        return;
    }
#endif

    // 打开进度对话框（完成/失败自动关闭）
    MigrationDialog::show(this, fromRoot, toRoot);

    QString err;
    const bool ok = store->switchStorageRoot(target, &err);
    if (!ok) {
        showAndroidToast(err.isEmpty() ? tr("迁移启动失败")
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
                showAndroidToast(tr("切换到 %1...").arg(name));
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
