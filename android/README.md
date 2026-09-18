# Android prefab package

The C++ client as an AAR carrying a [prefab] package, for Android NDK apps:

```kotlin
// app/build.gradle.kts
android {
    buildFeatures { prefab = true }
    defaultConfig {
        externalNativeBuild {
            cmake { arguments += "-DANDROID_STL=c++_shared" }
        }
    }
}

dependencies {
    implementation("tech.yaxi:routex-client-cxx:<version>")
}
```

```cmake
# app/src/main/cpp/CMakeLists.txt
find_package(routex-client REQUIRED CONFIG)
target_link_libraries(app PRIVATE routex-client::routex-client)
```

`ANDROID_STL` is not optional: AGP defaults consumers to `c++_static`, and the API
passes `std::string`, `std::vector` and `std::variant` across the library
boundary, so both sides need the same standard library.

Two modules sit over the same headers, `routex-client` (shared) and
`routex-client-static`. Link one or the other; neither needs anything added to the
link line. The static module folds the client into your own library and the linker
drops what you do not call — 6.9 MB inside your library against 7.7 MB beside it
on arm64-v8a.

ABIs are `arm64-v8a`, `armeabi-v7a` and `x86_64`, built against API 26. Consumers
building an ABI the package does not carry fail to configure, so set `abiFilters`
accordingly.

The client still brings its own HTTP stack (reqwest with rustls), whose platform
verifier needs a JNI handshake on Android that nothing here performs yet — expect
certificate verification to fail at runtime until that changes. `:consumer-test`
carries an instrumented test that demonstrates it. Nothing about the packaging
changes when it is fixed.

## Building

The Rust static libraries are cross-compiled per ABI outside Gradle and passed in
as `-Pyaxi.libDir=<dir>` (or `YAXI_LIB_DIR`), where `<dir>` holds
`<abi>/libroutex_client_cxx.a`. `:consumer-test` links against the result the way
a consuming app does, and `verify-aar.sh` checks the package layout, the metadata
AGP matches against, 16 KB page alignment and the exported API.

The version comes from the crate manifest, which names the version in development:
builds publish `<version>-SNAPSHOT`, `-Prelease` publishes the version itself.

`-Prelease` also requires `-Pyaxi.signing.keystore`, with the alias in
`-Pyaxi.signing.alias`, the store password in `YAXI_SIGNING_STOREPASS` and a
timestamp authority in `-Pyaxi.signing.tsa`. The AAR is jarsigned before anything
publishes or checksums it. The certificate is self-signed, so `jarsigner -verify`
passes while `-verify -strict` reports the chain.

[prefab]: https://google.github.io/prefab/
