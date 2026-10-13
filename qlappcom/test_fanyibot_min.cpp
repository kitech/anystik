#include "doctest.h"

#include <qapplication.h>
#include <qmutex.h>
#include <qthread.h>

#include "fanyibot.h"
#include "fanyibot.cpp"

static std::string bytesFromHex(const char* h) {
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') { return c - '0'; }
        if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
        if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
        return 0;
    };
    std::string out;
    for (size_t i = 0; h[i] && h[i + 1]; i += 2) {
        out += (char)((val(h[i]) << 4) | val(h[i + 1]));
    }
    return out;
}

static std::string toHex(const std::string& b) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < b.size(); i++) {
        out += hex[(unsigned char)b[i] >> 4];
        out += hex[(unsigned char)b[i] & 0xF];
    }
    return out;
}

static std::string b64urlFromChars(char a, char b) {
    std::string s;
    s += a;
    s += b;
    return s;
}

TEST_CASE("fanyibotEngines") {
    const auto& v = fanyibotEngines();
    CHECK(v.size() == 6);
}

TEST_CASE("fanyibotLangCode 基本") {
    CHECK(fanyibotLangCode(kFanyibotMsedge, "中文") == "zh-Hans");
    CHECK(fanyibotLangCode(kFanyibotGoogle, "中文") == "zh-CN");
    CHECK(fanyibotLangCode(kFanyibotYoudao, "中文") == "zh-CHS");
    CHECK(fanyibotLangCode(kFanyibotYandex, "中文") == "zh");
    CHECK(fanyibotLangCode(kFanyibotDeeplWeb, "中文") == "zh-Hans");
}

TEST_CASE("buildCandidates") {
    FanyibotRequest r;
    r.engine = "all";
    r.toLang = "English";
    std::string reason;
    auto c = buildCandidates(r, reason);
    CHECK(reason.empty());
    CHECK(c.size() == 4);
}

TEST_CASE("parseMsedge") {
    CHECK(parseMsedge("[{\"translations\":[{\"text\":\"你好\"}]}]") == "你好");
    CHECK(parseMsedge("{\"translations\":[{\"text\":\"hi\"}]}") == "hi");
    CHECK(parseMsedge("[{\"detectedLanguage\":{\"language\":\"en\"},"
                      "\"translations\":[{\"text\":\"你好，世界\",\"to\":\"zh-Hans\"}]}]")
          == "你好，世界");
}

TEST_CASE("parseGoogle") {
    CHECK(parseGoogle("[[[\"hi\",\"hello\"]]]") == "hi");
}

TEST_CASE("parseYoudaoV2") {
    CHECK(parseYoudaoV2("{\"translateResult\":[[{\"tgt\":\"abc\"}]]}") == "abc");
    CHECK(parseYoudaoV2("{\"translateResult\":[[{\"tgt\":\"hi\"}],[{\"tgt\":\" there\"}]]}")
          == "hi there");
    CHECK(parseYoudaoV2("{\"translation\":[{\"t\":\"legacy\"}]}") == "legacy");
}

TEST_CASE("youdaoResultCode") {
    CHECK(youdaoResultCode("{\"code\":0}") == 0);
    CHECK(youdaoResultCode("{\"code\":50}") == 50);
    CHECK(youdaoResultCode("eh6zVNeFZ0TD1ei8mSABcQ==") == 0);
}

TEST_CASE("scrapeYandexSid") {
    CHECK(scrapeYandexSid("sid: '1234567890abcdef.12345'") == "1234567890abcdef.12345");
    CHECK(scrapeYandexSid("no sid").empty());
}

TEST_CASE("parseYandex") {
    CHECK(parseYandex("{\"code\":200,\"text\":[\"hi\"]}") == "hi");
}

TEST_CASE("parseDeeplWeb") {
    CHECK(parseDeeplWeb("{\"translations\":[{\"text\":\"你好\"}]}") == "你好");
}

TEST_CASE("base64UrlDecode") {
    CHECK(base64UrlDecode("aGVsbG8=") == "hello");
    CHECK(base64UrlDecode("aGVsbG8") == "hello");
    CHECK(base64UrlDecode(b64urlFromChars('-', '_')) == bytesFromHex("fb"));
    CHECK(base64UrlDecode(b64urlFromChars('_', '-')) == bytesFromHex("ff"));
}

TEST_CASE("md5Raw") {
    CHECK(toHex(md5Raw("abc")) == "900150983cd24fb0d6963f7d28e17f72");
}

TEST_CASE("AES-128-CBC FIPS-197 单块") {
    uint8_t key[16], ct[16], out[16];
    for (int i = 0; i < 16; i++) { key[i] = (uint8_t)i; }
    const uint8_t expect[16] = { 0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
                                 0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff };
    const uint8_t ctBytes[16] = { 0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,
                                  0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a };
    std::memcpy(ct, ctBytes, 16);
    aesDecryptBlock(key, ct, out);
    for (int i = 0; i < 16; i++) { CHECK(out[i] == expect[i]); }
}

TEST_CASE("AES-128-CBC PKCS7") {
    std::string key;
    for (int i = 0; i < 16; i++) { key += (char)i; }
    const std::string iv(16, '\0');
    CHECK(aes128CbcDecrypt(key, iv,
        bytesFromHex("493e03c05f98d3c43fc46bbeff24ab50")) == "hello\n");
    const std::string multi =
        aes128CbcDecrypt(key, iv,
                         bytesFromHex("e2228e7dafd51dc06aa04110722be127164ece0e7684af8215b6e02f84f3a29837bfb07ca8e9b81966582efe3b02ccb7"));
    CHECK(multi == "the quick brown fox jumps over the lazy dog\n");
}