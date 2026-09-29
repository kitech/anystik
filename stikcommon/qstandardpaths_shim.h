#ifndef QLSTIK_QSTANDARDPATHS_SHIM_H
#define QLSTIK_QSTANDARDPATHS_SHIM_H

// QStandardPaths 垫片（Qt3/Qt4 专用）。
//
// QStandardPaths 是 Qt 5.0 才引入的，Qt3.5 / Qt4.8.7 均无此头文件。本垫片只实现
// davbisync_baseline.cpp 需要的 AppLocalDataLocation 写路径，算法与 Qt5/6 逐条对齐
// （基准由 Qt 6.7.3 真机探针实测，见下方"黄金基准"）。
//
// Qt5+ 走原生 QStandardPaths，本头不参与编译——只在
// `#if QT_VERSION < 0x050000` 分支里被 include。
//
// ── AppLocalDataLocation 判定链（Linux/XDG，与 Qt5/6 一致）─────────────────
//   1. base = $XDG_DATA_HOME，且必须是绝对路径
//      · 未设置 / 空串      → $HOME/.local/share
//      · 相对路径（非法）    → $HOME/.local/share   （Qt 同样忽略非法值）
//   2. organizationName 非空 → 追加 /<org>
//   3. applicationName 非空 → 追加 /<app>
//      · 为空              → 回退到可执行文件名（不含目录、不含 .exe）
//
// ── 黄金基准（Qt 6.7.3 实测，环境变量逐项压过）────────────────────────────
//   org=fedlet app=anystik → $HOME/.local/share/fedlet/anystik
//   org=<none>  app=qlstik → $HOME/.local/share/qlstik
//   org=<none>  app=<空>   → $HOME/.local/share/<可执行名>
//   XDG_DATA_HOME=""        → 同未设置（回退 $HOME）
//   XDG_DATA_HOME=rel/path  → 同未设置（非法 → 回退）
//   XDG_DATA_HOME=/abs      → /abs/<org?>/<app>
//   HOME 覆盖               → 随 HOME 走，XDG 优先于 HOME
//
// 关键：$HOME 为空且 XDG_DATA_HOME 非法时，Qt 会回退到当前目录下的
// ".local/share"（而不是空串），本垫片照此行为实现。

#ifdef QT3_BUILD
// Qt3.5 无 CamelCase 转发头
#include <qstring.h>
#else
#include <QString>
#endif

// Qt3.5 没有 QCoreApplication（Qt4 才引入），已有 qcoreapplication_shim.h 提供
// 该类；本垫片只在 Qt3 分支里把 app/org 名的存取补到那个类上，Qt4 用原生静态 API。
#if QT_VERSION < 0x040000
#include "qcoreapplication_shim.h"
#endif

class QStandardPaths
{
public:
    enum StandardLocation {
        DesktopLocation,
        DocumentsLocation,
        FontsLocation,
        ApplicationsLocation,
        MusicLocation,
        MoviesLocation,
        PicturesLocation,
        TempLocation,
        HomeLocation,
        // 新增项一律排最后，避免打乱 Qt 原生枚举序（本垫片顺序与 Qt5/6 一致）
        AppLocalDataLocation
    };

    static QString writableLocation(StandardLocation type);
};

#endif // QLSTIK_QSTANDARDPATHS_SHIM_H
