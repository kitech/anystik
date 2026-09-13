#include "myi18n.h"
#include "settings_trace.h"

#include <QLocale>
#include <QMetaObject>
#include <QSettings>
#include <utility>

Lang& Lang::instance() {
    static Lang g;
    return g;
}

Lang::Lang() {
    const QString saved = QSettings().value("language", QString()).toString();
    trace_settings("i18n-language-read");
    if (saved.isEmpty()) {
        const QLocale sys = QLocale::system();
        if (sys.language() == QLocale::English) {
            m_code = "en";
        } else if (sys.language() == QLocale::Chinese) {
            m_code = (sys.script() == QLocale::TraditionalChineseScript)
                         ? "zh-TW" : "zh-CN";
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
    QSettings().setValue("language", code);
    for (QObject* p : std::as_const(m_pages)) {
        p->metaObject()->invokeMethod(p, "retranslateUi", Qt::DirectConnection);
    }
    emit languageChanged();
}

void Lang::registerRetranslatable(QObject* page) {
    if (page && !m_pages.contains(page))
        m_pages.append(page);
}

void Lang::unregister(QObject* page) {
    m_pages.removeAll(page);
}