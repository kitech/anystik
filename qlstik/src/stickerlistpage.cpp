#include "stickerlistpage.h"
#include "compatcore34.h"
#include "config.h"
#include "storage.h"              // Storage::instance().init() + stickerDb()
#include "sticker_db.h"           // StickerRow / StickerPackRow / kPacksAll
#include "qstandardpaths_shim.h"
#include "qimagereader_shim.h"
#include "placeholderlineedit.h"   // Qt3 QLineEdit 无 placeholder → qlcomp 的替代
#include "toastwidget.h"           // 单击复制的提示（anysk stickerhomepage.cpp:578）

#include <algorithm>

#ifdef QT3_BUILD
#include <qpushbutton.h>
#include <qbuttongroup.h>
#include <qcombobox.h>
#include <qlabel.h>
#include <qlineedit.h>
#include <qpainter.h>
#include <qfont.h>
#include <qfontmetrics.h>
#include <qpixmap.h>
#include <qfile.h>
#include <qdir.h>
#include <qcstring.h>
#include <qevent.h>
#include <qapplication.h>
#include <qclipboard.h>
#else
#include <QPushButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPaintEvent>
#include <QImageReader>
#include <QFont>
#include <QFontMetrics>
#include <QPixmap>
#include <QFile>
#include <QDir>
#include <QEvent>
#include <QApplication>
#include <QClipboard>
#endif

// ═══════════════════════════════════════════════════════════════════
// 缩略图懒解码 + 缓存（照搬 anysk stickerlist.cpp:121-138）
// ═══════════════════════════════════════════════════════════════════
// ⚠ 与 anysk 的唯一**有意**差异（原因见头文件「Qt3 绘图/图像层实测结论」）：
//   缓存 QPixmap 而非 QImage：anysk 是 QQuickItem 走 scene graph，本页是
//   QWidget::paintEvent，QPixmap 才是 Qt3 的自然选择。
// 缩放语义与 anysk 对齐：shim 已提供 setScaledSize()，read() 直接返回 152px 缩略
// 图；动画只解当前帧（见 qimagereader_shim.cpp）。
// 缓存策略（上限 400、超限整体 clear、setStickers 先清）与 anysk 逐条一致。
//
// ⚠⚠ 缓存**不能**是本文件的 static：Qt3 的 QMapPrivate 构造时会默认构造一个
//   哨兵 QPixmap（qmap.h:436 → QMapNode → QPixmap::QPixmap），QPixmap 是
//   QPaintDevice，而文件作用域对象在静态初始化期（main() 之前、QApplication
//   之前）构造 → 直接 qFatal「Must construct a QApplication before a
//   QPaintDevice」并 SIGABRT。
//   故缓存在 StickerGridWidget::m_tileImageCache（头文件有完整说明）。

// 缩略图缩放：Qt3 是 smoothScale（返回**新副本**），Qt4+ 是 scaled(SmoothTransformation)。
// 只在 decodedTileImage 里对每张贴纸调用一次，不在 paintEvent 热路径上。
static QImage scaleThumb(const QImage& src, int px)
{
#ifdef QT3_BUILD
    return src.smoothScale(px, px);
#else
    return src.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
#endif
}

// cache 由调用方（StickerGridWidget 成员）传入，理由见上方关于 static 的说明
static QPixmap decodedTileImage(const QString& filePath,
                                QMap<QString, QPixmap>& cache)
{
    // ⚠ Qt3 QMap 无 value()，也没有 constFind()；用 find() + 迭代器比较
    QMap<QString, QPixmap>::iterator it = cache.find(filePath);
    if (it != cache.end()) {
        return *it;
    }

    QPixmap result;
    if (!filePath.isEmpty() && QFile::exists(filePath)) {
        QImageReader reader(filePath);
        reader.setAutoTransform(true);                     // stickerlist.cpp:130
        // 与 anystik 对齐（stickerlist.cpp:131）：让 shim 只解当前帧并缩到 152px
        // （= TILE_SIZE*2），不再先 read() 全尺寸再缩放。
        reader.setScaledSize(QSize(kThumbPx, kThumbPx));
        const QImage thumb = reader.read();
        if (!thumb.isNull()) {
            // ⚠⚠ 把 152px 缩略图**一次性**缩到最终显示尺寸 kImgInner 再缓存。
            //   旧实现缓存 152px 成品，然后在 drawTile 里 drawPixmap 缩到 62 ——
            //   Qt3 的 QPainter::drawPixmap(QRect,pm) 会**每帧**走 QImage::smoothScale
            //   （gdb 实测热点），滚动时每瓦片每帧重算。缓存成品后 paint 只做 1:1 blit。
            //  两步缩放（源→152→62）与 anystik 的「QImageReader 缩到 152、QSG 再
            //  缩到 62」逐像素一致。
            const QImage scaled = scaleThumb(thumb, kImgInner);
            if (!scaled.isNull()) {
                // ⚠ Qt3 无 QPixmap::fromImage()（Qt4 才有）
                result = QPixmap(scaled.size());
                if (result.convertFromImage(scaled)) {
                    cache.insert(filePath, result);
                }
            }
        }
    }
    // ⚠ 插入之后再判上限（与 stickerlist.cpp:135-136 先后顺序一致）
    if (cache.size() > kCacheMax) {
        cache.clear();       // 超限整体清，不是 LRU
    }
    return result;
}

static int clampInt(int v, int lo, int hi)
{
    // ⚠ Qt3 无 qBound；anysk 的 qBound(30.0, ...) 是 qreal 版
    return v < lo ? lo : (v > hi ? hi : v);
}

// ⚠ QString 去空白两代方法名不同：Qt3 是 stripWhiteSpace()，
//   Qt4 起改名 stripWhite()（Qt6 已移除 stripWhiteSpace）。
static QString qStripWhite(const QString& s) {
#ifdef QT3_BUILD
    return s.stripWhiteSpace();
#else
    return s.trimmed();
#endif
}

