#include "stickerhomepage.h"
#include "stickerlist.h"
#include "stickerpreviewoverlay.h"
#include "imagetmpuploader.h"
#include "imagesearchpopup.h"
#include "imageaiutil.h"
#include "davobfus.h"
#include "menuoverlay.h"
#include "dialogpopup.h"
#include "toastpopup.h"
#include "pushstatusbar.h"
#include "phonesmsstatusbar.h"
#include "settings_trace.h"
#include "androidutils.h"
#include "pagemanager.h"
#include "mysearchline.h"
#include "qwebdav.h"

#include <QskLinearBox.h>
#include <QskTextLabel.h>
#include <QskPushButton.h>
#include <QskTextField.h>
#include <QskTextInput.h>
#include <QskFontRole.h>
#include <QskGraphicLabel.h>
#include <QskGraphic.h>
#include <QskBox.h>
#include <QskGradient.h>
#include <QskTabBar.h>
#include <QskTabButton.h>
#include <QskComboBox.h>
#include <QskMenu.h>
#include <QskLabelData.h>
#include <QskDialog.h>
#include <QskBoxShapeMetrics.h>
#include <QskPopup.h>
#include <QskSimpleListBox.h>
#include <QskFunctions.h>
#include <QskQuick.h>
#include <QInputMethodEvent>
#include <QFontMetricsF>
#include <private/qquicktextedit_p.h>   // 真多行编辑：QskTextInput/TextField 内嵌单行不可换行

#include <QDir>
#include <QFileInfo>
#include <QPair>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <QQuickWindow>
#include <QUrl>
#include <QWindow>
#include <QGuiApplication>
#include <QClipboard>
#include <QSettings>
#include <QDesktopServices>
#include <QImageReader>
#include <QKeyEvent>
#include <QHoverEvent>
#include <QSet>

// 真多行编辑器：内嵌 QQuickTextEdit。回车/Ctrl+回车 插 \n、显式几何下自动折行、
// 光标/中文 IME 全原生（QskTextInput/QskTextField 内嵌单行 QQuickTextInput 做不到）。
class MultiLineTextEdit : public QskControl
{
    Q_OBJECT
public:
    explicit MultiLineTextEdit(QQuickItem* parent = nullptr)
        : QskControl(parent)
    {
        setPolishOnResize(true);
        setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Constrained);

        setFocusPolicy(Qt::StrongFocus);                  // 外壳为焦点对象（qskinny 输入法走线）
        setFlag(QQuickItem::ItemAcceptsInputMethod);      // 外壳是 IM 目标（仿 QskTextInput）

        m_background = new QskBox(this);
        m_background->setBoxShapeHint(QskBox::Panel,
            QskBoxShapeMetrics(8, Qt::AbsoluteSize));

        m_placeholder = new QskTextLabel(this);
        m_placeholder->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        m_placeholder->setFontRole(QskFontRole::Caption);

        m_edit = new QQuickTextEdit(this);
        m_edit->setWrapMode(QQuickTextEdit::WrapAnywhere);
        m_edit->setClip(true);
        m_edit->setFocusOnPress(false);                   // 焦点/输入法由外壳接管
        m_edit->setFlag(QQuickItem::ItemAcceptsInputMethod, false);
        m_edit->setAcceptedMouseButtons(Qt::NoButton);    // 鼠标事件由外壳转发

        connect(m_edit, &QQuickTextEdit::textChanged, this, [this]() {
            if (m_maxLength > 0) {
                const QString t = m_edit->text();
                if (t.size() > m_maxLength) {
                    m_edit->setText(t.left(m_maxLength));
                    m_edit->setCursorPosition(m_maxLength);
                }
            }
            updatePlaceholder();
            Q_EMIT textEdited();
        });
    }

    void setText(const QString& text) { m_edit->setText(text); if (!text.isEmpty()) m_edit->setCursorPosition(text.size()); updatePlaceholder(); }
    QString text() const { return m_edit->text(); }
    void setMaxLength(int max) { m_maxLength = max; }
    void setPlaceholderText(const QString& text)
    {
        m_placeholder->setText(text);
        updatePlaceholder();
    }
    void activate()
    {
        setFocus(true);
        m_engaged = true;
        m_edit->setCursorVisible(true);
        qskInputMethodSetVisible(this, true);
        updatePlaceholder();
    }

signals:
    void textEdited();

protected:
    using Inherited = QskControl;

    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::ShortcutOverride)
            return QCoreApplication::sendEvent(m_edit, event);
        return Inherited::event(event);
    }
    void keyPressEvent(QKeyEvent* event) override
    {
        if (!m_engaged) {
            m_engaged = true;
            m_edit->setCursorVisible(true);
            updatePlaceholder();
        }
        QCoreApplication::sendEvent(m_edit, event);
    }
    void keyReleaseEvent(QKeyEvent* event) override
    {
        QCoreApplication::sendEvent(m_edit, event);
    }
    void inputMethodEvent(QInputMethodEvent* event) override
    {
        QCoreApplication::sendEvent(m_edit, event);
    }
    QVariant inputMethodQuery(Qt::InputMethodQuery q) const override
    {
        return m_edit->inputMethodQuery(q);
    }
    QVariant inputMethodQuery(Qt::InputMethodQuery q, const QVariant& a) const
    {
        return m_edit->inputMethodQuery(q, a);
    }
    void focusInEvent(QFocusEvent* event) override
    {
        m_engaged = true;
        m_edit->setCursorVisible(true);
        updatePlaceholder();
        Inherited::focusInEvent(event);
    }
    void focusOutEvent(QFocusEvent* event) override
    {
        m_engaged = false;
        m_edit->setCursorVisible(false);
        qskInputMethodSetVisible(this, false);
        updatePlaceholder();
        Inherited::focusOutEvent(event);
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        QCoreApplication::sendEvent(m_edit, event);
        if (!m_engaged)
            activate();
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        QCoreApplication::sendEvent(m_edit, event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        QCoreApplication::sendEvent(m_edit, event);
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        QCoreApplication::sendEvent(m_edit, event);
    }
    void updateLayout() override
    {
        if (!m_colorApplied) {
            // 文字色对齐皮肤（placeholder 为 QskTextLabel，默认生效皮肤文本色）
            const QColor c = m_placeholder->textColor();
            if (c.isValid()) {
                m_edit->setColor(c);
                m_colorApplied = true;
            }
        }
        const QRectF r = contentsRect();
        m_background->setGeometry(r);
        const QRectF textRect = r.adjusted(8, 4, -8, -4);   // 显式几何 → 折行成立
        m_edit->setX(textRect.x());                          // QQuickTextEdit 为裸 QQuickItem，无 setGeometry
        m_edit->setY(textRect.y());
        m_edit->setWidth(textRect.width());
        m_edit->setHeight(textRect.height());
        m_placeholder->setGeometry(textRect);
    }

private:
    void updatePlaceholder()
    {
        m_placeholder->setVisible(
            m_edit->text().isEmpty() && !m_engaged);
    }

    QskBox* m_background = nullptr;
    QskTextLabel* m_placeholder = nullptr;
    QQuickTextEdit* m_edit = nullptr;
    int m_maxLength = 0;
    bool m_colorApplied = false;
    bool m_engaged = false;      // 外壳：编辑态（占位符/光标/输入法面板开关）
};

// 面板不透明度：回读皮肤面板填充色、仅改 alpha（对齐 ImageSearchPopup/SyncProgressPopup）
void applyDescPanelOpacity(QskBox* panel, qreal opacity)
{
    if (!panel) return;
    QskGradient g = panel->fillGradient();
    if (!g.isValid()) return;
    g.setAlpha(qRound(qBound(0.0, opacity, 1.0) * 255.0));
    panel->setFillGradient(g);
}

QRectF descParentRect(QQuickItem* parent)
{
    if (!parent) return {};
    if (auto* w = parent->window())
        return QRectF(QPointF(), w->size());
    return QRectF(-parent->x(), -parent->y(),
                  parent->width(), parent->height());
}

// 编辑描述浮层：QskBox 面板(alpha 0.9, 圆角14) + 固定 2/3 窗口高居中
class DescEditPopup : public QskPopup
{
public:
    QskLinearBox* m_layout = nullptr;
    MultiLineTextEdit* m_editInput = nullptr;   // 真多行主输入（QQuickTextEdit 封装）
    QskTextLabel* m_editCount = nullptr;

