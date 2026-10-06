#include "stickerlistpage.h"
#include "compatcore34.h"
#include "config.h"
#include "stickerops.h"            // 右键菜单的读图/写 DB 薄封装
#include "stickerpreviewoverlay.h"  // 「预览」的放大查看层
#include "storage.h"              // Storage::instance().init() + stickerDb()
#include "sticker_db.h"           // StickerRow / StickerPackRow / kPacksAll
#include "qstandardpaths_shim.h"
#include "qimagereader_shim.h"
#include "qurl_shim.h"             // qToPercentEncoding（「搜索相似」拼 URL 用）
#include "imagetmpuploader.h"      // 「搜索相似」的临时图床（stikcommon 已编入）
#include "lambdaslot.h"            // 菜单项零参槽代理（菜单项回调不能带参数）
#include "translator.h"            // _() / _A()
#include "placeholderlineedit.h"   // Qt3 QLineEdit 无 placeholder → qlcomp 的替代
#include "toastwidget.h"           // 单击复制的提示（anysk stickerhomepage.cpp:578）
#include "StyleParams.h"           // g_activeParams：圆钮取当前主题调色板
#include "ThemeManager.h"          // ThemeManager::isDarkMode()

#include <algorithm>

#ifdef QT3_BUILD
#include <qmessagebox.h>         // 删除确认（include 分支照 mainwindow.cpp:23-28）
#include <qinputdialog.h>        // 描述编辑
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
#include <qobjectlist.h>      // QObjectList/QPtrList：Qt3 无 findChildren，要自己遍历 children()
#include <string.h>            // strcmp：Qt3 无 qobject_cast，枚举预览层要比 className
#include <qevent.h>
#include <qapplication.h>
#include <qclipboard.h>
#else
#include <QMessageBox>
#include <QInputDialog>
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
#include <QList>
#include <QEnterEvent>
#include <QApplication>
#include <QClipboard>
#endif

// ═════════ anystik 悬浮圆钮：QLimeStyle 无法按控件强制 18px 圆角 ═════════
// anystik 的 QskPushButton 用 QskBoxShapeMetrics(18) 画正圆（stickerlist.cpp:217）。
// qlstik 的 LimeStyle 半径随主题（Flat 6 / Border 4 / Capsule=height/2），且
// LimeStyle.cpp 与 doxhttpd/qlcomp 是同一 inode 硬链接，不能改 buttonRadiusFor。
// 故此处自绘圆钮：无 Q_OBJECT（只用 QPushButton 继承来的 clicked()，无需 moc）。
namespace {
class CircleFloatButton : public QPushButton {
public:
    CircleFloatButton(const QString& text, QWidget* parent)
        : QPushButton(text, parent)
    {
        setFixedSize(36, 36);   // anystik stickerlist.cpp:219
    }
protected:
    virtual void paintEvent(QPaintEvent*)
    {
        QPainter p(this);
#ifndef QT3_BUILD
        p.setRenderHint(QPainter::Antialiasing, true);   // Qt3 无抗锯齿，与全 UI 一致
#endif
#ifdef QT3_BUILD
        const bool hovered = hasMouse();     // Qt3 无 underMouse()
#else
        const bool hovered = underMouse();
#endif
        QColor bg, fg, bd;
        const StyleParams* sp = g_activeParams;
        if (sp) {
            const StyleParams::Palette& pal =
                ThemeManager::isDarkMode() ? sp->dark : sp->light;
            bd = pal.border;
            fg = pal.textPrimary;
            if (isDown())       bg = pal.activeBg;
            else if (hovered)   bg = pal.hoverBg;
            else                bg = pal.surfaceBg;
        } else {   // 主题未初始化：用贴合本页深色网格底(0x1e1e34)的固定色
            bd = QColor(0x50, 0x50, 0x70);
            fg = QColor(0xdc, 0xdc, 0xdc);
            if (isDown())       bg = QColor(0x48, 0x48, 0x60);
            else if (hovered)   bg = QColor(0x40, 0x40, 0x58);
            else                bg = QColor(0x2a, 0x2a, 0x44);
        }
        QRect r = rect();
        r.setWidth(r.width() - 1);    // Qt3 QRect 无 adjusted()
        r.setHeight(r.height() - 1);
        p.setPen(bd);
        p.setBrush(bg);
        p.drawEllipse(r);
        p.setPen(fg);
        p.drawText(rect(), Qt::AlignCenter, text());
    }
#ifdef QT3_BUILD
    virtual void enterEvent(QEvent*) { update(); }
#else
    virtual void enterEvent(QEnterEvent*) { update(); }
#endif
    virtual void leaveEvent(QEvent*) { update(); }
};
} // namespace

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

// ══════════════════════════════════════════════════════════════════
// 预览层枚举（同 anysk stickerhomepage.cpp:1191 / :1216 的 findChildren）。
//
// 为什么不能直接抄 anysk 的 findChildren<StickerPreviewOverlay*>()：
//   本机 Qt3.8 的 QObject **完全没有 findChild* 任何版本**（模板版是
//   Qt4 才加的）。已核对：/opt/qt338sh/include/qobject.h 全文零命中，
//   唯一可用的是 qobject.h:111 `const QObjectList* children() const`。
//   qlcomp 的 compatcore34.h 也只提供 QString/QByteArray 助手，没有这层封装。
//
// 为什么用 POD 数组而不是 QPtrList/QList<StickerPreviewOverlay*>：
//   两个容器对「元素本身是指针」的引用展开不同 —— Qt3 的 QPtrList<T>::at()
//   返回 T* const&（即 StickerPreviewOverlay* const&），Qt6 的 QList<T>::at()
//   返回 T&。写哪种取法都会在另一版报错，故调用处统一用 items[i]。
//
// ⚠⚠ Qt3 分支必须每次重新取 children()，且**立刻判空**：Qt3 的 children()
//   返回成员 childObjects 的裸指针（qobject.h:111），而该列表在最后一个子
//   对象被销毁后会被**置空**（已用 mini3 探针实测：删掉唯一子控件后
//   children() 返回 nil，不是空列表）。直接 kids->begin() 会段错误。
//   这就是「统一闭路返回、每轮重取指针」的必要性。
//
// 为什么 Qt3 比 className() 字符串、Qt4+ 用 qobject_cast：
//   qobject_cast 这个模板是 Qt4 引入的（Qt3 头文件零命中）；
//   QObject::className() 则是 Qt6 **删除**的（查 qobject.h：Qt3:84 有虚拟
//   className()，Qt6 无该成员，官方文档改为 metaObject()->className()）。
//   两版唯一的公共交集就是虚函数 metaObject()。
struct PreviewOverlayList
{
    StickerPreviewOverlay* items[8];   // 同屏预览层最多 8 个
    int count;
};

