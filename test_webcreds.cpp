#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "webcreds.h"

namespace {

// 每个用例独立临时目录（进程内递增，避免串扰）。
std::string tmpDir() {
    static unsigned seq = 0;
    ++seq;
    std::string d = "/tmp/opencode/webcreds_test_"
                  + std::to_string((unsigned long)getpid()) + "_" + std::to_string(seq);
    return d;
}

void setUpCase(std::string& sidecar, std::string& token) {
    sidecar = tmpDir() + "/a/webcreds.enc";
    token = tmpDir() + "/a/webcreds.key";
    webCredsSetFileOverride(sidecar, token);
    unsetenv("QTOX_WEB_CRED_PASS");
    setenv("QTOX_WEB_CRED_ITERS", "2000", 1);   // 测试提速
    webCredsReload();
}

void readText(const std::string& path, std::string& out) {
    FILE* f = fopen(path.c_str(), "rb");
    REQUIRE(f != nullptr);
    char buf[4096];
    out.clear();
    size_t r;
    while ((r = fread(buf, 1, sizeof(buf), f)) > 0) { out.append(buf, r); }
    fclose(f);
}

} // namespace

TEST_CASE("webcreds: 信封往返（plain/token/pass 三态混写 + 解密）" * doctest::timeout(20)) {
    std::string sc, tk;
    setUpCase(sc, tk);

    std::vector<WebCredFieldIn> fields;
    WebCredFieldIn f1; f1.name = "deepseek";      f1.value = "plain-secret";    f1.mode = kWebCredPlainMode;
    WebCredFieldIn f2; f2.name = "gemini";        f2.value = "token-secret";    f2.mode = kWebCredTokenMode;
    WebCredFieldIn f3; f3.name = "grok";          f3.value = "pass-secret";     f3.mode = kWebCredPassMode;
    fields.push_back(f1); fields.push_back(f2); fields.push_back(f3);

    std::string err;
    REQUIRE(webCredsCreate("pw123", fields, err));

    // 侧车已存在且不含 token/pass 加密字段的明文
    std::string raw;
    readText(sc, raw);
    REQUIRE(raw.find("QLW1") != std::string::npos);
    CHECK(raw.find("token-secret") == std::string::npos);
    CHECK(raw.find("pass-secret") == std::string::npos);

    // 明文直读
    WebCredResult r = webCredGet("deepseek", std::string());
    CHECK(r.ok);
    CHECK(r.status == kWebCredPlain);
    CHECK(r.value == "plain-secret");

    // 令牌加密：零交互解密
    r = webCredGet("gemini", std::string());
    CHECK(r.ok);
    CHECK(r.status == kWebCredTokenEnc);
    CHECK(r.value == "token-secret");

    // 无口令 → 口令字段不可用但状态=口令加密（不回退常量）
    r = webCredGet("grok", "builtin-fallback");
    CHECK(!r.ok);
    CHECK(r.status == kWebCredPassEnc);

    // 有口令 → 可解
    setenv("QTOX_WEB_CRED_PASS", "pw123", 1);
    webCredsReload();
    r = webCredGet("grok", "builtin-fallback");
    CHECK(r.ok);
    CHECK(r.status == kWebCredPassEnc);
    CHECK(r.value == "pass-secret");

    // 错误口令 → Broken，绝不低于常量
    setenv("QTOX_WEB_CRED_PASS", "wrong", 1);
    webCredsReload();
    r = webCredGet("grok", "builtin-fallback");
    CHECK(!r.ok);
    CHECK(r.status == kWebCredBroken);
    CHECK(r.value != "builtin-fallback");
}

TEST_CASE("webcreds: 无侧车 → None / Constant 兜底" * doctest::timeout(10)) {
    std::string sc, tk;
    setUpCase(sc, tk);

    WebCredResult r = webCredGet("deepseek", std::string());
    CHECK(!r.ok);
    CHECK(r.status == kWebCredNone);

    r = webCredGet("deepseek", "constant-cred");
    CHECK(r.ok);
    CHECK(r.status == kWebCredConstant);
    CHECK(r.value == "constant-cred");
}

