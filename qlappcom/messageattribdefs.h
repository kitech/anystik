#ifndef MESSAGEATTRIBDEFS_H
#define MESSAGEATTRIBDEFS_H

#include <qmap.h>
#include <qstringlist.h>
#ifdef QT3_BUILD
#include <qvaluelist.h>
#else
#include <QList>
#endif

// 属性定义：key=ASCII wire 标识；label 展示文本(CJK 用 qFromUtf8 传入)
struct MessageAttrDef {
    enum Kind { kCombo = 0, kCheck, kSpin, kLine, kTags };
    QString key;
    QString label;
    Kind kind;
    QStringList options;      // kCombo（wire 值：itemValue() 恒取此列）
    QStringList displayOptions; // kCombo：非空且与 options 等长时作为下拉显示文本（发送值仍取 options）
    int spinMin; int spinMax; int spinStep;   // kSpin
    QString defValue;         // 默认值(combo=选项文本 / check="0"/"1" / spin=数字串 / line,tags=文本)
    QString hint;             // 全局/简短用法提示(Qt3 无 placeholder,仅 tooltip)
    QString tooltip;          // 该 key 作用与意义的详细说明(hint 与 tooltip 拼接显示)
};

#ifdef QT3_BUILD
typedef QValueList<MessageAttrDef> MessageAttrDefList;
#else
typedef QList<MessageAttrDef> MessageAttrDefList;
#endif

// 占位 schema：按联系人类型返回属性集(qt/qltox 与 qsktox 共用)
MessageAttrDefList messageAttribBarDefsForType(const QString& type);

#endif // MESSAGEATTRIBDEFS_H
