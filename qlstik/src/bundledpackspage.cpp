#include "bundledpackspage.h"

// ⚠ include 顺序约束（同 stickerstore.cpp:1-18，原因见该处注释）：Qt3 把
// QList/QVector 定义成指针容器宏，值容器 shim 必须 #undef 后补同名类，
// 故 qlist_shim/qvector_shim 必须先于 stickerstore.h 解析，否则该头里
// `QVector<StickerPackBrief> packs(...)` 会定型成 QPtrVector 声明，
// 与 shim 之后的定义是不同类型（"no declaration matches"）。
#include "qlist_shim.h"
#include "qvector_shim.h"

#include "confirmpopup.h"    // 卸载/彻底删除确认（模态 ask()）
#include "lambdaslot.h"      // 零参按钮回调（Qt3 无 lambda connect）
#include "stickerstore.h"    // StickerStore / StickerPackBrief / kBuiltinSources
#include "toastwidget.h"     // ToastWidget::show

#include "qdatetime_shim.h"  // qNowMsecs()（Qt3 无 currentMSecsSinceEpoch）
#include "qset_shim.h"       // QSet<QString>（Qt3 无 QSet）
#include "qsettings_shim.h"  // qSettingsValue*/qSettingsSet*（downloadedPacks 等）
#include "qvariant_shim.h"   // qVariantMapValue()（Qt3 QMap 无 value()）

#ifdef QT3_BUILD
#include <qcolor.h>
#include <qfont.h>
#include <qprogressbar.h>
#include <qscrollbar.h>
#include <qtimer.h>
#else
#include <QColor>
#include <QFont>
#include <QProgressBar>
#include <QScrollBar>
#include <QTimer>
#endif

namespace {

// 折行宽度：窗口 420 - 页面边距 16 - 卡片内边距 20 ≈ 384，取 380 保守值
const int kWrapWidth     = 380;
// 忙碌动画节拍/步进（头注释 6：Qt3 无 indeterminate 且 LimeStyle 只在
// totalSteps>0 时画填充 → 大小未知时用 0..99 循环冒充忙碌）
const int kBusyIntervalMs = 60;
const int kBusyStep       = 7;

// QWidget::setVisible 是 Qt4+ API；Qt3 用 show()/hide() 分流
//（stickerlistpage.cpp:334 同款）
void setVisible34(QWidget* w, bool on)
{
    if (on)
        w->show();
    else
        w->hide();
}

// QProgressBar Qt3/Qt4+ 收敛 —— 照抄 syncprogressdialog.cpp:47-58
//（inline 定义在那个 .cpp 里无法复用，故复制；注释同源：
// Qt3 setTotalSteps+setProgress / Qt4+ setRange+setValue）
inline void progressSetRange(QProgressBar* b, int total)
{
#ifdef QT3_BUILD
    b->setTotalSteps(total);
#else
    b->setRange(0, total);
#endif
}
inline void progressSetValue(QProgressBar* b, int v)
{
#ifdef QT3_BUILD
    b->setProgress(v);
#else
    b->setValue(v);
#endif
}

// 标题样式（anysk QskFontRole::Title 的近似：粗体 14pt）
void styleTitle(QLabel* l)
{
    QFont f = l->font();
    f.setBold(true);
    f.setPointSize(14);
    l->setFont(f);
}

// 提示小字灰色（anysk makeInfoLabel Caption 色；分流写法照
// qlcomp/screenshotpreview.cpp:154-172：Qt3 调色板前景 / Qt4+ 样式表）
void setHintColor(QLabel* l)
{
#ifdef QT3_BUILD
    l->setPaletteForegroundColor(QColor(140, 140, 140));
#else
    l->setStyleSheet("color: #8c8c8c;");
#endif
}

// 已下载对号 ✓ 绿色（anysk :239 QColor(120,210,140)）
void setCheckColor(QLabel* l)
{
#ifdef QT3_BUILD
    l->setPaletteForegroundColor(QColor(120, 210, 140));
#else
    l->setStyleSheet("color: #78d28c;");
#endif
}

// 长文本按测宽贪心折行 —— Qt3 QLabel 无 setWordWrap（qlabel.h 实证，
// confirmpopup.cpp 同注记）且 URL 无空格可断（头注释 9）
QString wrapToWidth(const QString& text, const QFontMetrics& fm, int maxW)
{
    if (text.isEmpty())
        return text;
    QString out;
    QString line;
    for (int i = 0; i < (int)text.length(); ++i) {
        const QChar ch = text.at(i);
        if (ch.unicode() == '\n') {
            out += line;
            out += "\n";
            line = QString();
            continue;
        }
        QString cand = line;
        cand += ch;
        if (!line.isEmpty() && qFontWidth(fm, cand) > maxW) {
            out += line;
            out += "\n";
            line = QString();
            line += ch;
        } else {
            line = cand;
        }
    }
    out += line;
    return out;
}

// 折行 + tooltip 全文（被窗口宽度截断时悬停可读全 URL）
void setTextWrapped(QLabel* l, const QString& text)
{
    const QFontMetrics fm(l->font());
    l->setText(wrapToWidth(text, fm, kWrapWidth));
    qSetToolTip(l, text);
}

// 提示小字（anysk makeInfoLabel 的等价物：灰、可横向收缩到 0 不撑宽视口）
QLabel* makeInfoLabel(QWidget* parent)
{
    QLabel* l = new QLabel(parent);
    setHintColor(l);
    // 横向 Ignored = 最小宽 0（settingspage.cpp:97 同款），长 URL 不会把
    // 视口撑出横向裁切
    l->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return l;
}

// 卡片框（anysk styleCard 的直角近似，头注释 7：Qt3 QFrame 无圆角）
QFrame* makeCard(QWidget* parent)
{
    QFrame* card = new QFrame(parent);
    card->setFrameShape(QFrame::StyledPanel);
    card->setFrameShadow(QFrame::Raised);
    return card;
}

} // namespace

