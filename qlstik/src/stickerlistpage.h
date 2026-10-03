#ifndef QLSTIK_STICKERLISTPAGE_H
#define QLSTIK_STICKERLISTPAGE_H

// 贴纸家页面 —— 功能规格照搬 anystik/src/stickerlist.cpp + stickerhomepage.cpp。
// 写法照搬 qldox/stickerpicker.cpp + chatview.cpp（数据用 std::vector、布局用
// qNewBoxLayout/qSetMargins、滚动用 LimeScrollBar、#ifdef 贴近使用处）。
//
// ── 为何不用 QScrollView / QGridLayout ──
// 上千张贴纸时 layout 方案 = 每项一个常驻 widget（3000 项即 3000 widget），
// 与 anystik「离屏瓦片根本不存在」不一致（stickerlist.cpp:320-330 只画可视行区间）。
// 这里照搬：m_items 只存元数据，paintEvent 只画可视区间的瓦片。
//
// ═══════ Qt3 绘图/图像层实测结论（g++ -fsyntax-only + xvfb 真机）═══════
//
//  1. **Qt3.5 无 QPainterPath**（整个 include 目录无此头文件），anysk 的
//     `clip.addRoundedRect` + `setClipPath` 用不了。
//     ✅ 但**根本不需要裁剪**：anystik 图片框 inner = 76-14 = 62 居中于 76×76，
//     圆角半径 12 的角圆心在 (12,12)，图片角点 (7,7) 到该圆心距离
//     √(25+25)=7.07 < 12 落在圆内 —— 图片永远碰不到被切掉的角。
//     → 圆角背景直接用 Qt3 原生 `drawRoundRect` + 画刷填充。
//
//  2. **无 `setRenderHint()`，也无 `Antialiasing`/`SmoothPixmapTransform` 枚举**
//     （Qt3 只有 `setf(uint)`）。→ 全页无抗锯齿。唯一无法弥补的视觉差异。
//
//  3. **⚠ Qt3 画 QPixmap 不做 alpha 混合**（xvfb 实测：32 位 QImage 逐像素写
//     qRgba(0,0,0,150) → convertFromImage 返回 true 但 depth 降为 24、alpha
//     被丢弃；贴到白底读回 R=0 纯黑，期望 ≈105）。且 QColor 无 setAlpha()/
//     alpha()/4 参构造。
//     → anysk 角标底 `QColor(0,0,0,150)` 只能用**不透明黑**替代。
//
//  4. **QImage 无 `Format_ARGB32` 等 `Format_*` 枚举**（Qt3 用 depth 位深）；
//     `QImage(w,h,1)`（位深 1）**无效**（实测 width 变 0、setPixel 全报越界）。
//
//  5. **无 `QPixmap::fromImage()`**（Qt4 才有），只有 `convertFromImage()`；
//     **QPainter 也画不到 QImage 上**（Qt3 构造只收 QPaintDevice*）。
//
//  6. **qimagereader_shim 已提供 `setScaledSize()`**（2026-10-04 惰性解码改造时
//     补上，对齐 Qt6 的不支持原生 ScaledSize 时的回退缩放）→ 缩略图改用
//     `setScaledSize(152,152)` + 单次 `read()`，shim/插件内部平滑缩放当前帧，
//     动画不再预解全部帧。
//
//  7. **QFontMetrics 无 `elidedText()` / `horizontalAdvance()`**，**无 `qBound`**。
//     → 宽度用 qlcomp 的 `qFontWidth()`，裁剪用 `qElideChars()`，clamp 手写。
//
//  8. **Qt3 QTimer 无 `setSingleShot()`** → 用 qltox messageinput.cpp:236 的
//     `start(ms, true)`（第二参 repeat=false 即单次）。
//
//  9. **QObject 无 `setProperty()`**（Qt3 无动态属性）→ tab 下标配数组存。
//
// 10. **QWidget 无 `setAutoFillBackground()`**（Qt4+）→ 背景在 paintEvent 自填。
//
// 11. **QScrollBar 无默认 parent 构造**（`QScrollBar(Orientation, QWidget*)` 的
//     parent 无默认值）；`setLineStep/setPageStep/lineStep/maxValue` 都存在。
//     滚动装配照 chatview.cpp:2310-2322。
//
// 12. **Qt3 QButtonGroup 信号是 `clicked(int id)`**，Qt4+ 是 `buttonClicked(int)`。
//
// 13. ⚠⚠ **Qt3 的 `QVector<T>` / `QList<T>` 是 `QPtrVector<T>`（指针语义）**，
//     非指针类型存进去会全变成 `T*`。→ 数据一律用 `std::vector`（qltox 同）。
//
// 14. **Qt3 无 `qbytearray.h` / `qboxlayout.h`**：QByteArray 在 `qstring.h`
//     （且 Qt3.5 里 `QByteArray` 只是 `typedef QMemArray<char>`，不是字符串类，
//     无 `const char*` 构造、无隐式转换）；QVBoxLayout/QHBoxLayout 在 `qlayout.h`。
//     → 传 `const char*` 给 SQLite C API 用 `QCString`（有 `operator const char*`）。
//
// 其余跨版本差异见 page.h 顶部「Qt3 容器铁律」10 条。

