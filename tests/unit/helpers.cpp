#include "helpers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <curl/curl.h>
#include <jwt-cpp/jwt.h>
#include <openssl/rand.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>

namespace yaxi::test {
namespace {

struct CurlGlobal {
    CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobal() { curl_global_cleanup(); }
};
CurlGlobal _curlGlobal;

std::optional<std::string> envOpt(const char *name) {
#ifdef _MSC_VER
    // MSVC deprecates std::getenv (C4996); _dupenv_s is the recommended
    // replacement. Returns 0 on success even when the variable is unset,
    // with buf == nullptr in that case.
    char *buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, name) != 0 || !buf)
        return std::nullopt;
    std::string value(buf);
    std::free(buf);
    if (value.empty())
        return std::nullopt;
    return value;
#else
    const char *v = std::getenv(name);
    if (!v || !*v)
        return std::nullopt;
    return std::string(v);
#endif
}

} // namespace

std::optional<Config> getConfig() {
    auto keyId = envOpt("YAXI_KEY_ID");
    auto apiKey = envOpt("YAXI_API_KEY");
    auto url = envOpt("ROUTEX_URL");
    if (!keyId || !apiKey || !url)
        return std::nullopt;
    while (!url->empty() && url->back() == '/')
        url->pop_back();
    return Config{*keyId, *apiKey, *url};
}

Config getConfigOrSkip() {
    auto cfg = getConfig();
    if (!cfg) {
        SKIP("Set YAXI_API_KEY, YAXI_KEY_ID and ROUTEX_URL to run [online] tests");
    }
    return *cfg;
}

nlohmann::json jwtDecodeUnverified(const std::string &jwt) {
    auto decoded = ::jwt::decode(jwt);
    return nlohmann::json::parse(decoded.get_payload());
}

std::string httpGetRedirectLocation(const std::string &url) {
    CURL *curl = curl_easy_init();
    if (!curl)
        throw std::runtime_error("curl_easy_init failed");
    auto guard = std::unique_ptr<CURL, void (*)(CURL *)>(curl, curl_easy_cleanup);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(
        curl, CURLOPT_WRITEFUNCTION,
        +[](char *, size_t s, size_t n, void *) -> size_t { return s * n; });

    if (auto rc = curl_easy_perform(curl); rc != CURLE_OK) {
        throw std::runtime_error(std::string("curl_easy_perform: ") + curl_easy_strerror(rc));
    }

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    REQUIRE(status == 303);

    char *loc = nullptr;
    curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &loc);
    return loc ? std::string{loc} : std::string{};
}

std::string uuidV4() {
    unsigned char b[16];
    if (RAND_bytes(b, 16) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }
    b[6] = static_cast<unsigned char>((b[6] & 0x0F) | 0x40); // version 4
    b[8] = static_cast<unsigned char>((b[8] & 0x3F) | 0x80); // variant 1
    char buf[37];
    std::snprintf(buf, sizeof(buf),
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0],
                  b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13],
                  b[14], b[15]);
    return std::string(buf, 36);
}

std::vector<uint8_t> base64Decode(const std::string &input) {
    auto s = ::jwt::base::decode<::jwt::alphabet::base64>(input);
    return std::vector<uint8_t>(s.begin(), s.end());
}

} // namespace yaxi::test
