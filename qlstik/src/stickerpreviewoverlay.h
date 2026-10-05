#ifndef QLSTIK_STICKERPREVIEWOVERLAY_H
#define QLSTIK_STICKERPREVIEWOVERLAY_H

// 贴纸放大预览层（移植 anystik StickerPreviewOverlay）。
//
// ⚠ **anysk 原实现无法移植**：它是 QQuickItem 子类 + QSGImageNode
//   （stickerpreviewoverlay.h:13、.cpp:278），走 Qt Quick 场景图，那是 Qt6 专属。
//   故这里按「同样的可见行为 + 同样的交互命中区」用纯 QWidget::paintEvent
//   从零重写，**不逐行对齐绘制代码**。
//
// ── 行为规格（逐条摘自 anysk stickerpreviewoverlay.cpp）──
//   遮罩   : 全页黑 200 alpha                            (:43)
//   ✕ 钮   : (w-56,12,44,44) 圆角12 底(40,40,50,220) ✕24px (:57-65,:83)
//   动作栏 : 高52 底(20,20,28) 三等分 复制/Emoji/删除      (:67-88)
//   元信息 : 高150 底(18,18,26) 14px(200,200,210)          (:19-39)
//   图片   : contain，scale=min(availW/w,availH/h)，**小图也放大**（无上限
//            钳制，刻意与 anysk 一致），居中上移 (ACTION_H+META_H)/2 (:41-54)
//   命中区 : 元信息区→复制元信息；✕/空白/Esc→关；
//            动作栏左1/3→复制、右1/3→删除确认 (:358-389,:334-342)
//
// ── Qt3 适配（下列结论均在本机 toolchain 逐个实测 + 官方文档交叉核对）──
//   1. 无 QPainterPath，且 drawRoundedRect 是 Qt4.1 才加的 → 圆角在 Qt3 分支
//      用原生 drawRoundRect(int×6)（qpainter.h:202；Qt4 起改名，故必须分支）。
//   2. 无 setRenderHint/RenderHint 枚举（qpainter.h 全无，Qt 4.3 才引入）
//      → Qt3 下无抗锯齿、缩放不平滑。缩放改为在**换帧/resize 时一次性**
//      smoothScale 成缓存（见 rebuildCache 注释），paintEvent 只 1:1 blit。
//   3. 换行标志：Qt3 叫 **Qt::WordBreak**（qnamespace.h:128，=0x0800），
//      Qt4 起改名 **Qt::TextWordWrap**（同一位值）→ 两分支各写各的名字，
//      行为与 anysk :36 完全一致。
//   4. 半透明遮罩：Qt4+ 用 WA_TranslucentBackground 保持 anysk 原值；
//      **Qt3 主动降级为不透明**（详见 backdropColor 注释）。
//   5. 事件坐标用 event->pos()（Qt3 QMouseEvent 无 position()/globalPosition()）。
//   6. Qt3 无 underMouse() → 光标在 mouseMoveEvent 里按区域判定。
//   7. Qt3 无 QString::split（stock Qt3 本来就没有，Qt4 才加）→ 需 split 时
//      走仓内 qSplit(str, sep)（compatcore34.cpp:160-166）。
//
// ── 动图：实测矩阵（探针 /tmp/opencode/probe-preview/probe_apng3*，非推测）──
//   anysk 走 QMovie 优先 + QImageReader 回退，且自注「APNG 只能静帧」
//   （stickerpreviewoverlay.cpp:31-32）。本机 Qt3 的 QMovie 只支持 MNG/GIF，
//   plugins/imageformats 里也只有 libqmng.so（无 libqgif.so），故**不用
//   QMovie**，改用本仓 qimagereader_shim.h（GIF→vendor/libnsgif、
//   WebP→libwebp、APNG→vendor/uc_apng_loader），nextImageDelay() 已是毫秒。
//   实测逐帧结果：
//     格式        Qt3(本仓 shim)              Qt6(系统插件)
//     PNG 静态    1 帧                          1 帧
//     GIF         3 帧动，delay 正确            3 帧动，delay 正确
//     WebP        3 帧动，delay 正确            3 帧动，delay 正确
//     APNG 带alpha 3 帧动，delay 正确           **1 帧静**（同 anysk）
//     APNG 无alpha **解码失败**（见 .cpp 注释） **1 帧静**
//   即：GIF/WebP 两版都能动，比 anysk 的 QMovie 覆盖广；APNG 在 Qt6 与 anysk
//   同为静帧，**不要**写成「APNG 也能播」。
//
// ⚠ 帧推进只能靠**反复 read()**：shim 的 read() 已无条件自增游标
//   （qimagereader_shim.cpp:525 的 ++m_index），再调 jumpToNextImage() 会跳帧；
//   而 read() 耗尽后不回头，末尾只能重建 reader 循环。见 onAnimTick 注释。

#include "compat34.h"

