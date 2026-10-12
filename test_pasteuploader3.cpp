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
    CHECK(c.empty() == false);
}
