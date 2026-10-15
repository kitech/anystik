// webcreds.cpp — qltox 凭据加密存储（rclone config encryption 风格，Qt-free，禁异常）
//
// 密码学（全部本地实现，无 OpenSSL 依赖）：
//   - PBKDF2-HMAC-SHA256（qlcomp/obsd_sha2.c + 本文件 HMAC 实现）
//   - AES-256-CBC（qlcomp/aes.c，运行时 key_bits=256）+ PKCS7 + HMAC-SHA256 做
//     encrypt-then-MAC（MAC 覆盖 iv||ct）
//   - 随机源 /dev/urandom；iv 每字段一次，salt 每字段随机。
//   - 令牌模式：encKey = SHA256("webcreds:enc:" + token)
//               macKey  = SHA256("webcreds:mac:" + token)
//   - 口令模式：PBKDF2(password, salt, i) 派生 64B → encKey[0..32) + macKey[32..64)
//
// 格式：
//   容器:  {"magic":"QLW1","version":1,"fields":{...}}  （0600 原子写）
//   信封:  {"m":"token"|"pass","s":saltB64,"i":iters,
//          "iv":ivB64,"ct":ctB64,"mac":macB64}
//   明文项: JSON 字符串（非信封对象即明文）
//   令牌:  webcreds.key（0600，hex 64 字符的 256-bit 随机密钥）

#include "webcreds.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>

extern "C" {
#include "cJSON.h"
#include "aes.h"
#include "obsd_sha2.h"
}

namespace {

const char* const kContainerMagic = "QLW1";
const int kContainerVersion = 1;
const unsigned kDefaultIters = 60000;    // PBKDF2 默认迭代（存于信封；解密用存储值，可升级）
const size_t kSaltLen = 16;
const size_t kIvLen = 16;
const size_t kKeyLen = 32;

std::string g_fileOverride;
std::string g_tokenOverride;

// 运行时侧车缓存
cJSON* g_root = nullptr;
bool g_loaded = false;
bool g_broken = false;   // 侧车文件存在但解析失败/MAC 错 → 全体字段 Broken
std::mutex g_lock;       // 保护 g_root/g_loaded/g_broken 与路径 override（aigptbot 工作线程 + UI/CLI 并发）

// ── 路径 ────────────────────────────────────────────────────────────────
std::string homeDir() {
    const char* h = getenv("HOME");
    return h ? std::string(h) : std::string();
}

std::string defaultSidecar() { return homeDir() + "/.config/qltox/webcreds.enc"; }
std::string defaultToken()   { return homeDir() + "/.config/qltox/webcreds.key"; }

std::string sidecarPath() {
    if (!g_fileOverride.empty()) { return g_fileOverride; }
    const char* e = getenv("QTOX_WEB_CRED_FILE");
    if (e && e[0]) { return std::string(e); }
    return defaultSidecar();
}

std::string tokenPath() {
    if (!g_tokenOverride.empty()) { return g_tokenOverride; }
    const char* e = getenv("QTOX_WEB_CRED_TOKEN");
    if (e && e[0]) { return std::string(e); }
    return defaultToken();
}

// ── 小工具 ──────────────────────────────────────────────────────────────
std::string trimStr(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) { return std::string(); }
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream ifs(path.c_str(), std::ios::binary);
    if (!ifs.is_open()) { return false; }
    std::stringstream ss;
    ss << ifs.rdbuf();
    out = ss.str();
    return true;
}

bool mkdirP(const std::string& path) {
    const std::string base = path.substr(0, path.rfind('/'));
    std::string cur;
    size_t i = 0;
    while (i <= base.size()) {
        const size_t slash = base.find('/', i);
        const std::string seg = base.substr(0, slash == std::string::npos ? base.size() : slash);
        if (!seg.empty()) {
            if (::mkdir(seg.c_str(), 0700) != 0 && errno != EEXIST) { return false; }
        }
        if (slash == std::string::npos) { break; }
        i = slash + 1;
    }
    return true;
}

