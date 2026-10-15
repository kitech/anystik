#ifndef EDGEDRM_H
#define EDGEDRM_H

#include <string>

// Microsoft Edge Read Aloud 的 Sec-MS-GEC 令牌与时钟校正（移植自 edge-tts drm.py）。
// Qt-free、禁异常；所有函数经返回值传递结果。
namespace edgedrm {

// 公共常量
extern const char* const kTrustedClientToken;   // "6A5AA1D4EAFF4E9FB37E23D68491D6F4"
extern const char* const kChromiumFullVersion;  // "143.0.3650.75"
extern const char* const kDefaultVoice;          // 默认音色
extern const char* const kWssHostLegacy;         // speech.platform.bing.com
extern const char* const kWssHostNew;            // api.msedgeservices.com

// 版本串（与令牌代次配对：v1→"1-<chromium>"，v2→"2-<chromium>"）
std::string versionV1();
std::string versionV2();

// v1（legacy）：标准 SHA-256(ticks + token) 的 64 位十六进制大写
std::string secMsGecV1(double unixTs);

// v2：自定义 CS64 + 改版 SHA-256，形如 "<16hex>-<64hex>"（大写）
std::string secMsGecV2(double unixTs);

// 时钟校正（秒，可正可负）
double clockSkew();
void   addClockSkew(double delta);
void   setClockSkew(double skewSeconds);

// 当前 UTC unix 时间戳 + 校正
double nowUnix();

// 解析 RFC 2616（HTTP Date）日期串；成功返回 true 并写出 unix 秒
bool parseRfc2616Date(const std::string& dateStr, double& outUnix);

// 随机 MUID（32 位十六进制大写）
std::string generateMuid();

// 生成符合 JavaScript 样式的 X-Timestamp（GMT+0000）
std::string dateToString();

} // namespace edgedrm

#endif // EDGEDRM_H