BundledPacksPage::BundledPacksPage(QWidget* parent)
    : Page(parent)
    , m_scroll(0), m_body(0), m_bodyLayout(0)
    , m_downloadedBox(0), m_downloadedLayout(0)
    , m_title(0), m_aTitle(0), m_bTitle(0), m_busyTimer(0)
    , m_downloadedCount(0), m_pendingScroll(0.0), m_scrollRetry(false)
{
    buildBody();
}

BundledPacksPage::~BundledPacksPage() {}

void BundledPacksPage::buildBody()
{
    QBoxLayout* root = qNewBoxLayout(this, QBoxLayout::TopToBottom, 0, 0);
    root->setSpacing(0);
    qSetMargins(root, 0, 0, 0, 0);

    // ── TopBar：返回 + 标题（样板 settingspage.cpp:156-167；末尾 +44 占位
    //    对称 anysk `topBar->addSpacer(44, 0)`，使标题居中于返回键中线）──
    QWidget* topBar = new QWidget(this);
    QBoxLayout* tb = qNewBoxLayout(topBar, QBoxLayout::LeftToRight, 0, 0);
    tb->setSpacing(8);
    qSetMargins(tb, 4, 4, 4, 4);
    QPushButton* back = new QPushButton(QString::fromUtf8("←"), topBar);
    back->setFixedSize(44, 44);
    connect(back, SIGNAL(clicked()), this, SLOT(onBackClicked()));
    m_title = new QLabel(QString::fromUtf8("表情包目录"), topBar);
    m_title->setAlignment(Qt::AlignCenter);
    QWidget* tbSpacer = new QWidget(topBar);
    tbSpacer->setFixedWidth(44);
    tb->addWidget(back, 0);
    tb->addWidget(m_title, 1);
    tb->addWidget(tbSpacer, 0);
    root->addWidget(topBar, 0);

    // ── 滚动主体（样板 settingspage.cpp:170-187，但**不用 AutoOneFit**）──
    // ⚠ AutoOneFit 实测会自己接管 contents 尺寸、把 contentsHeight 钉成 -1
    //   （滚动条 max=0、setContentsPos/setValue 全部失效 → 滚动位永远存不进也
    //   恢复不了。/tmp/opencode/scrollprobe{2,3,5} 真机实测：Manual +
    //   resizeContents 才能得到 contentsH=1200 / vbar.max=804 / setContentsPos
    //   生效）。故 Qt3 侧走 Manual + 每次内容变化 relayoutScroll() 显式喂高度。
#ifdef QT3_BUILD
    m_scroll = new QScrollView(this);
    m_scroll->setResizePolicy(QScrollView::Manual);
    m_scroll->setHScrollBarMode(QScrollView::AlwaysOff);
    m_body = new QWidget();
    m_scroll->addChild(m_body);
#else
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_body = new QWidget();
    m_scroll->setWidget(m_body);
#endif
    m_bodyLayout = qNewBoxLayout(m_body, QBoxLayout::TopToBottom, 0, 0);
    m_bodyLayout->setSpacing(16);
    qSetMargins(m_bodyLayout, 8, 8, 8, 8);
    root->addWidget(m_scroll, 1);

    // ── A 区：已下载 ──
    m_aTitle = new QLabel(QString::fromUtf8("已下载"), m_body);
    styleTitle(m_aTitle);
    m_bodyLayout->addWidget(m_aTitle, 0);

    m_downloadedBox = new QWidget(m_body);
    m_downloadedLayout = qNewBoxLayout(m_downloadedBox, QBoxLayout::TopToBottom, 0, 0);
    m_downloadedLayout->setSpacing(8);
    m_bodyLayout->addWidget(m_downloadedBox, 0);

    // ── B 区：下载源 ──
    m_bTitle = new QLabel(QString::fromUtf8("下载源"), m_body);
    styleTitle(m_bTitle);
    m_bodyLayout->addWidget(m_bTitle, 0);

    // 忙碌动画定时器（头注释 6；start/stop 见 onBusyTick/stopBusyTimerIfIdle）
    m_busyTimer = new QTimer(this);
    connect(m_busyTimer, SIGNAL(timeout()), this, SLOT(onBusyTick()));

    // ── StickerStore 四路信号 ──
    // ⚠ 字符串 connect 的 SIGNAL/SLOT 拼法必须与 stickerstore.h 的信号元串
    //   逐字一致（头注释 2，sigprobe 实测：Qt3.5 moc 不做 normalize）。
    StickerStore* store = StickerStore::instance();
    connect(store, SIGNAL(probeDone(const QString&,qint64,const QString&,const QString&,bool,const QString&)),
            this, SLOT(onProbeDone(const QString&,qint64,const QString&,const QString&,bool,const QString&)));
    connect(store, SIGNAL(progressChanged(const QString&,qint64,qint64)),
            this, SLOT(onProgress(const QString&,qint64,qint64)));
    connect(store, SIGNAL(downloadFinished(const QString&,bool,const QString&)),
            this, SLOT(onDownloadFinished(const QString&,bool,const QString&)));
    connect(store, SIGNAL(dataChanged()), this, SLOT(onStoreDataChanged()));

    // ── B 区源行（仅 enabled；anysk :144-151）──
    int srcIndex = 0;
    for (unsigned i = 0; i < kBuiltinSourceCount; ++i) {
        if (!kBuiltinSources[i].enabled)
            continue;
        ++srcIndex;
        addSourceRow(QString::fromUtf8(kBuiltinSources[i].name),
                     QString::fromUtf8(kBuiltinSources[i].url),
                     kBuiltinSources[i].previewUrl
                         ? QString::fromUtf8(kBuiltinSources[i].previewUrl)
                         : QString(),
                     QString::fromUtf8(kBuiltinSources[i].updated),
                     srcIndex);
    }

    // ── B2：清理非内置源的残留下载（.part + dlProgress 死条目）（anysk :153-158）──
    QStringList sourceUrls;
    for (unsigned i = 0; i < kBuiltinSourceCount; ++i)
        if (kBuiltinSources[i].enabled)
            sourceUrls << QString::fromUtf8(kBuiltinSources[i].url);
    store->cleanupAbandonedDownloads(sourceUrls);

    rebuildDownloaded();
    updateTitles();
    relayoutScroll();
}

