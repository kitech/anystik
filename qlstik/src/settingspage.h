#ifndef QLSTIK_SETTINGS_PAGE_H
#define QLSTIK_SETTINGS_PAGE_H

#include "page.h"

#include <qwidget.h>
#include <qstring.h>

class QLabel;
class QLineEdit;
class QPushButton;
class QComboBox;
class QCheckBox;

// 设置页 —— 对标 anystik/src/settingspage.{h,cpp}。
// 偏离（已裁定）：QSkinny 专属行（动画/字号/调试底色/来电/推送/Gotify）
// 保留 UI 但可点无响应；存储行只读展示 + 迁移按钮可点无响应（不碰 DB）。
// 文案硬编码中文，不接 lang；语言/主题/深色/DAV 为真功能。
class SettingsPage : public Page {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget* parent = 0);
    virtual ~SettingsPage();

    virtual void onCreate(const QVariantMap& launchArgs,
                          const QVariantMap& savedState);

public slots:
    void onBackClicked();
    void onLangChanged(int index);
    void onStyleChanged(int index);
    void onDarkToggled(bool on);
    void onDavUrlChanged(const QString& text);
    void onDavUserChanged(const QString& text);
    void onDavPassChanged(const QString& text);

private:
    void buildUi();

    QComboBox*   m_langCombo;
    QComboBox*   m_transitionCombo;
    QComboBox*   m_styleCombo;
    QCheckBox*   m_darkCheck;
    QComboBox*   m_fontCombo;
    QCheckBox*   m_debugCheck;
    QComboBox*   m_phoneCombo;
    QCheckBox*   m_pushCheck;
    QComboBox*   m_backendCombo;
    QLineEdit*   m_gotifyUrlEdit;
    QLineEdit*   m_gotifyTokenEdit;

    QLabel*      m_currentRootValue;
    QLabel*      m_picsRootValue;
    QLabel*      m_privateRootValue;
    QPushButton* m_migratePicsBtn;
    QPushButton* m_migratePrivateBtn;

    QLineEdit*   m_davUrlEdit;
    QLineEdit*   m_davUserEdit;
    QLineEdit*   m_davPassEdit;
};

#endif // QLSTIK_SETTINGS_PAGE_H
