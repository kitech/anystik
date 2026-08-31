#include <QGuiApplication>
#include <QIcon>
#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QStandardPaths>
#include <QScreen>
#include <QEvent>
#include <QKeyEvent>
#include <QElapsedTimer>
#include <QTimer>
#include <QShortcut>
#include <QSslSocket>
#include <QQuickItem>
#include <QskDialog.h>
#include <QskSkinManager.h>
#include <QskFontRole.h>
#include <QskWindow.h>
#include <QskLinearBox.h>
#include <QskStackBox.h>
#include <QskStackBoxAnimator.h>
#include <QskSkin.h>
#include <QskComboBox.h>
#include <QskSwitchButton.h>
#include "loginpage.h"
#include "settingspage.h"
#include "aboutpage.h"
#include "logpage.h"
#include "keepalive.h"
#include "networkmonitor.h"
#include "pushhandler.h"
#include "phonemonitor.h"
#include "androidutils.h"
#include "pagemanager.h"
#include "shareintentreceiver.h"
#include "stickerhomepage.h"
#include "bundledpackspage.h"
#include "stickerstore.h"
#include "android_tls_bootstrap.h"
#include "davobfus.h"

#include <memory>
#include <thread>

extern "C" void gosoMainLoop();
extern "C" void csoMainLoop();

namespace {

class BackButtonFilter : public QObject
{
public:
    BackButtonFilter(PageManager* pm, QObject* parent = nullptr)
        : QObject(parent), m_pageManager(pm) {}

protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Back) {
                event->accept();

                if (m_pageManager->depth() > 1) {
                    m_pageManager->back();
                } else if (m_backTimer.isValid() && m_backTimer.elapsed() < 2000) {
                    QCoreApplication::quit();
                } else {
                    m_backTimer.start();
                    showAndroidToast(QStringLiteral("Press again to exit"));
                }
                return true;
            }
        }
        return false;
    }

private:
    PageManager* m_pageManager;
    QElapsedTimer m_backTimer;
};

void applyAndroidFonts(const std::shared_ptr<FontSizes>& fontSizes)
{
#ifdef Q_OS_ANDROID
    auto* s = qskSkinManager->skin();
    if (!s) return;
    auto makeFont = [](int pt) { QFont f; f.setPointSizeF(pt); return f; };
    s->setFont({QskFontRole::Body, QskFontRole::Normal},    makeFont(fontSizes->body));
    s->setFont({QskFontRole::Title, QskFontRole::Normal},   makeFont(fontSizes->title));
    s->setFont({QskFontRole::Caption, QskFontRole::Normal}, makeFont(fontSizes->caption));
    QGuiApplication::setFont(makeFont(fontSizes->global));
    qDebug() << "[anystik] fonts re-applied (default family, CJK via font merging)";
#else
    Q_UNUSED(fontSizes)
#endif
}

static int countItems(const QQuickItem* item)
{
    if (!item) return 0;
    int count = 1;
    const auto children = item->childItems();
    for (const auto* c : children)
        count += countItems(c);
    return count;
}

static QTimer* s_statsTimer = nullptr;
static void toggleStatsTimer(QskWindow* win)
{
    if (s_statsTimer && s_statsTimer->isActive()) {
        s_statsTimer->stop();
        win->setTitle("anystik");
        qDebug() << "[anystik] stats timer stopped";
        return;
    }
    if (!s_statsTimer) {
        s_statsTimer = new QTimer(win);
        QObject::connect(s_statsTimer, &QTimer::timeout, [win]() {
            int n = countItems(win->contentItem());
            win->setTitle(QString("anystik | items: %1").arg(n));
        });
    }
    s_statsTimer->start(3000);
    int n = countItems(win->contentItem());
    win->setTitle(QString("anystik | items: %1").arg(n));
    qDebug() << "[anystik] stats timer started";
}

} // namespace