#include "compat34.h"
#include "LimeScrollBar.h"
#include "page.h"
#include "placeholderlineedit.h"

#include <set>
#include <vector>

#ifdef QT3_BUILD
#include <qwidget.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qmap.h>
#include <qlayout.h>
#include <qtimer.h>
#include <qpixmap.h>
#else
#include <QWidget>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QPixmap>
#endif

class QLabel;
class QLineEdit;
class QPushButton;
class QComboBox;
class QButtonGroup;
class QTimer;

// ── 网格常量（anystik stickerlist.cpp:18-20 逐条照搬）──
// 放文件作用域：.cpp 的 static 解码函数也要用，不能放类私有枚举。
enum {
    kTileSize   = 76,        // TILE_SIZE
    kTileGap    = 10,        // TILE_GAP（只参与 STEP 计算）
    kStep       = 86,        // STEP = TILE_SIZE + TILE_GAP
    kTileRadius = 12,        // stickerlist.cpp:44
    kThumbPx    = 152,       // stickerlist.cpp:131 TILE_SIZE*2
    kImgInner   = 62,        // stickerlist.cpp:66 inner = TILE_SIZE - 14
    kTagH       = 14,        // stickerlist.cpp:96
    kTagRadius  = 4,         // stickerlist.cpp:110
    kTagMargin  = 3,         // stickerlist.cpp:104
    kCacheMax   = 400        // stickerlist.cpp:135
};

enum { kSearchDebounceMs = 350 };   // anysk stickerhomepage.cpp:530-534

// ── 贴纸元数据 ──
// ⚠ 字段对齐 anystik/src/stickerstore.h 的 StickerBrief，但**不复用**那个结构体
//   （在 anystik/ 里，只读）。数据源是 qldox/sticker_db.h 的 StickerRow
//   （Qt-free，std::string 字段；qldox.pri 已把 db 层 7 件挂进 qlstik 构建）。
struct StickerItem {
    QString id;
    QString packId;
    QString filePath;      // **绝对路径**（已拼贴纸根目录）
    QString emoji;
    int     width;
    int     height;
    // ⚠ 用 long long 而非 Q_LLONG：qlcomp/compatcore34.h:163 自己就无条件用
    //   long long，qlcomp 没有 Q_LLONG 的 Qt4+ 垫片
    long long size;        // sticker_db.h:24 是 int；放宽只为 MB 换算不溢出
    long long lastUsed;
    QString description;

    StickerItem() : width(0), height(0), size(0), lastUsed(0) {}
};

// ═════════ StickerGridWidget：自绘贴纸网格 ═════════
// 直接继承 QWidget，**不放进任何滚动容器**；滚动由自管 LimeScrollBar 负责
// （与 qltox ChatView 同构，见 chatview.h:158）。
class StickerGridWidget : public QWidget {
    Q_OBJECT
public:
    explicit StickerGridWidget(QWidget* parent = 0);
    virtual ~StickerGridWidget();

    // ⚠ 照搬 stickerlist.cpp:282-283：先废缓存再赋值（包更新后同路径内容已变）
    void setStickers(const std::vector<StickerItem>& stickers);
    int  stickerCount() const { return int(m_items.size()); }
    // ⚠ updateCountLabel 照搬 anysk stickerhomepage.cpp:772-781：要按**当前
    //   网格里这批贴纸**的 packId 去重算「N 包」，不是用包总数（:630-632 的
    //   m_packs.size()）——「最近」/ 搜索结果的包数天然小于包总数。
    const std::vector<StickerItem>& stickers() const { return m_items; }

signals:
    void stickerClicked(const QString& filePath);

protected:
    void paintEvent(QPaintEvent* event);
    void wheelEvent(QWheelEvent* event);
    void resizeEvent(QResizeEvent* event);
    void mousePressEvent(QMouseEvent* event);

private slots:
    void onScrollChanged(int value);

private:
    void relayout();
    void drawTile(QPainter& p, int index, int x, int y);
    int  indexAt(const QPoint& contentPos) const;

