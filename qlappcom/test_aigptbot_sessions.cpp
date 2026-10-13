#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdio>
#include <string>

#include "aigpt_sessions.h"

namespace {

std::string tmpFile(const char* name) {
    return std::string("/tmp/opencode/qltox_sess_test_") + name;
}

} // namespace

TEST_CASE("aigptSessionKey 指纹") {
    const std::string a = aigptSessionKey("deepseek-web", "cred-1");
    const std::string b = aigptSessionKey("deepseek-web", "cred-1");
    const std::string c = aigptSessionKey("deepseek-web", "cred-2");
    CHECK(a == b);
    CHECK(a != c);
    CHECK(aigptSessionKey("grok-web", "") == "grok-web");
}

TEST_CASE("aigptSession 存取/失效") {
    const std::string file = tmpFile("store.json");
    std::remove(file.c_str());
    aigptSessionSetFileForTest(file);

    AigptSessionRecord in;
    in.sessionId = "sess-1";
    in.parentId  = "42";
    in.msgCount  = 3;
    in.createdAt = 100;
    aigptSessionPut("k", in);

    AigptSessionRecord out;
    CHECK(aigptSessionGet("k", out));
    CHECK(out.sessionId == "sess-1");
    CHECK(out.parentId == "42");
    CHECK(out.msgCount == 3);
    CHECK(out.createdAt == 100);

    aigptSessionInvalidate("k");
    CHECK(!aigptSessionGet("k", out));
    std::remove(file.c_str());
}

TEST_CASE("aigptSession 未命中") {
    const std::string file = tmpFile("missing.json");
    std::remove(file.c_str());
    aigptSessionSetFileForTest(file);
    AigptSessionRecord out;
    CHECK(!aigptSessionGet("nope", out));
}