    explicit DescEditPopup(QQuickItem* parent = nullptr)
        : QskPopup(parent)
    {
        setModal(true);
        setOverlay(true);
        setPopupFlag(QskPopup::DeleteOnClose, true);
        setPolishOnResize(true);
        setPolishOnParentResize(true);

        auto* panel = new QskBox(this);
        panel->setBoxShapeHint(QskBox::Panel,
            QskBoxShapeMetrics(14, Qt::AbsoluteSize));
        applyDescPanelOpacity(panel, 0.9);   // 面板透明度，对齐项目其它弹窗（窗口级 opacity 会致输入框文本不渲染）

        m_layout = new QskLinearBox(Qt::Vertical, panel);
        m_layout->setMargins(18);
        m_layout->setSpacing(10);

        auto* title = new QskTextLabel(tr("编辑描述简介"), m_layout);
        title->setFontRole(QskFontRole::Title);
        title->setAlignment(Qt::AlignCenter);

        m_thumb = new QskGraphicLabel(m_layout);
        m_thumb->setFillMode(QskGraphicLabel::PreserveAspectFit);
        m_thumb->setPreferredSize(200, 150);   // 对齐当前实际弹窗的缩略图尺寸

        m_meta = new QskTextLabel(m_layout);
        m_meta->setWrapMode(QskTextOptions::WrapAnywhere);
        m_meta->setSizePolicy(QskSizePolicy::Expanding,
                             QskSizePolicy::Constrained);

        // 主编辑：QQuickTextEdit 封装——文字可见、自动折行、回车/Ctrl+回车换行、
        // 中文 IME 全原生（已替代 QskTextInput/QskTextField，见 AGENTS.md）
        m_editInput = new MultiLineTextEdit(m_layout);
        m_editInput->setPlaceholderText(tr("输入描述(最多140字)"));
        m_editInput->setMaxLength(140);
        m_editInput->setFixedHeight(72);

        m_editCount = new QskTextLabel(m_layout);
        m_editCount->setAlignment(Qt::AlignRight);
        m_editCount->setFontRole(QskFontRole::Caption);
        m_editCount->setText(tr("%1/%2").arg(0).arg(140));

        connect(m_editInput, &MultiLineTextEdit::textEdited, this, [this]() {
            m_editCount->setText(
                tr("%1/%2").arg(m_editInput->text().size()).arg(140));
        });

        // 自动获取描述：默认后端 16 = Z.ai(国际)，本地图片 base64 走 OpenAI 兼容通道
        {
            auto* autoRow = new QskLinearBox(Qt::Horizontal, m_layout);
            autoRow->setSpacing(10);
            m_autoBtn = new QskPushButton(tr("自动获取描述"), autoRow);
            m_autoBtn->setBoxShapeHint(QskPushButton::Panel,
                QskBoxShapeMetrics(8, Qt::AbsoluteSize));
            m_autoBtn->setSizePolicy(QskSizePolicy::Expanding,
                                     QskSizePolicy::Preferred);
        }

        auto* btnBox = new QskLinearBox(Qt::Horizontal, m_layout);
        btnBox->setSpacing(10);
        auto* cancelBtn = new QskPushButton(tr("取消"), btnBox);
        cancelBtn->setBoxShapeHint(QskPushButton::Panel,
            QskBoxShapeMetrics(8, Qt::AbsoluteSize));
        connect(cancelBtn, &QskAbstractButton::clicked,
                this, &QskPopup::close);
        m_saveBtn = new QskPushButton(tr("确定"), btnBox);
        m_saveBtn->setBoxShapeHint(QskPushButton::Panel,
            QskBoxShapeMetrics(8, Qt::AbsoluteSize));
        m_saveBtn->setSizePolicy(QskSizePolicy::Expanding,
                                 QskSizePolicy::Preferred);

        // open 后补一次几何布局（对齐 ImageSearchPopup L167 范式）
        QTimer::singleShot(0, this, [this]() { updateGeometry(); });

        // 自动获取描述：在途防重、结果按 token 归属、关窗即取消。
        // 所有终态（成功/失败/关窗）统一经 resetAutoBusy 恢复按钮，杜绝卡死。
        auto resetAutoBusy = [this]() {
            m_autoReqId = 0;
            m_autoBtn->setEnabled(true);
            m_autoBtn->setText(tr("自动获取描述"));
        };

        connect(m_autoBtn, &QskAbstractButton::clicked, this, [this]() {
            if (m_autoReqId != 0 || m_localPath.isEmpty()) {
                return;
            }
            m_autoReqId = ImageAiUtil::instance()->fetchDescription(
                QString(), m_localPath);
            m_autoBtn->setEnabled(false);
            m_autoBtn->setText(tr("获取中…"));
        });

        ImageAiUtil* ai = ImageAiUtil::instance();
        connect(ai, &ImageAiUtil::descriptionReady, this,
            [this, resetAutoBusy](quint64 requestId, const QString&,
                                  const QString& desc) {
                if (requestId != m_autoReqId) {
                    return;
                }
                const QString text = desc.trimmed().left(140);
                m_editInput->setText(text);
                m_editCount->setText(tr("%1/%2").arg(text.size()).arg(140));
                resetAutoBusy();
            });
        connect(ai, &ImageAiUtil::failed, this,
            [this, resetAutoBusy](quint64 requestId, const QString&,
                                  const QString& reason) {
                if (requestId != m_autoReqId) {
                    return;
                }
                resetAutoBusy();
                ToastPopup::show(this, tr("获取描述失败：%1").arg(reason));
            });

        // 关窗兜底：取消在途请求（不再回发信号），避免 DeleteOnClose 后回调触碰已销毁弹窗
        connect(this, &QskPopup::closed, this, [this, resetAutoBusy]() {
            if (m_autoReqId != 0) {
                ImageAiUtil::instance()->cancelRequest(m_autoReqId);
                m_autoReqId = 0;
            }
            resetAutoBusy();
        });
    }

    void setBrief(const StickerBrief& brief)
    {
        m_localPath = brief.filePath;
        // 主输入预填原文（有则可见显示，placeholder 自动隐藏）
        m_editInput->setText(brief.description);
        m_editCount->setText(tr("%1/%2").arg(brief.description.size()).arg(140));

        QImageReader reader(brief.filePath);
        reader.setAutoTransform(true);
        const QImage img = reader.read();
        if (!img.isNull()) {
            m_thumb->setGraphic(QskGraphic::fromImage(
                img.scaled(200, 150, Qt::KeepAspectRatio,
                           Qt::SmoothTransformation)));
            m_thumb->setVisible(true);
        } else {
            m_thumb->setVisible(false);
        }

        const StickerMeta meta =
            StickerStore::instance()->stickerMeta(brief.filePath);
        m_meta->setText(formatStickerMeta(meta));
    }

    MultiLineTextEdit* editInput() const { return m_editInput; }
    QskPushButton* saveButton() const { return m_saveBtn; }

    // 保存取值：主输入内容（空 → 空串 → 清除 desc）
    QString validText() const
    {
        return m_editInput ? m_editInput->text().trimmed() : QString();
    }

protected:
    void updateLayout() override
    {
        updateGeometry();
        m_layout->setGeometry(layoutRect());
    }

private:
    void updateGeometry()
    {
        const auto parentRect = descParentRect(parentItem());
        if (parentRect.isEmpty()) return;

        const auto hint = m_layout->effectiveSizeHint(
            Qt::PreferredSize, QSizeF());
        const qreal maxW = 0.9 * parentRect.width();

        const qreal panelW = qMin(qMax(300.0, hint.width() + 36), maxW);
        const qreal panelH = parentRect.height() * 2.0 / 3.0;   // 固定占 2/3 窗口高

        QRectF r(0, 0, panelW, panelH);
        r.moveCenter(parentRect.center());
        setGeometry(r);
        for (auto* c : childItems()) {
            if (auto* box = qobject_cast<QskBox*>(c)) {
                box->setGeometry(r.translated(-r.topLeft()));
                break;
            }
        }
    }

    QString m_localPath;          // 当前贴纸本地图路径（自动获取描述用）
    QskGraphicLabel* m_thumb = nullptr;
    QskTextLabel* m_meta = nullptr;
    QskPushButton* m_autoBtn = nullptr;
    QskPushButton* m_saveBtn = nullptr;
    quint64 m_autoReqId = 0;      // 自动获取描述在途令牌（0=无在途）
};
#include <QUrl>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

static constexpr int FLAG_KEEP_SCREEN_ON = 0x80;

static void jniKeepScreenOn(bool on) {
#ifdef Q_OS_ANDROID
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([on]() {
        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (!activity.isValid()) return;
        QJniObject window = activity.callObjectMethod(
            "getWindow", "()Landroid/view/Window;");
        if (!window.isValid()) return;
        if (on)
            window.callMethod<void>("addFlags", "(I)V", FLAG_KEEP_SCREEN_ON);
        else
            window.callMethod<void>("clearFlags", "(I)V", FLAG_KEEP_SCREEN_ON);
    }).waitForFinished();
#else
    Q_UNUSED(on)
#endif
}

// Android 打开目录：经 ShareActivity.openDir 拉起文件管理器，
// 成功返回 true，失败返回 false（调用方据此提示）。
static bool jOpenDir(const QString& path)
{
#ifdef Q_OS_ANDROID
    bool ok = false;
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([&ok, path]() {
        QJniObject context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid()) return;
        ok = QJniObject::callStaticMethod<jboolean>(
            "io/fedlet/mobutil/ShareActivity", "openDir",
            "(Landroid/content/Context;Ljava/lang/String;)Z",
            context.object(), QJniObject::fromString(path).object());
    }).waitForFinished();
    return ok;
#else
    Q_UNUSED(path)
    return false;
#endif
}

StickerHomePage::StickerHomePage(QQuickItem* parent)
    : Page(parent)
{
    // 悬停级联子菜单：离开其区域/父菜单时的防抖关闭。原 singleShot(0) 会在
    // hover 事件批处理的间隙误执行 → 子菜单被反复关闭又重开（重复淡入）。
    m_subCloseTimer.setSingleShot(true);
    m_subCloseTimer.setInterval(225);
    connect(&m_subCloseTimer, &QTimer::timeout,
            this, [this]() { closeSubMenu(); });
}

