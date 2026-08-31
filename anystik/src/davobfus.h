#ifndef DAV_OBFUS_H
#define DAV_OBFUS_H

#include <QString>

// dav 鉴权密钥（编译期混淆持有，防 strings 低门槛提取）。
// 真实 key 未来替换 davobfus.cpp 内 AY_OBFUSCATE("AUTHKEY_PLACEHOLDER") 一处即可。
// format: https://user:pass@path/dav
// caller parse URL
QString davObfusKey();

#endif // DAV_OBFUS_H