void BundledPacksPage::onStart()
{
    Page::onStart();
    // ── 恢复滚动位（anysk :164-181）──
    // 构造期布局未定型会被钳 0 → 延迟一拍（onRestoreScroll）；未到位则
    // 150ms 后再试一次（m_scrollRetry）。
    // ⚠ 不在 onStart 里 relayoutScroll：PageManager 是先 onStart() 再
    //   setStackBoxCurrent()（pagemanager.cpp:289/312），此刻页面尚未 raise、
    //   布局无效，喂进去的高度是错的。统一放到 onRestoreScroll 首行 —— 那里
    //   已在事件循环跑过一拍之后，布局已定型。
    const double scrollY = qSettingsValueDouble("bundledpacks_scroll", 0.0);
    if (scrollY > 0) {
        m_pendingScroll = scrollY;
        m_scrollRetry = false;
        QTimer::singleShot(0, this, SLOT(onRestoreScroll()));
    }
}

void BundledPacksPage::onStop()
{
    // ── 保存滚动位（键 bundledpacks_scroll，double，与 anystik 同名同型）──
    // Qt3 取 contentsY()（= QScrollView 的纵向像素偏移，等价 anysk
    // scrollPos().y()）；Qt4+ 取 verticalScrollBar()->value()（QScrollArea 无
    // contentsY）。两者都是「纵向已滚像素数」，键名/类型跨版本一致。
#ifdef QT3_BUILD
    qSettingsSetDouble("bundledpacks_scroll",
                       m_scroll ? double(m_scroll->contentsY()) : 0.0);
#else
    qSettingsSetDouble("bundledpacks_scroll",
                       m_scroll ? double(m_scroll->verticalScrollBar()->value()) : 0.0);
#endif
    Page::onStop();
}

void BundledPacksPage::onRestoreScroll()
{
    if (!m_scroll || m_pendingScroll <= 0)
        return;
    // 首次进入时布局已定型（见 onStart 注释），但内容高要主动喂给 QScrollView
    //（Qt3 Manual 政策下它不会自己从子件推），否则 max=0、setContentsPos 无效。
    relayoutScroll();
    const int want = int(m_pendingScroll);
#ifdef QT3_BUILD
    m_scroll->setContentsPos(0, want);
    const int got = m_scroll->contentsY();
#else
    QScrollBar* sb = m_scroll->verticalScrollBar();
    if (!sb)
        return;
    sb->setValue(want);
    const int got = sb->value();
#endif
    // ── 修 #2：只有真到位才收工 ──
    // 旧代码把 `value() >= maxValue()` 也算收工，导致「布局未定型、max 尚小」
    // 这一**正是该重试**的情形被判成结束 → 150ms 重试分支成了死代码。现只认
    // 「已到目标位」，未到就（仅一次）再试。
    if (got >= want) {
        m_pendingScroll = 0.0;
        return;
    }
    if (!m_scrollRetry) {
        m_scrollRetry = true;
        QTimer::singleShot(150, this, SLOT(onRestoreScroll()));
    }
}

