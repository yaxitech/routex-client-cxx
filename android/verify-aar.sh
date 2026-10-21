#!/usr/bin/env bash
# Checks a built AAR against what prefab consumers rely on: the package layout,
# the metadata AGP matches against, and the properties of the shared library
# that would otherwise only surface at runtime.
#
#   ./verify-aar.sh routex-client/build/outputs/aar/routex-client-release.aar
set -euo pipefail

aar=${1:?usage: verify-aar.sh <path to .aar>}
abis=(arm64-v8a armeabi-v7a x86_64)
api=26
min_ndk=28

readelf=readelf
nm=nm
if [ -n "${ANDROID_NDK_ROOT:-}" ]; then
    toolchain="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin"
    if [ -x "$toolchain/llvm-readelf" ]; then
        readelf="$toolchain/llvm-readelf"
        nm="$toolchain/llvm-nm"
    fi
fi

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
unzip -q "$aar" -d "$dir"

fail=0

# check <description> <command...>
check() {
    local description=$1
    shift
    if "$@" >/dev/null 2>&1; then
        printf '  ok    %s\n' "$description"
    else
        printf '  FAIL  %s\n' "$description"
        fail=1
    fi
}

json_field() {
    tr -d ' \n' < "$1" | grep -qF "$2"
}

ndk_at_least() {
    local got
    got=$(tr -d ' \n' < "$1" | sed -n 's/.*"ndk":\([0-9]*\).*/\1/p')
    [ -n "$got" ] && [ "$got" -ge "$2" ]
}

not_duplicated() {
    ! find "$dir/jni" -name 'libroutex-client.so' 2>/dev/null | grep -q .
}

aligned_16k() {
    # Google Play requires 16 KB-aligned segments for API 35+. NDK r28 defaults
    # to it on the 64-bit ABIs, so this catches a toolchain change silently
    # dropping it.
    ! "$readelf" -lW "$1" | awk '$1 == "LOAD" && $NF != "0x4000" { found = 1 } END { exit !found }'
}

echo "prefab package"
pkg="$dir/prefab"
check "prefab/prefab.json exists" test -f "$pkg/prefab.json"
if [ -f "$pkg/prefab.json" ]; then
    check "package is named routex-client" json_field "$pkg/prefab.json" '"name":"routex-client"'
    check "schema_version is 2" json_field "$pkg/prefab.json" '"schema_version":2'
fi
# The jni/ copy is excluded so the library ships once; libc++_shared.so stays.
check "shared library is not duplicated in jni/" not_duplicated

for module in routex-client routex-client-static; do
    echo "module $module"
    root="$pkg/modules/$module"
    check "ships yaxi/routex-client.h" test -f "$root/include/yaxi/routex-client.h"

    if [ "$module" = routex-client ]; then
        lib=libroutex-client.so
        static=false
    else
        lib=libroutex-client-static.a
        static=true
    fi

    for abi in "${abis[@]}"; do
        libs="$root/libs/android.$abi"
        if [ ! -f "$libs/$lib" ]; then
            check "$abi: $lib" false
            continue
        fi
        check "$abi: $lib ($(du -h "$libs/$lib" | cut -f1))" true

        check "$abi: abi.json names $abi" json_field "$libs/abi.json" "\"abi\":\"$abi\""
        check "$abi: abi.json api $api" json_field "$libs/abi.json" "\"api\":$api"
        check "$abi: abi.json stl c++_shared" json_field "$libs/abi.json" '"stl":"c++_shared"'
        check "$abi: abi.json ndk >= $min_ndk" ndk_at_least "$libs/abi.json" "$min_ndk"
        check "$abi: abi.json static $static" json_field "$libs/abi.json" "\"static\":$static"

        if [ "$static" = true ]; then
            continue
        fi

        # 16 KB pages are a 64-bit Android feature; 32-bit ABIs stay at 4 KB.
        case $abi in
            arm64-v8a | x86_64)
                check "$abi: LOAD segments 16 KB aligned" aligned_16k "$libs/$lib"
                ;;
        esac
        check "$abi: links libc++_shared.so" \
            grep -q 'libc++_shared' <("$readelf" -dW "$libs/$lib")
        check "$abi: exports the yaxi::RoutexClient API" \
            grep -q 'yaxi::RoutexClient' <("$nm" -DC "$libs/$lib")
    done
done

exit "$fail"
