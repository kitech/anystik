#include "doctest.h"
#include "aigptbot.h"
#include "aigptbot.cpp"

namespace {
std::string parseOpenAiLike(const std::string& body) {
    std::string apiErr;
    return parseOpenAiContent(body, apiErr);
}
}

TEST_CASE("aigptbotProviders 基本") {
    const auto& v = aigptbotProviders();
    CHECK(v.size() > 2);
}

TEST_CASE("buildCandidates any 不包含 AI Horde") {
    AigptbotRequest r;
    r.provider = "any";
    r.text = "hi";
    std::string reason;
    auto c = buildCandidates(r, reason);
    for (size_t i = 0; i < c.size(); i++) {
        CHECK(std::string(c[i]->name) != "AI Horde");
        CHECK(std::string(c[i]->name) != "aihorde");
    }
}

TEST_CASE("buildCandidates all 不包含 AI Horde") {
    AigptbotRequest r;
    r.provider = "all";
    r.text = "hi";
    std::string reason;
    auto c1 = buildCandidates(r, reason);
    auto c2 = buildCandidates(r, reason);
    CHECK(c1.size() == c2.size());
    for (size_t i = 0; i < c1.size(); i++) {
        CHECK(std::string(c1[i]->name) != "AI Horde");
        CHECK(std::string(c1[i]->name) != "aihorde");
    }
}

TEST_CASE("buildCandidates 指定 aihorde 进入候选") {
    AigptbotRequest r;
    r.provider = "aihorde";
    r.text = "hi";
    std::string reason;
    auto c = buildCandidates(r, reason);
    CHECK(c.size() == 1);
    CHECK(std::string(c[0]->name) == "aihorde");
}

TEST_CASE("parseOpenAiLike 基本解析") {
    const char* j = R"({"choices":[{"message":{"content":"你好"}}]})";
    CHECK(parseOpenAiLike(j) == "你好");
    const char* j2 = R"({"choices":[{"text":"hi"}]})";
    CHECK(parseOpenAiLike(j2) == "hi");
    CHECK(parseOpenAiLike("not json").empty());
    CHECK(parseOpenAiLike("{}").empty());
}
