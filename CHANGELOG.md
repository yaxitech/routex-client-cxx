# Changelog

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