static PreviewOverlayList previewOverlays(QWidget* parent)
{
    PreviewOverlayList out;
    out.count = 0;
#ifdef QT3_BUILD
    // ⚠ 每轮重取：列表可能因上方对象被 deleteLater 而变空/被置空
    const QObjectList* kids = parent->children();
    if (!kids) {
        return out;
    }
    for (QObjectList::Iterator it = kids->begin(); it != kids->end(); ++it) {
        QObject* kid = *it;
#else
    const QObjectList kids = parent->children();
    for (int i = 0; i < kids.size(); ++i) {
        QObject* kid = kids.at(i);
#endif
        if (!kid) {
            continue;
        }
        StickerPreviewOverlay* ov = 0;
#ifdef QT3_BUILD
        const char* cn = kid->className();
        if (cn && strcmp(cn, "StickerPreviewOverlay") == 0) {
            ov = static_cast<StickerPreviewOverlay*>(kid);
        }
#else
        ov = qobject_cast<StickerPreviewOverlay*>(kid);
#endif
        if (ov && out.count < 8) {
            out.items[out.count++] = ov;
        }
    }
    return out;
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

// ── URL 百分号编码（「搜索相似」拼引擎 URL 用）──
// ⚠ 返回类型三代各不相同，只能逐分支写死，没法收敛成一个宏：
//   Qt3 走 stikcommon/qurl_shim.h 的 qToPercentEncoding → QByteArray
//   （qurl_shim.h:97，QT_VERSION < 0x040000 门控；Qt3 的 QUrl 没有这个静态方法）
//   Qt4 的 QUrl::toPercentEncoding → QString
//   Qt5+ 的 QUrl::toPercentEncoding → QByteArray
// 样板对齐 stikcommon/imagesearchclient.cpp:31-46 的 QLSTIK_PCT_ENCODE。
// ⚠ 走 QString::fromUtf8 而非 fromLatin1：编码结果含 %XX 与原 URL 的非 ASCII
//   字节，fromLatin1 会丢高位字节（AGENTS.md 编码纪律）。
static QString pctEncode(const QString& s)
{
#ifdef QT3_BUILD
    return QString::fromUtf8(qToPercentEncoding(s));
#elif QT_VERSION < 0x050000
    return QUrl::toPercentEncoding(s);
#else
    return QString::fromUtf8(QUrl::toPercentEncoding(s));
#endif
}

// ── 右键菜单：跨版本助手 ──
// ⚠ Menu34（QPopupMenu / QMenu 的版本别名）定义在头文件里，本文件直接用。
//   别名为什么必须放在头文件见那处注释。

// 插一个「按下即执行」的项：payload 由 LambdaSlot 绑进 lambda。
// ⚠ 菜单项回调不能带参数（移植计划.md:78），故每项挂一个零参代理槽。
//   Qt3：QMenuData::insertItem(text, const QObject* receiver, const char* member)
//        （qmenudata.h:165）内部连的是 QMenuItem 的零参 activated()，与 call() 严丝合缝。
//   Qt4+：QMenu::addAction(text, receiver, member) 连 QAction::triggered(bool)，
//        Qt 允许槽的参数表比信号**短**（多余尾部参数丢弃），故 call() 仍能接上。
//        此条已用 Qt 6.7.3 实测：SIGNAL(triggered(bool))→SLOT(call()) 命中。
//   连法与 mainwindow.cpp:468-489 的 addMenuItem(LambdaSlot, SLOT(call())) 一致。
static void insertActionItem(Menu34* menu, const QString& text,
                             std::function<void()> fn)
{
    // ⚠ LambdaSlot 的 parent 必须是菜单本体：LambdaSlot.h:14 的约定是
    //   「parent 设为 sender，sender 析构时自动清理」，漏挂就会每次弹菜单漏一批。
    LambdaSlot* slot = new LambdaSlot(menu, fn);
#ifdef QT3_BUILD
    menu->insertItem(text, slot, SLOT(call()));
#else
    menu->addAction(text, slot, SLOT(call()));
#endif
}

// 插一个子菜单，返回子菜单句柄。
// ⚠ 返回句柄是必须的：Qt3 要自建 QPopupMenu 再 insertItem 挂上去，
//   Qt4+ 反过来由 addMenu() 自己建、调用方不能自建再塞进去。
//   样板逐条对齐 qlstik/mainwindow.cpp:335-356 的 addSubMenu()。
static Menu34* insertSubMenu(Menu34* menu, const QString& text)
{
#ifdef QT3_BUILD
    // qmenudata.h:184 insertItem(text, QPopupMenu*, id=-1, index=-1)
    // ⚠ parent 选 menu 而非页面：顶层菜单析构时子菜单跟着走，不会成孤儿。
    QPopupMenu* sub = new QPopupMenu(menu);
    // ⚠ Qt3 的 QPopupMenu 不继承父控件字体，不显式设会落到系统默认字体
    //   （mainwindow.cpp:344-345 同款处理）。
    sub->setFont(menu->font());
    menu->insertItem(text, sub);
    return sub;
#else
    return menu->addMenu(text);
#endif
}

// ⚠ 刻意**没有**分隔条：anysk showStickerMenu:812-819 的 8 项之间也没有分隔条，
//   照搬其视觉分组（仅两个「›」子菜单自带层次）。若日后要加，注意 Qt3 的
//   insertSeparator() 会占用一个 item id —— 但本文件不依赖 id（payload 走
//   LambdaSlot 闭包），所以加分隔条不会打乱动作下标。

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
    , m_toTopBtn(0)
    , m_toBottomBtn(0)
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

    // ── 照搬 anystik stickerlist.cpp:212-226：滚动到顶/底的悬浮圆钮 ──
    m_toTopBtn    = new CircleFloatButton(QString(QChar(0x2191)), this);   // ↑
    m_toBottomBtn = new CircleFloatButton(QString(QChar(0x2193)), this);   // ↓
    m_toTopBtn->raise();
    m_toBottomBtn->raise();
    m_toTopBtn->hide();
    m_toBottomBtn->hide();
    connect(m_toTopBtn,    SIGNAL(clicked()), this, SLOT(onScrollToTop()));
    connect(m_toBottomBtn, SIGNAL(clicked()), this, SLOT(onScrollToBottom()));
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
    layoutScrollButtons();           // m_rows 变了 → 圆钮位置/显隐跟着变
}

void StickerGridWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // 滚动条贴右边（chatview.cpp:3961-3962）
    const int sbw = m_vBar->sizeHint().width();
    m_vBar->setGeometry(width() - sbw, 0, sbw, height());
    relayout();
}

