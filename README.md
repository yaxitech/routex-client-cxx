# routex-client-cxx

C++ client library for [YAXI's](https://yaxi.tech) Open Banking services.

Currently only a Windows distribution is available, shipped as a NuGet package with `x64`, `x86`, and `arm64` runtimes.
Service methods are synchronous and return a `yaxi::Result<T>`, a `std::variant<T, yaxi::Error>` where the error alternative is itself a `std::variant` of typed error structs (see [`include/yaxi/routex-client.h`](include/yaxi/routex-client.h)).

See the [documentation](https://docs.yaxi.tech) for the full API reference.

## Installation

Requires Windows, MSVC, and C++17 or newer. Available on NuGet as [`routex-client-cxx`](https://www.nuget.org/packages/routex-client-cxx):

```xml
<!-- packages.config -->
<packages>
  <package id="routex-client-cxx" version="0.0.1" targetFramework="native" />
</packages>
```

Visual Studio's NuGet Package Manager wires the matching `<Import>` entries into your `.vcxproj` automatically.

## Usage

```cpp
#include <yaxi/routex-client.h>

#include <utility>
#include <variant>

using namespace yaxi;

// std::visit helper
template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

// Unwrap Result<T> to T, throwing the typed error otherwise
template <typename T> T unwrap(Result<T> r) {
    if (auto* e = std::get_if<Error>(&r))
        std::visit([](auto const& err) { throw err; }, *e);
    return std::get<T>(std::move(r));
}

// Pass "https://integration.yaxi.tech" for the integration environment
RoutexClient client;

// ticket: a YAXI service ticket issued by your backend (see docs)

// Search for a bank
auto connections = unwrap(client.search(
    ticket,
    { TermSearchFilter{ "sparkasse" } },
    /*ibanDetection=*/true,
    /*limit=*/20
));

// Fetch accounts
Credentials credentials{ connectionId, /*userId=*/std::string{ "user" } };
auto response = unwrap(client.accounts(
    credentials,
    accountsTicket,
    { AccountField::Iban, AccountField::Currency, AccountField::OwnerName }
));

// Handle interrupts (dialogs, redirects)
response = std::visit(overloaded{
    [&](Dialog& d) -> ServiceResponse {
        return std::visit(overloaded{
            [&](Confirmation& c) {
                // Decoupled SCA or polling: confirm to proceed
                return unwrap(client.confirmAccounts(accountsTicket, c.context));
            },
            [&](Field& f) {
                // Text input required (e.g. TAN entry)
                return unwrap(client.respondAccounts(accountsTicket, f.context, userInput));
            },
            [&](Selection& s) {
                // Pick one option (e.g. TAN method)
                return unwrap(client.respondAccounts(accountsTicket, s.context, selectedKey));
            },
        }, d.input);
    },
    [&](Redirect& r) -> ServiceResponse {
        // Send the user to r.url (browser or WebView), then confirm
        return unwrap(client.confirmAccounts(accountsTicket, r.context));
    },
    [&](RedirectHandle& rh) -> ServiceResponse {
        // Register a redirect URI to obtain the URL to send the user to
        auto url = unwrap(client.registerRedirectUri(
            accountsTicket, rh.handle, "myapp://callback"));
        // Send user to url, then confirm
        return unwrap(client.confirmAccounts(accountsTicket, rh.context));
    },
    [&](ServiceResult& r) -> ServiceResponse { return std::move(r); },
}, response);

// Extract the result
if (auto* result = std::get_if<ServiceResult>(&response)) {
    // result->jwt: authenticated data as a signed JSON Web Token.
    //   Verify the signature in a trusted environment before acting on the data.
    // result->session: short-lived, pass to consecutive service calls to speed
    //   up authentication.
    // result->connectionData: persist alongside credentials to reuse the consent
    //   on subsequent calls (via Credentials.connectionData). Pass
    //   recurringConsents = true on the service call to request a long-lived
    //   consent that skips the interrupt loop until it expires.
}
```

Subsequent interrupts are resolved by repeating the same `std::visit` block until a `ServiceResult` is returned.
The typed errors carried by `yaxi::Error` are documented at [docs.yaxi.tech/errors.html](https://docs.yaxi.tech/errors.html); the full per-service reference lives in the [documentation](https://docs.yaxi.tech).

## License

Apache-2.0