// ⚠ PlaceholderLineEdit 在 Qt3 下用**真实文本**模拟占位符
//   （placeholderlineedit.cpp:34 showPlaceholder() → setText(m_placeholder)），
//   故未输入时 text() 返回占位串而非空。Qt4+ 走原生 setPlaceholderText，text()
//   本就是空。上层若直接拿 text() 判空，启动时会被误判成"正在搜索"而直接
//   return，贴纸列表永远不加载（实测探针 0 次 loadAll）。此 helper 把占位态
//   归一成空串，使两代"空搜索"语义一致。
static QString qSearchText(PlaceholderLineEdit* line) {
    if (!line) {
        return QString();
    }
    const QString t = line->text();
    return (t == line->placeholderText()) ? QString() : t;
}

// ⚠ Qt3 无 QWidget::setVisible()（Qt4+ 才有）—— qltox 同样按版本分流。
//   show()/hide() 两代都在语义上等价于 setVisible(bool)。
static void qSetVisible(QWidget* w, bool on)
{
    if (on) {
        w->show();
    } else {
        w->hide();
    }
}

// Qt3 的 QPainter::drawRoundRect(int×6) 在 Qt4 起被改名 drawRoundedRect，
// 且参数收 QRect/QRectF。这里统一收口，调用点只写一套。
static void drawRoundedBox(QPainter& p, int x, int y, int w, int h, int rx, int ry)
{
#ifdef QT3_BUILD
    p.drawRoundRect(x, y, w, h, rx, ry);
#else
    p.drawRoundedRect(QRectF(x, y, w, h), rx, ry);
#endif
}

// ── 跨版本 const char* 载体 ──
// StickerDbSyncInterface / Storage 的接口都收 const char*。
//   Qt3：QByteArray 只是 QMemArray<char>（无 const char* 构造、不能当字符串用），
//        要靠 QCString 的 operator const char*；
//   Qt4+：QByteArray::constData()，但 .toUtf8() 是临时对象，取指针即悬垂。
// 所以用持有成员的壳，两边都保证生命周期。
#ifdef QT3_BUILD
class TextArg {
public:
    explicit TextArg(const QString& s) : m_s(s.utf8()) {}
    operator const char*() const { return m_s; }
private:
    QCString m_s;
};
#else
class TextArg {
public:
    explicit TextArg(const QString& s) : m_s(s.toUtf8()) {}
    operator const char*() const { return m_s.constData(); }
private:
    QByteArray m_s;
};
#endif

// ── QComboBox 跨版本助手：逐条照搬 qltox/qldox/logindialog.cpp:23-53 ──
//   Qt3: insertItem(text) / setCurrentItem(i)
//   Qt4+: addItem(text) / setCurrentIndex(i)
static void qComboAddItem(QComboBox* combo, const QString& text) {
#ifdef QT3_BUILD
    combo->insertItem(text);
#else
    combo->addItem(text);
#endif
}

static void qComboSetCurrent(QComboBox* combo, int index) {
#ifdef QT3_BUILD
    combo->setCurrentItem(index);
#else
    combo->setCurrentIndex(index);
#endif
}

// 贴纸根目录 + 相对路径绝对化（anysk stickerstore.cpp:241-258 / 291-300 等价物）
static QString stickerBaseDir()
{
    // anysk：显式配置的根优先，非法则回落 AppLocalDataLocation。
    // qlstik 用 Config（cJSON）而非 QSettings。
    const QString saved = Config::value(QString::fromUtf8("storageRoot"));
    if (!saved.isEmpty() && QDir(saved).exists()) {
        return saved;
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

static QString stickerResolvePath(const QString& stored)
{
    if (stored.isEmpty()) {
        return stored;
    }
    if (stored.startsWith("/")) {
        return stored;                    // 绝对路径透传
    }
    return stickerBaseDir() + "/" + stored;
}

static StickerItem makeStickerItem(const StickerRow& row)
{
    StickerItem it;
    it.id = qFromUtf8(row.id);
    it.packId = qFromUtf8(row.pack_id);
    // ⚠ file_path 存相对路径，要拼根目录
    it.filePath = stickerResolvePath(qFromUtf8(row.file_path));
    it.emoji = qFromUtf8(row.emoji);
    it.width = row.width;
    it.height = row.height;
    it.size = (long long)(row.size);          // sticker_db.h:24 是 int
    it.lastUsed = (long long)(row.last_used);
    it.description = qFromUtf8(row.description);
    return it;
}

// ═══════════════════════════════════════════════════════════════════
// StickerGridWidget
// ═══════════════════════════════════════════════════════════════════

StickerGridWidget::StickerGridWidget(QWidget* parent)
    : QWidget(parent)
    , m_cols(4)
    , m_rows(0)
    , m_scrollPos(0)
    , m_scrollDelta(0)
    , m_vBar(0)
{
    // ⚠ 装配逐行照搬 qltox/qldox/chatview.cpp:2310-2322
    m_vBar = new LimeScrollBar(Qt::Vertical, this);
#ifdef QT3_BUILD
    m_vBar->setSteps(10, 50);            // Qt3：lineStep / pageStep
#else
    m_vBar->setSingleStep(10);
    m_vBar->setPageStep(50);
#endif
    connect(m_vBar, SIGNAL(valueChanged(int)), this, SLOT(onScrollChanged(int)));

#ifdef QT3_BUILD
    setFocusPolicy(QWidget::StrongFocus);
#else
    setFocusPolicy(Qt::StrongFocus);
#endif
    setMouseTracking(true);
}

StickerGridWidget::~StickerGridWidget()
{
}

void StickerGridWidget::setStickers(const std::vector<StickerItem>& stickers)
{
    // ⚠ 先废缓存（stickerlist.cpp:282-283）：包更新后同路径内容可能已变
    m_tileImageCache.clear();
    m_items = stickers;
    m_scrollPos = 0;
    m_scrollDelta = 0;
    relayout();
    update();
}

void StickerGridWidget::relayout()
{
    // ⚠ 列数按视口宽算（stickerlist.cpp:285）；STEP+0.5=86.5，int() 后仍是 86
    const int viewW = width() > 0 ? width() : 420;
    m_cols = viewW / kStep;
    if (m_cols < 1) {
        m_cols = 1;
    }
    m_rows = int(m_items.size() + m_cols - 1) / m_cols;

    // ⚠ anysk 的等价物在 stickerlist.cpp:299 ——「布局/列数变化会使瓦片索引映射
    //   失效，先整体清空再按新映射重建」。本页没有瓦片对象，索引→坐标映射是
    //   paintEvent 里现算的（index/m_cols），所以只需重算 m_cols/m_rows；
    //   但**滚动位置必须钳回新内容范围内**，否则窗口拉宽后 m_scrollPos 可能
    //   超出 maxScroll，页面会停在空白处。

    // ⚠ maxScroll = 内容高 - 视口高，**不是** rows*STEP（后者会多滚一行）。
    //   照搬 chatview.cpp:2388-2391；setRange 必须在 setPageStep 之前。
    const int contentH = m_rows * kStep;
    const int vpH = height();
    const int maxScroll = std::max(0, contentH - vpH);
    m_scrollPos = std::min(m_scrollPos, maxScroll);
    m_vBar->setRange(0, maxScroll);
    m_vBar->setPageStep(vpH);
    m_vBar->setValue(m_scrollPos);   // 让滚动条位置跟着钳制后的值走
}

void StickerGridWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // 滚动条贴右边（chatview.cpp:3961-3962）
    const int sbw = m_vBar->sizeHint().width();
    m_vBar->setGeometry(width() - sbw, 0, sbw, height());
    relayout();
}

void StickerGridWidget::onScrollChanged(int value)
{
    m_scrollPos = value;
    m_vBar->showTemporarily();          // chatview.cpp:3979
    update();
}

void StickerGridWidget::wheelEvent(QWheelEvent* event)
{
#ifndef QT3_BUILD
    if (qWheelIsHorizontal(event)) {    // chatview.cpp:3160-3164
        event->ignore();
        return;
    }
#endif
    // ⚠ delta 累加 + 每 120 一步 + 每步 5 倍行高（chatview.cpp:3166-3186）
    m_scrollDelta += qWheelDeltaY(event);
    int steps = m_scrollDelta / 120;
    if (steps == 0) {
        return;
    }
    m_scrollDelta -= steps * 120;

#ifdef QT3_BUILD
    const int step = m_vBar->lineStep();
    const int maxVal = m_vBar->maxValue();
#else
    const int step = m_vBar->singleStep();
    const int maxVal = m_vBar->maximum();
#endif
    int delta = m_scrollPos + (steps > 0 ? -step * 5 : step * 5);
    delta = std::max(0, std::min(delta, maxVal));
    m_scrollPos = delta;

    // ⚠ 必须 blockSignals，否则 setValue → valueChanged → onScrollChanged 回环
    m_vBar->blockSignals(true);
    m_vBar->setValue(delta);
    m_vBar->blockSignals(false);
    update();
    event->accept();
}

int StickerGridWidget::indexAt(const QPoint& contentPos) const
{
    // ⚠ 照搬 stickerlist.cpp:360-372：算格后**排除格子间隙**
    //   （GAP 10px 不可点，否则点空白会误触发上一张）
    const int col = contentPos.x() / kStep;
    const int row = contentPos.y() / kStep;
    if (col < 0 || col >= m_cols || row < 0 || row >= m_rows) {
        return -1;
    }
    const int inCellX = contentPos.x() - col * kStep;
    const int inCellY = contentPos.y() - row * kStep;
    if (inCellX >= kTileSize || inCellY >= kTileSize) {
        return -1;                       // 落在 GAP 里
    }
    const int idx = row * m_cols + col;
    if (idx < 0 || idx >= int(m_items.size())) {
        return -1;
    }
    return idx;
}

void StickerGridWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // ⚠ 事件坐标是视口坐标，减掉滚动偏移才是内容坐标
        const QPoint content = event->pos() - QPoint(0, m_scrollPos);
        const int idx = indexAt(content);
        if (idx >= 0) {
            emit stickerClicked(m_items[idx].filePath);
        }
    }
    QWidget::mousePressEvent(event);
}

void StickerGridWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    // ⚠ Qt3 无 setAutoFillBackground()（Qt4+），背景自己填
    painter.fillRect(event->rect(), QColor(0x1e, 0x1e, 0x34));

    if (m_items.empty() || m_cols < 1 || m_rows < 1) {
        return;
    }

    // ⚠ **只画可视行区间**（stickerlist.cpp:322-325）——上千张不卡的唯一原因。
    //   区间外的瓦片一个都不 new。
    //
    //   与 anysk 的差异（不是遗漏，是本实现更省）：
    //   * anysk 是 QQuickItem，每帧在「移出可视区的删掉、进入可视区的补建」
    //     （stickerlist.cpp:327-338），即**离屏瓦片对象仍然存在**；
    //   * 本页离屏**连数据绘制都不发生** —— paintEvent 里没有瓦片对象可删，
    //     进入可视区的图由 decodedTileImage 的 LRU 缓存供给，不重复磁盘解码。
    //   代价是没有 anysk 的按压态高亮/长按（因为没有持久 widget 承载交互态）。
    const int scrollY = m_scrollPos;
    const int viewH = height();
    const int rowMin = std::max(0, scrollY / kStep);
    const int rowMax = std::min(m_rows - 1, (scrollY + viewH + kStep - 1) / kStep);
    if (rowMax < rowMin) {
        return;
    }
    const int idxMin = rowMin * m_cols;
    const int idxMax = std::min(int(m_items.size()) - 1, rowMax * m_cols + (m_cols - 1));

    for (int idx = idxMin; idx <= idxMax; idx++) {
        const int row = idx / m_cols;
        const int col = idx % m_cols;
        drawTile(painter, idx, col * kStep, row * kStep - scrollY);
    }
}

