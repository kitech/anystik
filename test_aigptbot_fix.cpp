#include "doctest.h"
#include "aigptbot.h"
#include "aigptbot.cpp"

TEST_CASE("aigptbotProviders 基本") {
    const auto& v = aigptbotProviders();
    CHECK(v.size() > 0);
    CHECK(std::string(v[0].name) == "any");
    CHECK(std::string(v[1].name) == "all");
}
