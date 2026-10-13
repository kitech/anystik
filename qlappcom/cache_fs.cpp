#include "cache_fs.h"
extern "C" {
#include "md5.h"
}
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>

static std::string joinPath(const std::string& dir, const std::string& name) {
    return dir + "/" + name;
}

static std::string hexDigest(const uint8_t digest[16]) {
    static const char hex[] = "0123456789abcdef";
    char buf[33];
    for (int i = 0; i < 16; i++) {
        buf[i * 2]     = hex[(digest[i] >> 4) & 0xf];
        buf[i * 2 + 1] = hex[digest[i] & 0xf];
    }
    buf[32] = '\0';
    return std::string(buf, 32);
}

static bool mkdirIfNeeded(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        return true;
    }
    return mkdir(path.c_str(), 0755) == 0;
}

void initCacheFsDirs(const char* dataDir) {
    std::string base = joinPath(dataDir, "cache_fs");
    mkdirIfNeeded(base);
    static const char hex[] = "0123456789abcdef";
    for (int hi = 0; hi < 256; hi++) {
        char sub[3] = { hex[hi >> 4], hex[hi & 0xf], '\0' };
        mkdirIfNeeded(joinPath(base, sub));
    }
}

std::string makeCacheFsPath(const char* key) {
    MD5_CTX ctx;
    MD5_Init(&ctx);
    MD5_Update(&ctx, (const uint8_t*)key, std::strlen(key));
    uint8_t digest[16];
    MD5_Final(digest, &ctx);
    std::string hex = hexDigest(digest);
    std::string base = "cache_fs";
    return base + "/" + hex.substr(0, 2) + "/" + hex;
}

bool writeCacheFile(const std::string& fullPath, const void* data, size_t size) {
    FILE* f = std::fopen(fullPath.c_str(), "wb");
    if (!f) { return false; }
    bool ok = std::fwrite(data, 1, size, f) == size;
    std::fclose(f);
    return ok;
}

std::vector<uint8_t> readCacheFile(const std::string& fullPath) {
    FILE* f = std::fopen(fullPath.c_str(), "rb");
    if (!f) { return {}; }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    if (sz <= 0) { std::fclose(f); return {}; }
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf((size_t)sz);
    bool ok = std::fread(buf.data(), 1, (size_t)sz, f) == (size_t)sz;
    std::fclose(f);
    if (!ok) { return {}; }
    return buf;
}

bool removeCacheFile(const std::string& fullPath) {
    return ::remove(fullPath.c_str()) == 0;
}