void StickerGridWidget::drawTile(QPainter& painter, int index, int x, int y)
{
    const StickerItem& item = m_items[index];

    painter.save();
    painter.translate(x, y);

    // ── 背景：圆角 ──
    // ⚠ Qt3 无 QPainterPath，用原生 drawRoundRect + 画刷直接填圆角。
    //   无需裁剪：图片框 62 居中于 76、圆角半径 12，角点必在圆内。
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x1e, 0x1e, 0x34));      // stickerlist.cpp:47
    drawRoundedBox(painter, 0, 0, kTileSize, kTileSize, kTileRadius, kTileRadius);

    const QPixmap pm = decodedTileImage(item.filePath, m_tileImageCache);
    if (pm.isNull()) {
        // ⚠ stickerlist.cpp:57-62：解码失败画 "?" 后**直接 return**，
        //   不画边框也不画角标 —— 必须照做，否则失败图会多出边框和尺寸角标
        painter.setPen(QColor(0x66, 0x66, 0x66));
        QFont f = painter.font();
        f.setPixelSize(11);
        painter.setFont(f);
        painter.drawText(QRect(0, 0, kTileSize, kTileSize), Qt::AlignCenter,
                         QString::fromUtf8("?"));
        painter.restore();
        return;
    }

    // ── 图片：缓存里已是最终显示尺寸（decodedTileImage 缩到 kImgInner）──
    // 这里只做居中 1:1 blit。**不能**再用 drawPixmap(QRect, pm) 缩放：Qt3 会
    // 每帧走 QImage::smoothScale（gdb 实测热点，滚动时每瓦片每帧重算）。
    // 与 anystik 的 dst 居中算法（stickerlist.cpp:71）结果一致：都是 62 居中。
    if (pm.width() > 0 && pm.height() > 0) {
        painter.drawPixmap((kTileSize - pm.width()) / 2,
                           (kTileSize - pm.height()) / 2, pm);
    }

    // ── 边框（stickerlist.cpp:80-86）──
    QPen pen(QColor(0x2a, 0x2a, 0x42));
    pen.setWidth(1);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    drawRoundedBox(painter, 0, 0, kTileSize - 1, kTileSize - 1, kTileRadius, kTileRadius);

    // ── 右下角标：宽×高 · 大小（anysk stickerlist.cpp:83「0 也如实显示」）──
    //   即 width/height 为 0 时照样画「0×0 · 12 KB」，不隐藏、不写"—"
    const long long b = item.size;
    QString sizeStr;
    if (b < 1024) {
        sizeStr = QString::number(b) + QString::fromUtf8(" B");        // :92-93
    } else if (b < 1024 * 1024) {
        sizeStr = QString::number(b / 1024) + QString::fromUtf8(" KB"); // :94-95
    } else {
        // ⚠ Qt3 的 QString::number(double, char, int) **存在**（实测通过）
        sizeStr = QString::fromUtf8("%1 MB").arg(
            QString::number(double(b) / (1024.0 * 1024.0), 'f', 1));
    }
    const QString tag = QString::fromUtf8("%1×%2 · %3")
                        .arg(item.width).arg(item.height).arg(sizeStr);

    QFont tf = painter.font();
    tf.setPixelSize(8);
    // ⚠ Qt3 无 QFontMetricsF / horizontalAdvance → 用 qlcomp 的 qFontWidth
    const QFontMetrics fm(tf);
    const int tagW = clampInt(qFontWidth(fm, tag) + 6, 30, kTileSize - 6);
    const int tagX = kTileSize - tagW - kTagMargin;
    const int tagY = kTileSize - kTagH - kTagMargin;

    // ⚠⚠ 半透明做不到（xvfb 实测 Qt3 画 QPixmap 不混合 alpha），
    //   anysk 的 QColor(0,0,0,150) 退化为**不透明黑**。唯一被迫的可见差异。
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0));
    drawRoundedBox(painter, tagX, tagY, tagW, kTagH, kTagRadius, kTagRadius);

    painter.setFont(tf);
    painter.setPen(QColor(0xff, 0xff, 0xff));
    // ⚠ Qt3 无 elidedText()（像素级 elide），qlcomp 的 qElideChars 按字符数裁。
    //   角标最长约 "1024×1024 · 1023.9 MB" ≈ 21 字符，据此估字符预算。
    painter.drawText(QRect(tagX, tagY, tagW, kTagH), Qt::AlignCenter,
                     qElideChars(tag, std::max(3, (tagW - 4) / 4), ElideRight));

    painter.restore();
}

// ═══════════════════════════════════════════════════════════════════
// StickerListPage
// ═══════════════════════════════════════════════════════════════════

StickerListPage::StickerListPage(QWidget* parent)
    : Page(parent)
    , m_grid(0)
    , m_title(0)
    , m_pasteBtn(0)
    , m_importBtn(0)
    , m_syncBtn(0)
    , m_moreBtn(0)
    , m_searchLine(0)
    , m_countLabel(0)
    , m_tabBarBox(0)
    , m_packCombo(0)
    , m_currentTab(0)           // ⚠ 初始化顺序必须与头文件声明顺序一致
    , m_bottomHome(0)
    , m_bottomGen(0)
    , m_bottomOnline(0)
    , m_bottomSettings(0)
    , m_currentLoader(LoaderAll)
    , m_countStickers(0)
    , m_countPacks(0)
    , m_refreshingCombo(false)
    , m_initialLoaded(false)        // ⚠ 初始化顺序与头文件声明一致
    , m_searchTimer(0)
{
    for (int i = 0; i < 3; i++) {
        m_tabButtons[i] = 0;
    }

    // ⚠ 防抖 timer 照搬 qltox/qldox/messageinput.cpp:89-93：Qt3 无 setSingleShot
    m_searchTimer = new QTimer(this);
#ifndef QT3_BUILD
    m_searchTimer->setSingleShot(true);
#endif
    connect(m_searchTimer, SIGNAL(timeout()), this, SLOT(onSearchTimeout()));

    buildUi();
}

StickerListPage::~StickerListPage()
{
}

// ═══════════════════════════════════════════════════════════════════
// UI 构建 —— 布局逐段照搬 anysk stickerhomepage.cpp:404-624 的 onCreate
// ═══════════════════════════════════════════════════════════════════
// 基准的垂直主布局（:404-406）：panel、spacing 6，自上而下 5 段：
//   TopBar(h56) / searchRow / tabBarBox(h48) / grid / bottomBar(h36)
// （anysk 另有 PushStatusBar(:514) 与 PhoneSmsStatusBar(:517)，
//   注释明写「桌面包视情况自隐藏」，故本 Qt3 桌面版不移植。）
//
// ⚠ Qt3 映射说明：QskLinearBox → QBoxLayout + qNewBoxLayout；
//   setPreferredHeight → setFixedHeight；setPreferredWidth → setFixedWidth；
//   QskSizePolicy::Expanding → addWidget 的 stretch 参数。
void StickerListPage::buildUi()
{
    QBoxLayout* v = qNewBoxLayout(this, QBoxLayout::TopToBottom, 0, 0);
    v->setSpacing(6);            // :406
    qSetMargins(v, 2, 2, 2, 2);

    buildTopBar(v);
    buildSearchRow(v);
    buildTabBar(v);

    // ── 贴纸网格（:566-568，stretch 1 占满剩余空间）──
    m_grid = new StickerGridWidget(this);
    // ⚠ Qt3 字符串 connect **不做签名归一化**，必须逐字写成 moc 生成的形式：
    //   带 const 引用、带空格规范化后的 `const QString&`。写成 QString 时
    //   运行期才报「No such signal StickerGridWidget::stickerClicked(QString)」。
    //   qldox/contactlist.cpp:668 同写法。
    connect(m_grid, SIGNAL(stickerClicked(const QString&)),
            this, SLOT(onStickerClicked(const QString&)));
    v->addWidget(m_grid, 1);

    buildBottomBar(v);
}

