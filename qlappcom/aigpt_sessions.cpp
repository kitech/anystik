#include "aigpt_sessions.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>

extern "C" {
#include "cJSON.h"
#include "obsd_sha2.h"
}

namespace {

std::mutex g_lock;
cJSON* g_root = nullptr;   // {"<key>":{…}} 惰性加载
bool g_loaded = false;
std::string g_fileOverride;

std::string homeDir() {
    const char* h = getenv("HOME");
    return h ? std::string(h) : std::string();
}

std::string sessionPath() {
    if (!g_fileOverride.empty()) { return g_fileOverride; }
    const char* e = getenv("QTOX_AIGPT_SESSION_FILE");
    if (e && e[0]) { return std::string(e); }
    return homeDir() + "/.config/qltox/aigpt_sessions.json";
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

std::string sha16(const std::string& in) {
    unsigned char d[SHA256_DIGEST_LENGTH];
    SHA2_CTX ctx;
    SHA256Init(&ctx);
    SHA256Update(&ctx, in.data(), in.size());
    SHA256Final(d, &ctx);
    static const char* hexd = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 8; ++i) {
        out += hexd[d[i] >> 4];
        out += hexd[d[i] & 0x0f];
    }
    return out;
}

void ensureLoaded() {
    if (g_loaded) { return; }
    g_loaded = true;
    std::string data;
    if (!readFile(sessionPath(), data) || data.empty()) { return; }
    cJSON* root = cJSON_Parse(data.c_str());
    if (root && cJSON_IsObject(root)) { g_root = root; }
    else if (root) { cJSON_Delete(root); }
}

void save() {
    if (!g_root) { return; }
    char* s = cJSON_PrintUnformatted(g_root);
    if (!s) { return; }
    writeFileAtomic(sessionPath(), std::string(s), 0600);
    free(s);
}

} // namespace

std::string aigptSessionKey(const char* webKindName, const std::string& cred) {
    std::string k = webKindName ? webKindName : "";
    if (!cred.empty()) { k += "|" + sha16(cred); }
    return k;
}

bool aigptSessionGet(const std::string& key, AigptSessionRecord& out) {
    std::lock_guard<std::mutex> lock(g_lock);
    ensureLoaded();
    if (!g_root) { return false; }
    cJSON* n = cJSON_GetObjectItem(g_root, key.c_str());
    if (!n || !cJSON_IsObject(n)) { return false; }
    cJSON* sid = cJSON_GetObjectItem(n, "sid");
    if (!sid || !cJSON_IsString(sid) || !sid->valuestring || !sid->valuestring[0]) { return false; }
    out.sessionId = sid->valuestring;
    cJSON* pid = cJSON_GetObjectItem(n, "pid");
    if (pid && cJSON_IsString(pid) && pid->valuestring) { out.parentId = pid->valuestring; }
    cJSON* cnt = cJSON_GetObjectItem(n, "n");
    if (cnt && cJSON_IsNumber(cnt)) { out.msgCount = (long long)cnt->valuedouble; }
    cJSON* t = cJSON_GetObjectItem(n, "t");
    if (t && cJSON_IsNumber(t)) { out.createdAt = (long long)t->valuedouble; }
    return true;
}

void aigptSessionPut(const std::string& key, const AigptSessionRecord& rec) {
    std::lock_guard<std::mutex> lock(g_lock);
    ensureLoaded();
    if (!g_root) { g_root = cJSON_CreateObject(); }
    cJSON* n = cJSON_CreateObject();
    cJSON_AddStringToObject(n, "sid", rec.sessionId.c_str());
    cJSON_AddStringToObject(n, "pid", rec.parentId.c_str());
    cJSON_AddNumberToObject(n, "n", (double)rec.msgCount);
    cJSON_AddNumberToObject(n, "t", (double)rec.createdAt);
    cJSON_DeleteItemFromObject(g_root, key.c_str());
    cJSON_AddItemToObject(g_root, key.c_str(), n);
    save();
}

void aigptSessionInvalidate(const std::string& key) {
    std::lock_guard<std::mutex> lock(g_lock);
    ensureLoaded();
    if (!g_root) { return; }
    cJSON_DeleteItemFromObject(g_root, key.c_str());
    save();
}

void aigptSessionSetFileForTest(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_root) { cJSON_Delete(g_root); g_root = nullptr; }
    g_loaded = false;
    g_fileOverride = path;
}
