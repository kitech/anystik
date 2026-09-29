#ifndef ANYS_I18N_H
#define ANYS_I18N_H

#include "stdcompat_shim.h"   // std::as_const 在 c++14 构建下缺失
#ifdef QT3_BUILD
#include "qcoreapplication_shim.h"
#include "qvector_shim.h"
#include <qobject.h>
#include <qstring.h>
#include <qtranslator.h>
#else
#include <QCoreApplication>
#include <QObject>
#include <QString>
#include <QTranslator>
#include <QVector>
#endif

// 无 Q_OBJECT 类（stickerlist/myscrollarea/dialogpopup…）的翻译入口：
// context 显式指定，source 为中文原文；zh 缺失时原样回退。
// 注意：源码中此类调用统一写成 QCoreApplication::translate("Ctx", "原文")，
// 以便 lupdate 能直接提取；本 helper 仅供历史兼容，勿在新代码中使用。

// 语言单例：安装/切换 QTranslator，广播即时重译。
class Lang : public QObject {
    Q_OBJECT
public:
    static Lang& instance();
    QString code() const { return m_code; }
    void setLanguage(const QString& code);   // swap qm + QSettings 保存 + 广播
    void registerRetranslatable(QObject* page); // 需存在槽 retranslateUi()
    void unregister(QObject* page);

signals:
    void languageChanged();

private:
    Lang();
    void applyTranslator();
    QString m_code;
    QVector<QObject*> m_pages;
    QTranslator* m_zhCN   = nullptr; // 收录英文源串→中文（settings 页等）
    QTranslator* m_en     = nullptr;
    QTranslator* m_zhTW   = nullptr;
};

#endif // ANYS_I18N_H