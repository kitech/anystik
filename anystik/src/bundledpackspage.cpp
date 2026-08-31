#include "bundledpackspage.h"
#include "myscrollarea.h"
#include "androidutils.h"

#include <QSettings>
#include <QSet>
#include <QDebug>

#include <QskLinearBox.h>
#include <QskTextLabel.h>
#include <QskPushButton.h>
#include <QskProgressBar.h>
#include <QskDialog.h>
#include <QskFontRole.h>
#include <QskTextOptions.h>
#include <QskSizePolicy.h>

namespace {

struct PackSource {
    const char* name;
    const char* url;
};

// 内置下载源（唯一的改源点）：
// 1) WhatsApp 官方示例贴纸仓库(SDK，含 67 webp+100 png 示例图)；codeload zip 为
//    chunked 无 Content-Length → 大小未知，以下载实计。
// 2) Telegram 生态唯一稳定直链整包(Animals.stickerpack，206+Range)。
// 3/4) LINE CDN 静态/动态贴纸包（本环境不可达，仅真机可验证）。
const PackSource kSources[] = {
    { "WhatsApp 官方示例贴纸 (SDK)",
      "https://codeload.github.com/WhatsApp/stickers/zip/refs/heads/main" },
    { "Animals (Telegram)",
      "https://raw.githubusercontent.com/kanelai/stickerapp/master/Animals.stickerpack" },
    { "LINE 贴纸 2938",
      "https://stickershop.line-scdn.net/stickershop/v1/product/2938/iphone/stickers@2x.zip" },
    { "LINE 动态 18060",
      "https://stickershop.line-scdn.net/stickershop/v1/product/18060/iphone/stickerpack@2x.zip" },
};

void clearBox(QskLinearBox* box)
{
    const auto items = box->childItems();
    for (auto* it : items)
        it->deleteLater();
}

QskTextLabel* makeInfoLabel(QskLinearBox* parent)
{
    auto* label = new QskTextLabel(parent);
    label->setFontRole(QskFontRole::Caption);
    label->setWrapMode(QskTextOptions::WrapAnywhere);
    return label;
}

} // namespace

BundledPacksPage::BundledPacksPage(QQuickItem* parent)
    : Page(parent)
{
    buildBody();
}

void BundledPacksPage::buildBody()
{
    setAutoLayoutChildren(true);
    auto* root = new QskLinearBox(Qt::Vertical, this);
    root->setPanel(true);
    root->setSpacing(0);

    // ── TopBar：返回 + 标题 ──
    auto* topBar = new QskLinearBox(Qt::Horizontal, root);
    topBar->setPanel(true);
    topBar->setSpacing(8);
    topBar->setPreferredHeight(56);

    auto* back = new QskPushButton("←", topBar);
    back->setPreferredSize(44, 44);
    connect(back, &QskPushButton::clicked, this, [this]() { finish(); });

    auto* title = new QskTextLabel("表情包目录", topBar);
    title->setAlignment(Qt::AlignCenter);
    title->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    topBar->addSpacer(44, 0); // 与返回键对称

    // ── 滚动主体 ──
    m_scroll = new MyScrollArea(root);
    m_scroll->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Expanding);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_body = new QskLinearBox(Qt::Vertical, m_scroll);
    m_body->setSpacing(16);
    m_scroll->setItemResizable(true);
    m_scroll->setScrolledItem(m_body);

    // ── A 区：已下载 ──
    auto* aTitle = new QskTextLabel("已下载", m_body);
    aTitle->setFontRole(QskFontRole::Title);
    m_downloadedBox = new QskLinearBox(Qt::Vertical, m_body);
    m_downloadedBox->setSpacing(8);

    // ── B 区：下载源 ──
    auto* bTitle = new QskTextLabel("下载源", m_body);
    bTitle->setFontRole(QskFontRole::Title);

    auto* store = StickerStore::instance();
    connect(store, &StickerStore::probeDone, this, &BundledPacksPage::onProbeDone);
    connect(store, &StickerStore::progressChanged, this, &BundledPacksPage::onProgress);
    connect(store, &StickerStore::downloadFinished, this, &BundledPacksPage::onDownloadFinished);
    connect(store, &StickerStore::dataChanged, this, [this]() { rebuildDownloaded(); });

    for (const auto& src : kSources)
        addSourceRow(m_body, QString::fromUtf8(src.name), QString::fromUtf8(src.url));

    // B2 清理非内置源的残留下载（.part + dlProgress 死条目）
    QStringList sourceUrls;
    for (const auto& src : kSources)
        sourceUrls << QString::fromUtf8(src.url);
    StickerStore::instance()->cleanupAbandonedDownloads(sourceUrls);

    rebuildDownloaded();
}

