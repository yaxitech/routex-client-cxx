#include "helpers.hpp"
#include "ticket.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <yaxi/routex-client.h>

#include <stdexcept>
#include <string>
#include <variant>

using namespace yaxi;
using namespace yaxi::test;
using Catch::Matchers::ContainsSubstring;

// Offline tests: invalid input is rejected on the client side before any
// request reaches the transport, either as an error result or as
// `std::invalid_argument`, never as an exception from the Rust side.

namespace {
constexpr auto BASE_URL = "https://routex.invalid/";
constexpr auto CONNECTION_ID = "connection-96386142-60e5-4ca9-abcf-944efce5bc1e";
constexpr auto INVALID_CONNECTION_ID = "connection-not-an-id";

HttpResponse failingTransport(const HttpRequest &) {
    FAIL_CHECK("transport must not be called");
    return {};
}

// Tickets are only syntax-checked on the client side, so any key will do.
TicketGenerator offlineGenerator() { return {"offline-key-id", "b2ZmbGluZS1zZWNyZXQ="}; }

std::string accountsTicket() { return offlineGenerator().accounts(uuidV4()); }

std::string transferTicket() { return offlineGenerator().issue(uuidV4(), "Transfer"); }

TransferDetails transferDetails(std::string amount) {
    TransferDetails details{};
    details.amount = std::move(amount);
    details.currency = "EUR";
    details.creditorIban = "DE02120300000000202051";
    details.creditorName = "Jane Doe";
    return details;
}

template <typename T>
void requireUnexpectedValue(const Result<T> &result, const std::string &invalidValue) {
    REQUIRE_THAT(result, HoldsAlternative<Error>());
    auto &error = std::get<Error>(result);
    REQUIRE_THAT(error, HoldsAlternative<UnexpectedValue>());
    REQUIRE_THAT(std::get<UnexpectedValue>(error).error, ContainsSubstring(invalidValue));
}
} // namespace

TEST_CASE("Invalid input: info connection ID", "[invalid_input]") {
    RoutexClient client(failingTransport, BASE_URL);

    requireUnexpectedValue(client.info(accountsTicket(), INVALID_CONNECTION_ID),
                           INVALID_CONNECTION_ID);
}

TEST_CASE("Invalid input: accounts connection ID", "[invalid_input]") {
    RoutexClient client(failingTransport, BASE_URL);

    Credentials credentials{};
    credentials.connectionId = INVALID_CONNECTION_ID;

    requireUnexpectedValue(client.accounts(credentials, accountsTicket(), {AccountField::Iban}),
                           INVALID_CONNECTION_ID);
}

TEST_CASE("Invalid input: transfer amount", "[invalid_input]") {
    RoutexClient client(failingTransport, BASE_URL);

    Credentials credentials{};
    credentials.connectionId = CONNECTION_ID;

    requireUnexpectedValue(client.transfer(credentials, transferTicket(),
                                           PaymentProduct::SepaCreditTransfer,
                                           {transferDetails("1.00"), transferDetails("abc")}),
                           "`abc`");
}

TEST_CASE("Invalid input: systemVersion ticket ID", "[invalid_input]") {
    RoutexClient client(failingTransport, BASE_URL);

    REQUIRE_FALSE(client.systemVersion("not-a-ticket-id").has_value());
}

TEST_CASE("Invalid input: constructor URL", "[invalid_input]") {
    REQUIRE_THROWS_AS(RoutexClient(failingTransport, "not a url"), std::invalid_argument);
    REQUIRE_THROWS_WITH(RoutexClient(failingTransport, "not a url"),
                        ContainsSubstring("not a url"));
}

TEST_CASE("Invalid input: redirect URI", "[invalid_input]") {
    RoutexClient client(failingTransport, BASE_URL);

    REQUIRE_THROWS_AS(client.setRedirectUri("https://example.com/\n"), std::invalid_argument);
    REQUIRE_THROWS_WITH(client.setRedirectUri("https://example.com/\n"),
                        ContainsSubstring("Invalid redirect URI"));
    REQUIRE_NOTHROW(client.setRedirectUri("https://example.com/"));
}
