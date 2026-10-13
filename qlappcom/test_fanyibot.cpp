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

TEST_CASE("fanyibotEngines 基本结构") {
    const auto& v = fanyibotEngines();
    CHECK(v.size() == 6);
    CHECK(std::string(v[0].name) == "msedge");
    CHECK(std::string(v[1].name) == "google");
    CHECK(std::string(v[2].name) == "youdao");
    CHECK(std::string(v[3].name) == "yandex");
    CHECK(std::string(v[4].name) == "deepl");
    CHECK(std::string(v[5].name) == "deepl-web");
    CHECK(v[1].unsupported == true);   // google 已禁用
    CHECK(v[4].unsupported == true);
    CHECK(v[5].unsupported == false);
}

TEST_CASE("fanyibotLangCode 映射") {
    CHECK(fanyibotLangCode(kFanyibotMsedge, "中文") == "zh-Hans");
    CHECK(fanyibotLangCode(kFanyibotGoogle, "中文") == "zh-CN");
    CHECK(fanyibotLangCode(kFanyibotYoudao, "中文") == "zh-CHS");
    CHECK(fanyibotLangCode(kFanyibotYandex, "中文") == "zh");
    CHECK(fanyibotLangCode(kFanyibotDeepl, "English").empty());
    CHECK(fanyibotLangCode(kFanyibotDeeplWeb, "中文") == "zh-Hans");
    CHECK(fanyibotLangCode(kFanyibotDeeplWeb, "English") == "en-US");
    CHECK(fanyibotLangCode(kFanyibotDeeplWeb, "地球语") == "eo");
}

TEST_CASE("buildCandidates all 随机取序") {
    FanyibotRequest r;
    r.engine = "all";
    r.toLang = "English";
    std::string reason;
    auto c = buildCandidates(r, reason);
    CHECK(reason.empty());
    CHECK(c.size() == 4);
}

TEST_CASE("buildCandidates any 非空") {
    FanyibotRequest r;
    r.engine = "any";
    r.toLang = "English";
    std::string reason;
    auto c = buildCandidates(r, reason);
    CHECK(reason.empty());
    CHECK(c.size() == 4);
}

TEST_CASE("parseMsedge 数组形/对象形") {
    CHECK(parseMsedge("[{\"translations\":[{\"text\":\"你好\"}]}]") == "你好");
    CHECK(parseMsedge("{\"translations\":[{\"text\":\"hi\"}]}") == "hi");
    CHECK(parseMsedge("[{\"detectedLanguage\":{\"language\":\"en\",\"score\":0.9},"
                      "\"translations\":[{\"text\":\"你好，世界\",\"to\":\"zh-Hans\"}]}]")
          == "你好，世界");
}

TEST_CASE("parseGoogle 基本") {
    CHECK(parseGoogle("[[[\"hi\",\"hello\"]]]") == "hi");
}

TEST_CASE("parseYoudaoV2 基本") {
    CHECK(parseYoudaoV2("{\"translateResult\":[[{\"tgt\":\"你好\"}]]}") == "你好");
    CHECK(parseYoudaoV2("{\"translateResult\":[[{\"tgt\":\"abc\"}]]}") == "abc");
    CHECK(parseYoudaoV2("{\"translateResult\":[[{\"tgt\":\"hi\"}],[{\"tgt\":\" there\"}]]}")
          == "hi there");
    CHECK(parseYoudaoV2("{\"translateResult\":[[{\"t\":\"old\"}]]}") == "old");
    CHECK(parseYoudaoV2("{\"translation\":[{\"t\":\"legacy\"}]}") == "legacy");
    CHECK(parseYoudaoV2("{\"code\":50}").empty());
}

TEST_CASE("youdaoResultCode") {
    CHECK(youdaoResultCode("{\"code\":0}") == 0);
    CHECK(youdaoResultCode("{\"code\":50}") == 50);
    CHECK(youdaoResultCode("not json") == 0);
    CHECK(youdaoResultCode("eh6zVNeFZ0TD1ei8mSABcQ==") == 0);   // 裸 base64 响应
}

