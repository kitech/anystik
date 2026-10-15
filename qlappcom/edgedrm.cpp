#include "edgedrm.h"

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <vector>
#include <string>

extern "C" {
#include "obsd_sha2.h"
}

namespace edgedrm {

const char* const kTrustedClientToken = "6A5AA1D4EAFF4E9FB37E23D68491D6F4";
const char* const kChromiumFullVersion = "143.0.3650.75";
const char* const kDefaultVoice = "en-US-EmmaMultilingualNeural";
const char* const kWssHostLegacy = "speech.platform.bing.com";
const char* const kWssHostNew = "api.msedgeservices.com";

static const double kWinEpoch = 11644473600.0;   // 1601-01-01 → 1970-01-01 秒数
static double g_clockSkew = 0.0;

// ── 32 位旋转辅助 ──
static inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static inline uint32_t rol16(uint32_t x) { return (x << 16) | (x >> 16); }

// ── v2：改版 SHA-256（FIPS 180-4 结构 + 自定义初态/常量表）──
static const uint32_t kSha256Initial[8] = {
    0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
    0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19,
};
static const uint32_t kSha256State[8] = {
    0xFD9FBEA2, 0x91243CE7, 0xBE499094, 0xCD2E1E0A,
    0x6C006F49, 0x04BD35AB, 0xFEB0A7E4, 0x03EFFBD2,
};
static const uint32_t kSha256Schedule[14] = {
    0xEB24BF9E, 0x1558691F, 0xA0C09EE2, 0x0CFA32BA,
    0xA622EE8C, 0xD1DD9491, 0xC0DD25A0, 0xF9E70AB6,
    0xF0C31573, 0x7058CE4E, 0x7188E606, 0x00E5AB33,
    0xD258B060, 0x72766564,
};
static const uint32_t kSha256RoundConstants[64] = {
    0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5,
    0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
    0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3,
    0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
    0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC,
    0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
    0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7,
    0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
    0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13,
    0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
    0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3,
    0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
    0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5,
    0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
    0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208,
    0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2,
};
static const uint32_t kSha256Adjustment = 0xC9DC06A7;
static const uint32_t kSigma0Adjustment  = 0x059C0B80;
static const uint32_t kSigma1Adjustment  = 0x000B92C0;

// 小 sigma（与标准 SHA-256 等价，合并三次旋转/右移）
static inline uint32_t smallSigma0(uint32_t v) {
    return rotr(v, 7) ^ rotr(v, 18) ^ (v >> 3);
}
static inline uint32_t smallSigma1(uint32_t v) {
    return rotr(v, 17) ^ rotr(v, 19) ^ (v >> 10);
}

// SHA-256 压缩轮：从 firstRound 起算到 63；返回运行寄存器（不与初态相加）
static void sha256Rounds(const uint32_t in[8], const uint32_t words[64],
                         const uint32_t* consts, int firstRound, uint32_t out[8]) {
    uint32_t a = in[0], b = in[1], c = in[2], d = in[3];
    uint32_t e = in[4], f = in[5], g = in[6], h = in[7];
    for (int i = firstRound; i < 64; i++) {
        uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + s1 + ch + consts[i] + words[i];
        uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    out[0] = a; out[1] = b; out[2] = c; out[3] = d;
    out[4] = e; out[5] = f; out[6] = g; out[7] = h;
}

// 标准消息扩展（words[first..63]）
static void expandSchedule(uint32_t words[64], int first) {
    for (int i = first; i < 64; i++) {
        uint32_t x = words[i - 2];
        uint32_t y = words[i - 15];
        words[i] = smallSigma1(x) + words[i - 7] + smallSigma0(y) + words[i - 16];
    }
}

// 自定义初态（data7 为 str_to_hash 前 7 字节）
static void sha256Init(const unsigned char* data7, uint32_t out[8]) {
    uint32_t words[64];
    for (int i = 0; i < 14; i++) { words[i] = 0; }
    words[14] = ((uint32_t)data7[0] << 16) | ((uint32_t)data7[1] << 8) | data7[2];
    words[15] = ((uint32_t)data7[3] << 24) | ((uint32_t)data7[4] << 16)
              | ((uint32_t)data7[5] << 8) | data7[6];
    for (int index = 16; index <= 29; index++) {
        uint32_t value = kSha256Schedule[index - 16];
        uint32_t s1 = smallSigma1(words[index - 2]);
        if (index == 16) { s1 ^= kSigma1Adjustment; }
        value += s1;
        if (index - 7 >= 14) { value += words[index - 7]; }
        if (index - 15 >= 14) {
            uint32_t s0 = smallSigma0(words[index - 15]);
            if (index == 29) { s0 ^= kSigma0Adjustment; }
            value += s0;
        }
        words[index] = value;
    }
    words[30] = smallSigma1(words[28]) + words[23] + smallSigma0(words[15])
              + words[14] + kSha256Adjustment - kSha256RoundConstants[14];
    expandSchedule(words, 31);

    // 块常量：round[0..13] + 调整值 + round[15..63]
    uint32_t blockConstants[64];
    for (int i = 0; i < 64; i++) { blockConstants[i] = kSha256RoundConstants[i]; }
    blockConstants[14] = kSha256Adjustment;

    uint32_t working[8];
    sha256Rounds(kSha256State, words, blockConstants, 14, working);
    for (int i = 0; i < 8; i++) { out[i] = kSha256Initial[i] + working[i]; }
}

// 改版 SHA-256 摘要（data.size() >= 7）
static bool sha256Digest(const std::string& data, unsigned char out[32]) {
    const size_t n = data.size();
    if (n < 7) { return false; }
    const long long length = 57 + (long long)n;
    std::string padded;
    padded.append(data, 7, std::string::npos);
    padded.push_back((char)0x80);
    {
        long long z = (55 - length) % 64;
        if (z < 0) { z += 64; }
        padded.append((size_t)z, '\0');
    }
    {
        uint64_t bits = (uint64_t)length * 8ULL;
        for (int i = 7; i >= 0; i--) {
            padded.push_back((char)((bits >> (i * 8)) & 0xFF));
        }
    }

    uint32_t state[8];
    sha256Init((const unsigned char*)data.data(), state);
    for (size_t off = 0; off < padded.size(); off += 64) {
        uint32_t words[64];
        for (int i = 0; i < 16; i++) {
            const unsigned char* b = (const unsigned char*)padded.data() + off + i * 4;
            words[i] = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16)
                     | ((uint32_t)b[2] << 8) | b[3];
        }
        expandSchedule(words, 16);
        uint32_t working[8];
        sha256Rounds(state, words, kSha256RoundConstants, 0, working);
        for (int i = 0; i < 8; i++) { state[i] += working[i]; }
    }
    for (int i = 0; i < 8; i++) {
        out[i * 4 + 0] = (unsigned char)((state[i] >> 24) & 0xFF);
        out[i * 4 + 1] = (unsigned char)((state[i] >> 16) & 0xFF);
        out[i * 4 + 2] = (unsigned char)((state[i] >> 8) & 0xFF);
        out[i * 4 + 3] = (unsigned char)(state[i] & 0xFF);
    }
    return true;
}

// ── v2：CS64 摘要 ──
static const uint32_t kCs64Coeff[14][2] = {
    { 0xE82E91B1, 0xB32700D0 },
    { 0x44DE24C6, 0x88DFF51D },
    { 0x3BEB65BC, 0x29514038 },
    { 0xC89C5933, 0xB328CBDF },
    { 0x8D1EC211, 0xD405B4D5 },
    { 0x314EF5D5, 0x26E55F3C },
    { 0xE92491B1, 0x607801C6 },
    { 0xF0A5B9D5, 0x82B2C1FB },
    { 0x06064CF3, 0xFA51A3DA },
    { 0xDA79FA60, 0x207CC464 },
    { 0xBED20560, 0x3C3C74F3 },
    { 0x89F9C9F8, 0xD2D14B61 },
    { 0x35FA38C6, 0xBB04A2C1 },
    { 0xCB49E3C6, 0xC5626167 },
};
static const uint32_t kCs64Factor = 0x82893933;
static const uint32_t kCs64Adjustment0 = 0xF2F90B1A;
static const uint32_t kCs64Adjustment1 = 0x0B1A0000;

static void cs64Digest(const std::string& data, const unsigned char digest[32],
                       uint32_t& outA, uint32_t& outB) {
    uint32_t keyA = (uint32_t)digest[0] | ((uint32_t)digest[1] << 8)
                  | ((uint32_t)digest[2] << 16) | ((uint32_t)digest[3] << 24);
    uint32_t keyB = (uint32_t)digest[4] | ((uint32_t)digest[5] << 8)
                  | ((uint32_t)digest[6] << 16) | ((uint32_t)digest[7] << 24);
    keyA |= 1u;
    keyB |= 1u;
    uint32_t factors[2] = { keyA * kCs64Factor, keyB * kCs64Factor };

    // terms/offsets：14 个系数项 + (b"\x00"+data) 的 LE u32 词项
    std::vector<uint32_t> terms;
    std::vector<uint32_t> offsets;
    terms.reserve(40);
    offsets.reserve(40);
    for (int i = 0; i < 14; i++) {
        terms.push_back(kCs64Coeff[i][0] * factors[i % 2]);
        offsets.push_back(kCs64Coeff[i][1]);
    }

    // _words_le4_even(b"\x00" + data)
    std::string buf;
    buf.push_back('\0');
    buf.append(data);
    size_t pad = (4 - (buf.size() % 4)) % 4;
    buf.append(pad, '\0');
    std::vector<uint32_t> words;
    words.reserve(buf.size() / 4 + 1);
    for (size_t i = 0; i < buf.size(); i += 4) {
        words.push_back((uint32_t)(unsigned char)buf[i]
                      | ((uint32_t)(unsigned char)buf[i + 1] << 8)
                      | ((uint32_t)(unsigned char)buf[i + 2] << 16)
                      | ((uint32_t)(unsigned char)buf[i + 3] << 24));
    }
    if (words.size() % 2) { words.push_back(0); }

    for (size_t index = 0; index < words.size(); index++) {
        uint32_t word = words[index];
        uint32_t term = word * ((index % 2 == 0) ? keyA : keyB);
        uint32_t offset = rol16(word) * ((index % 2 == 0) ? 0xEF0569FBu : 0xC31713DBu);
        if (index == 0) {
            term += kCs64Adjustment0 * factors[0];
            offset += kCs64Adjustment1;
        }
        terms.push_back(term);
        offsets.push_back(offset);
    }

    uint32_t swapAcc = 0, swapLast = 0, state = 0, acc = 0;
    for (size_t index = 0; index + 1 < terms.size(); index += 2) {
        uint32_t x = terms[index], y = terms[index + 1];
        uint32_t t = rol16(offsets[index] + x);
        uint32_t q = t * 0x76F50F34u;
        swapAcc += rol16(q) * 0xC3EFEA97u + q;
        t = rol16(offsets[index + 1] + y);
        q = t * 0x1881CE3Cu;
        swapLast = rol16(q) * 0x35BD1EC9u + q;
        swapAcc = swapAcc + swapLast;

        state = state * keyA + x;
        state = rol16(state) * 0xCF98B111u;
        state = rol16(state) * 0x87085B9Fu;
        state = rol16(state) * 0x12CEB96Du;
        state = rol16(state) * 0x257E1D83u;
        acc += state;
        t = state * keyB + y;
        t = rol16(t) * 0xA27416F5u;
        t = rol16(t) * 0xD38396FFu;
        state = rol16(rol16(t) * 0x7C932B89u) * 0xBFA49F69u;
        acc = acc + state;
    }
    outA = swapLast ^ state;
    outB = swapAcc ^ acc;
}

static std::string hexUpper(const unsigned char* data, size_t len) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        out.push_back(kHex[(data[i] >> 4) & 0xF]);
        out.push_back(kHex[data[i] & 0xF]);
    }
    return out;
}