bool writeFileAtomic(const std::string& path, const std::string& data, mode_t mode) {
    if (!mkdirP(path)) { return false; }
    const std::string tmp = path + ".tmp" + std::to_string((unsigned long)getpid());
    {
        std::ofstream ofs(tmp.c_str(), std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) { return false; }
        ofs << data;
    }
    if (::chmod(tmp.c_str(), mode) != 0) { ::unlink(tmp.c_str()); return false; }
    if (::rename(tmp.c_str(), path.c_str()) != 0) { ::unlink(tmp.c_str()); return false; }
    return true;
}

std::string toHex(const unsigned char* d, size_t n) {
    static const char* hexd = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        out += hexd[d[i] >> 4];
        out += hexd[d[i] & 0x0f];
    }
    return out;
}

bool fromHex(const std::string& h, std::vector<unsigned char>& out) {
    auto cv = [](char c) -> int {
        if (c >= '0' && c <= '9') { return c - '0'; }
        if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
        if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
        return -1;
    };
    out.clear();
    if (h.size() % 2 != 0) { return false; }
    for (size_t i = 0; i < h.size(); i += 2) {
        const int hi = cv(h[i]);
        const int lo = cv(h[i + 1]);
        if (hi < 0 || lo < 0) { return false; }
        out.push_back((unsigned char)((hi << 4) | lo));
    }
    return true;
}

bool readRandom(unsigned char* out, size_t n) {
    FILE* f = fopen("/dev/urandom", "rb");
    if (!f) { return false; }
    size_t got = 0;
    while (got < n) {
        const size_t r = fread(out + got, 1, n - got, f);
        if (r == 0) { break; }
        got += r;
    }
    fclose(f);
    return got == n;
}

// ── base64（自含，标准 padded）──────────────────────────────────────────
std::string b64EncodeRaw(const unsigned char* d, size_t n) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    while (i + 3 <= n) {
        const unsigned v = ((unsigned)d[i] << 16) | ((unsigned)d[i + 1] << 8) | (unsigned)d[i + 2];
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];
        out += tbl[v & 63];
        i += 3;
    }
    if (i + 1 == n) {
        const unsigned v = (unsigned)d[i] << 16;
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == n) {
        const unsigned v = ((unsigned)d[i] << 16) | ((unsigned)d[i + 1] << 8);
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

bool b64DecodeRaw(const std::string& in, std::vector<unsigned char>& out) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') { return c - 'A'; }
        if (c >= 'a' && c <= 'z') { return c - 'a' + 26; }
        if (c >= '0' && c <= '9') { return c - '0' + 52; }
        if (c == '+') { return 62; }
        if (c == '/') { return 63; }
        return -1;
    };
    out.clear();
    unsigned acc = 0;
    int bits = 0;
    for (size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c == '=') { break; }
        const int v = val(c);
        if (v < 0) { return false; }
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((unsigned char)((acc >> bits) & 0xff));
        }
    }
    return true;
}

// ── SHA256 / HMAC-SHA256 / PBKDF2-HMAC-SHA256 ──────────────────────────
void sha256Raw(const unsigned char* d, size_t n, unsigned char out[kKeyLen]) {
    SHA2_CTX c;
    SHA256Init(&c);
    SHA256Update(&c, d, n);
    SHA256Final(out, &c);
}

void hmacSha256(const unsigned char* key, size_t klen,
                const unsigned char* msg, size_t mlen,
                unsigned char out[kKeyLen]) {
    unsigned char kk[SHA256_BLOCK_LENGTH];
    unsigned char ipad[SHA256_BLOCK_LENGTH];
    unsigned char opad[SHA256_BLOCK_LENGTH];
    memset(kk, 0, sizeof(kk));
    if (klen > SHA256_BLOCK_LENGTH) {
        sha256Raw(key, klen, kk);
    } else {
        memcpy(kk, key, klen);
    }
    for (size_t i = 0; i < SHA256_BLOCK_LENGTH; ++i) {
        ipad[i] = (unsigned char)(kk[i] ^ 0x36);
        opad[i] = (unsigned char)(kk[i] ^ 0x5c);
    }
    SHA2_CTX c;
    SHA256Init(&c);
    SHA256Update(&c, ipad, SHA256_BLOCK_LENGTH);
    SHA256Update(&c, msg, mlen);
    unsigned char inner[kKeyLen];
    SHA256Final(inner, &c);
    SHA256Init(&c);
    SHA256Update(&c, opad, SHA256_BLOCK_LENGTH);
    SHA256Update(&c, inner, kKeyLen);
    SHA256Final(out, &c);
}

