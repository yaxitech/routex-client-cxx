# Unit tests

Catch2-based unit tests for the `yaxi::*` C++ API declared in
`include/yaxi/routex-client.h`.

## Prerequisites

- CMake ≥ 3.28
- Ninja
- A C++20 toolchain matching one of the presets below
- `cargo` (unless `YAXI_BUILD_RUST_LIB=OFF` and `-DYAXI_LIB=…` are provided)
- On Windows: PowerShell (`pwsh` preferred, `powershell.exe` works) for MSVC Debug builds
- OpenSSL (system install; preinstalled on every GitHub Actions runner)

## Online tests

Tests tagged `[online]` exercise a real YAXI Routex service. They are
automatically **SKIPPED** unless the following environment variables are set:

| Variable        | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| `YAXI_API_KEY`  | Base64-encoded YAXI API key secret                          |
| `YAXI_KEY_ID`   | YAXI key ID                                                 |
| `ROUTEX_URL`    | Routex service URL                                          |

To run only offline tests: `ctest --preset <preset> -L '!online'`
(or `--exclude-tag` if invoking the binary directly).

## Running

```sh
cd tests/unit
cmake --preset <preset>
cmake --build  --preset <preset>
ctest          --preset <preset>
```

Build directories live under `tests/unit/build/<preset>/`.

## Presets

| Preset          | Platform | Compiler | Configuration | Sanitizer        |
| --------------- | -------- | -------- | ------------- | ---------------- |
| `msvc-debug`    | Windows  | MSVC     | Debug         | -                |
| `msvc-release`  | Windows  | MSVC     | Release       | -                |
| `msvc-asan`     | Windows  | MSVC     | Debug         | AddressSanitizer |
| `gcc-debug`     | Linux    | GCC      | Debug         | -                |
| `gcc-release`   | Linux    | GCC      | Release       | -                |
| `gcc-asan`      | Linux    | GCC      | Debug         | ASan + UBSan     |
| `clang-debug`   | Linux    | Clang    | Debug         | -                |
| `clang-release` | Linux    | Clang    | Release       | -                |
| `clang-asan`    | Linux    | Clang    | Debug         | ASan + UBSan     |
| `clang-tsan`    | Linux    | Clang    | Debug         | ThreadSanitizer  |
| `clang-msan`    | Linux    | Clang    | Debug         | MemorySanitizer  |

MSVC presets only resolve on Windows, GCC/Clang presets only on non-Windows
(enforced via the `hostSystemName` condition in `CMakePresets.json`).

> MemorySanitizer requires a libc++ built with MSan instrumentation. The
> preset will configure and build but is likely to report false positives in
> uninstrumented system libraries unless that is in place.

> Sanitizer flags are applied **only to the test executable**. The Rust
> component is not re-instrumented; that would require building cargo with
> `-Z sanitizer=…` on a nightly toolchain.

## Configuration options

| Variable               | Default                            | Purpose                                                       |
| ---------------------- | ---------------------------------- | ------------------------------------------------------------- |
| `YAXI_SANITIZER`       | `off`                              | `off`, `address`, `undefined`, `address+undefined`, `thread`, `memory` |
| `YAXI_BUILD_RUST_LIB`  | `ON`                               | Run `cargo build` as part of the CMake build                  |
| `YAXI_LIB`             | derived from `target/…`            | Path to the Rust static lib / Windows import lib              |
| `YAXI_DLL`             | derived (Windows only)             | Path to the Rust cdylib                                       |
| `YAXI_INCLUDE_DIR`     | `<repo>/include`                   | Where to find `yaxi/routex-client.h`                          |
| `YAXI_CARGO_TARGET`    | `""` (host triple)                 | `cargo --target` triple, e.g. `i686-pc-windows-msvc`          |

When `YAXI_BUILD_RUST_LIB=OFF`, `YAXI_LIB` (plus `YAXI_DLL` on Windows) must
point at pre-built artifacts; a missing file is a configure-time fatal error.