void StickerHomePage::onCreate(const QVariantMap& launchArgs,
                               const QVariantMap& savedState)
{
    Q_UNUSED(launchArgs)
    Q_UNUSED(savedState)

    StickerStore::instance()->ensureInit();

    // 恢复 keepScreenOn，延迟到窗口就绪后应用
    m_keepScreenOn = QSettings().value("keepScreenOn", true).toBool();
    trace_settings("home-keepScreenOn-read");
    QTimer::singleShot(50, this, [this]() { jniKeepScreenOn(m_keepScreenOn); });

    setAutoLayoutChildren(true);
    auto* layout = new QskLinearBox(Qt::Vertical, this);
    layout->setPanel(true);
    layout->setSpacing(6);

    // ── TopBar ──
    auto* topBar = new QskLinearBox(Qt::Horizontal, layout);
    topBar->setPanel(true);
    topBar->setPreferredHeight(56);
    topBar->setSpacing(8);

    m_title = new QskTextLabel(tr("😐 表情包"), topBar);
    m_title->setAlignment(Qt::AlignCenter);
    m_title->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    m_pasteBtn = new QskPushButton(tr("粘贴"), topBar);
    m_pasteBtn->setPreferredWidth(68);
    m_pasteBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    connect(m_pasteBtn, &QskAbstractButton::clicked,
        this, &StickerHomePage::requestPasteSticker);

    m_importBtn = new QskPushButton(tr("导入"), topBar);
    m_importBtn->setPreferredWidth(68);
    m_importBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    connect(m_importBtn, &QskAbstractButton::clicked, this, [this]() {
        showImportMenu(m_importBtn->mapToItem(this,
            QPointF(0, m_importBtn->height())));
    });

    m_syncBtn = new QskPushButton(tr("同步"), topBar);
    m_syncBtn->setPreferredWidth(68);
    m_syncBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    connect(m_syncBtn, &QskAbstractButton::clicked, this, [this]() {
        if (m_syncEngine && m_syncEngine->isRunning()) {
            ensureSyncPopup(false);
            return;
        }
        const QSettings dav;
        trace_settings("home-dav-read");
        QString davUrl  = dav.value("davUrl").toString().trimmed();
        QString davUser = dav.value("davUser").toString().trimmed();
        QString davPass = dav.value("davPass").toString().trimmed();

        auto validDav = [](const QString& u) {
            const QUrl url(u);
            return url.isValid() && !url.host().isEmpty();
        };

        const bool settingsComplete =
            !davUrl.isEmpty() && !davUser.isEmpty() && !davPass.isEmpty();
        // 仅当设置三项齐全且地址有效时才用设置项，否则整体回退内置混淆配置
        if (!settingsComplete || !validDav(davUrl)) {
            const QString key = davObfusKey();
            const QUrl ob(key);
            davUrl  = key;
            davUser = ob.userName();
            davPass = ob.password();
            qInfo() << "[dav] fallback to built-in config"
                    << "settingsComplete=" << settingsComplete
                    << "host=" << ob.host();
        }

        const QUrl url(davUrl);
        if (!url.isValid() || url.host().isEmpty()) {
            showToast(tr("WebDAV 地址无效"));
            return;
        }
        const bool https = url.scheme().startsWith(QLatin1String("https"));

        if (!m_syncEngine) {
            m_syncEngine = new SyncEngine(this);
            connect(m_syncEngine, &SyncEngine::finished, this,
                    [this](int exitCode, const QString& summary) {
                        m_syncBtn->setText(tr("同步"));
                        if (exitCode == davbisync::FinishOk) {
                            showToast(tr("同步完成"));
                        } else if (exitCode == davbisync::FinishCancelled) {
                            showToast(tr("已取消"));
                        } else {
                            showToast(tr("同步失败：%1").arg(summary));
                        }
                        qInfo() << "[sync] finished exit=" << exitCode << summary;
                    });
        }

        ensureSyncPopup();
        m_syncEngine->setConnectionSettings(
            https ? 2 : 1,
            url.host(),
            url.path(),
            davUser,
            davPass);
        if (m_syncEngine->startSync()) {
            m_syncBtn->setText(tr("同步中..."));
        }
    });

    auto* moreBtn = new QskPushButton(QString::fromUtf8("⋯"), topBar);
    moreBtn->setPreferredSize(44, 44);
    moreBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    connect(moreBtn, &QskAbstractButton::clicked, this,
        [this, moreBtn]() {
            showOptionsMenu(moreBtn->mapToItem(this, QPointF(0, moreBtn->height())));
        });

    // ── Push 连接状态（Android 可见，桌面包视情况自隐藏）──
    new PushStatusBar(layout);

    // ── 电话/短信状态栏（Android：计数 + 列表浮动框，桌面自隐藏）──
    new PhoneSmsStatusBar(layout);

    // ── 搜索框 + 当前贴纸计数（同一行左右分块）──
    auto* searchRow = new QskLinearBox(Qt::Horizontal, layout);
    searchRow->setSpacing(8);

    m_searchLine = new MySearchLine(searchRow);
    m_searchLine->setPlaceholderText(tr("搜索贴纸 / emoji..."));

    m_countLabel = new QskTextLabel(tr("%1 个 · %2 包").arg(0).arg(0), searchRow);
    m_countLabel->setPreferredWidth(72);
    m_countLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);

    m_searchDebounce.setSingleShot(true);
    connect(m_searchLine, &MySearchLine::textChanged, this,
        [this]() { m_searchDebounce.start(350); });
    connect(&m_searchDebounce, &QTimer::timeout, this,
        [this]() { doSearch(m_searchLine->text().trimmed()); });

    // ── 分组 Tab ──
    auto* tabBarBox = new QskLinearBox(Qt::Horizontal, layout);
    tabBarBox->setPanel(true);
    tabBarBox->setPreferredHeight(48);

    m_tabBar = new QskTabBar(Qt::TopEdge, tabBarBox);
    m_tabBar->setSizePolicy(QskSizePolicy::MinimumExpanding, QskSizePolicy::Expanding);
    m_tabBar->setAutoFitTabs(true);

    connect(m_tabBar, &QskTabBar::currentIndexChanged,
        this, &StickerHomePage::onTabChanged);

    m_packCombo = new QskComboBox(tabBarBox);
    m_packCombo->setPreferredWidth(150);
    m_packCombo->setSizePolicy(QskSizePolicy::Preferred, QskSizePolicy::Expanding);
    m_packCombo->setPlaceholderText(tr("更多分组…"));
    connect(m_packCombo, &QskComboBox::currentIndexChanged,
        this, &StickerHomePage::onPackComboChanged);
    // QskComboBox 弹菜单偶发失败对策（见 QskComboBox.cpp）：
    // A) window()==nullptr 时 setPopupOpen(true) 静默失败；
    // B) 关闭 fade 未结束前重开，openPopup 被残留 menu 拦截且 PopupOpen 态卡牢。
    // 每次按下先复位，再于下一事件循环兜底重开；setPopupOpen 幂等，不影响正常路径。
    connect(m_packCombo, &QskComboBox::pressed, this, [this]() {
        m_packCombo->setPopupOpen(false);
        QTimer::singleShot(0, this, [this]() {
            if (m_packCombo->window())
                m_packCombo->setPopupOpen(true);
        });
    });

    // ── 贴纸网格 ──
    m_grid = new StickerGridWidget(layout);
    m_grid->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Expanding);

    connect(m_grid, &StickerGridWidget::stickerClicked,
        this, [this](const StickerBrief& brief, int index) {
            const StickerMeta meta = StickerStore::instance()->stickerMeta(brief.filePath);
            const QString mime = meta.mime.isEmpty()
                ? QStringLiteral("unknown") : meta.mime;
            const qint64 size = brief.size ? qint64(brief.size) : meta.sizeBytes;
            StickerStore::instance()->touchSticker(brief.id);
            bool ok = StickerStore::instance()->copyStickerToClipboard(brief.filePath);
            showToast(ok
                ? tr("已复制到剪贴板 index=%1 size=%2 mime=%3")
                      .arg(index).arg(size).arg(mime)
                : tr("复制失败 index=%1 size=%2 mime=%3")
                      .arg(index).arg(size).arg(mime));
        });

    connect(m_grid, &StickerGridWidget::stickerDoubleClicked,
        this, &StickerHomePage::openPreview);

    connect(m_grid, &StickerGridWidget::stickerLongPressed,
        this, &StickerHomePage::showStickerMenu);

    // ── 底部导航栏：首页 / 生成表情 / 设置 ──
    m_bottomBar = new QskLinearBox(Qt::Horizontal, layout);
    m_bottomBar->setPanel(true);
    m_bottomBar->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    m_bottomBar->setFixedHeight(36);
    m_bottomBar->setSpacing(4);

    m_bottomHome = new QskPushButton(tr("首页"), m_bottomBar);
    m_bottomHome->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    connect(m_bottomHome, &QskAbstractButton::clicked, this, []() {
        // 首页即本页，占位无操作
    });

    m_bottomGen = new QskPushButton(tr("生成表情"), m_bottomBar);
    m_bottomGen->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    connect(m_bottomGen, &QskAbstractButton::clicked, this, [this]() {
        if (pageManager())
            pageManager()->open("stikergen");
    });

    m_bottomOnline = new QskPushButton(tr("在线表情"), m_bottomBar);
    m_bottomOnline->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    connect(m_bottomOnline, &QskAbstractButton::clicked, this, [this]() {
        if (pageManager())
            pageManager()->open("onlinepacks");
    });

    m_bottomSettings = new QskPushButton(tr("设置"), m_bottomBar);
    m_bottomSettings->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    connect(m_bottomSettings, &QskAbstractButton::clicked, this, [this]() {
        if (pageManager())
            pageManager()->open("settings");
    });

    // ── 数据 ──
    connect(StickerStore::instance(), &StickerStore::dataChanged,
        this, [this]() { refreshTabBar(); reloadActive(); });

    refreshTabBar();
    m_tabBar->setCurrentIndex(0);
    onTabChanged(m_tabBar->currentIndex());
}

