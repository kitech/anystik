#ifndef QLSTIK_QCOREAPPLICATION_SHIM_H
#define QLSTIK_QCOREAPPLICATION_SHIM_H

// Qt 3.5 没有 QCoreApplication（该类随 Qt 4 引入），也没有静态的
// QApplication::installTranslator / removeTranslator / translate（Qt4 才把它们
// 提到 QCoreApplication 的静态 API）。Qt3 下这几个动作都挂在全局 qApp 上，
// 故这里用同名类 + 静态包装把 Qt4 的调用形态补齐，源码零改动。
//
// 限制：Qt3 不存在脱离 QApplication 的 QCoreApplication 实例，
//       instance() 只能返回 qApp（GUI 程序语义等价）。

#include <qapp.h>
#include <qtranslator.h>

class QCoreApplication {
public:
    static void removeTranslator(QTranslator* t) {
        if (qApp) qApp->removeTranslator(t);
    }
    static void installTranslator(QTranslator* t) {
        if (qApp) qApp->installTranslator(t);
    }
    static QString translate(const char* context, const char* sourceText,
                             const char* disambiguation = 0) {
        return qApp ? qApp->translate(context, sourceText, disambiguation)
                    : QString::fromUtf8(sourceText);
    }
    static QApplication* instance() { return qApp; }

    // 应用名/组织名：Qt4 才把它们做成 QString 静态 API。Qt3 下应用名存于
    // QObject::name()（const char* 形态），且没有"组织名"概念——org 恒为空，
    // 这与 qlstik 的现状一致（不设 org，故 AppLocalDataLocation 少一层目录）。
    static QString applicationName() {
        return qApp ? QString::fromUtf8(qApp->name()) : QString();
    }
    static void setApplicationName(const QString& name) {
        if (qApp) {
#ifdef QT3_BUILD
            qApp->setName(name.utf8().data());
#else
            qApp->setName(name.toUtf8().data());
#endif
        }
    }
    static QString organizationName() { return QString(); }
    static void setOrganizationName(const QString&) { }
};

#endif // QLSTIK_QCOREAPPLICATION_SHIM_H
