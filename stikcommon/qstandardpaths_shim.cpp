// QStandardPaths 垫片实现（Qt3/Qt4）。算法与 Qt5/6 的 AppLocalDataLocation 对齐，
// 黄金基准见 qstandardpaths_shim.h 顶部注释（Qt 6.7.3 真机实测）。

#include "qstandardpaths_shim.h"
#include "qstring_shim.h"   // QLatin1Char：Qt3 由 qstring_shim 提供

#ifdef QT3_BUILD
#include <qdir.h>
#include <qfileinfo.h>
#else
#include <QDir>
#include <QFileInfo>
#endif

#if QT_VERSION >= 0x040000
#include <QCoreApplication>
#else
// Qt3.5 无 QCoreApplication，qstandardpaths_shim.h 已 include qcoreapplication_shim.h
#endif

#include <stdlib.h>
#include <string.h>

// 可执行文件名（不含目录、不含扩展名）——app 名为空时的回退，Qt5/6 同此行为。
// Qt4 的 QCoreApplication::applicationFilePath() 是静态的但要实例存在才返回
// 有值，否则打一声"Please instantiate"警告并回退空串；这里显式判 qApp，规避
// 警告且行为一致（无实例 → 空名 → 调用方不再追加 app 段）。
static QString shim_execBaseName()
{
    if (qApp) {
        return QFileInfo(qApp->applicationFilePath()).baseName();
    }
    return QString();
}

QString QStandardPaths::writableLocation(StandardLocation type)
{
    if (type != AppLocalDataLocation) {
        return QString();
    }

    // 1) base = $XDG_DATA_HOME，仅当它是绝对路径时采纳
    //    （Qt5/6 实测：未设、空串、相对路径都回退到 $HOME/.local/share）
    QString base;
    const char* xdg = getenv("XDG_DATA_HOME");
    if (xdg && *xdg && !QDir::isRelativePath(QString::fromUtf8(xdg))) {
        base = QString::fromUtf8(xdg);
    } else {
        const char* home = getenv("HOME");
        const QString h = home ? QString::fromUtf8(home) : QString();
        // HOME 为空时 Qt 落到当前目录下的 .local/share，而不是空串
        base = h.isEmpty() ? QString(".local/share")
                           : h + QString("/.local/share");
    }

    // 2) 追加组织名（非空才加）
    const QString org = QCoreApplication::organizationName();
    if (!org.isEmpty()) {
        base += QLatin1Char('/') + org;
    }

    // 3) 追加应用名（为空则回退可执行名，与 Qt5/6 一致）
    QString app = QCoreApplication::applicationName();
    if (app.isEmpty()) {
        app = shim_execBaseName();
    }
    if (!app.isEmpty()) {
        base += QLatin1Char('/') + app;
    }

    return base;
}