int main(int argc, char* argv[]) {
#ifdef Q_OS_ANDROID
    // 必须在任何 TLS 后端探测之前完成引导与日志开关：
    android_tls_bootstrap();          // B：绝对路径预加载 libcrypto_3/libssl_3
    qputenv("QSG_RENDER_LOOP", "basic");
    // 6.7.3 后端拼接规则是 "ssl" + 后缀（不自动插下划线）：
    // 填 "3" 会拼成 libssl3.so -> 探测全失败 -> "TLS initialization failed"。
    // 必须带下划线 "_3" 以匹配打包的 libssl_3.so / libcrypto_3.so（A）。
    qputenv("ANDROID_OPENSSL_SUFFIX", "_3");
    qputenv("QT_LOGGING_RULES",        // C：诊断开关（logcat 可看探测路径）
            "qt.tlsbackend.ossl.debug=true;qt.network.ssl.debug=true");
#endif

    QGuiApplication app(argc, argv);

    installLogHandler();

    QCoreApplication::setOrganizationName("fedlet");
    QCoreApplication::setApplicationName("anystik");

    // 预告填入：把内置源 approxSize(含 -1) 一次性写入 dlProgress 元数据（零网络）
    StickerStore::instance()->seedBuiltinApproxSizes();

#ifdef Q_OS_ANDROID
    // C：启动即打印 TLS 后端实际状态
    qDebug() << "[anystik] QSslSocket::supportsSsl() ="
             << QSslSocket::supportsSsl()
             << "; sslLibraryVersionString ="
             << QSslSocket::sslLibraryVersionString();
#else
    QString iconPath = QCoreApplication::applicationDirPath() + "/../app_icon.png";
    app.setWindowIcon(QIcon(iconPath));
#endif

#ifdef Q_OS_ANDROID
    const auto dataDir = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    qDebug() << "[anystik] AppLocalDataLocation:" << dataDir;

    const auto libPaths = QCoreApplication::libraryPaths();
    qDebug() << "[anystik] libraryPaths:" << libPaths;

    for (const auto& libPath : libPaths) {
        QDir srcDir(libPath);
        qDebug() << "[anystik] checking path:" << libPath
                 << "exists=" << srcDir.exists()
                 << "has_qskinny=" << srcDir.exists("libqskinny_arm64-v8a.so");

        if (!srcDir.exists("libqskinny_arm64-v8a.so"))
            continue;

        QDir().mkpath(dataDir + "/skins");
        for (const auto& name : {"libfusionskin", "libfluent2skin",
                                  "libmaterial3skin"}) {
            const QString fileName = name + QString("_arm64-v8a.so");
            const QString dst = dataDir + "/skins/" + fileName;

            if (QFile::exists(dst)) {
                qDebug() << "[anystik] already exists, skip:" << dst;
                continue;
            }

            const QString src = srcDir.absoluteFilePath(fileName);
            qDebug() << "[anystik] copying" << src << "->" << dst;

            if (QFile::copy(src, dst)) {
                QFile(dst).setPermissions(
                    QFile::ReadOwner | QFile::ExeOwner |
                    QFile::ReadUser  | QFile::ExeUser);
                qDebug() << "[anystik] copy OK";
            } else {
                qWarning() << "[anystik] copy FAILED:" << src;
            }
        }
        break;
    }
    QCoreApplication::addLibraryPath(dataDir);
    qDebug() << "[anystik] added libraryPath:" << dataDir;
#endif

    qDebug() << "[anystik] calling setPluginPaths...";
    {
        QStringList paths = QCoreApplication::libraryPaths();
#ifndef Q_OS_ANDROID
        paths.prepend("/opt/qt/qskinny/lib/qskinny/plugins");
#endif
        qskSkinManager->setPluginPaths(paths);
    }

    const auto availableSkins = qskSkinManager->skinNames();
    qDebug() << "[anystik] available skins:" << availableSkins;

    qskSkinManager->setSkin("Fusion");
    qDebug() << "[anystik] skin OK:" << qskSkinManager->skinName();
    qDebug() << "[anystik] DPI:" << QGuiApplication::primaryScreen()->logicalDotsPerInch()
             << "dpr:" << QGuiApplication::primaryScreen()->devicePixelRatio();

    // ── Shared font state ──
    auto fontSizes = std::make_shared<FontSizes>();
    fontSizes->body    = 21;
    fontSizes->title   = 29;
    fontSizes->caption = 19;
    fontSizes->global  = 16;

    auto fontApplier = [fontSizes]() { applyAndroidFonts(fontSizes); };
    fontApplier();

    SettingsPage::sharedFontSizes = fontSizes;
    SettingsPage::applyAndroidFonts = fontApplier;

    QObject::connect(qskSkinManager, &QskSkinManager::skinChanged,
        &app, fontApplier);

    // ── Root layout ──
    auto* rootBox = new QskLinearBox(Qt::Vertical);
    rootBox->setPanel(true);
    auto* stackBox = new QskStackBox(rootBox);
    stackBox->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Expanding);
    auto* defaultAnimator = new QskStackBoxAnimator4(stackBox);
    stackBox->setAnimator(defaultAnimator);

    // ── PageManager ──
    auto* pageManager = new PageManager(stackBox);

    // 分享导入完成后跳转贴纸主页
    registerShareNavigator([pageManager]() {
        pageManager->open("stickerhome");
    });

    // ── Restore persisted settings and apply to global state ──
    {
        QSettings s;

        int skinIdx = s.value("skin", 0).toInt();
        if (skinIdx > 0) {
            static const char* names[] = {"Fusion", "Fluent2", "Material3"};
            if (skinIdx < 3) {
                qskSkinManager->setSkin(names[skinIdx]);
            }
        }
        bool dark = s.value("darkMode", false).toBool();
        if (dark) {
            auto* skin = qskSkinManager->skin();
            if (skin) skin->setColorScheme(QskSkin::DarkScheme);
        }
        int fontIdx = s.value("fontScale", 1).toInt();
        if (fontIdx >= 0 && fontIdx < 4) {
            static const int sizes[][4] = {
                {16, 22, 14, 12},
                {21, 29, 19, 16},
                {28, 39, 25, 21},
                {35, 48, 32, 27},
            };
            fontSizes->body    = sizes[fontIdx][0];
            fontSizes->title   = sizes[fontIdx][1];
            fontSizes->caption = sizes[fontIdx][2];
            fontSizes->global  = sizes[fontIdx][3];
            fontApplier();
        }
        int transIdx = s.value("transition", 3).toInt();
        if (transIdx != 3) {
            QskStackBoxAnimator* a = nullptr;
            switch (transIdx) {
                case 0: a = new QskStackBoxAnimator1(stackBox); break;
                case 1: a = new QskStackBoxAnimator2(stackBox); break;
                case 2: a = new QskStackBoxAnimator3(stackBox); break;
            }
            if (a) stackBox->setAnimator(a);
        }
    }

    // ── Register pages ──
    pageManager->registerPage("login", []() -> Page* {
        return new LoginPage();
    }, {CachePolicy::Transient, LaunchMode::Standard});

    pageManager->registerPage("settings", []() -> Page* {
        return new SettingsPage();
    }, {CachePolicy::Permanent, LaunchMode::Standard});

    pageManager->registerPage("about", []() -> Page* {
        return new AboutPage();
    }, {CachePolicy::Transient, LaunchMode::Standard});

    pageManager->registerPage("logs", []() -> Page* {
        return new LogPage();
    }, {CachePolicy::Transient, LaunchMode::Standard});

    pageManager->registerPage("stickerhome", []() -> Page* {
        return new StickerHomePage();
    }, {CachePolicy::Permanent, LaunchMode::SingleInstance});

    pageManager->registerPage("bundledpacks", []() -> Page* {
        return new BundledPacksPage();
    }, {CachePolicy::Transient, LaunchMode::Standard});

    // ── Window ──
    QskWindow window;
    window.setTitle("anystik");
    window.addItem(rootBox);