void BundledPacksPage::onBackClicked()
{
    finish();
}

void BundledPacksPage::addSourceRow(const QString& name, const QString& url,
                                    const QString& previewUrl,
                                    const QString& updated, int index)
{
    BundledSourceRow row;
    row.url = url;
    row.updated = updated;

    QFrame* card = makeCard(m_body);
    QBoxLayout* cl = qNewBoxLayout(card, QBoxLayout::TopToBottom, 0, 0);
    cl->setSpacing(6);
    qSetMargins(cl, 10, 8, 10, 8);   // anysk styleCard QMarginsF(10,8,10,8)

    // 顶行 1：名称 + 对号（anysk 单行还塞 4 个钮，420px 放不下 → 拆两行，
    // 头注释 8）
    QWidget* r1 = new QWidget(card);
    QBoxLayout* r1l = qNewBoxLayout(r1, QBoxLayout::LeftToRight, 0, 0);
    r1l->setSpacing(8);
    QLabel* nameLabel = new QLabel(QString::fromUtf8("%1. %2").arg(index).arg(name), r1);
    nameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    row.check = new QLabel(QString::fromUtf8("✓"), r1);
    setCheckColor(row.check);
    setVisible34(row.check, !givenPackIdForUrl(url).isEmpty());
    r1l->addWidget(nameLabel, 1);
    r1l->addWidget(row.check, 0);
    cl->addWidget(r1, 0);

    // 顶行 2：预览 / 获取 / 下载安装·继续 / 取消
    QWidget* r2 = new QWidget(card);
    QBoxLayout* r2l = qNewBoxLayout(r2, QBoxLayout::LeftToRight, 0, 0);
    r2l->setSpacing(8);
    if (!previewUrl.isEmpty()) {
        row.preview = new QPushButton(QString::fromUtf8("预览"), r2);
        LambdaSlot* previewSlot = new LambdaSlot(row.preview,
            [previewUrl]() { qOpenUrl(previewUrl); });
        connect(row.preview, SIGNAL(clicked()), previewSlot, SLOT(call()));
        r2l->addWidget(row.preview, 0);
    }
    row.fetch = new QPushButton(QString::fromUtf8("获取"), r2);
    row.dl = new QPushButton(
        StickerStore::instance()->hasPartialDownload(url)
            ? QString::fromUtf8("继续") : QString::fromUtf8("下载安装"), r2);
    row.cancel = new QPushButton(QString::fromUtf8("取消"), r2);
    setVisible34(row.cancel, false);
    r2l->addWidget(row.fetch, 0);
    r2l->addWidget(row.dl, 0);
    r2l->addWidget(row.cancel, 0);
    r2l->addStretch(1);
    cl->addWidget(r2, 0);

    // URL / 更新时间 / 大小 / 状态行（anysk :254-273）
    QLabel* urlLabel = makeInfoLabel(card);
    setTextWrapped(urlLabel, url);
    cl->addWidget(urlLabel, 0);

    if (!updated.isEmpty()) {
        QLabel* updatedLabel = makeInfoLabel(card);
        setTextWrapped(updatedLabel, QString::fromUtf8("更新时间 %1").arg(updated));
        cl->addWidget(updatedLabel, 0);
    }

    row.size = makeInfoLabel(card);
    const qint64 approx = StickerStore::instance()->cachedApproxSize(url);
    setTextWrapped(row.size, approx > 0
        ? QString::fromUtf8("大小 约 %1").arg(formatSize(approx))
        : QString::fromUtf8("大小未知"));
    cl->addWidget(row.size, 0);

    row.dlhint = makeInfoLabel(card);
    setTextWrapped(row.dlhint, QString());
    cl->addWidget(row.dlhint, 0);

    // 进度条：范围 0..100 固定；确定态写实百分比，未知态由 onBusyTick
    // 跑 0..99 忙碌动画（头注释 6）。百分比文字两版本都藏（数字在 dlhint）。
    row.bar = new QProgressBar(card);
    progressSetRange(row.bar, 100);
    progressSetValue(row.bar, 0);
#ifdef QT3_BUILD
    row.bar->setPercentageVisible(false);
#else
    row.bar->setTextVisible(false);
#endif
    setVisible34(row.bar, false);
    cl->addWidget(row.bar, 0);

    QFrame* sep = new QFrame(card);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    cl->addWidget(sep, 0);

    // ── 按钮接线（anysk :282-307 的三个 lambda，改 LambdaSlot 形态）──
    LambdaSlot* fetchSlot = new LambdaSlot(row.fetch, [this, url]() {
        const int i = findRow(url);
        if (i < 0 || m_rows[i].probing)
            return;
        m_rows[i].probing = true;
        m_rows[i].fetch->setEnabled(false);
        setTextWrapped(m_rows[i].dlhint, QString::fromUtf8("检测中…"));
        StickerStore::instance()->probeRemote(url);
    });
    connect(row.fetch, SIGNAL(clicked()), fetchSlot, SLOT(call()));

    LambdaSlot* dlSlot = new LambdaSlot(row.dl, [this, url]() {
        if (!m_busyUrl.isEmpty())
            return;
        m_busyUrl = url;
        const int i = findRow(url);
        if (i < 0)
            return;
        setTextWrapped(m_rows[i].dlhint, QString::fromUtf8("准备下载…"));
        refreshButtons(m_rows[i]);
        StickerStore::instance()->downloadPack(url);
    });
    connect(row.dl, SIGNAL(clicked()), dlSlot, SLOT(call()));

    LambdaSlot* cancelSlot = new LambdaSlot(row.cancel, [url]() {
        StickerStore::instance()->cancelDownload(url);
    });
    connect(row.cancel, SIGNAL(clicked()), cancelSlot, SLOT(call()));

    // ⚠ Qsk 版 card 用 `new QskLinearBox(Qt::Vertical, body)`，构造即自挂到 body
    //   的布局；本页是 QWidget 移植，makeCard(m_body) 只设父、**不会**自动进布局，
    //   必须显式 addWidget，否则 B 区所有源行创建后不显示、也不计入滚动高度。
    m_bodyLayout->addWidget(card, 0);

    m_rows.push_back(row);
    refreshButtons(m_rows.back());
}

