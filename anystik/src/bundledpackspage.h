#ifndef BUNDLED_PACKS_PAGE_H
#define BUNDLED_PACKS_PAGE_H

#include "page.h"
#include "stickerstore.h"

#include <QHash>
#include <QStringList>

class QskLinearBox;
class QskTextLabel;
class QskPushButton;
class QskProgressBar;
class MyScrollArea;

// 表情包目录：A 区「已下载」管理已装包(版本/MD5/大小 + 启用停用/卸载/彻底删除)，
// B 区「下载源」为代码硬编码的内置地址(获取/下载安装/继续/取消 + 进度条；地址见 kBuiltinSources[]，
// 数量由 kBuiltinSourceCount 决定，且会随内置源增减而变)。
// 元数据(url/版本commit/MD5/目录/大小/时间)由 StickerStore 持久化于 QSettings，url 用于判定
// 「内置源下载包」——这类包在 WebDAV 上传同步中被排除。
class BundledPacksPage : public Page
{
    Q_OBJECT
public:
    BundledPacksPage(QQuickItem* parent = nullptr);
    Q_INVOKABLE void retranslateUi() override;

private:
    struct SourceRow {
        QString url;
        QskPushButton* preview = nullptr;
        QskTextLabel* status = nullptr;
        QskPushButton* fetch = nullptr;
        QskPushButton* dl = nullptr;
        QskPushButton* cancel = nullptr;
        QskProgressBar* bar = nullptr;
        bool probing = false;
    };

    void buildBody();
    void addSourceRow(QskLinearBox* body, const QString& name, const QString& url,
                      const QString& previewUrl);
    void refreshSourceTexts();
    void rebuildDownloaded();
    void addPackRow(QskLinearBox* list, const StickerPackBrief& pack, bool installed);
    void refreshButtons(const SourceRow& row);

    void onProbeDone(const QString& url, qint64 size, const QString& version,
                     const QString& versionRaw, bool ok, const QString& error);
    void onProgress(const QString& url, qint64 done, qint64 total);
    void onDownloadFinished(const QString& url, bool ok, const QString& error);

    void showToast(const QString& text);
    static QString formatSize(qint64 bytes);

    MyScrollArea* m_scroll = nullptr;
    QskLinearBox* m_body = nullptr;
    QskLinearBox* m_downloadedBox = nullptr;
    QHash<QString, SourceRow> m_rows;
    QString m_busyUrl;   // 正在下载的 url（并发门闩，仅允许同时下载一个）
    QskTextLabel* m_title = nullptr;
    QskTextLabel* m_aTitle = nullptr;
    QskTextLabel* m_bTitle = nullptr;
};

#endif