// 由 unix 秒生成 ticks 字符串（ticks+token），v1/v2 共用
static std::string ticksString(double unixTs) {
    double t = unixTs + kWinEpoch;
    t -= fmod(t, 300.0);
    t *= 10000000.0;   // S_TO_NS / 100
    char buf[64];
    snprintf(buf, sizeof(buf), "%.0f", t);
    std::string s = buf;
    s += kTrustedClientToken;
    return s;
}

std::string versionV1() { return std::string("1-") + kChromiumFullVersion; }
std::string versionV2() { return std::string("2-") + kChromiumFullVersion; }

std::string secMsGecV1(double unixTs) {
    std::string s = ticksString(unixTs);
    SHA2_CTX ctx;
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256Init(&ctx);
    SHA256Update(&ctx, s.data(), s.size());
    SHA256Final(digest, &ctx);
    return hexUpper(digest, SHA256_DIGEST_LENGTH);
}

std::string secMsGecV2(double unixTs) {
    std::string s = ticksString(unixTs);
    unsigned char digest[32];
    if (!sha256Digest(s, digest)) { return std::string(); }
    uint32_t a = 0, b = 0;
    cs64Digest(s, digest, a, b);
    unsigned char packed[8];
    packed[0] = (unsigned char)(a & 0xFF);
    packed[1] = (unsigned char)((a >> 8) & 0xFF);
    packed[2] = (unsigned char)((a >> 16) & 0xFF);
    packed[3] = (unsigned char)((a >> 24) & 0xFF);
    packed[4] = (unsigned char)(b & 0xFF);
    packed[5] = (unsigned char)((b >> 8) & 0xFF);
    packed[6] = (unsigned char)((b >> 16) & 0xFF);
    packed[7] = (unsigned char)((b >> 24) & 0xFF);
    return hexUpper(packed, 8) + "-" + hexUpper(digest, 32);
}