TEST_CASE("youdaoKeyParse") {
    std::string sec, a, iv, ck;
    CHECK(youdaoKeyParse(
        "{\"secretKey\":\"s\",\"aesKey\":\"ak\",\"aesIv\":\"iv\",\"cookie\":\"ck\"}",
        sec, a, iv, ck));
    CHECK(sec == "s");
    CHECK(a == "ak");
    CHECK(iv == "iv");
    CHECK(ck == "ck");

    sec = a = iv = ck = std::string();
    CHECK_FALSE(youdaoKeyParse("{\"secretKey\":\"s\",\"aesKey\":\"\"}", sec, a, iv, ck));

    sec = a = iv = ck = std::string();
    CHECK(youdaoKeyParse(
        "{\"code\":0,\"data\":{\"secretKey\":\"s2\",\"aesKey\":\"ak2\",\"aesIv\":\"iv2\"}}",
        sec, a, iv, ck));   // 现接口 data.{...} 嵌套
    CHECK(sec == "s2");
    CHECK(a == "ak2");
    CHECK(iv == "iv2");
    CHECK(ck.empty());
}

TEST_CASE("scrapeYandexSid") {
    CHECK(scrapeYandexSid("var t={srv:1,sid: '1234567890abcdef.12345',no:2}")
          == "1234567890abcdef.12345");
    CHECK(scrapeYandexSid("no sid here").empty());
}

TEST_CASE("parseYandex 基本") {
    CHECK(parseYandex("{\"code\":200,\"text\":[\"hi\"]}") == "hi");
}

TEST_CASE("parseDeeplWeb 基本") {
    CHECK(parseDeeplWeb("{\"translations\":[{\"detected_source_language\":\"en\","
                        "\"text\":\"你好\"}]}") == "你好");
    CHECK(parseDeeplWeb("{\"translations\":[{\"text\":\"hi\"}]}") == "hi");
    CHECK(parseDeeplWeb("{\"translations\":[]}").empty());
    CHECK(parseDeeplWeb("not json").empty());
}

TEST_CASE("base64UrlDecode") {
    CHECK(base64UrlDecode("aGVsbG8=") == "hello");
    CHECK(base64UrlDecode("aGVsbG8") == "hello");
    CHECK(base64UrlDecode(b64urlFromChars('-', '_')) == bytesFromHex("fb"));
    CHECK(base64UrlDecode(b64urlFromChars('_', '-')) == bytesFromHex("ff"));
    CHECK(base64UrlDecode("") == "");
}

TEST_CASE("md5Raw") {
    CHECK(toHex(md5Raw("abc")) == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(md5Raw("secret").size() == 16);
}

TEST_CASE("AES-128-CBC 单块 FIPS-197 已知答案") {
    uint8_t key[16], in[16], out[16];
    for (int i = 0; i < 16; i++) { key[i] = (uint8_t)i; }
    const uint8_t expect[16] = { 0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
                                 0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff };
    const uint8_t ct[16] = { 0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,
                             0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a };
    aesDecryptBlock(key, ct, out);
    for (int i = 0; i < 16; i++) { CHECK(out[i] == expect[i]); }
}

TEST_CASE("AES-128-CBC PKCS7" ) {
    std::string key;
    for (int i = 0; i < 16; i++) { key += (char)i; }
    const std::string iv(16, '\0');
    const std::string single =
        aes128CbcDecrypt(key, iv, bytesFromHex("493e03c05f98d3c43fc46bbeff24ab50"));
    CHECK(single == "hello\n");
    const std::string multi =
        aes128CbcDecrypt(key, iv,
                         bytesFromHex("e2228e7dafd51dc06aa04110722be127164ece0e7684af8215b6e02f84f3a29837bfb07ca8e9b81966582efe3b02ccb7"));
    CHECK(multi == "the quick brown fox jumps over the lazy dog\n");
    CHECK(aes128CbcDecrypt(key, iv, std::string(15, 'x')).empty());
    CHECK(aes128CbcDecrypt(std::string(8, 'k'), iv, std::string(16, 0)).empty());
}