void pbkdf2HmacSha256(const unsigned char* pass, size_t plen,
                      const unsigned char* salt, size_t slen,
                      unsigned iter, unsigned char* out, size_t outlen) {
    size_t block = 0;
    unsigned char* o = out;
    size_t left = outlen;
    while (left > 0) {
        ++block;
        unsigned char ib[4];
        ib[0] = (unsigned char)((block >> 24) & 0xff);
        ib[1] = (unsigned char)((block >> 16) & 0xff);
        ib[2] = (unsigned char)((block >> 8) & 0xff);
        ib[3] = (unsigned char)(block & 0xff);
        unsigned char u[kKeyLen];
        unsigned char t[kKeyLen];
        std::string sm((const char*)salt, slen);
        sm.append((const char*)ib, 4);
        hmacSha256(pass, plen, (const unsigned char*)sm.data(), sm.size(), u);
        memcpy(t, u, kKeyLen);
        for (unsigned r = 1; r < iter; ++r) {
            hmacSha256(pass, plen, u, kKeyLen, u);
            for (size_t j = 0; j < kKeyLen; ++j) { t[j] ^= u[j]; }
        }
        const size_t n = left < kKeyLen ? left : kKeyLen;
        memcpy(o, t, n);
        o += n;
        left -= n;
    }
}

// ── AES-256-CBC + PKCS7 ─────────────────────────────────────────────────
bool aesEncCbcPadded(const std::string& plain, const unsigned char key[kKeyLen],
                     const unsigned char iv[kIvLen], std::string& out) {
    const size_t plen = plain.size();
    const size_t padLen = AES_BLOCKLEN - (plen % AES_BLOCKLEN);
    std::vector<unsigned char> buf;
    buf.reserve(plen + padLen);
    buf.insert(buf.end(), plain.begin(), plain.end());
    buf.insert(buf.end(), padLen, (unsigned char)padLen);
    AES_ctx ctx;
    if (AES_init_ctx_iv(&ctx, key, 256, iv) != 0) { return false; }
    AES_CBC_encrypt_buffer(&ctx, buf.data(), buf.size());
    out.assign((const char*)buf.data(), buf.size());
    return true;
}

bool aesDecCbcPadded(const std::string& ct, const unsigned char key[kKeyLen],
                     const unsigned char iv[kIvLen], std::string& out) {
    if (ct.empty() || ct.size() % AES_BLOCKLEN != 0) { return false; }
    std::vector<unsigned char> buf(ct.begin(), ct.end());
    AES_ctx ctx;
    if (AES_init_ctx_iv(&ctx, key, 256, iv) != 0) { return false; }
    AES_CBC_decrypt_buffer(&ctx, buf.data(), buf.size());
    const size_t n = buf.size();
    const unsigned char plen = buf[n - 1];
    if (plen == 0 || plen > AES_BLOCKLEN || plen > n) { return false; }
    for (size_t j = n - plen; j < n; ++j) {
        if (buf[j] != plen) { return false; }
    }
    out.assign((const char*)buf.data(), n - plen);
    return true;
}

bool macEqual(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) { return false; }
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diff |= (unsigned char)((unsigned char)a[i] ^ (unsigned char)b[i]);
    }
    return diff == 0;
}

// ── 令牌文件 ────────────────────────────────────────────────────────────
bool loadTokenRaw(std::string& tokenRaw) {
    std::string hexText;
    if (!readFile(tokenPath(), hexText)) { return false; }
    std::vector<unsigned char> t;
    if (!fromHex(trimStr(hexText), t) || t.size() != kKeyLen) { return false; }
    tokenRaw.assign((const char*)t.data(), t.size());
    return true;
}