// ═══════════════════════════════════════════════════════════════════
// 数据加载与 Tab 切换
// ═══════════════════════════════════════════════════════════════════

void StickerHomePage::retranslateUi()
{
    if (!m_title)
        return;

    m_title->setText(tr("😐 表情包"));
    m_pasteBtn->setText(tr("粘贴"));
    m_importBtn->setText(tr("导入"));
    m_syncBtn->setText(tr("同步"));
    m_searchLine->setPlaceholderText(tr("搜索贴纸 / emoji..."));
    m_packCombo->setPlaceholderText(tr("更多分组…"));
    m_bottomHome->setText(tr("首页"));
    m_bottomGen->setText(tr("生成表情"));
    m_bottomOnline->setText(tr("在线表情"));
    m_bottomSettings->setText(tr("设置"));
    refreshTabBar();
    updateStickerCount();
}

void StickerHomePage::refreshTabBar()
{
    m_packs = StickerStore::instance()->packs();

    // 分离「粘贴板」与其余分组
    m_comboPacks.clear();
    m_pastePackId.clear();
    for (const auto& pack : m_packs) {
        if (pack.title == QString::fromUtf8("粘贴板")) {
            m_pastePackId = pack.id;
        } else {
            m_comboPacks.append(pack);
        }
    }

    // 固定 tabs：全部 / 最近 / 粘贴板（存在时）
    const int current = m_tabBar->currentIndex();
    m_tabBar->clear(true);
    m_tabBar->addTab(tr("全部"));
    m_tabBar->addTab(tr("最近"));
    if (!m_pastePackId.isEmpty()) {
        m_tabBar->addTab(tr("粘贴板"));
    }
    m_tabBar->setCurrentIndex(qMin(current, m_tabBar->count() - 1));

    // 其余包 → 下拉：清空重建，恢复当前选择（-1 即占位符态）
    m_packCombo->clear();
    for (const auto& pack : m_comboPacks) {
        m_packCombo->addOption(pack.title);
    }
    int comboIdx = -1;
    for (int i = 0; i < m_comboPacks.size(); ++i) {
        if (m_comboPacks[i].id == m_activeTab) {
            comboIdx = i;
            break;
        }
    }
    m_packCombo->setCurrentIndex(comboIdx);
    m_packCombo->setVisible(!m_comboPacks.isEmpty());
}

void StickerHomePage::onTabChanged(int index)
{
    if (!m_searchLine->text().trimmed().isEmpty()) {
        return; // 搜索状态优先
    }

    if (index <= 0) {
        m_activeTab = QStringLiteral("");
        loadAllStickers();
    } else if (index == 1) {
        m_activeTab = QStringLiteral("__recent");
        loadRecentStickers();
    } else if (index == 2 && !m_pastePackId.isEmpty()) {
        m_activeTab = m_pastePackId;
        loadPackStickers(m_pastePackId);
    }
}

void StickerHomePage::onPackComboChanged(int index)
{
    if (!m_searchLine->text().trimmed().isEmpty()) {
        return; // 搜索状态优先
    }
    if (index < 0 || index >= m_comboPacks.size()) {
        return;
    }
    m_activeTab = m_comboPacks[index].id;
    loadPackStickers(m_activeTab);
}

void StickerHomePage::reloadActive()
{
    if (m_activeTab == QStringLiteral("__recent")) {
        loadRecentStickers();
    } else if (m_activeTab.isEmpty()) {
        loadAllStickers();
    } else {
        // 兜底：分组已被卸载/停用 → 回退「全部」，避免网格空白
        bool stillExists = false;
        for (const auto& p : m_packs) {
            if (p.id == m_activeTab) { stillExists = true; break; }
        }
        if (!stillExists) {
            m_activeTab = QStringLiteral("");
            loadAllStickers();
            return;
        }
        loadPackStickers(m_activeTab);
    }
}

void StickerHomePage::loadAllStickers()
{
    QVector<StickerBrief> all;
    for (const auto& pack : m_packs) {
        all += StickerStore::instance()->stickers(pack.id);
    }
    m_grid->setStickers(all);
    updateStickerCount();
}

void StickerHomePage::loadRecentStickers()
{
    m_grid->setStickers(StickerStore::instance()->recent(60));
    updateStickerCount();
}

void StickerHomePage::loadPackStickers(const QString& packId)
{
    m_grid->setStickers(StickerStore::instance()->stickers(packId));
    updateStickerCount();
}

void StickerHomePage::doSearch(const QString& keyword)
{
    if (keyword.isEmpty()) {
        onTabChanged(m_tabBar->currentIndex());
        return;
    }
    m_grid->setStickers(StickerStore::instance()->search(keyword));
    updateStickerCount();
}

void StickerHomePage::updateStickerCount()
{
    if (!m_countLabel || !m_grid)
        return;
    const auto& sticks = m_grid->stickers();
    QSet<QString> packs;
    for (const auto& s : sticks)
        packs.insert(s.packId);
    m_countLabel->setText(tr("%1 个 · %2 包")
        .arg(sticks.size()).arg(packs.size()));
}

// ═══════════════════════════════════════════════════════════════════
// 贴纸长按菜单 / 预览
// ═══════════════════════════════════════════════════════════════════

void StickerHomePage::showStickerMenu(const StickerBrief& brief,
                                      const QPointF& scenePos)
{
    m_ctxBrief = brief;

    for (auto* old : findChildren<QskMenu*>())
        old->deleteLater();
    for (auto* old : findChildren<MenuOverlay*>())
        old->deleteLater();

    auto* menu = new QskMenu(this);
    menu->setModal(true);
    menu->setPopupFlag(QskPopup::DeleteOnClose, false);
    m_ctxMenu = menu;
    menu->installEventFilter(this);
    const int idxCopy = menu->addOption(QskLabelData(tr("复制")));
    const int idxScaleSub = menu->addOption(QskLabelData(tr("缩放拷贝 ›")));
    m_ctxScaleSubIdx = idxScaleSub;
    const int idxPreview = menu->addOption(QskLabelData(tr("预览")));
    const int idxCopyMeta = menu->addOption(QskLabelData(tr("复制元信息")));
    const int idxEditDesc = menu->addOption(QskLabelData(tr("编辑描述简介")));
    const int idxShare = menu->addOption(QskLabelData(tr("分享")));
    const int idxDelete = menu->addOption(QskLabelData(tr("删除")));
    const int idxSearch = menu->addOption(QskLabelData(tr("搜索相似 ›")));
    m_ctxSearchSubIdx = idxSearch;

    // 菜单无边界翻转逻辑（QskMenu.cpp 仅 setPosition(origin)）——origin 靠近
    // 视图底部时剩余高度不足，菜单会被窗口裁剪。按实际尺寸向上翻转并钳制顶部。
    QPointF origin = scenePos;
    const qreal menuH = menu->sizeConstraint().height();
    if (menuH > 0) {
        const QRectF bounds = window() ? window()->contentItem()->boundingRect()
                                       : QRectF();
        if (bounds.isValid() && origin.y() + menuH > bounds.bottom()) {
            origin.setY(qMax(bounds.top(), origin.y() - menuH));
        }
    }
    menu->setOrigin(origin);

    // 兜底：打开后实测底部越界再校准一次（估算误差场景）
    connect(menu, &QskPopup::opened, this, [this, menu]() {
        if (!menu->window() || menu->height() <= 0)
            return;
        const QRectF bounds = menu->window()->contentItem()->boundingRect();
        const qreal bottom = menu->mapToScene(QPointF(0, menu->height())).y();
        if (bottom > bounds.bottom()) {
            menu->setPosition(menu->x(),
                              qMax(bounds.top(), bottom - menu->height()));
        }
    });

    // 菜单宽由 skinlet 按最长文本自动测量；补 strut 下限防被皮肤压缩
    {
        const QFontMetricsF fm(menu->effectiveFont(QskMenu::Text));
        const qreal pad = menu->paddingHint(QskMenu::Segment).left()
                        + menu->paddingHint(QskMenu::Segment).right();
        const qreal minW = qskHorizontalAdvance(fm, QString::fromUtf8("编辑描述简介"))
                         + pad + 10;
        menu->setStrutSizeHint(QskMenu::Panel, QSizeF(minW, 0));
    }

    // 缩放复制（抽为辅助，二级缩放子菜单与未来入口共用）
    auto copyScaled = [this](qreal scale, const QString& toast) {
        StickerStore::instance()->touchSticker(m_ctxBrief.id);
        const bool ok = StickerStore::instance()
            ->copyStickerScaledToClipboard(m_ctxBrief.filePath, scale);
        showToast(ok ? toast : tr("复制失败"));
    };

    connect(menu, &QskMenu::triggered, this,
        [this, menu, idxCopy, idxScaleSub, idxPreview, idxCopyMeta, idxEditDesc, idxShare, idxDelete, idxSearch](int index) {
        if (index == idxCopy) {
            StickerStore::instance()->touchSticker(m_ctxBrief.id);
            bool ok = StickerStore::instance()->copyStickerToClipboard(m_ctxBrief.filePath);
            showToast(ok ? tr("已复制")
                         : tr("复制失败"));
        } else if (index == idxScaleSub) {
            // 缩放拷贝：级联打开二级缩放子菜单（主菜单保持打开）
            openSubMenu(menu, idxScaleSub);
            return; // 主菜单不关闭，保证悬停级联的“父保持打开”语义
        } else if (index == idxPreview) {
            openPreview(m_ctxBrief);
        } else if (index == idxCopyMeta) {
            const StickerMeta meta =
                StickerStore::instance()->stickerMeta(m_ctxBrief.filePath);
            QGuiApplication::clipboard()->setText(formatStickerMeta(meta));
            showToast(tr("已复制元信息"));
        } else if (index == idxEditDesc) {
            editStickerDescription(m_ctxBrief);
        } else if (index == idxShare) {
            if (!StickerStore::instance()->shareStickerFile(m_ctxBrief.filePath)) {
                showToast(tr("桌面暂不支持分享"));
            }
        } else if (index == idxDelete) {
            const StickerBrief brief = m_ctxBrief;
            // QskMenu 自带 DeleteOnClose，close() 的 deleteLater 会在 question()
            // 的嵌套事件循环里被冲刷 → 本菜单于 QskPopup::event() 派发栈内被销毁，
            // :491 window() 打悬垂指针 SIGSEGV。确认框延迟到本次派发完成后弹出。
            QTimer::singleShot(0, this, [this, brief]() { confirmDeleteSticker(brief); });
        } else if (index == idxSearch) {
            // 搜索相似：级联打开二级搜索引擎子菜单（主菜单保持打开），触摸兜底
            openSubMenu(menu, idxSearch);
            return; // 主菜单不关闭，保证悬停级联的“父保持打开”语义
        }
        menu->close();
    });

    {
        auto* overlay = new MenuOverlay(menu);
        m_ctxOverlay = overlay;
        connect(menu, &QObject::destroyed, overlay, &QObject::deleteLater);
        // 菜单关闭即移除铺满父页的 overlay：DeleteOnClose=false 时菜单 close()
        // 不触发 destroyed，若不在此删除，overlay 残留可见会让
        // MyScrollArea::isCoveredByOverlay 恒命中 → 滚轮永久失效（重启才恢复）。
        connect(menu, &QskPopup::closed, overlay, &QObject::deleteLater);
        connect(menu, &QskPopup::closed, this, [this]() {
            if (m_ctxSub && m_ctxSub->isOpen())
                m_ctxSub->close(); // 父菜单关闭时连带收掉子菜单
            m_ctxMenu = nullptr;
            m_ctxOverlay = nullptr;
            m_ctxSubRow = -1;
            m_subCloseTimer.stop();
        });
    }
    menu->open();
}