// ── TopBar（:408-511）：标题居中 + 粘贴/导入/同步/⋯ ──
void StickerListPage::buildTopBar(QBoxLayout* parent)
{
    QWidget* topBar = new QWidget(this);
    QBoxLayout* h = qNewBoxLayout(topBar, QBoxLayout::LeftToRight, 0, 0);
    h->setSpacing(8);            // :412
    qSetMargins(h, 0, 0, 0, 0);

    // ⚠ 标题文案照抄 anysk:414「😐 表情包」——之前自造的「贴纸家」已废弃。
    //   走 QString::fromUtf8 保非 ASCII（AGENTS.md 编码纪律）
    m_title = new QLabel(topBar);
    m_title->setText(QString::fromUtf8("\xF0\x9F\x98\x90 表情包"));
    m_title->setAlignment(Qt::AlignCenter);
    h->addWidget(m_title, 1);    // :417 Expanding

    // ⚠ 四个按钮只搬布局与文案；点击按本批范围统一接 onTopBarButton() 空处理
    //   （anysk 分别接 requestPasteSticker / showImportMenu / DAV 同步 / showOptionsMenu）
    static const char* kTopBtnText[4] = { "粘贴", "导入", "同步", "\xE2\x8B\xAF" };
    QPushButton** topBtns[4] = { &m_pasteBtn, &m_importBtn, &m_syncBtn, &m_moreBtn };
    for (int i = 0; i < 4; i++) {
        // ⚠ Qt3 QPushButton 无默认构造，用 (parent) + setText 避开
        //   Qt3 (parent,text) / Qt4+ (text,parent) 的参数顺序差异
        QPushButton* b = new QPushButton(topBar);
        b->setText(QString::fromUtf8(kTopBtnText[i]));
        connect(b, SIGNAL(clicked()), this, SLOT(onTopBarButton()));
        // 前三个宽 68（:420/428/436），⋯ 是 44×44（:505）
        if (i < 3) {
            b->setFixedWidth(68);
        } else {
            b->setFixedSize(44, 44);
        }
        h->addWidget(b, 0);
        *(topBtns[i]) = b;
    }
    parent->addWidget(topBar, 0);
    topBar->setFixedHeight(56);   // :411
}

// ── searchRow（:519-528）：搜索框 + 右侧计数 label（同一行）──
void StickerListPage::buildSearchRow(QBoxLayout* parent)
{
    QWidget* row = new QWidget(this);
    QBoxLayout* h = qNewBoxLayout(row, QBoxLayout::LeftToRight, 0, 0);
    h->setSpacing(8);            // :521
    qSetMargins(h, 0, 0, 0, 0);

    // ⚠ Qt3 QLineEdit 无 setPlaceholderText()（Qt4.1+ 才有）→ 用 qlcomp 的
    //   PlaceholderLineEdit（已含 placeholder 绘制与聚焦时清除占位逻辑）
    m_searchLine = new PlaceholderLineEdit(QString::fromUtf8("搜索贴纸 / emoji..."), row);
    // ⚠ 同 stickerClicked：Qt3 必须写全签名 `const QString&`
    connect(m_searchLine, SIGNAL(textChanged(const QString&)),
            this, SLOT(onSearchTextChanged(const QString&)));
    h->addWidget(m_searchLine, 1);

    // 计数 label 在搜索框**右侧**同一行（:526-528），不是页面底部
    m_countLabel = new QLabel(row);
    m_countLabel->setText(QString::fromUtf8("0 个 · 0 包"));
    m_countLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    m_countLabel->setFixedWidth(72);        // :527
    h->addWidget(m_countLabel, 0);

    parent->addWidget(row, 0);
}

// ── tabBarBox（:536-565）：Tab[全部│最近│粘贴板] + 右侧包下拉 ──
void StickerListPage::buildTabBar(QBoxLayout* parent)
{
    m_tabBarBox = new QWidget(this);
    QBoxLayout* h = qNewBoxLayout(m_tabBarBox, QBoxLayout::LeftToRight, 0, 0);
    h->setSpacing(6);
    qSetMargins(h, 0, 0, 0, 0);

    // Tab 组：固定项在 refreshTabBar() 里按有无「粘贴板」包动态增删（:670-681）
    QButtonGroup* group = new QButtonGroup(m_tabBarBox);
    group->setExclusive(true);
    static const char* kTabNames[3] = { "全部", "最近", "粘贴板" };
    for (int i = 0; i < 3; i++) {
        QPushButton* b = new QPushButton(m_tabBarBox);
        b->setText(QString::fromUtf8(kTabNames[i]));
        qSetCheckable(b, true);
        h->addWidget(b, 1);      // :572 MinimumExpanding
        m_tabButtons[i] = b;
        // ⚠ QButtonGroup 加按钮的 API 两代不同：Qt3 是 insert，Qt5+ 只有 addButton
#ifdef QT3_BUILD
        group->insert(b, i);
#else
        group->addButton(b, i);
#endif
    }
    // ⚠ Qt3 的 QButtonGroup 信号是 clicked(int)，Qt4+ 是 buttonClicked(int)
#ifdef QT3_BUILD
    connect(group, SIGNAL(clicked(int)), this, SLOT(onTabChanged(int)));
#else
    connect(group, SIGNAL(buttonClicked(int)), this, SLOT(onTabChanged(int)));
#endif

    // 其余分组走右侧下拉（:548-565）：宽度 150、占位文案「更多分组…」
    m_packCombo = new QComboBox(m_tabBarBox);
    // ⚠ Qt3 QComboBox 无 setPlaceholderText()（Qt4.1+）→ 用「首个占位项」等价实现：
    //   无任何分组时插入占位项并禁用下拉（见 refreshTabBar）
    m_packCombo->setFixedWidth(150);       // :549
    // ⚠ Qt3 QComboBox 没有 currentIndexChanged，只有 activated(int)
    //   （qcombobox.h:157；qldox/mainwindow.cpp:749 同写法）。
    //   Qt4.1 起两者并存，用哪个都行；这里按代分流。
#ifdef QT3_BUILD
    connect(m_packCombo, SIGNAL(activated(int)),
            this, SLOT(onPackComboChanged(int)));
#else
    connect(m_packCombo, SIGNAL(currentIndexChanged(int)),
            this, SLOT(onPackComboChanged(int)));
#endif
    h->addWidget(m_packCombo, 0);

    parent->addWidget(m_tabBarBox, 0);
    m_tabBarBox->setFixedHeight(48);       // :538
}

