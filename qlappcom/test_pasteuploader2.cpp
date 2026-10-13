#include "doctest.h"
#include "pasteuploader.h"
#include "pasteuploader.cpp"

TEST_CASE("kHosts 表结构与顺序") {
    CHECK(kHostCount == 11);
    CHECK(std::string(kHosts[0].name) == "dpaste.com");
    CHECK(std::string(kHosts[1].name) == "catbox");
    CHECK(std::string(kHosts[2].name) == "0x0.st");
    CHECK(std::string(kHosts[3].name) == "transfer.sh");
}

TEST_CASE("buildCandidates all text") {
    PasteRequest r;
    r.kind = kPasteKindText;
    r.expire = kPasteExpNever;
    r.provider = "all";
    std::string reason;
    auto c = buildCandidates(r, reason);
    CHECK(reason.empty());
    CHECK(c.size() > 0);
    for (size_t i = 0; i < c.size(); i++) {
        CHECK(c[i]->supportsText == true);
    }
}