// 主列表右键/长按菜单的二级悬停级联子菜单（模拟经典 Qt 子菜单）：当前支持
// “缩放拷贝”与“搜索相似”两个入口，内容按父行 entryIndex 分支。QskMenu 无原生
// 子菜单（0.8.0 仅有 addOption/addSeparator，cascading 只是定位模式），此处用
// 独立二级 QskMenu 模拟：父菜单保持打开，子菜单悬挂该行右缘、行中对齐；悬停
// 子菜单保持，悬停到其他父行/空白时 225ms 防抖关闭。
void StickerHomePage::openSubMenu(QskMenu* parent, int entryIndex)
{
    // 已开：同行仅按父菜单该项行的当前几何重锚；跨行（“缩放拷贝/搜索相似”
    // 两个子菜单行互斥切换）→ 先关旧子菜单再重建。
    if (m_ctxSub && m_ctxSub->isOpen()) {
        if (m_ctxSubRow == entryIndex) {
            applySubMenuAnchor(parent, m_ctxSub, entryIndex);
            return;
        }
        m_ctxSub->close();
        m_ctxSub = nullptr;
        m_ctxSubRow = -1;
    }

    // 清理上次残留（如有）；主菜单必须保留
    if (m_ctxSub)
        m_ctxSub->deleteLater();

    // 隐藏主菜单 overlay，改由父子对 overlay 统一裁决
    // （否则按下子菜单时会被主菜单 overlay 判为“菜单外”而误关父菜单）
    if (m_ctxOverlay)
        m_ctxOverlay->setVisible(false);

    auto* sub = new QskMenu(this);
    // 非 modal 且不 CloseOnPressOutside：QskPopup::updateInputGrabber 不会为其
    // 创建铺满页面的 InputGrabber，才不致于抢走父菜单的 hover（Qt 只把 hover
    // 发给最顶层接收者，模态子菜单一开父菜单即 HoverLeave 且再无 HoverMove，
    // 导致子菜单开了又关、反复重弹）。点击外部关闭由 MenuOverlay 兜底。
    sub->setModal(false);
    sub->setPopupFlag(QskPopup::CloseOnPressOutside, false);
    sub->setPopupFlag(QskPopup::DeleteOnClose, false);
    m_ctxSub = sub;
    m_ctxSubRow = entryIndex;
    sub->installEventFilter(this);

    QString fallbackW; // 预锚估算用最长文本（sizeConstraint 为空时兜底）
    auto copyScaled = [this](qreal scale, const QString& toast) {
        StickerStore::instance()->touchSticker(m_ctxBrief.id);
        const bool ok = StickerStore::instance()
            ->copyStickerScaledToClipboard(m_ctxBrief.filePath, scale);
        showToast(ok ? toast : tr("复制失败"));
    };

    if (entryIndex == m_ctxScaleSubIdx) {
        const int idx01 = sub->addOption(QskLabelData(QStringLiteral("复制x0.1")));
        const int idx025 = sub->addOption(QskLabelData(QStringLiteral("复制x0.25")));
        const int idx05 = sub->addOption(QskLabelData(QStringLiteral("复制x0.5")));
        const int idx20 = sub->addOption(QskLabelData(QStringLiteral("复制x2.0")));
        fallbackW = QStringLiteral("复制x0.25");
        connect(sub, &QskMenu::triggered, this,
            [this, sub, parent, copyScaled, idx01, idx025, idx05, idx20](int index) {
                if (index == idx01) copyScaled(0.1, tr("已复制x0.1"));
                else if (index == idx025) copyScaled(0.25, tr("已复制x0.25"));
                else if (index == idx05) copyScaled(0.5, tr("已复制x0.5"));
                else if (index == idx20) copyScaled(2.0, tr("已复制x2.0"));
                // 经典语义：选择子项后整组收拢
                parent->close();
                sub->close();
            });
    } else if (entryIndex == m_ctxSearchSubIdx) {
        // 搜索相似：四个搜索引擎。Google/Bing/Yandex 走“托管上传 → 引擎页”；
        // DuckDuckGo 无 URL 图搜接口，降级为直接打开图搜页（需手动上传）。
        const int idxGoogle = sub->addOption(QskLabelData(tr("Google")));
        const int idxBing = sub->addOption(QskLabelData(tr("Bing")));
        const int idxYandex = sub->addOption(QskLabelData(tr("Yandex")));
        const int idxDdg = sub->addOption(QskLabelData(tr("DuckDuckGo")));
        const int idxLens = sub->addOption(QskLabelData(tr("Google Lens")));
        fallbackW = QStringLiteral("Google Lens");
        const QString filePath = m_ctxBrief.filePath;
        connect(sub, &QskMenu::triggered, this,
            [this, sub, parent, idxGoogle, idxBing, idxYandex, idxDdg, idxLens, filePath](int index) {
                parent->close();
                sub->close();
                if (index == idxGoogle) {
                    startImageSearch(0, filePath);
                } else if (index == idxBing) {
                    startImageSearch(1, filePath);
                } else if (index == idxYandex) {
                    startImageSearch(2, filePath);
                } else if (index == idxDdg) {
                    showToast(tr("DuckDuckGo 需手动上传"));
                    QDesktopServices::openUrl(QUrl(
                        QStringLiteral("https://duckduckgo.com/?iax=images&ia=images")));
                } else if (index == idxLens) {
                    startImageSearch(4, filePath);
                }
            });
    }

    // 宽度下限（与主菜单同款防压缩）
    {
        const QFontMetricsF fm(sub->effectiveFont(QskMenu::Text));
        const qreal pad = sub->paddingHint(QskMenu::Segment).left()
                        + sub->paddingHint(QskMenu::Segment).right();
        sub->setStrutSizeHint(QskMenu::Panel,
            QSizeF(qskHorizontalAdvance(fm, fallbackW) + pad + 10, 0));
    }

    // 预锚：打开前即按真实尺寸（sizeConstraint）+ 父菜单已布局几何精确定位。
    // 注意 QskMenu::updateResources()（每次 polish 用 origin 覆盖 setPosition）
    // 只会采用 setOrigin 设置的坐标 —— 这里统一用 setOrigin。
    {
        const QFontMetricsF fm(sub->effectiveFont(QskMenu::Text));
        const qreal padT = sub->paddingHint(QskMenu::Segment).top();
        const qreal padB = sub->paddingHint(QskMenu::Segment).bottom();
        const qreal rowH = fm.height() + padT + padB;
        const QSizeF sc = sub->sizeConstraint();
        const qreal estW = sc.width() > 0 ? sc.width()
            : (qskHorizontalAdvance(fm, fallbackW)
               + sub->paddingHint(QskMenu::Segment).left()
               + sub->paddingHint(QskMenu::Segment).right() + 10);
        const qreal estH = sc.height() > 0 ? sc.height()
            : (5.0 * rowH + sub->paddingHint(QskMenu::Panel).top()
               + sub->paddingHint(QskMenu::Panel).bottom());

        const QPointF po = parent->mapToScene(QPointF(0.0, 0.0));
        const qreal pw = parent->width() > 0 ? parent->width()
                                             : parent->sizeConstraint().width();
        constexpr qreal gap = 4.0;

        qreal x = po.x() + pw + gap;
        qreal y = po.y();
        const QRectF cell = parent->cellRect(entryIndex);
        if (cell.isValid())
            y += cell.center().y() - (padT + rowH / 2.0);
        else
            y = (entryIndex + 0.5) * rowH - (padT + rowH / 2.0);

        const QRectF bounds = window() ? window()->contentItem()->boundingRect()
                                       : QRectF();
        if (bounds.isValid()) {
            // 右侧放不下 → 完全翻到父菜单左侧（gap 分隔），只有窗口连左侧也放
            // 不下时才钳制到窗口左缘（此时才允许最小覆盖）
            if (x + estW > bounds.right())
                x = qMax(bounds.left(), po.x() - gap - estW);
            if (y + estH > bounds.bottom())
                y = qMax(bounds.top(), bounds.bottom() - estH);
        }
        sub->setOrigin(sub->window() ? sub->parentItem()->mapFromScene(QPointF(x, y))
                                     : QPointF(x, y));
    }

    // opened：按真实几何校正锚点（行中对齐 + 越界侧翻/底部钳制）
    connect(sub, &QskPopup::opened, this,
        [this, sub, parent, entryIndex]() {
            if (sub != m_ctxSub)
                return;
            applySubMenuAnchor(parent, sub, entryIndex);
        });

    {
        auto* overlay = new MenuOverlay(sub, parent);
        connect(sub, &QObject::destroyed, overlay, &QObject::deleteLater);
        connect(sub, &QskPopup::closed, overlay, &QObject::deleteLater);
    }
    connect(sub, &QskPopup::closed, this, [this]() {
        if (m_ctxMenu && m_ctxMenu->isOpen() && m_ctxOverlay)
            m_ctxOverlay->setVisible(true);
        m_ctxSub = nullptr;
        m_ctxSubRow = -1;
    });
    sub->open();
}