void BundledPacksPage::rebuildDownloaded()
{
    if (!m_downloadedBox)
        return;

    // ── 清空旧卡片 ──
    // Qt3 children() 返回 const QObjectList*，且最后一个子对象销毁后**变 nil**
    //（stickerlistpage.cpp:249-253 实测注记）→ 先收集进 std::vector 再
    // deleteLater，模式照抄 stickerlistpage.cpp:266-299（头注释 10）。
    // 必须 deleteLater 不能同步 delete：本函数可能正处在某张卡片按钮的槽栈上
    // （setPackInstalled → dataChanged → 本函数），同步删会自毁执行栈。
    // Qt3 对同一对象重复 deleteLater 安全（/tmp/opencode/dlexprobe 实测 exit 0）。
    std::vector<QWidget*> doomed;
#ifdef QT3_BUILD
    const QObjectList* kids = m_downloadedBox->children();
    if (kids) {
        for (QObjectList::Iterator it = kids->begin(); it != kids->end(); ++it) {
            QWidget* w = dynamic_cast<QWidget*>(*it);
            if (w)
                doomed.push_back(w);
        }
    }
#else
    const QObjectList kids = m_downloadedBox->children();
    for (int i = 0; i < kids.size(); ++i) {
        QWidget* w = dynamic_cast<QWidget*>(kids.at(i));
        if (w)
            doomed.push_back(w);
    }
#endif
    for (size_t i = 0; i < doomed.size(); ++i)
        doomed[i]->deleteLater();

    m_downloadedCount = 0;   // 子代数不靠 children() 数（头注释 10），自记

    StickerStore* store = StickerStore::instance();
    const QStringList ids = qSettingsValueStringList("downloadedPacks");
    if (ids.isEmpty()) {
        QLabel* hint = makeInfoLabel(m_downloadedBox);
        setTextWrapped(hint, QString::fromUtf8("尚未下载任何表情包"));
        updateTitles();
        relayoutScroll();
        return;
    }

    QSet<QString> installed;
    const QVector<StickerPackBrief> installedPacks = store->packs(1);
    for (int i = 0; i < (int)installedPacks.size(); ++i)
        installed.insert(installedPacks[i].id);

    const QVector<StickerPackBrief> all = store->packs(-1, "title ASC");
    int idx = 0;
    for (int k = 0; k < (int)ids.size(); ++k) {
        const QString id = ids[k];
        ++idx;
        for (int j = 0; j < (int)all.size(); ++j) {
            if (all[j].id == id) {
                addPackRow(idx, id, all[j].title, installed.contains(id));
                ++m_downloadedCount;
                break;
            }
        }
    }
    updateTitles();
    relayoutScroll();
}

