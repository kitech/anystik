#ifndef SETTINGS_PAGE_H
#define SETTINGS_PAGE_H

#include "page.h"
#include "stickerstore.h"
#include <QskComboBox.h>
#include <QskSwitchButton.h>
#include <QPointer>
#include <memory>
#include <functional>

class QskTextField;
class QskSeparator;
class QskLinearBox;
class QskTextLabel;
class QskPushButton;

struct FontSizes {
    int body = 21, title = 29, caption = 19, global = 16;
};

class SettingsPage : public Page
{
public:
    SettingsPage(QQuickItem* parent = nullptr);

    int transitionIndex() const { return m_transitionCombo->currentIndex(); }
    int skinIndex() const { return m_skinCombo->currentIndex(); }
    bool isDarkMode() const { return m_darkSwitch->isChecked(); }
    int fontScaleIndex() const { return m_fontScaleCombo->currentIndex(); }

    QskComboBox* transitionCombo() const { return m_transitionCombo; }
    QskComboBox* skinCombo() const { return m_skinCombo; }
    QskSwitchButton* darkModeSwitch() const { return m_darkSwitch; }
    QskComboBox* fontScaleCombo() const { return m_fontScaleCombo; }

    static void changeFontScale(int delta);
    ~SettingsPage();

    // Shared state accessible from main.cpp
    static std::shared_ptr<FontSizes> sharedFontSizes;
    static std::function<void()>     applyAndroidFonts;

protected:
    void onCreate(const QVariantMap& launchArgs,
                  const QVariantMap& savedState) override;

private:
    void updateGotifyVisibility(int backendIndex);
    void rebuildBackendLabels(const QStringList& installed);
    void refreshStorageRows();
    void onMigrateStorageClicked(StickerStore::StorageRoot target);
    QString targetPath(StickerStore::StorageRoot r) const;

    bool m_signalsConnected = false;
    int m_currentAnimatorIdx = 3;
    QskComboBox* m_transitionCombo = nullptr;
    QskComboBox* m_skinCombo = nullptr;
    QskSwitchButton* m_darkSwitch = nullptr;
    QskComboBox* m_fontScaleCombo = nullptr;
    QskSwitchButton* m_debugBgSwitch = nullptr;
    QskComboBox* m_phoneAnswerCombo = nullptr;
    QskSwitchButton* m_pushNotifySwitch = nullptr;
    QskComboBox* m_backendCombo = nullptr;
    QskTextField* m_gotifyUrlEdit = nullptr;
    QskTextField* m_gotifyTokenEdit = nullptr;
    QskLinearBox* m_gotifyRow = nullptr;
    QskLinearBox* m_gotifyRow2 = nullptr;
    QskSeparator* m_gotifySep1 = nullptr;
    QskSeparator* m_gotifySep2 = nullptr;
    QStringList m_knownDistPackages;

    // ── 存储位置（贴纸 base 目录切换）──
    QskTextLabel* m_currentRootValue = nullptr;
    QskTextLabel* m_targetPicsValue = nullptr;
    QskPushButton* m_migratePicsButton = nullptr;
    QskTextLabel* m_targetPrivateValue = nullptr;
    QskPushButton* m_migratePrivateButton = nullptr;

    static QPointer<SettingsPage> s_instance;
};

#endif