// ── bottomBar（:591-623）：首页 / 生成表情 / 在线表情 / 设置 ──
void StickerListPage::buildBottomBar(QBoxLayout* parent)
{
    QWidget* bar = new QWidget(this);
    QBoxLayout* h = qNewBoxLayout(bar, QBoxLayout::LeftToRight, 0, 0);
    h->setSpacing(4);            // :595
    qSetMargins(h, 0, 0, 0, 0);

    static const char* kBottomText[4] = { "首页", "生成表情", "在线表情", "设置" };
    QPushButton** bottomBtns[4] = {
        &m_bottomHome, &m_bottomGen, &m_bottomOnline, &m_bottomSettings
    };
    for (int i = 0; i < 4; i++) {
        QPushButton* b = new QPushButton(bar);
        b->setText(QString::fromUtf8(kBottomText[i]));
        connect(b, SIGNAL(clicked()), this, SLOT(onBottomButton()));
        h->addWidget(b, 1);      // :597 等均 Expanding
        *(bottomBtns[i]) = b;
    }
    parent->addWidget(bar, 0);
    bar->setFixedHeight(36);     // :596
}

void StickerListPage::onCreate(const QVariantMap& launchArgs,
                              const QVariantMap& savedState)
{
    Q_UNUSED(launchArgs);
    Q_UNUSED(savedState);

    // ⚠ anysk 在 StickerStore::ensureInit（stickerstore.cpp:203-224）里用
    //   AppLocalDataLocation 建库，这里等价照搬。
    //   db 层（storage.cpp 等 7 件）已由 stikcommon/qldox.pri 挂进 qlstik 构建，
    //   SQLite 由 qlstik.pro:153-163 配好。
    const QString dataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    // ⚠ const char* 跨版本写法照搬 qldox/mainwindow.cpp:502-506
    //   （Qt3 的 QByteArray 只是 QMemArray<char>，无 const char* 构造）
#ifdef QT3_BUILD
    const char* dir = dataDir.utf8();
#else
    const char* dir = dataDir.toUtf8().constData();
#endif
    if (!Storage::instance().init(dir)) {
        // ⚠ Qt3 的 qWarning 是 printf 风格，没有流运算符（page.h 陷阱 #9）
        qWarning("[StickerListPage] Storage::init failed");
        return;
    }
    // ⚠ 照搬 anysk :626-631 的启动顺序：先刷 tab 栏拿到包列表，
    //   再落到第 0 个 tab。⚠ 手动 onTabChanged(0) 已**延后**到首帧门之后
    //   （用户 2026-10-04：首帧须含除数据/图片外的全部框架；全量列表加载延后）。
    refreshTabBar();
    m_currentTab = 0;
    qSetChecked(m_tabButtons[0], true);
}

// ── 首帧门回调（由 MainWindow → PageManager::notifyFirstFrame 触发）──
//   此刻外壳 + 本页框架（顶栏/搜索/分类栏/网格底/底栏）已绘制且事件排干，
//   才执行全量列表查询 + 缩略图解码绘制。
void StickerListPage::onFirstFrame()
{
    if (m_initialLoaded) { return; }
    m_initialLoaded = true;
    // m_currentTab 初值 0 且 tab0 已 setChecked；首次 setChecked 不发信号，
    // 故这里显式调一次，语义与延后前的 onCreate 一致。
    onTabChanged(0);
}

void StickerListPage::loadAllStickers()
{
    m_currentLoader = LoaderAll;
    StickerDbSyncInterface* db = Storage::instance().stickerDb();
    if (!db) {
        return;
    }
    // ⚠ 照搬 anysk stickerhomepage.cpp:749-757 的 N+1 全量聚合，不分页
    const std::vector<StickerPackRow> packs = db->list_packs(kPacksAll);
    std::vector<StickerItem> all;
    for (size_t i = 0; i < packs.size(); i++) {
        const std::vector<StickerRow> rows =
            db->list_stickers(TextArg(qFromUtf8(packs[i].id)));
        for (size_t j = 0; j < rows.size(); j++) {
            all.push_back(makeStickerItem(rows[j]));
        }
    }
    // ⚠ 这里**不**设 m_countPacks：包数由 updateCountLabel() 按当前这批
    //   贴纸的 packId 去重现算（anysk 同），loadAll 只是包总数会被覆盖
    m_grid->setStickers(all);
    updateCountLabel();
}

void StickerListPage::loadRecentStickers()
{
    m_currentLoader = LoaderRecent;
    StickerDbSyncInterface* db = Storage::instance().stickerDb();
    if (!db) {
        return;
    }
    const std::vector<StickerRow> rows = db->list_recent_stickers(60);
    std::vector<StickerItem> items;
    for (size_t i = 0; i < rows.size(); i++) {
        items.push_back(makeStickerItem(rows[i]));
    }
    m_grid->setStickers(items);
    updateCountLabel();
}

// ── 按包加载（基准 loadPackStickers，stickerhomepage.cpp:765-769）──
//   下拉选中的分组走这里；tab 只有「全部/最近/粘贴板」三项（:670-681）
void StickerListPage::loadPackStickers(const QString& packId)
{
    m_currentLoader = LoaderPack;
    StickerDbSyncInterface* db = Storage::instance().stickerDb();
    if (!db || packId.isEmpty()) {
        return;
    }
    m_activeTab = packId;      // :723 记录激活分组，供 reloadActive 兜底
    const std::vector<StickerRow> rows =
        db->list_stickers(TextArg(packId));
    std::vector<StickerItem> items;
    for (size_t i = 0; i < rows.size(); i++) {
        items.push_back(makeStickerItem(rows[i]));
    }
    m_grid->setStickers(items);
    updateCountLabel();
}