void BundledPacksPage::relayoutScroll()
{
#ifdef QT3_BUILD
    // Qt3 的 QScrollView **不会**从子件自动推出可滚高度：用 AutoOneFit 时
    // contentsHeight 实测恒为 -1、vbar.max=0（滚动位存不进也恢复不了），改用
    // Manual 后必须显式 resizeContents 喂内容高，否则 setContentsPos 无处可去。
    //（/tmp/opencode/scrollprobe{2,3,5,9} 真机实测的全部依据。）
    //
    // 高度取 m_body 顶层布局的 sizeHint().height()（probe9 实测：结构正确时它
    // 与「逐子件 sizeHint 求和」一致，且 resizeContents 后 vbar.max 正确）。
    // 逐子件求和作兜底：Qt3 布局 sizeHint 在某些嵌套下会低估（probe7 实证），
    // 故两者取较大值更稳。
    if (!m_scroll || !m_body)
        return;
    QBoxLayout* lay = m_bodyLayout;
    int wantH = 0;
    if (lay) {
        wantH = lay->sizeHint().height();
        int sum = 0;
        bool first = true;
        QLayoutIterator it = lay->iterator();
        for (QLayoutItem* li = it.current(); li; li = ++it) {
            QWidget* w = li->widget();
            if (!w)
                continue;
            if (!first)
                sum += lay->spacing();
            first = false;
            int ph = w->sizeHint().height();
            if (ph <= 0)
                ph = w->minimumSize().height();
            sum += ph;
        }
        if (sum > wantH)
            wantH = sum;
    }
    if (wantH <= 0)
        wantH = m_body->sizeHint().height();
    // 宽度贴当前视口（vbar 出现会使视口变窄）→ 多趟收敛，避免横向滚动条
    //（probe9：3 趟后 contentsW==viewportW、hbarMax=0）。
    // ⚠ 循环内**不** processEvents：本函数可能在某按钮槽栈上被 dataChanged 间接
    //   调到，嵌套 processEvents 有重入风险；probeA 实测不发事件也能让
    //   setContentsPos 落到正确位置（只是 vbar.maxValue 要到下一轮事件循环才刷新，
    //   真实 app 事件循环永续，无影响）。
    for (int pass = 0; pass < 3; ++pass)
        m_scroll->resizeContents(m_scroll->viewport()->width(), wantH);
#else
    // Qt4+ 的 QScrollArea + setWidgetResizable(true) 自带内容高推导，无需手喂
#endif
}

void BundledPacksPage::addPackRow(int index, const QString& id,
                                  const QString& title, bool installed)
{
    QFrame* card = makeCard(m_downloadedBox);
    QBoxLayout* cl = qNewBoxLayout(card, QBoxLayout::TopToBottom, 0, 0);
    cl->setSpacing(6);
    qSetMargins(cl, 10, 8, 10, 8);   // anysk styleCard QMarginsF(10,8,10,8)

    // 顶行：名称 + 启用/停用（同源卡拆行，头注释 8）
    QWidget* top = new QWidget(card);
    QBoxLayout* tl = qNewBoxLayout(top, QBoxLayout::LeftToRight, 0, 0);
    tl->setSpacing(8);
    QLabel* name = new QLabel(QString::fromUtf8("%1. %2").arg(index).arg(title), top);
    name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QPushButton* toggle = new QPushButton(
        installed ? QString::fromUtf8("停用") : QString::fromUtf8("启用"), top);
    tl->addWidget(name, 1);
    tl->addWidget(toggle, 0);
    cl->addWidget(top, 0);

    // 信息行：大小 · 版本 · md5（anysk :388-399）
    StickerStore* store = StickerStore::instance();
    const QVariantMap meta = store->packMeta(id);
    QString info = formatSize(store->packDiskSize(id));
    const QString version = qVariantMapValue(meta, QString::fromUtf8("version")).toString();
    if (!version.isEmpty() && version != QString::fromUtf8("未知"))
        info += QString::fromUtf8("  ·  ") + version;
    const QString md5 = qVariantMapValue(meta, QString::fromUtf8("md5")).toString();
    if (md5.length() >= 8)   // Qt3 无 QString::size()，用 length() 两版同源
        info += QString::fromUtf8("  ·  md5 ") + md5.left(8);
    QLabel* infoLabel = makeInfoLabel(card);
    setTextWrapped(infoLabel, info);
    cl->addWidget(infoLabel, 0);

    // 按钮排：卸载 / 彻底删除（anysk :401-408）
    QWidget* btns = new QWidget(card);
    QBoxLayout* bl = qNewBoxLayout(btns, QBoxLayout::LeftToRight, 0, 0);
    bl->setSpacing(8);
    QPushButton* uninstallBtn = new QPushButton(QString::fromUtf8("卸载"), btns);
    QPushButton* wipeBtn = new QPushButton(QString::fromUtf8("彻底删除"), btns);
    bl->addWidget(uninstallBtn, 0);
    bl->addWidget(wipeBtn, 0);
    bl->addStretch(1);
    cl->addWidget(btns, 0);

    QFrame* sep = new QFrame(card);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    cl->addWidget(sep, 0);

    // ── 启用/停用（anysk :410-416：成功/失败各自 Toast）──
    LambdaSlot* toggleSlot = new LambdaSlot(toggle, [this, store, id, installed]() {
        if (store->setPackInstalled(id, !installed))
            showToast(installed ? QString::fromUtf8("已停用")
                                : QString::fromUtf8("已启用"));
        else
            showToast(QString::fromUtf8("操作失败"));
    });
    connect(toggle, SIGNAL(clicked()), toggleSlot, SLOT(call()));

    // ── 卸载（anysk :418-432：确认 → 保留文件卸载）──
    LambdaSlot* uninstallSlot = new LambdaSlot(uninstallBtn,
        [this, store, id, title]() {
            if (ConfirmPopup::ask(this, QString::fromUtf8("卸载"),
                    QString::fromUtf8("卸载分组「%1」？图片文件保留（可再次导入）。")
                        .arg(title),
                    QString::fromUtf8("卸载"), QString::fromUtf8("取消"))) {
                if (store->uninstallPack(id, false))
                    showToast(QString::fromUtf8("已卸载，文件保留"));
                else
                    showToast(QString::fromUtf8("卸载失败"));
            }
        });
    connect(uninstallBtn, SIGNAL(clicked()), uninstallSlot, SLOT(call()));

    // ── 彻底删除（anysk :434-448：确认 → 连文件删除，不可恢复）──
    LambdaSlot* wipeSlot = new LambdaSlot(wipeBtn, [this, store, id, title]() {
        if (ConfirmPopup::ask(this, QString::fromUtf8("彻底删除"),
                QString::fromUtf8("删除分组「%1」及其全部图片文件？此操作不可恢复。")
                    .arg(title),
                QString::fromUtf8("删除"), QString::fromUtf8("取消"))) {
            if (store->uninstallPack(id, true))
                showToast(QString::fromUtf8("已彻底删除"));
            else
                showToast(QString::fromUtf8("删除失败"));
        }
    });
    connect(wipeBtn, SIGNAL(clicked()), wipeSlot, SLOT(call()));

    // ⚠ 同 addSourceRow：QWidget 移植下 makeCard 只设父，必须显式进布局，
    //   否则 A 区已下载卡片不显示、不计入滚动高度。
    m_downloadedLayout->addWidget(card, 0);
}