void BundledPacksPage::addSourceRow(QskLinearBox* body, const QString& name, const QString& url)
{
    SourceRow row;
    row.url = url;

    auto* card = new QskLinearBox(Qt::Vertical, body);
    card->setSpacing(6);

    auto* r1 = new QskLinearBox(Qt::Horizontal, card);
    r1->setSpacing(8);
    auto* nameLabel = new QskTextLabel(name, r1);
    nameLabel->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    row.dl = new QskPushButton(StickerStore::instance()->hasPartialDownload(url) ? "继续" : "下载安装", r1);

    auto* r2 = new QskLinearBox(Qt::Horizontal, card);
    r2->setSpacing(8);
    auto* urlLabel = makeInfoLabel(r2);
    urlLabel->setText(url);
    urlLabel->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    row.fetch = new QskPushButton("获取", r2);
    row.cancel = new QskPushButton("取消", r2);
    row.cancel->setVisible(false);

    row.status = makeInfoLabel(card);
    const qint64 approx =
        StickerStore::instance()->cachedApproxSize(url);
    row.status->setText(approx > 0
        ? ("大小 约 " + formatSize(approx))
        : "待检测");

    row.bar = new QskProgressBar(0.0, 1.0, card);
    row.bar->setVisible(false);

    connect(row.fetch, &QskPushButton::clicked, this, [this, url]() {
        auto it = m_rows.find(url);
        if (it == m_rows.end() || it->probing)
            return;
        it->probing = true;
        it->fetch->setEnabled(false);
        it->status->setText("检测中…");
        StickerStore::instance()->probeRemote(url);
    });

    connect(row.dl, &QskPushButton::clicked, this, [this, url]() {
        if (!m_busyUrl.isEmpty())
            return;
        m_busyUrl = url;
        auto it = m_rows.find(url);
        if (it == m_rows.end())
            return;
        it->status->setText("准备下载…");
        refreshButtons(it.value());
        StickerStore::instance()->downloadPack(url);
    });

    connect(row.cancel, &QskPushButton::clicked, this, [this, url]() {
        StickerStore::instance()->cancelDownload(url);
    });

    m_rows.insert(url, row);
    refreshButtons(row);
}

void BundledPacksPage::rebuildDownloaded()
{
    if (!m_downloadedBox)
        return;
    clearBox(m_downloadedBox);

    auto* store = StickerStore::instance();
    const QStringList ids = QSettings().value("downloadedPacks").toStringList();
    if (ids.isEmpty()) {
        auto* hint = makeInfoLabel(m_downloadedBox);
        hint->setText("尚未下载任何表情包");
        return;
    }

    QSet<QString> installed;
    for (const auto& p : store->packs(1))
        installed.insert(p.id);

    const auto all = store->packs(-1, "title ASC"); // kPacksAll
    for (const QString& id : ids) {
        for (const auto& p : all) {
            if (p.id == id) {
                addPackRow(m_downloadedBox, p, installed.contains(id));
                break;
            }
        }
    }
}

void BundledPacksPage::addPackRow(QskLinearBox* list, const StickerPackBrief& pack, bool installed)
{
    auto* card = new QskLinearBox(Qt::Vertical, list);
    card->setSpacing(6);

    auto* top = new QskLinearBox(Qt::Horizontal, card);
    top->setSpacing(8);
    auto* name = new QskTextLabel(pack.title, top);
    name->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    auto* toggle = new QskPushButton(installed ? "停用" : "启用", top);

    auto* store = StickerStore::instance();
    const QVariantMap meta = store->packMeta(pack.id);

    QString info = formatSize(store->packDiskSize(pack.id));
    const QString version = meta.value("version").toString();
    if (!version.isEmpty() && version != "未知")
        info += "  ·  " + version;
    const QString md5 = meta.value("md5").toString();
    if (md5.size() >= 8)
        info += "  ·  md5 " + md5.left(8);
    auto* infoLabel = makeInfoLabel(card);
    infoLabel->setText(info);

    auto* btns = new QskLinearBox(Qt::Horizontal, card);
    btns->setSpacing(8);
    auto* uninstall = new QskPushButton("卸载", btns);
    auto* wipe = new QskPushButton("彻底删除", btns);

    auto* dialog = qskDialog;
    connect(toggle, &QskPushButton::clicked, this, [this, store, id = pack.id, installed]() {
        if (store->setPackInstalled(id, !installed))
            showToast(installed ? "已停用" : "已启用");
    });

    connect(uninstall, &QskPushButton::clicked, this,
            [this, store, dialog, title = pack.title, id = pack.id]() {
        if (dialog->question("卸载", "卸载分组「" + title
                + "」？图片文件保留（可再次导入）。",
                QskDialog::Actions(QskDialog::Yes | QskDialog::No))
                == QskDialog::Yes) {
            if (store->uninstallPack(id, false))
                showToast("已卸载，文件保留");
        }
    });

    connect(wipe, &QskPushButton::clicked, this,
            [this, store, dialog, title = pack.title, id = pack.id]() {
        if (dialog->question("彻底删除", "删除分组「" + title
                + "」及其全部图片文件？此操作不可恢复。",
                QskDialog::Actions(QskDialog::Yes | QskDialog::No))
                == QskDialog::Yes) {
            if (store->uninstallPack(id, true))
                showToast("已彻底删除");
        }
    });
}

