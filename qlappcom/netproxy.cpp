#include "netproxy.h"

#include <qmutex.h>

#include <cstdlib>
#include <cctype>

#include "limelog.h"

namespace {

QMutex g_mutex;
bool g_enabled = false;
std::string g_url;
std::string g_envWarnedRaw;

std::string trimCopy(const std::string& s) {
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && std::isspace((unsigned char)s[begin])) { begin++; }
    while (end > begin && std::isspace((unsigned char)s[end - 1])) { end--; }
    return s.substr(begin, end - begin);
}

std::string lowerCopy(const std::string& s) {
    std::string out = s;
    for (size_t i = 0; i < out.size(); i++) {
        out[i] = (char)std::tolower((unsigned char)out[i]);
    }
    return out;
}

// 校验代理 URL 格式；不合法时 why 返回原因。
bool proxyUrlValid(const std::string& raw, std::string& why) {
    std::string s = trimCopy(raw);
    if (s.empty()) {
        why = "empty";
        return false;
    }
    for (size_t i = 0; i < s.size(); i++) {
        if (std::isspace((unsigned char)s[i])) {
            why = "contains whitespace";
            return false;
        }
    }
    size_t sep = s.find("://");
    if (sep == std::string::npos) {
        return true;
    }
    std::string scheme = lowerCopy(s.substr(0, sep));
    if (scheme != "http" && scheme != "https" && scheme != "socks4" &&
        scheme != "socks4a" && scheme != "socks5" && scheme != "socks5h") {
        why = "unsupported scheme '" + scheme + "'";
        return false;
    }
    if (s.substr(sep + 3).empty()) {
        why = "missing host";
        return false;
    }
    return true;
}

// 未设/空 → 空（继续下一个变量）；非空非法 → 告警(去重)并置 stop=true（直连）。
std::string envProxyVar(const char* name, bool& stop) {
    const char* v = getenv(name);
    if (!v || !*v) {
        return std::string();
    }
    std::string why;
    if (!proxyUrlValid(v, why)) {
        std::string raw(v);
        if (raw != g_envWarnedRaw) {
            ALOG_WARN("invalid proxy env", name, "=", v, ":", why);
            g_envWarnedRaw = raw;
        }
        stop = true;
        return std::string();
    }
    return std::string(v);
}

std::string envProxyFallback() {
    bool stop = false;
    std::string p = envProxyVar("https_proxy", stop);
    if (p.empty() && !stop) { p = envProxyVar("HTTPS_PROXY", stop); }
    if (p.empty() && !stop) { p = envProxyVar("ALL_PROXY", stop); }
    if (p.empty() && !stop) { p = envProxyVar("all_proxy", stop); }
    return p;
}

}

void netProxySet(bool enabled, const std::string& url) {
    QMutexLocker lock(&g_mutex);
    g_enabled = enabled;
    g_url.clear();
    if (!enabled) {
        return;
    }
    std::string trimmed = trimCopy(url);
    if (trimmed.empty()) {
        ALOG_WARN("proxy enabled but url empty");
        return;
    }
    std::string why;
    if (proxyUrlValid(trimmed, why)) {
        g_url = trimmed;
    } else {
        ALOG_WARN("invalid user proxy url", trimmed, ":", why);
    }
}

std::string netProxyFor(bool needsProxy) {
    if (!needsProxy) {
        return std::string();
    }
    QMutexLocker lock(&g_mutex);
    if (g_enabled) {
        return g_url;
    }
    return envProxyFallback();
}
