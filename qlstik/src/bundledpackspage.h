#ifndef QLSTIK_BUNDLED_PACKS_PAGE_H
#define QLSTIK_BUNDLED_PACKS_PAGE_H

// 表情包目录页 —— 对标 anystik/src/bundledpackspage.{h,cpp}（只读参考，不改）。
//
// 结构 1:1：A 区「已下载（N）」管理已装包（大小/版本/MD5 + 启用停用/卸载/彻底删除），
// B 区「下载源（N）」为 stikcommon/builtinsources.h 的内置地址表（获取 / 下载安装·继续·
// 重新下载 / 取消 + 进度条）。元数据(url/版本commit/MD5)由 StickerStore 持久化在
// QSettings（downloadedPacks + downloadedPackMeta/<id>），url 用于判定「内置源下载包」。
//
// ── 与 anysk 的偏离（逐条，实测依据在注释里）──
//   1. Qsk/Qt6 → QWidget/Qt3：QskLinearBox→QBoxLayout、QskPushButton/QskTextLabel→
//      QPushButton/QLabel、QskProgressBar→QProgressBar、MyScrollArea→ScrollArea
//      （compat34.h 的 QScrollView/QScrollArea typedef；样板= settingspage.cpp:150-187）。
//   2. **PMF connect → 字符串 connect，且拼法必须逐字等于信号声明**：
//      Qt3.5 moc 不做 normalize —— sigprobe 实测 `probeDone(const QString&,qint64,…)`
//      命中，而去掉 const/& 的 `probeDone(QString,qint64,…)` 报 "No such signal"。
//      故下面 private slots 的形参写法与 stickerstore.h 的信号**同文**（含 const QString&）。
//      零参按钮回调不进 slots（Qt3 无 lambda connect）→ 调用点用 qlcomp/LambdaSlot。
//   3. tr() → 硬编码中文 QString::fromUtf8（§10 UI 铁律；settingspage 同做法，不接
//      lang/_()）→ 因此**不覆写** retranslateUi（基类空实现；切换语言后本页保持中文，
//      与设置页一致）。anysk 的 refreshSourceTexts 只服务 tr()，随之取消。
//   4. 容器：Qt3 的 QList<T>/QVector<T> 非指针类型是 QPtrList/QPtrVector（指针语义，
//      stickerlistpage.h:13 铁律）→ 行表用 std::vector<BundledSourceRow>（moc 对
//      std::vector 成员有先例 stickerlistpage.h:201）。
//   5. Qt3 缺口全走既有垫片：qint64（qglobaltype_shim.h —— compatcore34 不引入它）、
//      QSet（qset_shim.h）、QDateTime::currentMSecsSinceEpoch → qNowMsecs()
//      （qdatetime_shim.h）、QSettings::value/setValue → qsettings_shim 的
//      QSettings34（qlcomp；键名与 anystik 同名同型，共用 ~/.config/fedlet/anystik.conf，
//      落盘格式对齐 Qt6；⚠ 其无自动 sync（无析构/事件循环 sync），写后须手动 sync()）、
//      QWidget::setVisible → show()/hide() 分流（同 stickerlistpage.cpp:334）。
//   6. 进度条大小未知（anysk setIndeterminate(true)）：Qt3 QProgressBar 无该态，
//      且 LimeStyle 只在 totalSteps>0 时画填充（LimeStyle.cpp:1189）→ 双版本统一
//      「忙碌动画」：QTimer 60ms 把进度 0..99 步进 7（见 cpp onBusyTick）。
//   7. 卡片圆角/渐变（anysk QskBoxShapeMetrics(8) + lighter/darker）：Qt3 QFrame 无圆角
//      → QFrame::StyledPanel 直角；分隔线的深浅色联动（colorSchemeChanged）不移植。
//   8. 源卡顶行**拆两行**（名称+对号 / 按钮排）：420px 窗宽下单行放不下 4 个中文钮
//      （Qt3 QPushButton 默认 sizeHint 比 Qsk 紧凑钮宽）。
//   9. 长文本折行：Qt3 QLabel 无 setWordWrap（qlabel.h 实证；confirmpopup.cpp 同注记），
//      且 URL 无空格 → url/dlhint 用 qFontWidth 贪心测宽折行（cpp wrapToWidth）+ tooltip 全文。
//      ⚠ 折行宽度取固定 kWrapWidth=380（按窗口约 420–480px 定），窗口 resize 后不重算；
//      更窄窗时折行位置有偏差但不裁字，属可接受近似（anysk WrapAnywhere 按实宽动态折行）。
//  10. 卡片清空：Qt3 children() 是 const QObjectList* 且最后子对象销毁后**变 nil**
//      （stickerlistpage.cpp:249-253 实测注记）→ 先收集再 deleteLater，模式照抄
//      stickerlistpage.cpp:266-299；子代数不靠 children() 数（m_downloadedCount 自记）。
//
// 保留的 anystik 逻辑：m_busyUrl 单下载并发门闩、速度 EMA 平滑、A3「已装且未变化」判定、
// B2 cleanupAbandonedDownloads 残留清理、bundledpacks_scroll 滚动位存取、
// givenPackIdForUrl 的「url → 已下载 packId」反查。

#include "compat34.h"    // ScrollArea typedef、qNewBoxLayout/qSetMargins/qFontWidth/qSetToolTip
#include "page.h"
#include "qsettings34.h" // 滚动位 bundledpacks_scroll 读写

