#include "helpers.hpp"
#include "ticket.hpp"

#include <catch2/catch_test_macros.hpp>
#include <yaxi/routex-client.h>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

using namespace yaxi;
using namespace yaxi::test;

namespace {
constexpr auto DEMO_CONNECTION_ID = "connection-96386142-60e5-4ca9-abcf-944efce5bc1e";

ServiceResult requireServiceResult(Result<ServiceResponse> &result) {
    REQUIRE_THAT(result, HoldsAlternative<ServiceResponse>());
    auto &response = std::get<ServiceResponse>(result);
    REQUIRE_THAT(response, HoldsAlternative<ServiceResult>());
    return std::get<ServiceResult>(std::move(response));
}
} // namespace

TEST_CASE_METHOD(OnlineFixture, "AccountFilter", "[online][account_filter]") {
    auto fetchAccounts = [&](std::optional<AccountFilter> filter) -> nlohmann::json {
        auto ticket = generator.accounts(uuidV4());
        Credentials credentials{};
        credentials.connectionId = DEMO_CONNECTION_ID;
        credentials.userId = "result";
        auto result = client.accounts(credentials, ticket, {AccountField::Iban}, std::move(filter));
        auto sr = requireServiceResult(result);
        return jwtDecodeUnverified(sr.jwt).at("data").at("data");
    };

    SECTION("all with 0 elements (always true)") {
        auto accounts = fetchAccounts(std::nullopt);
        REQUIRE_FALSE(accounts.empty());
    }

    SECTION("all with 1 element") {
        auto accounts = fetchAccounts(AccountField::Iban != std::optional<std::string>{});
        REQUIRE_FALSE(accounts.empty());
    }

    SECTION("all with 1 element (no match)") {
        auto accounts = fetchAccounts(AccountField::Iban == std::optional<std::string>{});
        REQUIRE(accounts.empty());
    }

    SECTION("all with 2 elements") {
        auto accounts = fetchAccounts((AccountField::Iban != std::optional<std::string>{}) &&
                                      AccountFilter::supports(SupportedService::CollectPayment));
        REQUIRE_FALSE(accounts.empty());
    }

    SECTION("all with 2 elements (no match)") {
        auto accounts = fetchAccounts((AccountField::Iban != std::optional<std::string>{}) &&
                                      (AccountField::Iban == std::optional<std::string>{}));
        REQUIRE(accounts.empty());
    }

    SECTION("all with more than 2 elements") {
        auto accounts = fetchAccounts((AccountField::Iban != std::optional<std::string>{}) &&
                                      AccountFilter::supports(SupportedService::CollectPayment) &&
                                      (AccountField::Bic != std::optional<std::string>{}));
        REQUIRE_FALSE(accounts.empty());
    }

    SECTION("all with more than 2 elements (no match)") {
        auto accounts = fetchAccounts((AccountField::Iban != std::optional<std::string>{}) &&
                                      AccountFilter::supports(SupportedService::CollectPayment) &&
                                      (AccountField::Iban == std::optional<std::string>{}));
        REQUIRE(accounts.empty());
    }

    SECTION("any with 1 element") {
        auto accounts = fetchAccounts(AccountFilter::supports(SupportedService::CollectPayment));
        REQUIRE_FALSE(accounts.empty());
    }

    SECTION("any with 1 element (no match)") {
        auto accounts = fetchAccounts(AccountField::Iban == std::optional<std::string>{});
        REQUIRE(accounts.empty());
    }

    SECTION("any with 2 elements") {
        auto accounts = fetchAccounts((AccountField::Iban == std::optional<std::string>{}) ||
                                      (AccountField::Iban != std::optional<std::string>{}));
        REQUIRE_FALSE(accounts.empty());
    }

    SECTION("any with 2 elements (no match)") {
        auto accounts = fetchAccounts((AccountField::Iban == std::optional<std::string>{}) ||
                                      (AccountField::Bic == std::optional<std::string>{}));
        REQUIRE(accounts.empty());
    }

    SECTION("any with more than 2 elements") {
        auto accounts = fetchAccounts((AccountField::Iban == std::optional<std::string>{}) ||
                                      (AccountField::Bic == std::optional<std::string>{}) ||
                                      (AccountField::Iban != std::optional<std::string>{}));
        REQUIRE_FALSE(accounts.empty());
    }

    SECTION("any with more than 2 elements (no match)") {
        auto accounts = fetchAccounts((AccountField::Iban == std::optional<std::string>{}) ||
                                      (AccountField::Bic == std::optional<std::string>{}) ||
                                      (AccountField::Iban == std::optional<std::string>{}));
        REQUIRE(accounts.empty());
    }

    SECTION("nested any inside all") {
        auto accounts = fetchAccounts((AccountField::Iban != std::optional<std::string>{}) &&
                                      ((AccountField::Iban == std::optional<std::string>{}) ||
                                       AccountFilter::supports(SupportedService::CollectPayment)));
        REQUIRE_FALSE(accounts.empty());
    }

    SECTION("nested any inside all (no match)") {
        auto accounts = fetchAccounts((AccountField::Iban != std::optional<std::string>{}) &&
                                      ((AccountField::Iban == std::optional<std::string>{}) ||
                                       (AccountField::Bic == std::optional<std::string>{})));
        REQUIRE(accounts.empty());
    }

    SECTION("nested all inside any") {
        auto accounts = fetchAccounts((AccountField::Iban == std::optional<std::string>{}) ||
                                      ((AccountField::Iban != std::optional<std::string>{}) &&
                                       AccountFilter::supports(SupportedService::CollectPayment)));
        REQUIRE_FALSE(accounts.empty());
    }

    SECTION("nested all inside any (no match)") {
        auto accounts = fetchAccounts((AccountField::Iban == std::optional<std::string>{}) ||
                                      ((AccountField::Iban != std::optional<std::string>{}) &&
                                       (AccountField::Iban == std::optional<std::string>{})));
        REQUIRE(accounts.empty());
    }
}
