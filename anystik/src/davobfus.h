#ifndef DAV_OBFUS_H
#define DAV_OBFUS_H

#include <QString>

// dav 鉴权密钥（编译期混淆持有，防 strings 低门槛提取）+ Z.ai 智谱国际版 API key。
// 真实 key 未来替换 davobfus.cpp 内 AY_OBFUSCATE("AUTHKEY_PLACEHOLDER") 一处即可。
// format: https://user:pass@path/dav
// user/pass 含 @ 需写成 %40
// caller parse URL
QString davObfusKey();

// Z.ai 智谱国际版 API key（编译期混淆持有；真实值替换 davobfus.cpp 内 ZAI_OBFUS_KEY 一处即可）
QString zaiApiKey();

#endif // DAV_OBFUS_H