// 按父菜单该项行的真实几何重锚子菜单：行中对齐（父行中线 == 子菜单首行中线）、
// 紧贴父右缘，右/底部放不下时侧翻到父左侧/上移钳制。
// 位置统一走 setOrigin（QskMenu::updateResources 每次 polish 用 origin 覆盖
// setPosition）；外力 setPosition 无效，需 setOrigin + polish 才生效。
void StickerHomePage::applySubMenuAnchor(QskMenu* parent, QskMenu* sub, int entryIndex)
{
    if (!sub->window() || !parent->isOpen())
        return;
    const QRectF pcell = parent->cellRect(entryIndex);
    const QRectF scell = sub->cellRect(0);
    if (!pcell.isValid() || !scell.isValid())
        return;

    const QPointF po = parent->mapToScene(QPointF(0.0, 0.0));
    const qreal pw = parent->width() > 0 ? parent->width()
                                         : parent->sizeConstraint().width();
    constexpr qreal gap = 4.0;

    qreal x = po.x() + pw + gap;
    qreal y = po.y() + pcell.center().y() - scell.center().y();

    const QRectF bounds = sub->window()->contentItem()->boundingRect();
    if (bounds.isValid()) {
        if (x + sub->width() > bounds.right())
            x = qMax(bounds.left(), po.x() - gap - sub->width());
        if (y + sub->height() > bounds.bottom())
            y = qMax(bounds.top(), bounds.bottom() - sub->height());
    }

    const QPointF origin = sub->parentItem()->mapFromScene(QPointF(x, y));
    if (qFuzzyCompare(origin.x(), sub->origin().x()) &&
        qFuzzyCompare(origin.y(), sub->origin().y()))
        return; // 无需重锚，避免每次鼠标移动都触发 polish

    sub->setOrigin(origin);
    sub->polish();
}

void StickerHomePage::closeSubMenu()
{
    if (m_ctxMenu && m_ctxMenu->isOpen() && m_ctxOverlay)
        m_ctxOverlay->setVisible(true);
    if (m_ctxSub && m_ctxSub->isOpen())
        m_ctxSub->close();
    m_ctxSub = nullptr;
    m_ctxSubRow = -1;
}

void StickerHomePage::scheduleSubClose()
{
    m_subCloseTimer.start();
}

void StickerHomePage::cancelSubClose()
{
    m_subCloseTimer.stop();
}

bool StickerHomePage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_ctxMenu) {
        switch (event->type()) {
        case QEvent::HoverMove: {
            cancelSubClose();
            const auto& he = *static_cast<QHoverEvent*>(event);
            const int idx = m_ctxMenu->indexAtPosition(he.position());
            if (idx == m_ctxScaleSubIdx || idx == m_ctxSearchSubIdx)
                openSubMenu(m_ctxMenu, idx);
            else if (m_ctxSub && m_ctxSub->isOpen())
                scheduleSubClose(); // 移动子菜单时可能划过其他父行，延迟关闭（sloppy）
            break;
        }
        case QEvent::HoverLeave:
            scheduleSubClose();
            break;
        default:
            break;
        }
    } else if (watched == m_ctxSub) {
        switch (event->type()) {
        case QEvent::HoverEnter:
        case QEvent::HoverMove:
            // 指针已进入子菜单：保住它，取消父菜单 HoverLeave 的防抖关闭
            cancelSubClose();
            break;
        case QEvent::HoverLeave:
            // 防抖关闭：若指针随后回到父菜单对应子行，会被父 HoverMove 取消
            scheduleSubClose();
            break;
        default:
            break;
        }
    }
    return false;
}

void StickerHomePage::openPreview(const StickerBrief& brief)
{
    for (auto* old : findChildren<StickerPreviewOverlay*>())
        old->deleteLater();
    auto* overlay = new StickerPreviewOverlay(this);
    overlay->show(brief);
    connect(overlay, &StickerPreviewOverlay::deleteRequested,
        this, [this](const StickerBrief& b) {
        // 确认框开嵌套事件循环，延迟到本次按钮事件派发完成后弹出，避免再入。
        QTimer::singleShot(0, this, [this, b]() { confirmDeleteSticker(b); });
    });
    connect(overlay, &StickerPreviewOverlay::closed,
            overlay, &QObject::deleteLater);
}

// 通用删除（软删除：DB 置 deleted=1，文件保留）——网格长按菜单与预览页共用
void StickerHomePage::confirmDeleteSticker(const StickerBrief& brief)
{
    ConfirmPopup::show(this, tr("删除贴纸"),
        tr("确定删除这张贴纸？"),
        tr("删除"), tr("取消"),
        [this, brief](bool accepted) {
            if (!accepted) {
                return;
            }
            if (StickerStore::instance()->deleteSticker(brief.id)) {
                showToast(tr("已删除"));
                for (auto* o : findChildren<StickerPreviewOverlay*>())
                    o->deleteLater();
                reloadActive();
            } else {
                showToast(tr("删除失败"));
            }
        });
}

// ═══════════════════════════════════════════════════════════════════
// 顶栏菜单：导入 / 分组管理 / 系统入口
// ═══════════════════════════════════════════════════════════════════

void StickerHomePage::showOptionsMenu(const QPointF& origin)
{
    for (auto* old : findChildren<QskMenu*>())
        old->deleteLater();
    for (auto* old : findChildren<MenuOverlay*>())
        old->deleteLater();

    auto* menu = new QskMenu(this);
    menu->setModal(true);
    menu->setPopupFlag(QskPopup::DeleteOnClose, false);

    int idxBundled = 1;
    int idxImport = 0;
    int idxPaste = 2;
    int idxManage = -1;
    menu->addOption(QskLabelData(tr("导入表情包文件夹")));
    menu->addOption(QskLabelData(tr("表情包目录")));
    menu->addOption(QskLabelData(tr("粘贴添加")));
    if (!m_packs.isEmpty()) {
        idxManage = 3;
        menu->addOption(QskLabelData(tr("分组管理")));
    }
    menu->addSeparator();
    const int idxLog = menu->addOption(QskLabelData(tr("App Log")));
    const int idxSettings = menu->addOption(QskLabelData(tr("Settings")));
    const int idxAbout = menu->addOption(QskLabelData(tr("About")));
    menu->addSeparator();
    const int idxKeep = menu->addOption(QskLabelData(
        m_keepScreenOn ? tr("✓ Keep Screen On")
                       : tr("  Keep Screen On")));
    menu->setOrigin(origin);

    connect(menu, &QskMenu::triggered, this, [this, idxBundled, idxImport, idxPaste,
        idxManage, idxLog, idxSettings, idxAbout, idxKeep](int index) {
        if (index == idxImport) {
            requestImportFolder();
        } else if (index == idxBundled) {
            pageManager()->open("bundledpacks");
        } else if (index == idxPaste) {
            requestPasteSticker();
        } else if (idxManage >= 0 && index == idxManage) {
            showPackManageMenu();
        } else if (index == idxLog) {
            pageManager()->open("logs");
        } else if (index == idxSettings) {
            pageManager()->open("settings");
        } else if (index == idxAbout) {
            pageManager()->open("about");
        } else if (index == idxKeep) {
            m_keepScreenOn = !m_keepScreenOn;
            jniKeepScreenOn(m_keepScreenOn);
            QSettings().setValue("keepScreenOn", m_keepScreenOn);
            trace_settings("home-keepScreenOn");
            qDebug() << "[StickerHomePage] keepScreenOn:" << m_keepScreenOn;
        }

        if (auto* m = qobject_cast<QskMenu*>(sender()))
            m->close();
    });

    {
        auto* overlay = new MenuOverlay(menu);
        connect(menu, &QObject::destroyed, overlay, &QObject::deleteLater);
        connect(menu, &QskPopup::closed, overlay, &QObject::deleteLater);
    }
    menu->open();
}