void BundledPacksPage::updateTitles()
{
    if (!m_aTitle || !m_bTitle)
        return;
    m_aTitle->setText(QString::fromUtf8("已下载（%1）").arg(m_downloadedCount));
    int enabled = 0;
    for (unsigned i = 0; i < kBuiltinSourceCount; ++i)
        if (kBuiltinSources[i].enabled)
            ++enabled;
    m_bTitle->setText(QString::fromUtf8("下载源（%1）").arg(enabled));
}

int BundledPacksPage::findRow(const QString& url) const
{
    for (size_t i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].url == url)
            return (int)i;
    return -1;
}

void BundledPacksPage::refreshButtons(const BundledSourceRow& row)
{
    const bool busy = (m_busyUrl == row.url);
    setVisible34(row.cancel, busy);
    row.fetch->setEnabled(!busy && !row.probing);
    row.dl->setEnabled(!busy && m_busyUrl.isEmpty());
    if (!busy && !givenPackIdForUrl(row.url).isEmpty())
        row.dl->setText(QString::fromUtf8("重新下载"));
}

void BundledPacksPage::onProbeDone(const QString& url, qint64 /*size*/,
                                   const QString& version,
                                   const QString& versionRaw, bool ok,
                                   const QString& error)
{
    const int i = findRow(url);
    if (i < 0)
        return;
    BundledSourceRow& row = m_rows[i];
    row.probing = false;
    row.fetch->setEnabled(true);

    if (!ok) {
        setTextWrapped(row.dlhint, QString::fromUtf8("获取失败：%1").arg(error));
        refreshButtons(row);
        return;
    }

    QString text;
    if (!version.isEmpty() && version != QString::fromUtf8("未知"))
        text = QString::fromUtf8("版本 %1").arg(version);
    if (!versionRaw.isEmpty()) {
        // A3：仅当该包仍处于**启用态**时才提示「已装且未变化」（anysk :490-505）
        QSet<QString> installed;
        const QVector<StickerPackBrief> ip = StickerStore::instance()->packs(1);
        for (int k = 0; k < (int)ip.size(); ++k)
            installed.insert(ip[k].id);
        const QStringList ids = qSettingsValueStringList("downloadedPacks");
        for (int k = 0; k < (int)ids.size(); ++k) {
            const QVariantMap meta = StickerStore::instance()->packMeta(ids[k]);
            if (installed.contains(ids[k])
                    && qVariantMapValue(meta, QString::fromUtf8("url")).toString() == url
                    && qVariantMapValue(meta, QString::fromUtf8("versionRaw")).toString() == versionRaw) {
                if (!text.isEmpty())
                    text += "  ";
                text += QString::fromUtf8("已装且未变化");
                break;
            }
        }
    }
    setTextWrapped(row.dlhint, text);
    refreshButtons(row);
}

