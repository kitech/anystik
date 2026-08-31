// android_tls_bootstrap.cpp (B：跨版本/多机型加固)
// 在 QGuiApplication 构造前调用：解析 /proc/self/maps 定位应用自带
// libQt6Core 所在目录，以绝对路径 dlopen libcrypto_3/libssl_3（裸名兜底），
// RTLD_NOW|RTLD_GLOBAL。绝对路径 dlopen 不受各 Android 版本/ROM 的
// linker namespace 配置差异影响；预加载后 Qt "ssl_3" 探测在同 namespace
// 内按已加载 SONAME 直接命中。各步输出 dlerror() 便于 logcat 排查。

#include "android_tls_bootstrap.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#ifdef Q_OS_ANDROID
#include <android/log.h>
#endif

#ifdef Q_OS_ANDROID

#define LOG_TAG "anystik-tls"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

// 从 /proc/self/maps 找出第一个属于应用的库所在目录。
bool findNativeLibDir(char *out, size_t outSize)
{
    static const char *kMarks[] = {
        "libQt6Core_arm64-v8a.so",
        "libanystik_arm64-v8a.so",
    };
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f)
        return false;
    char line[1024];
    bool ok = false;
    for (const char *mark : kMarks) {
        while (fgets(line, sizeof(line), f)) {
            if (!strstr(line, mark))
                continue;
            char *p = strchr(line, '/');           // 绝对路径起点
            char *slash = p ? strrchr(p, '/') : nullptr;
            if (p && slash && slash > p) {
                size_t n = static_cast<size_t>(slash - p);
                if (n > 0 && n < outSize) {
                    memcpy(out, p, n);
                    out[n] = '\0';
                    ok = true;
                }
            }
            break;
        }
        if (ok)
            break;
        rewind(f);
    }
    fclose(f);
    return ok;
}

void tryLoad(const char *dir, const char *name)
{
    char path[512];
    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= static_cast<int>(sizeof(path)))
        return;
    void *h = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (h) {
        LOGI("loaded %s", path);
    } else {
        LOGE("dlopen %s failed: %s", path, dlerror());
    }
}

} // namespace

void android_tls_bootstrap()
{
    char dir[256] = {0};
    if (!findNativeLibDir(dir, sizeof(dir))) {
        LOGE("cannot locate native lib dir from /proc/self/maps");
        return;
    }
    LOGI("native lib dir: %s", dir);

    // 先 crypto 后 ssl；带后缀名优先，裸名兜底。
    tryLoad(dir, "libcrypto_3.so");
    tryLoad(dir, "libssl_3.so");
    tryLoad(dir, "libcrypto.so");
    tryLoad(dir, "libssl.so");
}

#endif // Q_OS_ANDROID