bool ensureToken(std::string& err) {
    std::string t;
    if (loadTokenRaw(t)) { return true; }
    unsigned char k[kKeyLen];
    if (!readRandom(k, kKeyLen)) { err = "无法读取 /dev/urandom"; return false; }
    if (!writeFileAtomic(tokenPath(), toHex(k, kKeyLen) + "\n", 0600)) {
        err = "写入令牌文件失败: " + tokenPath();
        return false;
    }
    return true;
}

// ── 信封加解密 ──────────────────────────────────────────────────────────
struct Envelope {
    WebCredMode mode = kWebCredPlainMode;
    std::string salt;   // 原始字节（仅 pass）
    unsigned iters = 0;
    std::string iv;     // 原始字节
    std::string ct;     // 原始密文
    std::string mac;    // 原始 32B HMAC(iv||ct)
};

bool jsonToEnv(cJSON* obj, Envelope& e) {
    cJSON* m = cJSON_GetObjectItem(obj, "m");
    if (!m || m->type != cJSON_String) { return false; }
    if (strcmp(m->valuestring, "token") == 0) {
        e.mode = kWebCredTokenMode;
    } else if (strcmp(m->valuestring, "pass") == 0) {
        e.mode = kWebCredPassMode;
        cJSON* s = cJSON_GetObjectItem(obj, "s");
        cJSON* it = cJSON_GetObjectItem(obj, "i");
        if (!s || s->type != cJSON_String) { return false; }
        std::vector<unsigned char> sb;
        if (!b64DecodeRaw(s->valuestring, sb) || sb.size() != kSaltLen) { return false; }
        e.salt.assign((const char*)sb.data(), sb.size());
        if (!it || it->type != cJSON_Number || it->valuedouble <= 0) { return false; }
        e.iters = (unsigned)it->valuedouble;
    } else {
        return false;
    }
    std::vector<unsigned char> vb;
    cJSON* iv = cJSON_GetObjectItem(obj, "iv");
    if (!iv || iv->type != cJSON_String || !b64DecodeRaw(iv->valuestring, vb) || vb.size() != kIvLen) { return false; }
    e.iv.assign((const char*)vb.data(), vb.size());
    cJSON* ct = cJSON_GetObjectItem(obj, "ct");
    if (!ct || ct->type != cJSON_String || !b64DecodeRaw(ct->valuestring, vb)) { return false; }
    e.ct.assign((const char*)vb.data(), vb.size());
    cJSON* mac = cJSON_GetObjectItem(obj, "mac");
    if (!mac || mac->type != cJSON_String || !b64DecodeRaw(mac->valuestring, vb) || vb.size() != kKeyLen) { return false; }
    e.mac.assign((const char*)vb.data(), vb.size());
    return true;
}

bool envToJson(const Envelope& e, cJSON* obj) {
    if (e.mode == kWebCredTokenMode) {
        cJSON_AddItemToObject(obj, "m", cJSON_CreateString("token"));
    } else if (e.mode == kWebCredPassMode) {
        cJSON_AddItemToObject(obj, "m", cJSON_CreateString("pass"));
        cJSON_AddItemToObject(obj, "s", cJSON_CreateString(
            b64EncodeRaw((const unsigned char*)e.salt.data(), e.salt.size()).c_str()));
        cJSON_AddItemToObject(obj, "i", cJSON_CreateNumber((double)e.iters));
    } else {
        return false;
    }
    cJSON_AddItemToObject(obj, "iv", cJSON_CreateString(
        b64EncodeRaw((const unsigned char*)e.iv.data(), e.iv.size()).c_str()));
    cJSON_AddItemToObject(obj, "ct", cJSON_CreateString(
        b64EncodeRaw((const unsigned char*)e.ct.data(), e.ct.size()).c_str()));
    cJSON_AddItemToObject(obj, "mac", cJSON_CreateString(
        b64EncodeRaw((const unsigned char*)e.mac.data(), e.mac.size()).c_str()));
    return true;
}