// ── 照搬 anystik stickerlist.cpp:386-404 ──
void StickerGridWidget::layoutScrollButtons()
{
    if (!m_toTopBtn || !m_toBottomBtn) {
        return;
    }
    const int MARGIN = 12;               // stickerlist.cpp:393
    const int SPACING = 6;               // stickerlist.cpp:394
    const int BTN = 36;                  // stickerlist.cpp:395
    const int w = width();
    const int h = height();
    m_toTopBtn->setGeometry(w - MARGIN - BTN,
                            h - MARGIN - BTN * 2 - SPACING, BTN, BTN);
    m_toBottomBtn->setGeometry(w - MARGIN - BTN,
                               h - MARGIN - BTN, BTN, BTN);
    const bool show = (m_rows > 1);      // stickerlist.cpp:399/403
#ifdef QT3_BUILD
    m_toTopBtn->setShown(show);          // Qt3 无 setVisible()
    m_toBottomBtn->setShown(show);
#else
    m_toTopBtn->setVisible(show);
    m_toBottomBtn->setVisible(show);
#endif
}

// anystik scrollToY(0) / scrollToY(scrollableSize().height())
void StickerGridWidget::onScrollToTop()
{
    m_vBar->setValue(0);                 // → valueChanged → onScrollChanged → update()
}