// 导入按钮弹出菜单：导入 / 打开目录… / 各聊天 App 图片目录（单层平级）。
// 各 App 目录取第一个存在的候选路径，不存在则自动隐藏该项（多系统安全）。
void StickerHomePage::showImportMenu(const QPointF& origin)
{
    for (auto* old : findChildren<QskMenu*>())
        old->deleteLater();
    for (auto* old : findChildren<MenuOverlay*>())
        old->deleteLater();

    auto* menu = new QskMenu(this);
    menu->setModal(true);
    menu->setPopupFlag(QskPopup::DeleteOnClose, false);

    const int idxImportFolder = menu->addOption(QskLabelData(tr("导入表情包文件夹")));
    const int idxOpenDir = menu->addOption(QskLabelData(tr("打开目录…")));
    menu->addSeparator();

    QVector<QPair<int, QString>> dirItems; // <menu index, 已存在的目录路径>
    auto addDir = [&](const QString& label, const QStringList& candidates) {
        QString path;
        for (const QString& c : candidates) {
            if (QDir(c).exists()) {
                path = c;
                break;
            }
        }
        if (!path.isEmpty()) {
            const int idx = menu->addOption(QskLabelData(label));
            dirItems.append(qMakePair(idx, path));
        }
    };

#ifdef Q_OS_ANDROID
    const QString root = QStringLiteral("/storage/emulated/0");
    addDir(tr("QQ 图片目录"), { root + "/tencent/QQ_Images",
                                root + "/Pictures/QQ" });
    addDir(tr("微信图片目录"), { root + "/tencent/MicroMsg/WeiXin",
                                 root + "/Pictures/WeiXin" });
    addDir(tr("WhatsApp 图片目录"),
        { root + "/Android/media/com.whatsapp/WhatsApp/Media/WhatsApp Images",
          root + "/WhatsApp/Media/WhatsApp Images" });
    addDir(tr("Telegram 图片目录"),
        { root + "/Telegram/Telegram Images",
          root + "/Android/media/org.telegram.messenger/Telegram/Telegram Images" });
    addDir(tr("LINE 图片目录"), { root + "/Pictures/LINE",
                                  root + "/LINE" });
    addDir(tr("Matrix 图片目录"), { root + "/Pictures/Element",
                                    root + "/Element" });
#else
    // 桌面端只列出可预测且存在的目录（Telegram Desktop 等）
    addDir(tr("Telegram 图片目录"),
        { QDir::homePath() + "/Downloads/Telegram Desktop" });
#endif

    menu->setOrigin(origin);

    connect(menu, &QskMenu::triggered, this,
        [this, idxImportFolder, idxOpenDir, dirItems](int index) {
            if (index == idxImportFolder) {
                requestImportFolder();
            } else if (index == idxOpenDir) {
                showDirPicker(false);
            } else {
                for (const auto& it : dirItems) {
                    if (it.first == index) {
                        openChatDir(it.second);
                        break;
                    }
                }
            }
            if (auto* m = qobject_cast<QskMenu*>(sender()))
                m->close();
        });

    {
        auto* overlay = new MenuOverlay(menu);
        connect(menu, &QObject::destroyed, overlay, &QObject::deleteLater);
        connect(menu, &QskPopup::closed, overlay, &QObject::deleteLater);
    }
    menu->open();
}

void StickerHomePage::showPackManageMenu()
{
    QStringList names;
    for (const auto& pack : m_packs) {
        names.append(pack.title);
    }

    SelectPopup::show(this, tr("选择分组"), names,
        [this, names](const QString& selected) {
            if (selected.isEmpty()) {
                return;
            }
            int idx = names.indexOf(selected);
            if (idx < 0 || idx >= m_packs.size()) {
                return;
            }
            m_ctxPack = m_packs[idx];

            auto* menu = new QskMenu(this);
            menu->setModal(true);
            menu->setPopupFlag(QskPopup::DeleteOnClose, false);
            menu->addOption(QskLabelData(tr("重命名")));
            menu->addOption(QskLabelData(tr("删除分组")));
            const QPointF center(this->width() / 2, this->height() / 2);
            menu->setOrigin(center);

            connect(menu, &QskMenu::triggered, this, [this](int index) {
                if (index == 0) {
                    showRenameDialog(m_ctxPack);
                } else if (index == 1) {
                    removePack(m_ctxPack);
                }
                if (auto* m = qobject_cast<QskMenu*>(sender()))
                    m->close();
            });

            {
                auto* overlay = new MenuOverlay(menu);
                connect(menu, &QObject::destroyed, overlay, &QObject::deleteLater);
                connect(menu, &QskPopup::closed, overlay, &QObject::deleteLater);
            }
            menu->open();
        });
}

void StickerHomePage::showRenameDialog(const StickerPackBrief& pack)
{
    auto* popup = new QskPopup(this);
    popup->setModal(true);
    popup->setOverlay(true);
    popup->setPopupFlag(QskPopup::DeleteOnClose, true);

    auto* box = new QskLinearBox(Qt::Vertical, popup);
    box->setPreferredWidth(280);
    box->setPreferredHeight(150);
    box->setSpacing(10);

    auto* label = new QskTextLabel(tr("重命名分组"), box);
    label->setAlignment(Qt::AlignCenter);

    auto* field = new QskTextField(pack.title, box);
    field->setPlaceholderText(tr("分组名称"));
    field->setPreferredWidth(240);
    field->setBoxShapeHint(QskTextField::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));

    auto* btnBox = new QskLinearBox(Qt::Horizontal, box);
    btnBox->setSpacing(10);

    auto* cancelBtn = new QskPushButton(tr("取消"), btnBox);
    cancelBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    connect(cancelBtn, &QskAbstractButton::clicked, popup, &QskPopup::close);

    auto* okBtn = new QskPushButton(tr("确定"), btnBox);
    okBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    connect(okBtn, &QskAbstractButton::clicked, popup, [this, field, popup]() {
        if (StickerStore::instance()->renamePack(m_ctxPack.id, field->text())) {
            showToast(tr("已重命名"));
        } else {
            showToast(tr("重命名失败"));
        }
        popup->close();
    });

    connect(popup, &QskPopup::closed, popup, &QObject::deleteLater);
    popup->open();

    field->setFocus(true);
}

// 长按菜单「编辑描述简介」：弹窗同时展示图片预览 + 元信息 + 多行/60字描述输入
void StickerHomePage::editStickerDescription(const StickerBrief& brief)
{
    auto* popup = new DescEditPopup(this);
    popup->setBrief(brief);

    connect(popup->saveButton(), &QskAbstractButton::clicked, popup,
        [this, popup, brief]() {
            const QString text = popup->validText();
            const bool ok = StickerStore::instance()
                ->setStickerDescription(brief.id, text);
            showToast(ok ? tr("已保存描述")
                         : tr("保存描述失败：%1").arg(brief.id));
            popup->close();
        });

    connect(popup, &QskPopup::closed, popup, &QObject::deleteLater);
    popup->open();

    // 真多行编辑器：等几何 polish 定形后，让内嵌 QQuickTextEdit 持主动焦点
    // （回车换行/自动折行/中文 IME 全原生，无需 setEditing 强制编辑态）
    QTimer::singleShot(80, popup->editInput(), [popup]() {
        popup->editInput()->activate();
    });
}

void StickerHomePage::removePack(const StickerPackBrief& pack)
{
    ConfirmPopup::show(this, tr("删除分组"),
        tr("确定删除「%1」及其全部贴纸？\n文件不会被删除。").arg(pack.title),
        tr("删除"), tr("取消"),
        [this, pack](bool accepted) {
            if (accepted) {
                if (StickerStore::instance()->deletePack(pack.id)) {
                    showToast(tr("已删除分组"));
                }
            }
        });
}

// ═══════════════════════════════════════════════════════════════════
// 导入
// ═══════════════════════════════════════════════════════════════════

void StickerHomePage::requestImportFolder()
{
#ifdef Q_OS_ANDROID
    showToast(tr("Android 请通过「分享到 anystik」导入图片"));
#else
    showDirPicker(true);
#endif
}

void StickerHomePage::requestPasteSticker()
{
    QString err, resurrectId;
    bool dup = false;
    if (!StickerStore::instance()->pasteFromClipboard(&err, &dup, &resurrectId)) {
        showToast(err.isEmpty() ? tr("粘贴失败") : err);
        return;
    }
    if (!resurrectId.isEmpty()) {
        ConfirmPopup::show(this, tr("重新添加贴纸"),
            tr("该图片此前已从「粘贴板」删除，要还原吗？"),
            tr("还原"), tr("取消"),
            [this, resurrectId](bool ok) {
                if (ok && StickerStore::instance()->restoreSticker(resurrectId)) {
                    showToast(tr("已还原"));
                    reloadActive();
                }
            });
        return;
    }
    showToast(dup ? tr("该图片已在「粘贴板」中")
                  : tr("已粘贴到「粘贴板」"));
}