#ifdef QT3_BUILD
// qint64/qreal 是 Qt4 引入的别名，Qt3 只有 Q_LONG；compatcore34.h 不提供，必须自取
// （stickerstore.h:23-25 同一垫片）。
#include "qglobaltype_shim.h"
#endif

#include <qstring.h>
#include <qmap.h>   // m_urlToPackId（Qt3 QMap 无 value()，取值用 find()）
#include <vector>

class QFrame;
class QProgressBar;
class QTimer;

// 源行 —— 字段 1:1 对应 anysk BundledPacksPage::SourceRow（指针换 QWidget 系；
// 另加忙碌动画两态，见头注释 6）。显式默认构造（不写 NSDMI，moc 3.5 稳妥起见）。
struct BundledSourceRow {
    QString url;
    QString updated;
    QPushButton* preview;    // 0 = 该源无预览页
    QLabel*      check;      // 已下载对号 ✓（绿）
    QLabel*      size;       // 大小固定行（已下载/未下载一致）
    QLabel*      dlhint;     // 状态提示行（检测中/下载中/已安装/失败/版本）
    QPushButton* fetch;      // 获取 → probeRemote
    QPushButton* dl;         // 下载安装 / 继续 / 重新下载 → downloadPack
    QPushButton* cancel;     // 取消 → cancelDownload（仅下载中可见）
    QProgressBar* bar;
    bool    probing;
    bool    busyAnim;        // 大小未知 → 忙碌动画进行中
    int     busyPos;         // 忙碌动画相位 0..99
    qint64  lastSpeedBytes;  // 上次回调字节数（速度采样）
    qint64  lastSpeedMsec;   // 上次回调时刻(ms)
    double  smoothSpeedBps;  // EMA 平滑速度(B/s)

    BundledSourceRow()
        : preview(0), check(0), size(0), dlhint(0), fetch(0), dl(0), cancel(0),
          bar(0), probing(false), busyAnim(false), busyPos(0),
          lastSpeedBytes(0), lastSpeedMsec(0), smoothSpeedBps(0.0) {}
};

class BundledPacksPage : public Page {
    Q_OBJECT
public:
    explicit BundledPacksPage(QWidget* parent = 0);
    virtual ~BundledPacksPage();

    // 滚动位：onStart 延迟恢复（构造期/刚 show 时内容未定型会被钳 0）、onStop 保存。
    // 键 bundledpacks_scroll（double）与 anystik 同名同型。
    virtual void onStart();
    virtual void onStop();

private slots:
    // ⚠ 形参拼法必须与 stickerstore.h 信号逐字一致（头注释 2，sigprobe 实测）。
    void onBackClicked();
    void onRestoreScroll();
    void onBusyTick();
    void onStoreDataChanged();
    void onProbeDone(const QString& url, qint64 size, const QString& version,
                     const QString& versionRaw, bool ok, const QString& error);
    void onProgress(const QString& url, qint64 done, qint64 total);
    void onDownloadFinished(const QString& url, bool ok, const QString& error);

private:
    void buildBody();
    void addSourceRow(const QString& name, const QString& url,
                      const QString& previewUrl, const QString& updated,
                      int index);
    void rebuildDownloaded();
    void addPackRow(int index, const QString& id, const QString& title,
                    bool installed, const QVariantMap& meta);
    void updateTitles();
    int  findRow(const QString& url) const;          // -1 = 未找到
    void refreshButtons(const BundledSourceRow& row);
    void showToast(const QString& text);
    void stopBusyTimerIfIdle();
    void relayoutScroll();                       // 内容变化后重算可滚高度（Qt3：resizeContents）
    QString givenPackIdForUrl(const QString& url) const;  // 已下载源 → packId，未下载 → 空
    static QString formatSize(qint64 bytes);

    ScrollArea*          m_scroll;
    QWidget*             m_body;            // 滚动视口内的主体
    QBoxLayout*          m_bodyLayout;
    QWidget*             m_downloadedBox;   // A 区容器
    QBoxLayout*          m_downloadedLayout;
    QLabel*              m_title;
    QLabel*              m_aTitle;
    QLabel*              m_bTitle;
    QTimer*              m_busyTimer;       // 忙碌动画（仅 busyAnim 行存在时运转）
    std::vector<BundledSourceRow> m_rows;   // B 区源行（按 url 查找，行数=内置源数）
    QString              m_busyUrl;         // 正在下载的 url（并发门闩，同一时刻仅一个）
    int                  m_downloadedCount; // A 区卡片数（children() 在 Qt3 不可数，自记）
    double               m_pendingScroll;   // 待恢复滚动位；<=0 = 无待恢复
    bool                 m_scrollRetry;     // 恢复被钳位后是否已重试过一次

    // ── 临时计时埋点（定位切页卡顿；确认后可删）──
    mutable qint64       m_dbgGivenPackMsec;   // givenPackIdForUrl 累计耗时
    mutable int          m_dbgGivenPackCalls;  // givenPackIdForUrl 调用次数
    qint64               m_dbgPackSizeMsec;    // packDiskSize 累计耗时
    int                  m_dbgPackSizeCalls;   // packDiskSize 调用次数

    // 「已下载源 url → packId」内存反查表：buildBody 开头一次性从局部 QSettings34
    // 读入（用完即释放那个实例），之后 givenPackIdForUrl 只查内存，不再解析配置。
    QMap<QString, QString> m_urlToPackId;
};

#endif // QLSTIK_BUNDLED_PACKS_PAGE_H