void BundledPacksPage::onProgress(const QString& url, qint64 done, qint64 total)
{
    const int i = findRow(url);
    if (i < 0)
        return;
    BundledSourceRow& row = m_rows[i];

    // ── 速度 EMA（anysk :517-529：≥200ms 采样一拍，0.6/0.4 平滑）──
    //（Qt3 垫片 qNowMsecs / Qt4+ 原生 —— davbisync.cpp:381-383 同款分流）
#ifdef QT3_BUILD
    const qint64 now = qNowMsecs();
#else
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
#endif
    if (row.lastSpeedMsec > 0) {
        const qint64 dMs = now - row.lastSpeedMsec;
        const qint64 dB = done - row.lastSpeedBytes;
        if (dMs >= 200 && dB >= 0) {
            const double inst = double(dB) * 1000.0 / double(dMs);
            row.smoothSpeedBps = row.smoothSpeedBps > 0
                ? row.smoothSpeedBps * 0.6 + inst * 0.4
                : inst;
        }
    }
    row.lastSpeedMsec = now;
    row.lastSpeedBytes = done;

    QString text = QString::fromUtf8("下载中  %1").arg(formatSize(done));
    if (total > 0) {
        text += " / " + formatSize(total);
    } else {
        const qint64 approx = StickerStore::instance()->cachedApproxSize(url);
        if (approx > 0)
            text += QString::fromUtf8("（大小未知，约 %1）").arg(formatSize(approx));
        else
            text += QString::fromUtf8("（大小未知）");
    }
    if (row.smoothSpeedBps > 0.01)
        text += QString::fromUtf8(" · ") + formatSize(qint64(row.smoothSpeedBps)) + "/s";

    setVisible34(row.bar, true);
    if (total > 0) {
        // 确定态：实百分比（范围固定 0..100，见 addSourceRow）
        row.busyAnim = false;
        const int pct = int(done >= total ? 100 : (done * 100) / total);
        progressSetValue(row.bar, pct);
        if (done >= total) {
            text = QString::fromUtf8("下载完成，正在安装…");
            row.smoothSpeedBps = 0;
        }
    } else {
        // 未知态：忙碌动画（anysk setIndeterminate(true) 的近似，头注释 6）
        if (!row.busyAnim) {
            row.busyAnim = true;
            row.busyPos = 0;
        }
        if (!m_busyTimer->isActive())
            m_busyTimer->start(kBusyIntervalMs);
    }
    setTextWrapped(row.dlhint, text);
    refreshButtons(row);
    stopBusyTimerIfIdle();
}

void BundledPacksPage::onDownloadFinished(const QString& url, bool ok,
                                           const QString& error)
{
    m_busyUrl = QString();   // Qt3 无 QString::clear()
    const int i = findRow(url);
    if (i < 0)
        return;
    BundledSourceRow& row = m_rows[i];

    setVisible34(row.bar, false);
    row.busyAnim = false;
    progressSetValue(row.bar, 0);
    if (ok) {
        setTextWrapped(row.dlhint, QString::fromUtf8("已安装：%1").arg(error));
        row.dl->setText(QString::fromUtf8("重新下载"));
        showToast(QString::fromUtf8("已安装 %1").arg(error));
    } else {
        setTextWrapped(row.dlhint, QString::fromUtf8("下载失败：%1").arg(error));
        row.dl->setText(StickerStore::instance()->hasPartialDownload(url)
            ? QString::fromUtf8("继续") : QString::fromUtf8("下载安装"));
        showToast(error);
    }
    row.probing = false;
    refreshButtons(row);
    rebuildDownloaded();
    stopBusyTimerIfIdle();
}

void BundledPacksPage::onStoreDataChanged()
{
    rebuildDownloaded();
}

void BundledPacksPage::onBusyTick()
{
    for (size_t i = 0; i < m_rows.size(); ++i) {
        BundledSourceRow& row = m_rows[i];
        if (!row.busyAnim || !row.bar)
            continue;
        row.busyPos = (row.busyPos + kBusyStep) % 100;
        progressSetValue(row.bar, row.busyPos);
    }
    stopBusyTimerIfIdle();
}

void BundledPacksPage::stopBusyTimerIfIdle()
{
    if (!m_busyTimer || !m_busyTimer->isActive())
        return;
    for (size_t i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].busyAnim)
            return;
    m_busyTimer->stop();
}

void BundledPacksPage::showToast(const QString& text)
{
    ToastWidget::show(this, text);
}

QString BundledPacksPage::givenPackIdForUrl(const QString& url) const
{
    const QStringList ids = qSettingsValueStringList("downloadedPacks");
    for (int i = 0; i < (int)ids.size(); ++i) {
        const QVariantMap meta = StickerStore::instance()->packMeta(ids[i]);
        if (qVariantMapValue(meta, QString::fromUtf8("url")).toString() == url)
            return ids[i];
    }
    return QString();
}

QString BundledPacksPage::formatSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < qint64(1024) * 1024 * 1024)
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 1) + " GB";
}