// ── 同步进度浮动窗口：懒创建；closed → deleteLater（QPointer 自动置空）──
void StickerHomePage::ensureSyncPopup(bool reset)
{
    if (!m_syncEngine) {
        return;
    }
    if (!m_syncPopup) {
        m_syncPopup = new SyncProgressPopup(m_syncEngine, this);
        connect(m_syncPopup, &QskPopup::closed,
                m_syncPopup, &QObject::deleteLater);
        connect(m_syncPopup, &QskPopup::closed, this, [this]() {
            if (m_syncEngine && !m_syncEngine->isRunning()) {
                m_syncEngine->deleteLater();
                m_syncEngine = nullptr;
            }
        });
    }
    if (reset) {
        m_syncPopup->resetForRun();
    }
    m_syncPopup->open();
}

// ── 搜索相似（以图搜图）：托管上传 + 打开系统浏览器 ──
void StickerHomePage::startImageSearch(int engine, const QString& filePath)
{
    m_pendingEngine = engine;
    m_searchLocalPath = filePath;

    if (!m_search) {
        m_search = new ImageTmpUploader(this);
        connect(m_search, &ImageTmpUploader::progressChanged, this,
            [this](int percent, qint64 sent, qint64 total, const QString& status) {
                if (m_searchPopup) {
                    m_searchPopup->setProgress(percent, sent, total, status);
                }
            });
        connect(m_search, &ImageTmpUploader::uploaded, this,
            [this](const QString& url) {
                if (m_searchPopup) {
                    m_searchPopup->setUploadedUrl(url);
                }
                openSearchEngine(m_pendingEngine, url);
                // 上传成功 → 并发获取图片描述（通用工具，单例排队）
                m_descReqId = ImageAiUtil::instance()->fetchDescription(
                    url, m_searchLocalPath);
            });
        connect(m_search, &ImageTmpUploader::failed, this,
            [this](const QString& reason) {
                if (m_searchPopup) {
                    m_searchPopup->setFailed(reason);
                }
            });
    }

    if (!m_searchPopup) {
        m_searchPopup = new ImageSearchPopup(this);
        connect(m_searchPopup, &QskPopup::closed,
                m_searchPopup, &QObject::deleteLater);
        connect(m_searchPopup, &QskPopup::closed, this, [this]() {
            if (m_search) {
                m_search->cancel();
            }
            if (m_descReqId) {
                ImageAiUtil::instance()->cancelRequest(m_descReqId);
                m_descReqId = 0;
            }
        });
        ImageAiUtil* ai = ImageAiUtil::instance();
        connect(ai, &ImageAiUtil::descriptionReady, this,
            [this](quint64 requestId, const QString&, const QString& desc) {
                if (requestId == m_descReqId && m_searchPopup) {
                    m_searchPopup->setDescription(desc);
                    m_descReqId = 0;
                }
            });
        connect(ai, &ImageAiUtil::failed, this,
            [this](quint64 requestId, const QString&, const QString& reason) {
                if (requestId == m_descReqId && m_searchPopup) {
                    m_searchPopup->setDescriptionFailed(reason);
                    m_descReqId = 0;
                }
            });
    }

    // 文件名 + 目标引擎（DDG 走降级分支不经此处）
    static const char* kEngineNames[] = { "Google", "Bing", "Yandex", "DuckDuckGo", "Google Lens" };
    const int engineIdx = qBound(0, engine, 4);
    m_searchPopup->resetForRun(QFileInfo(filePath).fileName(),
        QString::fromLatin1(kEngineNames[engineIdx]));
    // 弹出的时序让步：父/子菜单刚 close（含淡出），下一事件循环再开浮层更稳
    QTimer::singleShot(0, this, [this]() {
        if (m_searchPopup) {
            m_searchPopup->open();
        }
    });
    m_search->upload(filePath);
}

void StickerHomePage::openSearchEngine(int engine, const QString& imageUrl)
{
    const QString enc = QString::fromLatin1(QUrl::toPercentEncoding(imageUrl));
    // 固定的图+关键词偏置（后续可替换为弹窗输入框的值）
    const QString kw = QString::fromLatin1(
        QUrl::toPercentEncoding(QStringLiteral("相似表情包")));
    QUrl url;
    switch (engine) {
        case 0: // Google
            url = QUrl(QStringLiteral(
                "https://www.google.com/searchbyimage?image_url=") + enc
                + QStringLiteral("&gl=US&hl=en&q=") + kw);
            break;
        case 1: // Bing
            url = QUrl(QStringLiteral(
                "https://www.bing.com/images/searchbyimage?cbir=sbi&imgurl=") + enc
                + QStringLiteral("&q=") + kw);
            break;
        case 4: // Google Lens
            url = QUrl(QStringLiteral(
                "https://lens.google.com/uploadbyurl?url=") + enc
                + QStringLiteral("&gl=US&hl=en&q=") + kw);
            break;
        default: // Yandex
            url = QUrl(QStringLiteral(
                "https://yandex.com/images/search?url=") + enc
                + QStringLiteral("&rpt=imageview&text=") + kw);
            break;
    }
    if (url.isValid()) {
        QDesktopServices::openUrl(url);
    }
}

// ── 轻量目录选择器（纯 QSkinny，无 QtWidgets 依赖）──
// forImport=true：用户选目录后导入；false：只把所选目录在文件管理器打开。
void StickerHomePage::showDirPicker(bool forImport)
{
    auto* picker = new QskPopup(this);
    picker->setModal(true);
    picker->setOverlay(true);
    picker->setPopupFlag(QskPopup::DeleteOnClose, true);

    auto* box = new QskLinearBox(Qt::Vertical, picker);
    box->setPreferredWidth(320);
    box->setPreferredHeight(420);
    box->setSpacing(8);

    auto* pathLabel = new QskTextLabel(box);
    pathLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    pathLabel->setWrapMode(QskTextOptions::Wrap);

    auto* listBox = new QskSimpleListBox(box);
    listBox->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Expanding);

    auto* btnBox = new QskLinearBox(Qt::Horizontal, box);
    btnBox->setSpacing(10);

    auto* upBtn = new QskPushButton(tr("↑ 上级"), btnBox);
    upBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));

    auto* cancelBtn = new QskPushButton(tr("取消"), btnBox);
    cancelBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));

    auto* okBtn = new QskPushButton(
        forImport ? tr("导入此目录") : tr("打开"), btnBox);
    okBtn->setBoxShapeHint(QskPushButton::Panel,
        QskBoxShapeMetrics(8, Qt::AbsoluteSize));
    okBtn->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    QString startPath;
#ifdef Q_OS_ANDROID
    startPath = QDir(QStringLiteral("/storage/emulated/0")).exists()
        ? QStringLiteral("/storage/emulated/0") : QDir::home().path();
#else
    startPath = QDir::home().path();
#endif
    auto* currentDir = new QDir(startPath);

    auto refresh = [listBox, pathLabel, currentDir]() {
        pathLabel->setText(currentDir->absolutePath());
        QStringList entries;
        const QFileInfoList dirs = currentDir->entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name);
        for (const auto& fi : dirs) {
            if (fi.isSymLink() && !QDir(fi.absoluteFilePath()).exists())
                continue;
            entries.append(fi.fileName() + "/");
        }
        listBox->setEntries(entries);
    };

    refresh();

    auto enterDir = [currentDir, refresh, picker](const QString& name) {
        QString clean = name.endsWith('/') ? name.chopped(1) : name;
        QDir target = *currentDir;
        if (target.cd(clean)) {
            *currentDir = target;
            refresh();
        } else {
             picker->close();
        }
    };

    connect(upBtn, &QskAbstractButton::clicked, [currentDir, refresh]() {
        if (currentDir->cdUp())
            refresh();
    });
    connect(cancelBtn, &QskAbstractButton::clicked, picker, &QskPopup::close);
    connect(okBtn, &QskAbstractButton::clicked, picker,
        [this, currentDir, picker, forImport]() {
            if (forImport) {
                QString err;
                if (StickerStore::instance()->importDirectory(
                        currentDir->absolutePath(), &err)) {
                    showToast(tr("导入成功"));
                } else {
                    showToast(err.isEmpty() ? tr("导入失败")
                                            : err);
                }
            } else {
                openChatDir(currentDir->absolutePath());
            }
            picker->close();
        });

    connect(listBox, &QskSimpleListBox::selectedEntryChanged,
        [enterDir](const QString& name) {
            if (!name.isEmpty())
                enterDir(name);
        });

    connect(picker, &QskPopup::closed, picker, &QObject::deleteLater);
    picker->open();
}

void StickerHomePage::showToast(const QString& text)
{
    ToastPopup::show(this, text);
    qDebug() << "[StickerHomePage]" << text;
}

void StickerHomePage::openStickerFolder()
{
    const QString baseDir = StickerStore::instance()->currentStickerBaseDir();
    if (baseDir.isEmpty()) {
        showToast(tr("无法获取目录路径"));
        return;
    }

#ifdef Q_OS_ANDROID
    if (!jOpenDir(baseDir))
        showToast(tr("无法打开贴纸目录"));
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(baseDir));
#endif
}

// 在文件管理器打开任意目录：Android 经 ShareActivity.openDir，桌面走本地 URL。
void StickerHomePage::openChatDir(const QString& path)
{
#ifdef Q_OS_ANDROID
    if (!path.isEmpty() && jOpenDir(path))
        return;
    showToast(tr("无法打开目录"));
#else
    if (!path.isEmpty() && QDir(path).exists() &&
        QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        return;
    showToast(tr("无法打开目录"));
#endif
}

#include "moc_stickerhomepage.cpp"
#include "stickerhomepage.moc"   // 本文件内 Q_OBJECT 局部类（DescEditPopup/MultiLineTextEdit）