#ifdef Q_OS_ANDROID
    window.show();
    qDebug() << "[anystik] window size:" << window.size()
             << "contentItem size:" << window.contentItem()->size()
             << "isExposed:" << window.isExposed();
#else
    window.setPreferredSize({420, 780});
    window.show();
#endif

    // ── Start with sticker home page ──  (window 已就绪，控件可正常解析 skin hint)
    pageManager->open("stickerhome");

    // 处理 Android 启动时（Qt 就绪前）到达的分享意图
    drainPendingShareIntents();

#ifdef Q_OS_ANDROID
    window.update();
#endif

    // ── Application lifecycle → sync QSettings ──
    QObject::connect(&app, &QGuiApplication::applicationStateChanged,
        [](Qt::ApplicationState state) {
            if (state == Qt::ApplicationInactive
             || state == Qt::ApplicationSuspended) {
                qDebug() << "[anystik] applicationState:" << state
                         << "-> syncing QSettings";
                QSettings().sync();
            }
#ifdef Q_OS_ANDROID
            if (state == Qt::ApplicationActive) {
                NetworkMonitor::checkNetwork();
            }
#endif
        });

#ifndef Q_OS_ANDROID
    {
        auto* sc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), &window);
        QObject::connect(sc, &QShortcut::activated,
            []{ SettingsPage::changeFontScale(+1); });

        sc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_G), &window);
        QObject::connect(sc, &QShortcut::activated,
            []{ SettingsPage::changeFontScale(-1); });

        sc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), &window);
        QObject::connect(sc, &QShortcut::activated,
            [&window]{ toggleStatsTimer(&window); });
    }
#endif

    app.installEventFilter(new BackButtonFilter(pageManager));