    std::vector<StickerItem> m_items;
    // ⚠ 缩略图缓存必须是**类成员**，不能是文件作用域 static：
    //   Qt3 的 QMapPrivate 构造时会默认构造一个哨兵 QPixmap（qmap.h:436），
    //   QPixmap 是 QPaintDevice —— 若写成文件作用域 static，它在静态初始化期
    //   （main() 之前、QApplication 之前）构造，直接 qFatal：
    //   "QPaintDevice: Must construct a QApplication before a QPaintDevice"。
    //   gdb 实证栈：QMap<QString,QPixmap>::QMap → QMapNode → QPixmap::QPixmap。
    //   qltox 同样只用成员容器（唯一那个 QMap<QString,QString> 值是 QString，
    //   不需要 QApplication）。
    QMap<QString, QPixmap> m_tileImageCache;

    int m_cols;
    int m_rows;
    int m_scrollPos;              // = qltox m_scrollPos
    int m_scrollDelta;            // 滚轮 delta 累加（chatview.cpp:3167）
    LimeScrollBar* m_vBar;        // = qltox m_vScrollBar
};

// ═════════ StickerListPage：贴纸家页面 ═════════
class StickerListPage : public Page {
    Q_OBJECT
public:
    explicit StickerListPage(QWidget* parent = 0);
    virtual ~StickerListPage();

protected:
    virtual void onCreate(const QVariantMap& launchArgs,
                          const QVariantMap& savedState);
    virtual void retranslateUi();
    virtual void onSaveInstanceState(QVariantMap& outState);
    virtual void onRestoreInstanceState(const QVariantMap& savedState);

private slots:
    void onSearchTextChanged(const QString& text);
    void onSearchTimeout();
    void onTabChanged(int id);
    void onPackComboChanged(int index);
    void onStickerClicked(const QString& filePath);
    // 顶栏/底栏按钮：布局与文案照搬 anysk，行为按本批范围留空
    void onTopBarButton();
    void onBottomButton();

private:
    void buildUi();
    void buildTopBar(class QBoxLayout* parent);
    void buildSearchRow(class QBoxLayout* parent);
    void buildTabBar(class QBoxLayout* parent);
    void buildBottomBar(class QBoxLayout* parent);
    void refreshTabBar();
    void reloadActive();

    // 数据加载（anystik stickerhomepage.cpp:749-769 的三个 loader）
    void loadAllStickers();
    void loadRecentStickers();
    void loadPackStickers(const QString& packId);
    void applySearch(const QString& keyword);
    void updateCountLabel();

    enum LoaderKind { LoaderAll, LoaderRecent, LoaderPack, LoaderSearch };

    StickerGridWidget* m_grid;

    // ── TopBar（stickerhomepage.cpp:408-511）──
    QLabel*      m_title;
    QPushButton* m_pasteBtn;
    QPushButton* m_importBtn;
    QPushButton* m_syncBtn;
    QPushButton* m_moreBtn;

    // ── searchRow（:519-528）：搜索框 + 右侧计数，同一行 ──
    // ⚠ 类型必须是 PlaceholderLineEdit 而非 QLineEdit：Qt3 QLineEdit 没有
    //   setPlaceholderText()（Qt4.1+ 才有），retranslateUi 要重设占位文案。
    //   基类指针会丢掉那个 setter。
    class PlaceholderLineEdit* m_searchLine;
    QLabel*    m_countLabel;

    // ── tabBarBox（:536-565）：Tab[全部│最近│粘贴板] + 右侧包下拉 ──
    QWidget*     m_tabBarBox;
    QPushButton* m_tabButtons[3];   // 最多 3 个：全部 / 最近 / 粘贴板
    QComboBox*   m_packCombo;
    int          m_currentTab;      // = QskTabBar::currentIndex()（本批用按钮数组代替）

    // ── bottomBar（:591-623）：首页 / 生成表情 / 在线表情 / 设置 ──
    QPushButton* m_bottomHome;
    QPushButton* m_bottomGen;
    QPushButton* m_bottomOnline;
    QPushButton* m_bottomSettings;

    LoaderKind m_currentLoader;
    int        m_countStickers;
    int        m_countPacks;
    // refreshTabBar 里把「粘贴板」从普通包中摘出（:657-671）
    QStringList m_packIds;
    QStringList m_packTitles;
    QString     m_pastePackId;

    // ── 当前激活分组（照搬 anysk 的 m_activeTab，stickerhomepage.cpp:706/714/718）──
    //   ""        → 全部（index 0）
    //   "__recent"→ 最近（index 1）
    //   其余      → pack id（粘贴板 tab 或下拉选中的包）
    // ⚠ 它的存在是为了 reloadActive()：分组被卸载时能回退到「全部」而不是
    //   留下空白网格（stickerhomepage.cpp:721-737）。
    QString m_activeTab;

    // ⚠ 刷新下拉时抑制 currentIndexChanged 回调：clear()+addOption() 本身
    //   就会触发信号（stickerhomepage.cpp:687-694 靠 Qsk 的 -1 占位态天然规避，
    //   本实现用占位项替代，见 refreshTabBar 注释）
    bool m_refreshingCombo;

    QTimer* m_searchTimer;
};

#endif