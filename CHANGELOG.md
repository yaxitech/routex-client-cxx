# Changelog

## [0.3.0] - unreleased

### Added

- `HttpTransport`, the callback through which `RoutexClient` sends its HTTP requests, with `HttpRequest`, `HttpResponse` and `HttpHeader`.
- An Android distribution: an AAR carrying a prefab package with the modules `routex-client` (shared) and `routex-client-static`, for `arm64-v8a`, `armeabi-v7a` and `x86_64`.

### Changed

- **Breaking:** The `RoutexClient` constructor takes an `HttpTransport` as its first argument. The application supplies the HTTP stack.

### Removed

- The bundled HTTP stack (reqwest, hyper, rustls, tokio). The library no longer does any networking or TLS itself.

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