#ifdef Q_OS_ANDROID
    KeepAlive::start();
    NetworkMonitor::start();
    PushHandler::start();
    PhoneMonitor::start();

    // distributor 选择对话框（仅启动时、多于1个分发器时弹出）
    QObject::connect(PushHandler::instance(), &PushHandler::distributorsFound,
        [&window](const QStringList& distributors) {
            if (distributors.size() <= 1) return;
            QStringList displayNames;
            for (const auto& d : distributors) {
                displayNames.append(d);
            }
            QskDialog* dialog = qskDialog;
            dialog->setTransientParent(&window);
            QString selected = dialog->select("选择推送服务", displayNames);
            if (!selected.isEmpty()) {
                int idx = displayNames.indexOf(selected);
                if (idx >= 0) {
                    PushHandler::instance()->selectDistributor(distributors[idx]);
                }
            }
        });

    // 注册失败
    QObject::connect(PushHandler::instance(), &PushHandler::registrationFailed,
        [](const QString& reason) {
            QString toastMsg;
            if (reason.contains(QString::fromUtf8("未安装")) || reason.contains("not installed")) {
                toastMsg = QString::fromUtf8("⚠️ %1\n请安装后重新打开应用").arg(reason);
            } else if (reason.contains(QString::fromUtf8("未找到")) || reason.contains("no distributor")
                       || reason.contains("getDistributors")) {
                toastMsg = QString::fromUtf8("⚠️ 未检测到推送服务\n请安装 ntfy (UnifiedPush) 后重试");
            } else if (reason.contains(QString::fromUtf8("超时"))) {
                toastMsg = QString::fromUtf8("⚠️ %1\n请打开 ntfy 后重试").arg(reason);
            } else if (reason.contains(QString::fromUtf8("启动失败")) || reason.contains("FAILED")) {
                toastMsg = QString::fromUtf8("⚠️ 推送服务启动失败\n请检查 ntfy 是否在后台运行");
            } else {
                toastMsg = QString::fromUtf8("⚠️ Push 注册失败: %1").arg(reason);
            }
            showAndroidToast(toastMsg);
        });

    // endpoint 注册成功
    QObject::connect(PushHandler::instance(), &PushHandler::pushReceived,
        [](const QString& endpoint, const QString& instance) {
            qDebug() << "[PushHandler] endpoint:" << endpoint;
        });

    // 收到推送消息
    QObject::connect(PushHandler::instance(), &PushHandler::pushMessage,
        [](const QByteArray& message, const QString& instance) {
            QString preview = QString::fromUtf8(message).left(200);
            qDebug() << "[PushHandler] push received, size:" << message.size()
                     << "content:" << preview;
            showAndroidToast(QStringLiteral("推送: %1").arg(preview));
        });

    // 触发注册流程
    {
        PushProviderType type = static_cast<PushProviderType>(
            QSettings().value("pushProvider", 0).toInt());
        if (type == PushProviderType::UnifiedPush) {
            PushHandler::instance()->registerDevice();
        } else {
            qDebug() << "[anystik] push provider type:" << static_cast<int>(type)
                     << "- not registering (Gotify not yet implemented)";
        }
    }

    qDebug() << "[anystik] KeepAlive service started";
#endif

    std::thread(gosoMainLoop).detach();
    std::thread(csoMainLoop).detach();
    qDebug() << "[anystik] extras threads started";

    // ── 下载安装链路自检（ANYSTIK_SELFTEST=1，offscreen 冒烟用）──
    if (qEnvironmentVariableIntValue("ANYSTIK_SELFTEST") > 0) {
        const QString url = QString::fromUtf8(
            "https://codeload.github.com/WhatsApp/stickers/zip/refs/heads/main");
        QTimer::singleShot(500, [url]() {
            auto* store = StickerStore::instance();
            if (!store->ensureInit()) {
                qCritical() << "[selftest] Storage init failed";
                QCoreApplication::exit(1);
                return;
            }
            QObject::connect(store, &StickerStore::progressChanged,
                [](const QString& u, qint64 done, qint64 total) {
                    qInfo() << "[selftest] progress" << u << done << '/' << total;
                });
            QObject::connect(store, &StickerStore::downloadFinished,
                [](const QString& u, bool ok, const QString& msg) {
                    qInfo() << "[selftest] finished" << u << ok << msg;
                    QCoreApplication::exit(ok ? 0 : 1);
                });
            qInfo() << "[selftest] downloading" << url;
            store->downloadPack(url);
        });
    }

    // dav 鉴权 key 演示输出（混淆后运行时解码，event loop 启动前打印）
    qDebug() << "[davobfus] auth key:" << davObfusKey();

    return app.exec();
}