TEST_CASE("webcreds: 令牌缺失 → Broken，且删字段才回退常量" * doctest::timeout(20)) {
    std::string sc, tk;
    setUpCase(sc, tk);

    std::vector<WebCredFieldIn> fields;
    WebCredFieldIn f; f.name = "gemini"; f.value = "token-secret"; f.mode = kWebCredTokenMode;
    fields.push_back(f);
    std::string err;
    REQUIRE(webCredsCreate("pw", fields, err));
    REQUIRE(webCredsTokenFileExists());

    // 删令牌文件 → 该字段不可用（Broken）
    REQUIRE(unlink(tk.c_str()) == 0);
    webCredsReload();
    WebCredResult r = webCredGet("gemini", "constant-cred");
    CHECK(!r.ok);
    CHECK(r.status == kWebCredBroken);

    // 重新生成令牌，恢复
    err.clear();
    REQUIRE(webCredsSetField("gemini", "token-secret-2", kWebCredTokenMode, std::string(), err));
    r = webCredGet("gemini", "constant-cred");
    CHECK(r.ok);
    CHECK(r.status == kWebCredTokenEnc);
    CHECK(r.value == "token-secret-2");

    // 删字段 → 回退常量
    REQUIRE(webCredsRemoveField("gemini", err));
    r = webCredGet("gemini", "constant-cred");
    CHECK(r.ok);
    CHECK(r.status == kWebCredConstant);
    CHECK(r.value == "constant-cred");
}

TEST_CASE("webcreds: 篡改密文 → MAC 拒绝（Broken）" * doctest::timeout(20)) {
    std::string sc, tk;
    setUpCase(sc, tk);

    std::vector<WebCredFieldIn> fields;
    WebCredFieldIn f; f.name = "grok"; f.value = "tamper-me-value"; f.mode = kWebCredPassMode;
    fields.push_back(f);
    std::string err;
    REQUIRE(webCredsCreate("pw123", fields, err));

    // 篡改一个字符（把某处 'A'-'Z' 换掉）
    std::string raw;
    readText(sc, raw);
    bool flipped = false;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] >= 'A' && raw[i] <= 'Z') {
            raw[i] = (char)((raw[i] - 'A' + 1) % 26 + 'A');
            flipped = true;
            break;
        }
    }
    REQUIRE(flipped);
    FILE* w = fopen(sc.c_str(), "wb");
    REQUIRE(w);
    fwrite(raw.data(), 1, raw.size(), w);
    fclose(w);

    setenv("QTOX_WEB_CRED_PASS", "pw123", 1);
    webCredsReload();
    WebCredResult r = webCredGet("grok", "constant-cred");
    CHECK(!r.ok);
    CHECK(r.status == kWebCredBroken);
}

TEST_CASE("webcreds: list/status 名称/known-fields/wipe" * doctest::timeout(10)) {
    std::string sc, tk;
    setUpCase(sc, tk);

    size_t kc = 0;
    const char* const* known = webCredsKnownFields(kc);
    CHECK(kc >= 4);
    CHECK(kc >= 27);   // 7 web + 20 noweb API key，防字段数回退
    CHECK(known != nullptr);

    // 状态名非空
    CHECK(std::string(webCredStatusName(kWebCredNone)).size() > 0);
    CHECK(std::string(webCredStatusName(kWebCredBroken)).size() > 0);

    std::vector<WebCredFieldIn> fields;
    WebCredFieldIn f; f.name = "grok"; f.value = "v"; f.mode = kWebCredPassMode;
    fields.push_back(f);
    std::string err;
    REQUIRE(webCredsCreate("pw123", fields, err));

    std::vector<WebCredListEntry> out;
    REQUIRE(webCredsList(false, std::string(), out, err));
    bool found = false;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i].name == "grok") {
            found = true;
            CHECK(out[i].status == kWebCredPassEnc);
            CHECK(!out[i].ok);
            CHECK(out[i].value.empty());   // 遮蔽
        }
    }
    CHECK(found);

    // wipe 清理
    REQUIRE(webCredsWipe(err));
    CHECK(!webCredsTokenFileExists());
    std::string raw;
    FILE* fh = fopen(sc.c_str(), "rb");
    CHECK(fh == nullptr);
    if (fh) { fclose(fh); }
}