bool makeEnvelope(WebCredMode mode, const std::string& plain,
                  const std::string& passphrase, const std::string& tokenRaw,
                  Envelope& out) {
    unsigned char encKey[kKeyLen];
    unsigned char macKey[kKeyLen];
    const unsigned iters = [&]() {
        const char* e = getenv("QTOX_WEB_CRED_ITERS");
        if (e && e[0]) {
            const int v = atoi(e);
            if (v >= 1000) { return (unsigned)v; }
        }
        return kDefaultIters;
    }();
    (void)iters;
    if (mode == kWebCredTokenMode) {
        const std::string d1 = "webcreds:enc:" + tokenRaw;
        const std::string d2 = "webcreds:mac:" + tokenRaw;
        sha256Raw((const unsigned char*)d1.data(), d1.size(), encKey);
        sha256Raw((const unsigned char*)d2.data(), d2.size(), macKey);
    } else if (mode == kWebCredPassMode) {
        if (passphrase.empty()) { return false; }
        unsigned char salt[kSaltLen];
        if (!readRandom(salt, kSaltLen)) { return false; }
        unsigned char dk[64];
        pbkdf2HmacSha256((const unsigned char*)passphrase.data(), passphrase.size(),
                         salt, kSaltLen, iters, dk, 64);
        memcpy(encKey, dk, 32);
        memcpy(macKey, dk + 32, 32);
        out.salt.assign((const char*)salt, kSaltLen);
        out.iters = iters;
    } else {
        return false;
    }
    unsigned char iv[kIvLen];
    if (!readRandom(iv, kIvLen)) { return false; }
    out.mode = mode;
    out.iv.assign((const char*)iv, kIvLen);
    if (!aesEncCbcPadded(plain, encKey, iv, out.ct)) { return false; }
    const std::string data = out.iv + out.ct;
    unsigned char mac[32];
    hmacSha256(macKey, kKeyLen, (const unsigned char*)data.data(), data.size(), mac);
    out.mac.assign((const char*)mac, 32);
    return true;
}

bool breakEnvelope(const Envelope& env, const std::string& passphrase,
                   const std::string& tokenRaw, std::string& plain) {
    unsigned char encKey[kKeyLen];
    unsigned char macKey[kKeyLen];
    if (env.mode == kWebCredTokenMode) {
        const std::string d1 = "webcreds:enc:" + tokenRaw;
        const std::string d2 = "webcreds:mac:" + tokenRaw;
        sha256Raw((const unsigned char*)d1.data(), d1.size(), encKey);
        sha256Raw((const unsigned char*)d2.data(), d2.size(), macKey);
    } else if (env.mode == kWebCredPassMode) {
        if (passphrase.empty()) { return false; }
        if (env.salt.empty() || env.iters == 0) { return false; }
        unsigned char dk[64];
        pbkdf2HmacSha256((const unsigned char*)passphrase.data(), passphrase.size(),
                         (const unsigned char*)env.salt.data(), env.salt.size(),
                         env.iters, dk, 64);
        memcpy(encKey, dk, 32);
        memcpy(macKey, dk + 32, 32);
    } else {
        return false;
    }
    const std::string data = env.iv + env.ct;
    unsigned char mac[32];
    hmacSha256(macKey, kKeyLen, (const unsigned char*)data.data(), data.size(), mac);
    if (!macEqual(std::string((const char*)mac, 32), env.mac)) { return false; }
    return aesDecCbcPadded(env.ct, encKey, (const unsigned char*)env.iv.data(), plain);
}

// ── 运行入口口令来源（env 优先，其次命令）───────────────────────────────
std::string runtimePass() {
    const char* p = getenv("QTOX_WEB_CRED_PASS");
    if (p && p[0]) { return std::string(p); }
    const char* cmd = getenv("QTOX_WEB_CRED_PASS_COMMAND");
    if (cmd && cmd[0]) {
        FILE* f = popen(cmd, "r");
        if (f) {
            char buf[512];
            std::string out;
            while (fgets(buf, (int)sizeof(buf), f)) { out += buf; }
            pclose(f);
            return trimStr(out);
        }
    }
    return std::string();
}

