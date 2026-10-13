#include "doctest.h"
#include "aigptbot.h"
#include "aigptbot.cpp"

TEST_CASE("aigptbotProviders") {
    const auto& v = aigptbotProviders();
    CHECK(v.size() > 2);
}
