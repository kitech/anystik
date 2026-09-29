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
};

#endif // QLSTIK_QCOREAPPLICATION_SHIM_H