// ── 容器加载 ────────────────────────────────────────────────────────────
void ensureLoaded() {
    if (g_loaded) { return; }
    g_loaded = true;
    if (g_root) {
        cJSON_Delete(g_root);
        g_root = nullptr;
    }
    g_broken = false;
    std::string data;
    if (!readFile(sidecarPath(), data)) { return; }   // 无侧车 → None/Constant
    cJSON* r = cJSON_Parse(data.c_str());
    if (!r) { g_broken = true; return; }
    cJSON* magic = cJSON_GetObjectItem(r, "magic");
    if (!magic || magic->type != cJSON_String ||
        strcmp(magic->valuestring, kContainerMagic) != 0) {
        cJSON_Delete(r);
        g_broken = true;
        return;
    }
    g_root = r;
}

cJSON* newRoot() {
    cJSON* r = cJSON_CreateObject();
    cJSON_AddItemToObject(r, "magic", cJSON_CreateString(kContainerMagic));
    cJSON_AddItemToObject(r, "version", cJSON_CreateNumber(kContainerVersion));
    cJSON_AddItemToObject(r, "fields", cJSON_CreateObject());
    return r;
}

bool saveRoot(cJSON* root) {
    char* s = cJSON_PrintUnformatted(root);
    if (!s) { return false; }
    const std::string data(s);
    free(s);
    return writeFileAtomic(sidecarPath(), data, 0600);
}

// 装载并处理返回给管理接口的花费集合（损坏→err）
cJSON* loadRootForWrite(std::string& err) {
    std::string data;
    if (!readFile(sidecarPath(), data)) { return newRoot(); }
    cJSON* r = cJSON_Parse(data.c_str());
    if (!r) { err = "侧车文件损坏（JSON 解析失败）"; return nullptr; }
    cJSON* magic = cJSON_GetObjectItem(r, "magic");
    if (!magic || magic->type != cJSON_String ||
        strcmp(magic->valuestring, kContainerMagic) != 0) {
        cJSON_Delete(r);
        err = "侧车文件损坏（magic 不匹配）";
        return nullptr;
    }
    if (!cJSON_GetObjectItem(r, "fields")) {
        cJSON_AddItemToObject(r, "fields", cJSON_CreateObject());
    }
    return r;
}

// 单字段解析。passOverride 非空时作为口令来源（CLI）；空则用环境来源。
WebCredResult resolveField(cJSON* root, const std::string& field,
                           const std::string& fallback,
                           const std::string& passOverride) {
    WebCredResult r;
    cJSON* fields = root ? cJSON_GetObjectItem(root, "fields") : nullptr;
    cJSON* f = fields ? cJSON_GetObjectItem(fields, field.c_str()) : nullptr;
    if (!f) {
        r.status = fallback.empty() ? kWebCredNone : kWebCredConstant;
        r.value = fallback;
        r.ok = !fallback.empty();
        return r;
    }
    if (f->type == cJSON_String) {
        r.status = kWebCredPlain;
        r.value = f->valuestring ? f->valuestring : "";
        r.ok = true;
        return r;
    }
    if (f->type != cJSON_Object) {
        r.status = kWebCredBroken;
        return r;
    }
    Envelope env;
    if (!jsonToEnv(f, env)) {
        r.status = kWebCredBroken;
        return r;
    }
    if (env.mode == kWebCredTokenMode) {
        std::string token;
        if (!loadTokenRaw(token)) { r.status = kWebCredBroken; return r; }
        std::string plain;
        if (!breakEnvelope(env, std::string(), token, plain)) { r.status = kWebCredBroken; return r; }
        r.status = kWebCredTokenEnc;
        r.value = plain;
        r.ok = true;
        return r;
    }
    // pass 模式
    std::string pass = passOverride.empty() ? runtimePass() : passOverride;
    if (pass.empty()) {
        r.status = kWebCredPassEnc;   // 已配置但无口令 → 不可用，不回退常量
        r.ok = false;
        return r;
    }
    std::string plain;
    if (!breakEnvelope(env, pass, std::string(), plain)) {
        r.status = kWebCredBroken;
        return r;
    }
    r.status = kWebCredPassEnc;
    r.value = plain;
    r.ok = true;
    return r;
}

