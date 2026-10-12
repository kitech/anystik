#include "doctest.h"
#include "pasteuploader.h"
#include "pasteuploader.cpp"

TEST_CASE("kHosts 基本") {
    CHECK(kHostCount == 11);
    CHECK(std::string(kHosts[0].provider) == "dpaste.com");
}

TEST_CASE("all text 非空") {
    PasteRequest r;
    r.kind = kPasteKindText;
    r.expire = kPasteExpNever;
    r.provider = "all";
    std::string reason;
    auto c = buildCandidates(r, reason);
    CHECK(reason.empty());
    CHECK(!c.empty());
    for (size_t i = 0; i < c.size(); i++) {
        CHECK(c[i]->supportsText);
    }
}

TEST_CASE("all image 过滤") {
    PasteRequest r;
    r.kind = kPasteKindImage;
    r.expire = kPasteExpNever;
    r.provider = "all";
    std::string reason;
    auto c = buildCandidates(r, reason);
    CHECK(reason.empty());
    for (size_t i = 0; i < c.size(); i++) {
        CHECK(c[i]->supportsImage);
    }
}

TEST_CASE("显式指定") {
    PasteRequest r;
    r.kind = kPasteKindText;
    r.expire = kPasteExpNever;
    r.provider = "catbox";
    std::string reason;
    auto c = buildCandidates(r, reason);
    CHECK(c.size() == 1);
}
