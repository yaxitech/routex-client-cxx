# Changelog

## [0.3.0] - 2026-10-21

### Added

- `HttpTransport`, the callback through which `RoutexClient` sends its HTTP requests, with `HttpRequest`, `HttpResponse` and `HttpHeader`.

### Changed

- **Breaking:** The `RoutexClient` constructor takes an `HttpTransport` as its first argument. The application supplies the HTTP stack.
- **Breaking:** `AccountReference` is a variant of `IbanAccountReference` and `NumberAccountReference`, so `balances` supports accounts without an IBAN.
- **Breaking:** `collectPayment` and `transfer` take a `DebtorAccountReference`, a variant of `IbanAccountReference`, instead of an `AccountReference`.

### Removed

- The bundled HTTP stack (reqwest, hyper, rustls, tokio). The library no longer does any networking or TLS itself.

### Fixed

- An invalid connection ID is reported as an `UnexpectedValue` error (`info` and all services taking `Credentials`).
- An invalid `TransferDetails.amount` or `requestedExecutionDate` is reported as an `UnexpectedValue` error by `transfer`.
- `systemVersion` returns `std::nullopt` for an invalid ticket ID instead of throwing.
- The `RoutexClient` constructor and `setRedirectUri` throw `std::invalid_argument` for an invalid URL or redirect URI.

## [0.2.0] - 2026-07-31

### Added

- `ConnectionInfo.bankCodes`, the national bank codes. Request it by passing `Details::BankCodes` to `search`.
- `ConnectionInfo.labels`, the labels categorizing a connection (e.g. `beta`, `fints`).
- `InterruptError`, indicating that a request without credentials cannot complete without user interaction.

### Changed

- **Breaking (ABI):** `accounts`, `balances` and `transactions` take a `std::variant<Credentials, ConnectionData>` instead of `Credentials`, so a service can be run with the `connectionData` of an earlier `ServiceResult` instead of credentials. `recurringConsents` has no effect then, as no consent gets established.

### Removed

- **Breaking:** `ConsentExpired`. Expired consents now surface as `Unauthorized`.

## [0.1.0] - 2026-05-27

Initial release.