double clockSkew() { return g_clockSkew; }
void addClockSkew(double delta) { g_clockSkew += delta; }
void setClockSkew(double skewSeconds) { g_clockSkew = skewSeconds; }

double nowUnix() { return (double)time(NULL) + g_clockSkew; }

// 解析 HTTP Date（RFC 1123: "Mon, 12 Oct 2026 03:16:09 GMT"）。
// 手写解析以避免 strptime/timegm 依赖，仅接受服务器实际发送的 GMT/UTC 形式。
bool parseRfc2616Date(const std::string& dateStr, double& outUnix) {
    std::string s = dateStr;
    // 去尾部 GMT / UTC / 时区偏移（仅正确处理 GMT/UTC；带偏移的少见形式按 UTC 处理）
    int day = 0, year = 0, hh = 0, mm = 0, ss = 0;
    char mon[8] = {0};
    const char* p = strchr(s.c_str(), ',');
    if (p) { p = p + 1; } else { p = s.c_str(); }
    while (*p == ' ') { p++; }
    if (sscanf(p, "%d %3s %d %d:%d:%d", &day, mon, &year, &hh, &mm, &ss) != 6) {
        return false;
    }
    static const char* kMonths[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    int month = -1;
    for (int i = 0; i < 12; i++) {
        if (strncmp(mon, kMonths[i], 3) == 0) { month = i; break; }
    }
    if (month < 0 || day < 1 || day > 31 || hh < 0 || hh > 23
        || mm < 0 || mm > 59 || ss < 0 || ss > 60) {
        return false;
    }
    // days since 1970-01-01（Howard Hinnant 算法）
    int y = year;
    int m = month + 1;
    y -= (m <= 2);
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)(day - 1);
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    long long days = (long long)era * 146097LL + (long long)doe - 719468LL;
    long long secs = days * 86400LL + hh * 3600LL + mm * 60LL + ss;
    outUnix = (double)secs;
    return true;
}

std::string generateMuid() {
    unsigned char raw[16];
    FILE* f = fopen("/dev/urandom", "rb");
    if (f) {
        if (fread(raw, 1, sizeof(raw), f) != sizeof(raw)) {
            for (size_t i = 0; i < sizeof(raw); i++) { raw[i] = (unsigned char)(rand() & 0xFF); }
        }
        fclose(f);
    } else {
        for (size_t i = 0; i < sizeof(raw); i++) { raw[i] = (unsigned char)(rand() & 0xFF); }
    }
    return hexUpper(raw, sizeof(raw));
}

std::string dateToString() {
    time_t t = time(NULL);
    struct tm tm;
    gmtime_r(&t, &tm);
    char buf[128];
    strftime(buf, sizeof(buf), "%a %b %d %Y %H:%M:%S GMT+0000 (Coordinated Universal Time)", &tm);
    return std::string(buf);
}

} // namespace edgedrm