void BundledPacksPage::refreshButtons(const SourceRow& row)
{
    const bool busy = (m_busyUrl == row.url);
    row.cancel->setVisible(busy);
    row.fetch->setEnabled(!busy && !row.probing);
    row.dl->setEnabled(!busy && m_busyUrl.isEmpty());
    if (!busy) {
        // 已装包且未在下载时可改名「重新下载」
        const QStringList ids = QSettings().value("downloadedPacks").toStringList();
        for (const QString& id : ids) {
            const QVariantMap meta = StickerStore::instance()->packMeta(id);
            if (meta.value("url").toString() == row.url) {
                row.dl->setText("重新下载");
                break;
            }
        }
    }
}

void BundledPacksPage::onProbeDone(const QString& url, qint64 size, const QString& version,
                                   const QString& versionRaw, bool ok, const QString& error)
{
    auto it = m_rows.find(url);
    if (it == m_rows.end())
        return;
    it->probing = false;
    it->fetch->setEnabled(true);

    QString text;
    if (!ok) {
        text = "获取失败：" + error;
    } else {
        const qint64 approx =
            StickerStore::instance()->cachedApproxSize(url);
        if (size >= 0)
            text = "大小 " + formatSize(size);
        else if (approx > 0)
            text = "大小 约 " + formatSize(approx) + "（上次实测）";
        else
            text = "大小未知（以下载实计）";
        if (!version.isEmpty() && version != "未知")
            text += "  ·  版本 " + version;
        if (!versionRaw.isEmpty()) {
            // A3：仅当该包仍处于启用态时才提示「已装且未变化」
            QSet<QString> installed;
            for (const auto& p : StickerStore::instance()->packs(1))
                installed.insert(p.id);
            const QStringList ids = QSettings().value("downloadedPacks").toStringList();
            for (const QString& id : ids) {
                const QVariantMap meta = StickerStore::instance()->packMeta(id);
                if (installed.contains(id)
                        && meta.value("url").toString() == url
                        && meta.value("versionRaw").toString() == versionRaw) {
                    text += "  ·  已装且未变化";
                    break;
                }
            }
        }
    }
    it->status->setText(text);
    if (!ok)
        it->status->setText("获取失败：" + error);
    refreshButtons(it.value());
}

void BundledPacksPage::onProgress(const QString& url, qint64 done, qint64 total)
{
    auto it = m_rows.find(url);
    if (it == m_rows.end())
        return;

    QString text = "下载中  " + formatSize(done);
    if (total > 0)
        text += " / " + formatSize(total);
    else
        text += "（大小未知）";

    const bool known = (total > 0);
    it->bar->setVisible(known);
    if (known) {
        it->bar->setValueAsRatio(done / double(total));
        if (done >= total)
            text = "下载完成，正在安装…";
    }
    it->status->setText(text);
    refreshButtons(it.value());
}

void BundledPacksPage::onDownloadFinished(const QString& url, bool ok, const QString& error)
{
    m_busyUrl.clear();
    auto it = m_rows.find(url);
    if (it == m_rows.end())
        return;

    it->bar->setVisible(false);
    it->bar->setValue(0.0);
    if (ok) {
        it->status->setText("已安装：" + error);
        it->dl->setText("重新下载");
        showToast("已安装 " + error);
    } else {
        it->status->setText("下载失败：" + error);
        it->dl->setText(StickerStore::instance()->hasPartialDownload(url) ? "继续" : "下载安装");
        showToast(error);
    }
    it->probing = false;
    refreshButtons(it.value());
    rebuildDownloaded();
}

void BundledPacksPage::showToast(const QString& text)
{
    qDebug() << "[BundledPacksPage]" << text;
    showAndroidToast(text);
}

QString BundledPacksPage::formatSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024 * 1024 * 1024)
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 1) + " GB";
}