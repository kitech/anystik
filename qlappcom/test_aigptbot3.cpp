#include "doctest.h"
#include "aigptbot.h"
#include "aigptbot.cpp"

TEST_CASE("aigptbotProviders 基本结构") {
    const auto& v = aigptbotProviders();
    CHECK(v.size() > 2);
}