#ifdef QT3_BUILD
#include <qwidget.h>
#include <qstring.h>
#include <qimage.h>
#include <qpixmap.h>
#include <qtimer.h>
#else
#include <QWidget>
#include <QString>
#include <QImage>
#include <QPixmap>
#include <QTimer>
#endif

class StickerPreviewOverlay : public QWidget {
    Q_OBJECT
public:
    explicit StickerPreviewOverlay(QWidget* parent = 0);
    // ⚠ 必须显式析构：m_reader（qimagereader_shim.h 的 QImageReader）不是 QObject，
    //   挂不上 parent，Qt 不会替我们回收。
    ~StickerPreviewOverlay();

    // 打开预览。metaText 传 StickerOps::formatMeta() 的多行拼接串。
    void showSticker(const QString& filePath,
                     const QString& id,
                     const QString& emoji,
                     const QString& metaText);

signals:
    void closed();
    // 请求删除。由 StickerListPage 走已有的 pending 延迟确认机制处理
    // （anysk :1195-1199 同款：确认框开嵌套事件循环，须延迟弹）。
    void deleteRequested();

protected:
    void paintEvent(QPaintEvent* event);
    void mousePressEvent(QMouseEvent* event);
    void mouseMoveEvent(QMouseEvent* event);
    void keyPressEvent(QKeyEvent* event);
      void resizeEvent(QResizeEvent* event);
      // ⚠ 动画定时器必须在隐藏时停掉：本控件**每次预览都新建**（同 anysk），
      //   但 QWidget::close() 走完 Close 事件只是隐藏、此刻对象可能还没被
      //   deleteLater 回收 —— 定时器若不停就会在看不见的时候继续逐帧解码空转。
      void hideEvent(QHideEvent* event);
      // ⚠ 唯一权威的关闭出口：发 closed() + 停动画。页面侧和鼠标/Esc 全部
      //   改走 close()，保证「谁关的」只有这一条路径，页面才能靠
      //   onPreviewClosed() 复位 m_previewStickerId。
      void closeEvent(QCloseEvent* event);
      // ⚠ 预览层是 StickerListPage 的**子控件**且不进任何 layout，故页面
      //   resize 时 Qt 不会替我们改几何，必须自己跟（见 cpp 里 eventFilter）。
      //   走 eventFilter 而非改 Page 的 resizeEvent，是为了不侵入既有链。
      bool eventFilter(QObject* watched, QEvent* event);
  
  private slots:
    // ⚠⚠ **必须是槽**：Qt3 的 QTimer::singleShot/connect 只能按 moc 注册的槽名
    //   查找。若把 onAnimTick() 放在普通 private: 里（moc 只扫 slots 段），
    //   connect 会静默失败并打 "No such slot" 警告，动图**永远不播**。
    //   探针 /tmp/opencode/probe-preview/probe_layout 就是靠这条警告抓到的。
    void onAnimTick();

private:
    // 布局常量，1:1 取自 anysk stickerpreviewoverlay.cpp:16-21
    enum {
        kCloseBtnSize = 44, kActionH = 52, kMetaH = 150,
        kMetaPadLeft = 16, kMetaPadTop = 10
    };

    // anysk 是 inline 判断；拆成函数便于 xvfb 探针直接断言命中区
    QRect closeButtonRect() const;
    QRect metaRect() const;
    QRect actionBarRect() const;
    QRect scaledImageRect() const;
    bool metaRegionContains(const QPoint& p) const;
    bool actionBarContains(const QPoint& p) const;
    bool isInteractive(const QPoint& p) const;
    QColor backdropColor() const;

    // ⚠ 缩放缓存：**手法照 qltox/photoviewer.cpp 的 PhotoCanvas**
    //   （rebuildCache :256-269 把缩放结果预渲染进 m_cachedPixmap；
    //     paintEvent :271-281 只做 1:1 drawPixmap blit）。
    //   不这么做的代价：Qt3 的 drawPixmap(QRect,pm) **每次调用**都重跑一遍
    //   QImage::smoothScale（stickerlistpage.cpp:168-169 已实测记录），
    //   动图每帧调一次 = 每帧一次全图缩放，滚动/动画都会卡。
    void rebuildCache();

    // 建/重建解码句柄。循环播放到末尾时也走这里回到首帧（reason 见 onAnimTick）。
    bool openReader();
    void stopAnimation();
    void copyMetaToClipboard();

    // 完整类型只在 .cpp 用；这里只需指针，前置声明即可（避免把 shim 泄进头文件）
    class QImageReader* m_reader;
    QString m_filePath, m_id, m_emoji, m_metaText;
    QImage  m_image;          // 当前帧（原始尺寸）
    int     m_frameCount;     // 结构扫描帧数；>1 才起动图
    bool    m_animated;
    // member QTimer：避开 Qt3 无 functor 版 singleShot 的坑，也免 new/delete
    QTimer  m_animTimer;
    QPixmap m_cachedPixmap;   // 按 contain 比例预渲染好的当前帧
};

#endif // QLSTIK_STICKERPREVIEWOVERLAY_H