// ═══════════════════════════════════════════════════════════════════
// refreshTabBar（结构照搬 stickerhomepage.cpp:657-695，有一处有意偏离）
// ═══════════════════════════════════════════════════════════════════
// 基准语义：把名为「粘贴板」的包从普通分组里摘出 → 单独成固定 Tab；
// 其余包全部进右侧下拉；刷新时**保留当前 tab 与下拉选择**。
//
// ⚠⚠ 有意偏离 anystik（用户 2026-10-03 明确要求）：
//   anystik 在「无粘贴板包」时不加第 3 个 tab、在「无其它包」时隐藏下拉
//   （:675-676/:695）。本页改为**始终显示**「全部/最近/粘贴板」和 combobox
//   占位「更多分组…」，使空数据库也保持设计图的分类栏。
//   代价：空库时「粘贴板」tab 与下拉都无实际数据，点击不切换内容。
//   数据存在时行为与 anystik 完全一致（走下面同一段逻辑）。
void StickerListPage::refreshTabBar()
{
    StickerDbSyncInterface* db = Storage::instance().stickerDb();
    m_packIds.clear();
    m_packTitles.clear();
    m_pastePackId = QString();
    if (db) {
        const std::vector<StickerPackRow> packs = db->list_packs(kPacksAll);
        m_countPacks = int(packs.size());
        for (size_t i = 0; i < packs.size(); i++) {
            // ⚠ 非 ASCII：走 qFromUtf8，绝不用 latin1()（AGENTS.md 编码纪律）
            const QString id = qFromUtf8(packs[i].id);
            const QString title = qFromUtf8(packs[i].title);
            if (title == QString::fromUtf8("粘贴板")) {
                m_pastePackId = id;          // :664-665
            } else {
                m_packIds.append(id);         // :667
                m_packTitles.append(title);
            }
        }
    }

    // 固定 Tab：全部 / 最近 / 粘贴板 —— 始终显示（偏离见函数头注释）
    qSetVisible(m_tabButtons[0], true);
    qSetVisible(m_tabButtons[1], true);
    qSetVisible(m_tabButtons[2], true);
    // ⚠ 保留原 tab（:671 setCurrentIndex(qMin(current, count-1))）；三个 tab
    //   现在恒可见，故上界恒为 2
    m_currentTab = clampInt(m_currentTab, 0, 2);
    for (int i = 0; i < 3; i++) {
        qSetChecked(m_tabButtons[i], i == m_currentTab);
    }

    // 其余包 → 下拉：清空重建，恢复当前选择 —— :683-694
    // ⚠ 两处 Qt3 适配：
    //   1) Qsk 用 setCurrentIndex(-1) 表示「无选中」+ 独立占位文案，
    //      Qt3 QComboBox 无占位 API，故 index 0 插一条真占位项；
    //   2) clear()/insertItem() 会触发 currentIndexChanged，必须用
    //      m_refreshingCombo 抑制，否则重建过程会误触发 loader。
    m_refreshingCombo = true;
    m_packCombo->clear();
    qComboAddItem(m_packCombo, QString::fromUtf8("更多分组…"));
    int comboIdx = 0;                        // :688-693 找回当前包
    for (int i = 0; i < int(m_packIds.size()); i++) {
        qComboAddItem(m_packCombo, m_packTitles[i]);
        if (m_packIds[i] == m_activeTab) {
            comboIdx = i + 1;                // +1 跳过占位项
        }
    }
    qComboSetCurrent(m_packCombo, comboIdx);
    m_refreshingCombo = false;
    // ⚠ anystik 在无普通分组时隐藏下拉（:695）；本页始终显示（偏离见函数头）
    qSetVisible(m_packCombo, true);
}

void StickerListPage::onPackComboChanged(int index)
{
    // 搜索状态优先（anysk :721-723）
    if (!qStripWhite(qSearchText(m_searchLine)).isEmpty()) {
        return;
    }
    if (m_refreshingCombo) {
        return;                              // 重建中，忽略
    }
    // ⚠ index 0 是占位项，不是真实分组（refreshTabBar 里插在最前）
    if (index <= 0 || index > int(m_packIds.size())) {
        return;
    }
    loadPackStickers(m_packIds[index - 1]);
}

// ── reloadActive（照搬 stickerhomepage.cpp:721-737）──
//   数据变更后重载当前视图；分组已卸载/停用则回退「全部」，避免空白网格。
void StickerListPage::reloadActive()
{
    if (m_activeTab == QString::fromUtf8("__recent")) {
        loadRecentStickers();
        return;
    }
    if (m_activeTab.isEmpty()) {
        loadAllStickers();
        return;
    }
    // 兜底：该分组已不存在（被卸载/停用）→ 回退「全部」（:731-735）
    if ((!m_pastePackId.isEmpty() && m_activeTab == m_pastePackId)
        || m_packIds.contains(m_activeTab)) {
        loadPackStickers(m_activeTab);
        return;
    }
    m_activeTab = QString();
    loadAllStickers();
}

// ── doSearch（照搬 anysk stickerhomepage.cpp:771-777 的结构）──
//   ⚠ 关键语义：关键词为空时**不是**清空网格，而是回到当前 tab/combo 的内容
//     （anysk 调 onTabChanged(m_tabBar->currentIndex())）。
//     本实现用 reloadActive() 等价——m_activeTab 记录的分组在 tab 与 combo
//     两条路径下都已写入（:706/714/718/723）。
void StickerListPage::applySearch(const QString& keyword)
{
    if (keyword.isEmpty()) {
        reloadActive();
        return;
    }
    m_currentLoader = LoaderSearch;
    StickerDbSyncInterface* db = Storage::instance().stickerDb();
    if (!db) {
        return;
    }
    std::vector<StickerItem> items;
    if (!keyword.isEmpty()) {
        // ⚠ 非 ASCII 安全：走 utf8()，绝不用 latin1()（AGENTS.md 编码纪律）
        TextArg q(keyword);
        // ⚠ DB 侧自带 LIMIT 50（sticker_db.cpp）
        const std::vector<StickerRow> rows = db->search_stickers(q);
        for (size_t i = 0; i < rows.size(); i++) {
            items.push_back(makeStickerItem(rows[i]));
        }
    }
    m_grid->setStickers(items);
    updateCountLabel();
}

