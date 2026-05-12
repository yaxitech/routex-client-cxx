fn main() {
    let mut build = cxx_build::bridge("src/lib.rs");
    build
        .include("include")
        .file("src/lib.cc")
        .std("c++17")
        .flag_if_supported("-pedantic-errors");
    #[cfg(windows)]
    if target_is_windows() {
        configure_windows(&mut build);
    }
    build.compile("routex-client");

    println!("cargo:rerun-if-changed=src/lib.cc");
    println!("cargo:rerun-if-changed=include/yaxi/routex-client.h");
}

#[cfg(windows)]
fn configure_windows(build: &mut cc::Build) {
    const BUILD_WRAPPER_MSG: &str = "Make sure you build via `Invoke-WithDebugFlags`.";

    let flags = std::env::var("CARGO_ENCODED_RUSTFLAGS").unwrap_or_default();
    let flags: Vec<_> = flags.split('\x1f').collect();

    assert!(
        flags
            .windows(2)
            .any(|w| w == ["-C", "target-feature=-crt-static"])
    );

    if is_debug_profile() {
        // Should be set by the wrapper script
        for arg in ["/nodefaultlib:msvcrt", "/defaultlib:msvcrtd"] {
            assert!(
                flags
                    .windows(2)
                    .any(|w| w == ["-C", &format!("link-arg={arg}")]),
                "needs to be built with linker arg `{arg}`\n{BUILD_WRAPPER_MSG}"
            );
        }

        for var_name in ["CFLAGS", "CXXFLAGS"] {
            assert!(
                std::env::var(var_name).unwrap_or_default().contains("/MDd"),
                "`{var_name}` should contain `/MDd` for debug builds.\n{BUILD_WRAPPER_MSG}"
            );
        }
    }

    build.static_crt(false).define("YAXI_BUILDING_DLL", None);

    println!("cargo:rerun-if-env-changed=GITHUB_SHA");
    let mut res = winresource::WindowsResource::new();
    if let Ok(commit) = std::env::var("GITHUB_SHA") {
        res.set("Comments", &format!("Commit: {commit}"));
    }
    res.compile().unwrap();
}

#[cfg(windows)]
fn is_debug_profile() -> bool {
    std::env::var("PROFILE").expect("Should be set by cargo") == "debug"
}

#[cfg(windows)]
fn target_is_windows() -> bool {
    std::env::var("CARGO_CFG_TARGET_OS").expect("Should be set by cargo") == "windows"
}