bool addFieldToRoot(cJSON* root, const std::string& name, const std::string& value,
                    WebCredMode mode, const std::string& passphrase,
                    std::string& err) {
    if (mode == kWebCredPassMode && passphrase.empty()) {
        err = "pass 模式需要口令（--pass 或 QTOX_WEB_CRED_PASS）";
        return false;
    }
    std::string tokenRaw;
    if (mode == kWebCredTokenMode) {
        if (!ensureToken(err)) { return false; }
        if (!loadTokenRaw(tokenRaw)) {
            err = "令牌不可用";
            return false;
        }
    }
    cJSON* fields = cJSON_GetObjectItem(root, "fields");
    cJSON_DeleteItemFromObject(fields, name.c_str());
    if (mode == kWebCredPlainMode) {
        cJSON_AddItemToObject(fields, name.c_str(), cJSON_CreateString(value.c_str()));
        return true;
    }
    Envelope env;
    if (!makeEnvelope(mode, value, passphrase, tokenRaw, env)) {
        err = "加密失败";
        return false;
    }
    cJSON* obj = cJSON_CreateObject();
    if (!envToJson(env, obj)) {
        cJSON_Delete(obj);
        err = "信封序列化失败";
        return false;
    }
    cJSON_AddItemToObject(fields, name.c_str(), obj);
    return true;
}

} // namespace

// ── 公开接口 ────────────────────────────────────────────────────────────
// 无锁重置缓存（所有持锁路径统一调用，勿直接走公开 webCredsReload 以免嵌套死锁）
static void reloadNoLock() {
    if (g_root) {
        cJSON_Delete(g_root);
        g_root = nullptr;
    }
    g_loaded = false;
    g_broken = false;
}

void webCredsSetFileOverride(const std::string& sidecarPath,
                             const std::string& tokenPath) {
    std::lock_guard<std::mutex> lock(g_lock);
    g_fileOverride = sidecarPath;
    g_tokenOverride = tokenPath;
    reloadNoLock();
}

void webCredsReload() {
    std::lock_guard<std::mutex> lock(g_lock);
    reloadNoLock();
}

WebCredResult webCredGet(const std::string& field, const std::string& fallback) {
    std::lock_guard<std::mutex> lock(g_lock);
    ensureLoaded();
    WebCredResult r;
    if (g_broken) {
        r.status = kWebCredBroken;
        return r;
    }
    return resolveField(g_root, field, fallback, std::string());
}

WebCredResult webCredGet(const std::string& field, const std::string& fallback,
                         const std::string& passOverride) {
    std::lock_guard<std::mutex> lock(g_lock);
    ensureLoaded();
    WebCredResult r;
    if (g_broken) {
        r.status = kWebCredBroken;
        return r;
    }
    return resolveField(g_root, field, fallback, passOverride);
}

WebCredStatus webCredStatus(const std::string& field) {
    return webCredGet(field, std::string()).status;
}

const char* webCredStatusName(WebCredStatus s) {
    switch (s) {
    case kWebCredNone:        return "未配置";
    case kWebCredConstant:    return "内置";
    case kWebCredPlain:       return "明文";
    case kWebCredTokenEnc:    return "令牌加密";
    case kWebCredPassEnc:     return "口令加密";
    case kWebCredBroken:      return "解不开";
    }
    return "未知";
}

bool webCredsTokenFileExists() {
    std::string t;
    return loadTokenRaw(t);
}

bool webCredsCreate(const std::string& passphrase,
                    const std::vector<WebCredFieldIn>& fields,
                    std::string& err) {
    std::lock_guard<std::mutex> lock(g_lock);
    cJSON* root = newRoot();
    for (size_t i = 0; i < fields.size(); ++i) {
        const WebCredFieldIn& f = fields[i];
        if (!addFieldToRoot(root, f.name, f.value, f.mode, passphrase, err)) {
            cJSON_Delete(root);
            return false;
        }
    }
    const bool ok = saveRoot(root);
    cJSON_Delete(root);
    if (!ok) {
        err = "写入侧车文件失败: " + sidecarPath();
        return false;
    }
    reloadNoLock();
    return true;
}

