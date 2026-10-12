#include "doctest.h"
#include "translate_util.cpp"

TEST_CASE("isNeedTranslateToChinese - 基础分类") {
    SUBCASE("纯英文 - NEED") {
        CHECK(isNeedTranslateToChinese("damn cool") == true);
        CHECK(isNeedTranslateToChinese("hello world") == true);
        CHECK(isNeedTranslateToChinese("the quick brown fox jumps over the lazy dog") == true);
    }

    SUBCASE("纯中文 - NO_NEED") {
        CHECK(isNeedTranslateToChinese("你好世界") == false);
        CHECK(isNeedTranslateToChinese("真他妈酷") == false);
    }

    SUBCASE("空字符串 - NO_NEED") {
        CHECK(isNeedTranslateToChinese("") == false);
    }
}

TEST_CASE("isNeedTranslateToChinese - 不可翻译区间保护") {
    SUBCASE("中文+URL - NO_NEED") {
        CHECK(isNeedTranslateToChinese("你好 https://example.com/very/long/path") == false);
    }

    SUBCASE("英文+Matrix ID - NEED") {
        CHECK(isNeedTranslateToChinese("damn cool @_discord_xxx:matrix.server.com") == true);
    }

    SUBCASE("中文+邮件 - NO_NEED") {
        CHECK(isNeedTranslateToChinese("你好 world@test.com") == false);
    }

    SUBCASE("纯URL - NO_NEED") {
        CHECK(isNeedTranslateToChinese("https://example.com/very/long/path/that/is/really/long") == false);
    }
}

TEST_CASE("needsAutoTranslate - 语言分发") {
    SUBCASE("目标中文") {
        CHECK(needsAutoTranslate("damn cool", "zh") == true);
        CHECK(needsAutoTranslate("你好世界", "zh") == false);
    }

    SUBCASE("目标英文 - 总是翻译") {
        CHECK(needsAutoTranslate("damn cool", "en") == true);
        CHECK(needsAutoTranslate("你好世界", "en") == true);
    }
}