void StickerListPage::updateCountLabel()
{
    // ── 照搬 anysk stickerhomepage.cpp:781-791 ──
    //   「%2 包」统计的是**当前这批贴纸归属的去重包数**，不是包总数。
    //   所以「最近」/ 搜索结果横跨的包天然少于全部包总数。
    const std::vector<StickerItem>& items = m_grid->stickers();
    m_countStickers = int(items.size());
    // ⚠ std::set<QString> 而非 QSet：QSet 在 Qt3/4+ 都是容器类但无 Qt3
    //   实测结论，这里保守用标准容器（qltox 同款 std 容器风格）
    std::set<QString> packIds;
    for (size_t i = 0; i < items.size(); i++) {
        packIds.insert(items[i].packId);
    }
    m_countPacks = int(packIds.size());
    m_countLabel->setText(QString::fromUtf8("%1 个 · %2 包")
                          .arg(m_countStickers).arg(m_countPacks));
}

void StickerListPage::onSearchTextChanged(const QString& text)
{
    Q_UNUSED(text);
    // ⚠ Qt3 QLineEdit 无 setPlaceholderText()（Qt4.1+ 才有），改用 qlcomp 的
    //   PlaceholderLineEdit（:buildSearchRow 已按该类型建），其余行为不变。
    // ⚠ 每次输入都重启定时器 → 350ms 内的连续输入只会触发一次查询
#ifdef QT3_BUILD
    m_searchTimer->start(kSearchDebounceMs, true);      // Qt3：第二参 false 即单次
#else
    m_searchTimer->start(kSearchDebounceMs);
#endif
}

void StickerListPage::onSearchTimeout()
{
    applySearch(qSearchText(m_searchLine));
}

void StickerListPage::onTabChanged(int id)
{
    m_currentTab = id;
    // 搜索状态优先：搜索词非空时不切 loader（anysk :701-703）
    if (!qStripWhite(qSearchText(m_searchLine)).isEmpty()) {
        return;
    }
    // ⚠ m_activeTab 三态照搬 anysk :705-719：
    //   "" / "__recent" / packId。reloadActive() 靠它回退（:721-737）。
    if (id <= 0) {
        m_activeTab = QString();
        loadAllStickers();
    } else if (id == 1) {
        m_activeTab = QString::fromUtf8("__recent");
        loadRecentStickers();
    } else if (id == 2 && !m_pastePackId.isEmpty()) {
        loadPackStickers(m_pastePackId);   // :718 内部会写 m_activeTab
    }
}

// 顶栏按钮：布局文案已照搬 anysk，行为按本批范围留空处理
void StickerListPage::onTopBarButton()
{
    // anysk 对应：粘贴→requestPasteSticker / 导入→showImportMenu /
    //             同步→DAV 同步引擎 / ⋯→showOptionsMenu（:419-511）
}

void StickerListPage::onBottomButton()
{
    // anysk 对应：首页(无操作) / 生成表情→stikergen /
    //             在线表情→onlinepacks / 设置→settings（:596-623）
}

void StickerListPage::onStickerClicked(const QString& filePath)
{
    // ── 单击：复制到剪贴板（照搬 stickerhomepage.cpp:570-583 的语义）──
    // 复制**原图**而不是 152px 缩略图：瓦片缓存里只有缩略图，
    // 粘出去的图必须是原始分辨率，否则用户拿到的就是糊的。
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        ToastWidget::show(this, QString::fromUtf8("复制失败"), 2000);
        return;
    }
    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    const QImage full = reader.read();
    if (full.isNull()) {
        ToastWidget::show(this, QString::fromUtf8("复制失败"), 2000);
        return;
    }
    // qltox photoviewer.cpp:92-105 就是用 QClipboard::setImage() 递图
    QApplication::clipboard()->setImage(full);
    ToastWidget::show(this, QString::fromUtf8("已复制到剪贴板"), 2000);

    // 本批未实现（anysk 基准里有，此处明确留白而非静默丢弃）：
    //   * 双击预览（stickerhomepage.cpp:585-586 → openPreview）
    //   * 长按菜单（:588-589 → showStickerMenu）
    //   * 滚动到顶部/底部浮动按钮（stickerlist.cpp:212）
    //   * 点击反馈（anysk 有按压态高亮）
}

void StickerListPage::retranslateUi()
{
    // 文案与基准 :638-655 一致（标题「😐 表情包」，非自造名）
    if (m_title) {
        m_title->setText(QString::fromUtf8("\xF0\x9F\x98\x90 表情包"));
    }
    if (m_pasteBtn)   { m_pasteBtn->setText(QString::fromUtf8("粘贴")); }
    if (m_importBtn)  { m_importBtn->setText(QString::fromUtf8("导入")); }
    if (m_syncBtn)    { m_syncBtn->setText(QString::fromUtf8("同步")); }
    if (m_searchLine) {
        m_searchLine->setPlaceholderText(QString::fromUtf8("搜索贴纸 / emoji..."));
    }
    // ⚠ 下拉占位文案在 refreshTabBar() 里随列表一起重建（:687），
    //   这里不能重复插，否则每调一次 retranslateUi 就多一条「更多分组…」。
    if (m_bottomHome)     { m_bottomHome->setText(QString::fromUtf8("首页")); }
    if (m_bottomGen)      { m_bottomGen->setText(QString::fromUtf8("生成表情")); }
    if (m_bottomOnline)   { m_bottomOnline->setText(QString::fromUtf8("在线表情")); }
    if (m_bottomSettings) { m_bottomSettings->setText(QString::fromUtf8("设置")); }
    refreshTabBar();
    updateCountLabel();
}

void StickerListPage::onSaveInstanceState(QVariantMap& outState)
{
    outState[QString::fromUtf8("countStickers")] = m_countStickers;
    outState[QString::fromUtf8("countPacks")] = m_countPacks;
}

void StickerListPage::onRestoreInstanceState(const QVariantMap& savedState)
{
    m_countStickers = savedState[QString::fromUtf8("countStickers")].toInt();
    m_countPacks = savedState[QString::fromUtf8("countPacks")].toInt();
}