bool webCredsSetField(const std::string& name, const std::string& value,
                      WebCredMode mode, const std::string& passphrase,
                      std::string& err) {
    std::lock_guard<std::mutex> lock(g_lock);
    cJSON* root = loadRootForWrite(err);
    if (!root) { return false; }
    const bool ok = addFieldToRoot(root, name, value, mode, passphrase, err);
    if (ok && saveRoot(root)) {
        cJSON_Delete(root);
        reloadNoLock();
        return true;
    }
    cJSON_Delete(root);
    if (err.empty()) { err = "写入侧车文件失败: " + sidecarPath(); }
    return false;
}

bool webCredsRemoveField(const std::string& name, std::string& err) {
    std::lock_guard<std::mutex> lock(g_lock);
    cJSON* root = loadRootForWrite(err);
    if (!root) { return false; }
    cJSON* fields = cJSON_GetObjectItem(root, "fields");
    if (fields) { cJSON_DeleteItemFromObject(fields, name.c_str()); }
    const bool ok = saveRoot(root);
    cJSON_Delete(root);
    if (!ok) {
        err = "写入侧车文件失败: " + sidecarPath();
        return false;
    }
    reloadNoLock();
    return true;
}

bool webCredsList(bool reveal, const std::string& passphrase,
                  std::vector<WebCredListEntry>& out, std::string& err) {
    std::lock_guard<std::mutex> lock(g_lock);
    ensureLoaded();
    out.clear();
    if (g_broken) {
        err = "侧车文件损坏";
        return false;
    }
    std::vector<std::string> names;
    size_t kc = 0;
    const char* const* known = webCredsKnownFields(kc);
    for (size_t i = 0; i < kc; ++i) { names.push_back(std::string(known[i])); }
    cJSON* fields = g_root ? cJSON_GetObjectItem(g_root, "fields") : nullptr;
    if (fields) {
        for (cJSON* ch = fields->child; ch; ch = ch->next) {
            const std::string n = ch->string ? ch->string : "";
            bool seen = false;
            for (size_t i = 0; i < names.size(); ++i) {
                if (names[i] == n) { seen = true; break; }
            }
            if (!seen) { names.push_back(n); }
        }
    }
    for (size_t i = 0; i < names.size(); ++i) {
        WebCredResult r = resolveField(g_root, names[i], std::string(), passphrase);
        WebCredListEntry e;
        e.name = names[i];
        e.status = r.status;
        e.ok = r.ok;
        if (r.ok && (r.status == kWebCredPlain || reveal)) {
            e.value = r.value;
        }
        out.push_back(e);
    }
    return true;
}

bool webCredsWipe(std::string& err) {
    std::lock_guard<std::mutex> lock(g_lock);
    bool ok = true;
    if (::unlink(sidecarPath().c_str()) != 0 && errno != ENOENT) { ok = false; }
    if (::unlink(tokenPath().c_str()) != 0 && errno != ENOENT) { ok = false; }
    reloadNoLock();
    if (!ok) {
        err = "删除侧车/令牌文件失败";
        return false;
    }
    return true;
}

const char* const* webCredsKnownFields(size_t& count) {
    static const char* const kKnown[] = {
        "deepseek_cookie", "deepseek_device_id",
        // API key 字段：<slug>_api（OpenAI 兼容 provider 的 Authorization）
        "pollinations_api", "zhipu_api", "siliconflow_api", "nvidia_api",
        "openrouter_api", "llm7_api", "cloudflare_api", "dashscope_api",
        "ovh_api", "volcengine_api", "modelscope_api", "modelscope_intl_api",
        "groq_api", "huggingface_api", "gemini_api", "zai_api",
        "groq_viacf_api", "gemini_viacf_api", "aihorde_api", "meta_api",
        // TTS：OpenAI / ElevenLabs 凭据（dashscope_api 复用上面同名键）
        "openai_api", "elevenlabs_api",
        // web 登录字段：<slug>_web
        "deepseek_web", "gemini_web", "groq_web", "chatgpt_web", "meta_web",
    };
    count = sizeof(kKnown) / sizeof(kKnown[0]);
    return kKnown;
}