void StickerGridWidget::onScrollToBottom()
{
#ifdef QT3_BUILD
    m_vBar->setValue(m_vBar->maxValue());    // 同本文件 :358 写法
#else
    m_vBar->setValue(m_vBar->maximum());     // 同本文件 :361 写法
#endif
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
    else if (event->button() == Qt::RightButton) {
        // ⚠ 与左键同一条内容坐标换算，别让右键菜单贴到错误的格子上。
        const QPoint content = event->pos() - QPoint(0, m_scrollPos);
        const int idx = indexAt(content);
        // ⚠ 只在瓦片上弹菜单：空白处 indexAt 返回 -1，直接不弹。
        //   顺带把 qlstik/mainwindow.cpp:290-313 的 QMenu 惯例（parent=this）搬过来，
        //   菜单与页面同生命周期、不会泄漏。
        if (idx >= 0) {
            emit stickerContextRequested(idx, content.x(), content.y());
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
    , m_gridDeferTimer(0)
    , m_currentItemIndex(-1)      // -1 = 当前没有菜单目标
    , m_imageUploader(0)
    , m_searchEngine(0)
    , m_ctxMenu(0)
    , m_pendingMenuDialog(0)
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

    // ⚠ 延后一回合再让 grid 绘制：loader 先更新计数 label，本回合 XPending
    //   先把 label flush 上屏，下一次 timeout 才 setStickers（纯 Qt，无 flushX）
    m_gridDeferTimer = new QTimer(this);
#ifndef QT3_BUILD
    m_gridDeferTimer->setSingleShot(true);
#endif
    connect(m_gridDeferTimer, SIGNAL(timeout()), this, SLOT(applyPendingStickers()));

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
    // ⚠ 同上，签名必须是 moc 生成的原样。已用 /opt/qt338sh/bin/moc 实测
    //   生成 stickerContextRequested(int,int,int)，写 QString 之类会运行期报错。
    connect(m_grid, SIGNAL(stickerContextRequested(int,int,int)),
            this, SLOT(onStickerContextRequested(int,int,int)));
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
    m_countLabel->setMinimumWidth(72);      // :527 优选宽度（非固定，随文本增长不裁字）
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
    updateCountLabelFor(all);
    m_pendingItems = all;
#ifdef QT3_BUILD
    m_gridDeferTimer->start(kGridDeferMs, true);
#else
    m_gridDeferTimer->start(kGridDeferMs);
#endif
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
    updateCountLabelFor(items);
    m_pendingItems = items;
#ifdef QT3_BUILD
    m_gridDeferTimer->start(kGridDeferMs, true);
#else
    m_gridDeferTimer->start(kGridDeferMs);
#endif
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
    updateCountLabelFor(items);
    m_pendingItems = items;
#ifdef QT3_BUILD
    m_gridDeferTimer->start(kGridDeferMs, true);
#else
    m_gridDeferTimer->start(kGridDeferMs);
#endif
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
    // ⚠ 列表即将整体重建，m_items 下标全部作废：作废菜单目标，否则用户在旧菜单
    //   上点动作时会按新列表的下标取到另一张贴纸。
    m_currentItemIndex = -1;
    // 同理关掉预览：它还指着即将消失的那一张。close() 走 closeEvent → closed
    // → deleteLater（anysk :1216-1217 删除成功后同样 deleteLater 全部）。
    const PreviewOverlayList prevs = previewOverlays(this);
    for (int i = 0; i < prevs.count; ++i) {
        prevs.items[i]->close();
    }

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
    updateCountLabelFor(items);
    m_pendingItems = items;
#ifdef QT3_BUILD
    m_gridDeferTimer->start(kGridDeferMs, true);
#else
    m_gridDeferTimer->start(kGridDeferMs);
#endif
}

void StickerListPage::applyPendingStickers()
{
    // 延后一回合后真正交给 grid 绘制（此时 label 已在本回合 XPending 时上屏）
    m_grid->setStickers(m_pendingItems);
}

void StickerListPage::updateCountLabel()
{
    // 供 retranslateUi（:1229）复用：按当前网格内容刷新
    updateCountLabelFor(m_grid->stickers());
}

void StickerListPage::updateCountLabelFor(const std::vector<StickerItem>& items)
{
    // ── 照搬 anysk stickerhomepage.cpp:781-791 ──
    //   「%2 包」统计的是**当前这批贴纸归属的去重包数**，不是包总数。
    //   所以「最近」/ 搜索结果横跨的包天然少于全部包总数。
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
    emit countsChanged(m_countLabel->text(), m_countStickers);
}

QString StickerListPage::countLabelText() const
{
    return m_countLabel ? m_countLabel->text() : QString();
}

int StickerListPage::stickerCount() const
{
    return m_countStickers;
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
    //
    // ⚠ 读图/解码逻辑收敛到 StickerOps::copyToClipboard，与右键「复制」走同一
    //   份代码，免得两处解码参数（setAutoTransform）日后各改一处而行为漂移。
    //   Toast 文案统一为英文，与本文件 retranslateUi/底栏/顶栏
    //   （:638-655、:876-884、:920-930）现有硬编码文案风格一致；本批不改
    //   Toast 的硬编码风格，详见文件开头说明。
    if (!StickerOps::copyToClipboard(filePath)) {
        ToastWidget::show(this, QString::fromUtf8("复制失败"), 2000);
        return;
    }
    ToastWidget::show(this, QString::fromUtf8("已复制到剪贴板"), 2000);

    // 本批未实现（anysk 基准里有，此处明确留白而非静默丢弃）：
    //   * 双击预览（stickerhomepage.cpp:585-586 → openPreview）
    //   * 长按菜单（:588-589 → showStickerMenu）—— 本批只做桌面端右键
    //   * 滚动到顶部/底部浮动按钮（stickerlist.cpp:212）
    //   * 点击反馈（anysk 有按压态高亮）
}

// ═══════════════════════════════════════════════════════════════════
// 贴纸右键菜单（anysk stickerhomepage.cpp:797-901 的 8 项去掉「预览」→ 7 项）
// ═══════════════════════════════════════════════════════════════════

// 延迟弹窗的执行体。⚠ Qt3 的 QTimer::singleShot 只有 (int, QObject*, const char*)
//   一个重载（qtimer.h:62），**没有 functor 版**，所以不能把 lambda 塞进去
//   （本文件 mainwindow.cpp:605 的 onFirstPaintComplete 是同款「记状态+连真槽」
//   手法）。这里先把参数**取到局部**再清成员：弹窗是嵌套事件循环，期间
//   m_pendingMenuId 若被改写，拿局部副本才不会作用到错误的贴纸上。
void StickerListPage::onDeferredMenuDialog()
{
    const int action = m_pendingMenuDialog;
    const QString id = m_pendingMenuId;
    const QString desc = m_pendingMenuDesc;
    // ⚠ 先清待办：弹窗返回后若还有嵌套派发，不应重入同一次动作。
    //   ⚠ 用 = QString() 而非 clear()：Qt3 的 QString 没有 clear()，
    //   只有 truncate(uint)/setLength(uint)（qstring.h:431/712）。
    m_pendingMenuDialog = 0;
    m_pendingMenuId = QString();
    m_pendingMenuDesc = QString();

    if (id.isEmpty()) {
        return;
    }
    switch (action) {
    case kPendingEditDesc: editStickerDescription(id, desc); return;
    case kPendingDelete: confirmDeleteSticker(id); return;
    default: return;
    }
}

// 缩放子菜单的 4 档。⚠ 下标即 onStickerScaleAction 的参数，与 lang/*.json 的
//   sticker_menu.scale_0_1 等键**成对对应**，增删必须同步改两处。
static const double kScaleFactors[4] = { 0.1, 0.25, 0.5, 2.0 };
static const char* const kScaleKeys[4] = {
    "sticker_menu.scale_0_1", "sticker_menu.scale_0_25",
    "sticker_menu.scale_0_5", "sticker_menu.scale_2_0"
};
// 成功 toast 里填的**纯数字**。⚠ 不要复用 kScaleKeys 的文案（那是菜单里的
//   「复制x0.1」），否则会拼成「已复制：复制x0.1」这种病句。
static const char* const kScaleNums[4] = { "0.1", "0.25", "0.5", "2.0" };

// 搜索引擎子菜单：5 项。⚠ 下标即 onStickerSearchAction / openSearchEngine 的
//   engine 参数（anysk 用 0/1/2/4 跳号，我这里连续编号，见下）。
//   ⚠ **3 = DuckDuckGo 没有 by-image 端点**，只能开图片搜索页让用户手传
//   （anysk stickerhomepage.cpp:1007-1010 同款，故 toast「需手动上传」）。
static const int kSearchEngineCount = 5;

void StickerListPage::onStickerContextRequested(int index, int contentX, int contentY)
{
    // ⚠ 信号与瓦片弹出之间列表可能被重建（搜索重排/分组切换），
    //   入口必须重新校验，不能信信号带来的下标。
    if (index < 0 || index >= m_grid->stickerCount()) {
        return;
    }
    showStickerMenu(index, contentX, contentY);
}

void StickerListPage::showStickerMenu(int index, int contentX, int contentY)
{
    // ⚠ 每次弹菜单先清上一次的：否则 QMenu 累积泄漏（anysk showStickerMenu:806-809
    //   用 findChildren<QskMenu*> 做同一件事；此处改用成员指针，因 Qt3 没有模板版
    //   findChildren，见头文件 m_ctxMenu 处注释）。
    // ⚠ 必须 deleteLater 不能 delete：此刻还在 mousePressEvent 的派发栈里，
    //   delete 会当场析构正在被弹出使用的菜单。
    if (m_ctxMenu) {
        m_ctxMenu->deleteLater();
    }

    m_currentItemIndex = index;
    // ⚠ 这里**不**缓存 StickerItem 引用/指针：菜单项回调发生在 popup() 之后，
    //   期间列表可能已重建，缓存的引用会悬空或指向别的贴纸。回调里一律走
    //   currentMenuSticker() 现取现校验。

    // parent=this：与页面同生命周期；LambdaSlot 也挂在菜单上，随菜单回收。
    Menu34* menu = new Menu34(this);
    m_ctxMenu = menu;
#ifdef QT3_BUILD
    // ⚠ Qt3 的 QPopupMenu 不继承父字体，不显式设会落到系统默认字体。
    menu->setFont(font());
    // ⚠ Qt3 的 QPopupMenu 无 setMinimumWidth（QMenu 才有）；不设下限的话
    //   长文案「编辑描述简介」会把弹窗挤得比文字还窄。
    menu->setMinimumWidth(180);
#endif

    // 顺序**必须**与 StickerMenuAction 枚举一致（头文件该枚举处已注明）。
    insertActionItem(menu, _(qFromUtf8("sticker_menu.copy")), [this]() {
        onStickerMenuAction(MenuCopy);
    });
    // 缩放子菜单
    Menu34* scaleSub = insertSubMenu(menu, _(qFromUtf8("sticker_menu.scale")));
    for (int i = 0; i < 4; i++) {
        insertActionItem(scaleSub, _(qFromUtf8(kScaleKeys[i])), [this, i]() {
            onStickerScaleAction(i);
        });
    }
    // 「预览」插在第 3 位（缩放子菜单之后、复制元信息之前），
    // 与 anysk stickerhomepage.cpp 的菜单顺序一致。
    insertActionItem(menu, _(qFromUtf8("sticker_menu.preview")), [this]() {
        onStickerMenuAction(MenuPreview);
    });
    insertActionItem(menu, _(qFromUtf8("sticker_menu.copy_meta")), [this]() {
        onStickerMenuAction(MenuCopyMeta);
    });
    insertActionItem(menu, _(qFromUtf8("sticker_menu.edit_desc")), [this]() {
        onStickerMenuAction(MenuEditDesc);
    });
    insertActionItem(menu, _(qFromUtf8("sticker_menu.share")), [this]() {
        onStickerMenuAction(MenuShare);
    });
    insertActionItem(menu, _(qFromUtf8("sticker_menu.delete")), [this]() {
        onStickerMenuAction(MenuDelete);
    });
    // 搜索子菜单
    Menu34* searchSub = insertSubMenu(menu, _(qFromUtf8("sticker_menu.search")));
    static const char* const kEngineKeys[kSearchEngineCount] = {
        "sticker_menu.engine_google", "sticker_menu.engine_bing",
        "sticker_menu.engine_yandex", "sticker_menu.engine_ddg",
        "sticker_menu.engine_lens"
    };
    for (int i = 0; i < kSearchEngineCount; i++) {
        insertActionItem(searchSub, _(qFromUtf8(kEngineKeys[i])), [this, i]() {
            onStickerSearchAction(i);
        });
    }

    // ⚠ 网格自管滚动（不在任何 QScrollView 里），所以内容坐标加视口原点即全局坐标。
    //   viewport 坐标 = 内容坐标 + m_scrollPos（见 mousePressEvent 的换算）。
    // ⚠⚠ 必须用 **m_grid->**mapToGlobal，不能用本页的 mapToGlobal：
    //   信号的 contentX/contentY 是**网格**坐标（网格自己按 m_scrollPos 画滚动，
    //   发射时 content = event->pos() - (0,m_scrollPos)）。加上 viewportPos()
    //   之后拿回的是**网格控件系**的 event->pos()，而网格是页面的子控件、
    //   位于顶栏与搜索行**之下**。若拿页面 mapToGlobal 去映射网格坐标，弹窗会
    //   整体下移「顶栏+搜索行」的高度，菜单跟右键位置对不上。
    const QPoint globalPos =
        m_grid->mapToGlobal(QPoint(contentX, contentY) + m_grid->viewportPos());
    // ⚠ popup() 是**非阻塞**的：它显示菜单后立刻返回，用户点击发生在之后。
    //   所以 m_currentItemIndex 必须**保持**为 index 直到用户点动作（或列表重建）。
    //   别在这里清掉 —— 清了的话每个动作槽都会因下标 -1 而直接返回。
    menu->popup(globalPos);
}

// 取当前菜单目标的瓦片；失效返回 0。
// ⚠ 每个动作槽都必须走这里 —— 菜单弹出后列表可能已重建（reloadActive 会把
//   m_currentItemIndex 置 -1），直接下标取会操作到另一张贴纸。
const StickerItem* StickerListPage::currentMenuSticker() const
{
    if (m_currentItemIndex < 0
        || m_currentItemIndex >= int(m_grid->stickers().size())) {
        return 0;
    }
    return &m_grid->stickers()[m_currentItemIndex];
}

void StickerListPage::onStickerMenuAction(int action)
{
    // ⚠ 所有分支共用「先取目标、失效即静默返回」这一道闸。菜单弹出后列表可能
    //   已重建（下标会指向别的贴纸），currentMenuSticker() 会挡掉。
    const StickerItem* item = currentMenuSticker();
    if (!item) {
        return;
    }
    // 各分支统一 touch_sticker：anysk copyScaled(:860) / copyStickerToClipboard 路径
    // 都会刷新 lastUsed，「最近」分组才排得对。软删分支不 touch（马上就删了）。
    switch (action) {
    case MenuCopy:
        touchSticker(item->id);
        if (!StickerOps::copyToClipboard(item->filePath)) {
            ToastWidget::show(this, _(qFromUtf8("sticker_msg.copy_failed")), 2000);
            return;
        }
        ToastWidget::show(this, _(qFromUtf8("sticker_msg.copied")), 2000);
        return;

    case MenuScaleSub:
        // 子菜单入口本身不做事：Qt 的子菜单由 QMenuData/QMenu 自己弹出。
        return;

    case MenuPreview:
        // ⚠ **不 touch**：anysk 里 touch 只挂在 copy/copyScaled/copyMeta 路径，
        //   单纯打开预览不改 lastUsed。
        openPreview();
        return;

    case MenuCopyMeta: {
        StickerMetaLite meta;
        if (!StickerOps::collectMeta(item->filePath, meta)) {
            ToastWidget::show(this, _(qFromUtf8("sticker_msg.meta_failed")), 2000);
            return;
        }
        touchSticker(item->id);
        // qltox photoviewer.cpp:92-105 同款：setText 递纯文本
        QApplication::clipboard()->setText(StickerOps::formatMeta(meta));
        ToastWidget::show(this, _(qFromUtf8("sticker_msg.meta_copied")), 2000);
        return;
    }

    case MenuEditDesc:
        // ⚠ 菜单此时正在派发栈里，若直接弹 QInputDialog::getText 的嵌套事件循环，
        //   菜单可能在嵌套循环中被销毁 → 等 dialog 返回后再用 menu 就是悬垂指针。
        //   anysk 对删除也踩过同样的坑（stickerhomepage.cpp:889-891 注释：QskMenu 的
        //   close().deleteLater 会在 question() 嵌套循环里被冲刷，随后 :491
        //   window() 打悬垂指针 SIGSEGV），解法是推迟到本次派发结束后再弹。
        //   这里用「记 pending + singleShot 连真槽」，因为 Qt3 的 singleShot
        //   收不了 lambda（见 onDeferredMenuDialog()）。
        m_pendingMenuDialog = kPendingEditDesc;
        m_pendingMenuId = item->id;
        m_pendingMenuDesc = item->description;
        QTimer::singleShot(0, this, SLOT(onDeferredMenuDialog()));
        return;

    case MenuShare:
        // ⚠ 桌面端没有系统分享表（anysk shareStickerFile 失败时 toast
        //   「桌面暂不支持分享」，stickerhomepage.cpp:884）。照搬该行为。
        touchSticker(item->id);
        ToastWidget::show(this, _(qFromUtf8("sticker_msg.share_unsupported")), 2000);
        return;

    case MenuDelete:
        // 与 MenuEditDesc 同款推迟：QMessageBox 是嵌套事件循环。
        m_pendingMenuDialog = kPendingDelete;
        m_pendingMenuId = item->id;
        m_pendingMenuDesc = QString();
        QTimer::singleShot(0, this, SLOT(onDeferredMenuDialog()));
        return;

    case MenuSearchSub:
        return;

    default:
        return;
    }
}

void StickerListPage::openPreview()
{
    // ⚠ 这里必须重取目标：菜单弹出后列表可能已重建（分组切换/搜索重排），
    //   m_currentItemIndex 可能已指向另一张贴纸。与所有动作槽同一道闸。
    const StickerItem* item = currentMenuSticker();
    if (!item) {
        return;
    }
    // 元信息读不出来也要能看图，只是底部留空，故 collectMeta 失败不中断。
    StickerMetaLite meta;
    QString metaText;
    if (StickerOps::collectMeta(item->filePath, meta)) {
        metaText = StickerOps::formatMeta(meta);
    }
    // 每次预览都新建（anysk stickerhomepage.cpp:1191-1201 同款：先把旧实例
    // deleteLater，再 new 一个）。本类**不存 overlay 指针**，一律靠
    // previewOverlays(this) 枚举，与 anysk 用 findChildren 的思路一致。
    const PreviewOverlayList olds = previewOverlays(this);
    for (int i = 0; i < olds.count; ++i) {
        olds.items[i]->deleteLater();
    }

    StickerPreviewOverlay* overlay = new StickerPreviewOverlay(this);
    // ⚠ deleteRequested 无参数（Qt3 菜单/信号都不能带参），故页面自己记住
    //   是哪一张，由 onPreviewDeleteRequested() 取用。
    connect(overlay, SIGNAL(deleteRequested()),
            this, SLOT(onPreviewDeleteRequested()));
    // closed → 自销毁，同 anysk :1200-1201（closed 绑 deleteLater）；
    // 同时通知页面复位 m_previewStickerId。
    connect(overlay, SIGNAL(closed()), overlay, SLOT(deleteLater()));
    connect(overlay, SIGNAL(closed()), this, SLOT(onPreviewClosed()));
    m_previewStickerId = item->id;
    // ⚠ 必须显式铺满页面：预览层是本页子控件且不在 layout 里，Qt 不会自动
    //   替它跟随页面尺寸（带 parent 构造的 QWidget 默认只有 100x30）。
    overlay->setGeometry(rect());
    overlay->showSticker(item->filePath, item->id, item->emoji, metaText);
}

void StickerListPage::onPreviewClosed()
{
    // 本类不存 overlay 指针，故这里无需置空；实例已由 closed→deleteLater 回收。
    // 只把 m_previewStickerId 复位，免得删除流程走完后残留一个失效 id。
    m_previewStickerId = QString();
}

void StickerListPage::onPreviewDeleteRequested()
{
    // ⚠ pending 只能在**这里**（用户真点了删除）填，不能在 openPreview() 里
    //   提前填 —— 否则用户按 Esc / 点空白关掉预览，也会在下次事件循环里
    //   弹出无来由的删除确认框。
    if (m_previewStickerId.isEmpty()) {
        return;
    }
    m_pendingMenuDialog = kPendingDelete;
    m_pendingMenuId = m_previewStickerId;
    m_pendingMenuDesc = QString();
    // 推迟到本次事件派发结束后再弹，理由同 MenuDelete 分支（QMessageBox
    // 是嵌套事件循环，预览层本身也要先关掉）。
    QTimer::singleShot(0, this, SLOT(onDeferredMenuDialog()));
}

void StickerListPage::onStickerScaleAction(int scaleIndex)
{
    if (scaleIndex < 0 || scaleIndex > 3) {
        return;
    }
    const StickerItem* item = currentMenuSticker();
    if (!item) {
        return;
    }
    touchSticker(item->id);
    bool fellBackToPng = false;
    const bool ok = StickerOps::copyScaledToClipboard(item->filePath,
                                                      kScaleFactors[scaleIndex],
                                                      &fellBackToPng);
    if (!ok) {
        ToastWidget::show(this, _(qFromUtf8("sticker_msg.copy_failed")), 2000);
        return;
    }
    // ⚠ 成功文案带档位号，用 _A() 的 {0} 占位（Translator::t 从 {0} 起算，
    //   translator.cpp 里按 args 下标替换）。⚠ Qt3 分支会把 {0} 换成 %1。
    //   回退 PNG 时改文案：原格式没有同格式编码器（动图三兄弟，编码器在批次
    //   4/5/6），提醒用户拿到的不再是原格式（§18.8）。
    ToastWidget::show(this,
        _A(qFromUtf8(fellBackToPng ? "sticker_msg.copied_scale_fallback"
                                   : "sticker_msg.copied_scale"), QStringList()
           << QString::fromUtf8(kScaleNums[scaleIndex])), 2000);
}

void StickerListPage::onStickerSearchAction(int engine)
{
    if (engine < 0 || engine >= kSearchEngineCount) {
        return;
    }
    const StickerItem* item = currentMenuSticker();
    if (!item) {
        return;
    }
    // ⚠ engine 3 = DuckDuckGo 没有 by-image 端点：只能开图片搜索页让用户手传
    //   （anysk stickerhomepage.cpp:1007-1010 同款，故 toast「需手动上传」）。
    if (engine == 3) {
        ToastWidget::show(this, _(qFromUtf8("sticker_msg.ddg_manual")), 2500);
        qOpenUrl(QString("https://duckduckgo.com/?iax=images&ia=images"));
        return;
    }
    // 其余 4 个引擎都要先把图传成公网直链（anysk startImageSearch 同款）。
    touchSticker(item->id);
    m_searchEngine = engine;
    if (!m_imageUploader) {
        // 只建一个复用（anysk 亦然）；parent=this 随页面回收
        m_imageUploader = new ImageTmpUploader(this);
        // ⚠ 字符串 connect 逐字对齐 moc 生成的签名（带 const 引用）
        connect(m_imageUploader, SIGNAL(uploaded(const QString&)),
                this, SLOT(onImageUploaded(const QString&)));
        connect(m_imageUploader, SIGNAL(failed(const QString&)),
                this, SLOT(onImageUploadFailed(const QString&)));
    }
    // ⚠ 单实例复用：上次在途的回包可能串到本次 engine 上，upload() 前先 cancel
    //   （ImageTmpUploader.h:21-22 的 cancel 正是为此）。
    m_imageUploader->cancel();
    ToastWidget::show(this, _(qFromUtf8("sticker_msg.uploading")), 2000);
    m_imageUploader->upload(item->filePath);
}

void StickerListPage::onImageUploaded(const QString& imageUrl)
{
    if (imageUrl.isEmpty()) {
        ToastWidget::show(this, _(qFromUtf8("sticker_msg.upload_failed")), 2000);
        return;
    }
    StickerListPage::openSearchEngine(m_searchEngine, imageUrl);
}

void StickerListPage::onImageUploadFailed(const QString& reason)
{
    // ⚠ reason 是图床侧的原始报错（含 host 名/网络细节），只进日志不进 UI：
    //   直接弹给用户等于泄露内部 host 拓扑。
    qWarning("StickerListPage: image upload failed: %s", qToUtf8(reason).data());
    ToastWidget::show(this, _(qFromUtf8("sticker_msg.upload_failed")), 2000);
}

// touch_sticker（刷新 lastUsed）。anysk copyScaled(:860)/copyStickerToClipboard 路径
// 都会刷新，「最近」分组才排得对。失败静默：排序刷新不是用户可见功能。
void StickerListPage::touchSticker(const QString& id)
{
    StickerOps::touch(id);
}

// 描述编辑：原生 QInputDialog。⚠ 140 字上限由调用方（DB 层）截断，这里照
//   anysk editStickerDescription 的做法先截，避免把超长文本塞进 DB 再被丢。
void StickerListPage::editStickerDescription(const QString& id,
                                             const QString& currentDesc)
{
    bool ok = false;
#ifdef QT3_BUILD
    // ⚠ Qt3 的 parent 是**倒数第二**个参数（qinputdialog.h:77-78），Qt4+ 挪到了
    //   最前面。两边顺序不同，必须分支，不能照抄 Qt4 文档的写法。
    const QString entered = QInputDialog::getText(
        _(qFromUtf8("sticker_menu.edit_desc")),
        _(qFromUtf8("sticker_msg.desc_prompt")),
        QLineEdit::Normal, currentDesc, &ok, this);
#else
    const QString entered = QInputDialog::getText(
        this,
        _(qFromUtf8("sticker_menu.edit_desc")),
        _(qFromUtf8("sticker_msg.desc_prompt")),
        QLineEdit::Normal, currentDesc, &ok);
#endif
    if (!ok) {
        return;     // 用户取消
    }
    if (entered == currentDesc) {
        return;     // 没改就不写库
    }
    QString desc = entered;
    if (desc.length() > 140) {
        desc = desc.left(140);
    }
    if (!StickerOps::setDescription(id, desc)) {
        ToastWidget::show(this, _(qFromUtf8("sticker_msg.desc_failed")), 2000);
        return;
    }
    ToastWidget::show(this, _(qFromUtf8("sticker_msg.desc_saved")), 2000);
    // 瓦片上的描述标签要跟着变
    reloadActive();
}

// 删除确认 + 软删。
// ⚠ 软删：只调 DB 的 delete_sticker，**不删磁盘文件** —— 与 anysk
//   deleteSticker 逐字一致（anysk/src/stickerstore.h:9-11 有同样说明）。
//   「删除」语义是移出列表/不再出现在最近，可从回收站或包里恢复。
void StickerListPage::confirmDeleteSticker(const QString& id)
{
    #ifdef QT3_BUILD
    // ⚠ Qt3 的 question() 返回 **int**、按钮是 enum{Yes=3,No=4,...}（qmessagebox.h:74），
    //   拿不到 StandardButton；Qt4+ 返回 StandardButton 且默认按钮单独传参。
    //   ⚠ 默认按钮给 No：误回车不该删东西。Qt3 无「默认按钮」概念，只能靠
    //   按钮顺序把 No 放后面（QMessageBox 的默认焦点取第一个按钮），
    //   故显式把 Yes/No 都传出来。
    const int ret = QMessageBox::question(
        this,
        _(qFromUtf8("sticker_menu.delete")),
        _(qFromUtf8("sticker_msg.delete_confirm")),
        (int)QMessageBox::Yes, (int)QMessageBox::No);
    if (ret != (int)QMessageBox::Yes) {
        return;
    }
#else
    // ⚠ Qt4/5/6 的 question() 重载**没有**带 icon 的 (parent,icon,title,text,
    //   buttons,default) 版本（qmessagebox.h:178-180 只有 title/text/buttons/default），
    //   带 icon 的那个返回 int 且已过时（:181）。想不出问号图标就别硬塞。
    const QMessageBox::StandardButton ret = QMessageBox::question(
        this,
        _(qFromUtf8("sticker_menu.delete")),
        _(qFromUtf8("sticker_msg.delete_confirm")),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);        // ⚠ 默认焦点给「否」
    if (ret != QMessageBox::Yes) {
        return;
    }
#endif
    if (!StickerOps::remove(id)) {
        ToastWidget::show(this, _(qFromUtf8("sticker_msg.delete_failed")), 2000);
        return;
    }
    ToastWidget::show(this, _(qFromUtf8("sticker_msg.deleted")), 2000);
    // 被删的正是在预览里看的那张：关掉全部预览，否则会留一张已不存在的图的窗口。
    // close() → closeEvent → closed → deleteLater（anysk :1216-1217 同款）。
    const PreviewOverlayList prevs = previewOverlays(this);
    for (int i = 0; i < prevs.count; ++i) {
        prevs.items[i]->close();
    }
    reloadActive();
}

void StickerListPage::openSearchEngine(int engine, const QString& imageUrl)
{
    // ⚠ 用 fromUtf8 而非 anysk 的 fromLatin1（stickerhomepage.cpp:1309/1311）：
    //   imageUrl 是图床返回的公网直链，含非 ASCII 路径段时 fromLatin1 会丢
    //   高位字节（AGENTS.md 编码纪律）。pctEncode 三分支见本文件该函数。
    const QString enc = pctEncode(imageUrl);
    // 固定的图+关键词偏置（anysk :1311-1313 同款，后续可换成弹窗输入的值）
    const QString kw = pctEncode(QString::fromUtf8("相似表情包"));
    QUrl url;
    switch (engine) {
    case 0: // Google
        url = QUrl(QString("https://www.google.com/searchbyimage?image_url=") + enc
                    + QString("&gl=US&hl=en&q=") + kw);
        break;
    case 1: // Bing
        url = QUrl(QString("https://www.bing.com/images/searchbyimage?cbir=sbi&imgurl=")
                    + enc + QString("&q=") + kw);
        break;
    case 4: // Google Lens
        url = QUrl(QString("https://lens.google.com/uploadbyurl?url=") + enc
                    + QString("&gl=US&hl=en&q=") + kw);
        break;
    default: // 2 = Yandex（anysk 也用 default 兜底）
        url = QUrl(QString("https://yandex.com/images/search?url=") + enc
                    + QString("&rpt=imageview&text=") + kw);
        break;
    }
    if (url.isValid()) {
        qOpenUrl(url.toString());
    }
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
