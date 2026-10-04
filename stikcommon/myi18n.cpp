#include "myi18n.h"
#include "settings_trace.h"

#include "qstring_shim.h"   // qUtf8Printable 在 Qt<5.10 缺失
#ifdef QT3_BUILD
#include <qlocale.h>
#include <qsettings.h>
#else
#include <QLocale>
#include <QMetaObject>
#include <QSettings>
#endif
#include <utility>

Lang& Lang::instance() {
    static Lang g;
    return g;
}

Lang::Lang() {
#if defined(QT3_BUILD)
    // Qt 3.5 的 QSettings 只有 readEntry/writeEntry（QVariant 版 value/setValue 是
    // Qt4 才有的），且同名派生不可行——qapp.h 已提前引入 QSettings 定义。
    const QString saved = QSettings().readEntry("language", QString());
#else
    const QString saved = QSettings().value("language", QString()).toString();
#endif
    trace_settings("i18n-language-read");
    if (saved.isEmpty()) {
        const QLocale sys = QLocale::system();
        if (sys.language() == QLocale::English) {
            m_code = "en";
        } else if (sys.language() == QLocale::Chinese) {
#if defined(QT3_BUILD)
            m_code = "zh-CN";   // Qt 3.5 无 QLocale::script()，不做繁简判别
#else
            m_code = (sys.script() == QLocale::TraditionalChineseScript)
                         ? "zh-TW" : "zh-CN";
#endif
        } else {
            m_code = "zh-CN";
        }
    } else {
        m_code = saved;
    }
    applyTranslator();
}

void Lang::applyTranslator() {
    if (m_zhCN) QCoreApplication::removeTranslator(m_zhCN);
    if (m_en)   QCoreApplication::removeTranslator(m_en);
    if (m_zhTW) QCoreApplication::removeTranslator(m_zhTW);

    QTranslator** slot;
    QString qmName;
    if (m_code == "en") {
        slot   = &m_en;
        qmName = "anystik_en";
    } else if (m_code == "zh-TW") {
        slot   = &m_zhTW;
        qmName = "anystik_zh_TW";
    } else {
        slot   = &m_zhCN;
        qmName = "anystik_zh_CN";
    }

    if (*slot == nullptr)
        *slot = new QTranslator(this);
    if (!(*slot)->load(QString(":/i18n/%1.qm").arg(qmName)))
        qWarning("[i18n] failed to load %s.qm", qUtf8Printable(qmName));
    QCoreApplication::installTranslator(*slot);
}

void Lang::setLanguage(const QString& code) {
    if (code == m_code)
        return;
    m_code = code;
    applyTranslator();
#if defined(QT3_BUILD)
    QSettings().writeEntry("language", code);
#else
    QSettings().setValue("language", code);
#endif
    for (QObject* p : std::as_const(m_pages)) {
#if defined(QT3_BUILD)
        // Qt 3.5 无 QMetaObject（Qt4 才引入），无法按名反射调用 retranslateUi；
        // Qt3 下页面重译由调用方自行触发（qlstik 壳走 qlcomp Translator 那条路）。
        Q_UNUSED(p);
#else
        p->metaObject()->invokeMethod(p, "retranslateUi", Qt::DirectConnection);
#endif
    }
    emit languageChanged();
}

void Lang::registerRetranslatable(QObject* page) {
    if (page && !m_pages.contains(page))
        m_pages.append(page);
}

void Lang::unregister(QObject* page) {
#if QT_VERSION >= 0x040000 && QT_VERSION < 0x050000
    // Qt 4.8 的 QVector 无 removeAll（Qt5 起 QVector=QList 才有），倒序逐个删
    for (int i = m_pages.size() - 1; i >= 0; --i) {
        if (m_pages.at(i) == page)
            m_pages.remove(i);
    }
#else
    m_pages.removeAll(page);
#endif
}