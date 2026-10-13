#ifndef TRANSLATE_UTIL_H
#define TRANSLATE_UTIL_H

#include <qstring.h>
#include <stdint.h>

// 判断文本是否需要翻译到中文（10分类 → 聚类 → UTF-8字节权重 ratio < 0.27 → 需要翻译）
bool isNeedTranslateToChinese(const QString& text);

// 判断文本是否需要自动翻译（相对目标语言）
bool needsAutoTranslate(const QString& text, const QString& targetLang);

#endif
