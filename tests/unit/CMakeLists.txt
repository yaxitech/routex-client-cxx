cmake_minimum_required(VERSION 3.28...4.2)

project(yaxi_unit_tests
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(YAXI_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "Repository root")
get_filename_component(YAXI_REPO_ROOT "${YAXI_REPO_ROOT}" ABSOLUTE)

# ── Options ──────────────────────────────────────────────────────────────────
set(YAXI_SANITIZER "off" CACHE STRING
    "Sanitizer to enable: off, address, undefined, address+undefined, thread, memory")
set_property(CACHE YAXI_SANITIZER PROPERTY STRINGS
    off address undefined address+undefined thread memory)

option(YAXI_BUILD_RUST_LIB "Build the Rust component via cargo as part of this build" ON)

set(YAXI_INCLUDE_DIR "${YAXI_REPO_ROOT}/include" CACHE PATH "YAXI public include directory")
set(YAXI_CARGO_TARGET "" CACHE STRING "cargo --target triple (empty = host)")

# ── Language / runtime ───────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# /MD in Release, /MDd in Debug
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

if(MSVC)
    add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# ── Sanitizers ───────────────────────────────────────────────────────────────
# Applied build-wide so vendored deps (Catch2) match. MSVC's STL emits
# differing annotate_string/annotate_vector/annotate_optional symbols based on
# /fsanitize=address; mixing instrumented + uninstrumented TUs triggers
# LNK2038. Scoped to C/CXX via genex so assemblers (NASM, etc.) used by
# vendored deps do not receive the flag and treat it as an input file. The
# cxx-bridge glue compiled by cargo is not instrumented - would require
# -Z sanitizer=… on a nightly toolchain.
if(NOT YAXI_SANITIZER STREQUAL "off")
    if(MSVC)
        if(YAXI_SANITIZER STREQUAL "address")
            add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/fsanitize=address>)
        else()
            message(WARNING "YAXI_SANITIZER=${YAXI_SANITIZER} is not supported by MSVC; ignoring")
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
        if(YAXI_SANITIZER STREQUAL "address")
            set(_san_flag "address")
        elseif(YAXI_SANITIZER STREQUAL "undefined")
            set(_san_flag "undefined")
        elseif(YAXI_SANITIZER STREQUAL "address+undefined")
            set(_san_flag "address,undefined")
        elseif(YAXI_SANITIZER STREQUAL "thread")
            set(_san_flag "thread")
        elseif(YAXI_SANITIZER STREQUAL "memory")
            if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
                message(FATAL_ERROR "YAXI_SANITIZER=memory requires Clang")
            endif()
            set(_san_flag "memory")
        else()
            message(FATAL_ERROR "Unknown YAXI_SANITIZER value: ${YAXI_SANITIZER}")
        endif()
        add_compile_options(
            $<$<COMPILE_LANGUAGE:C,CXX>:-fsanitize=${_san_flag}>
            $<$<COMPILE_LANGUAGE:C,CXX>:-fno-omit-frame-pointer>
            $<$<COMPILE_LANGUAGE:C,CXX>:-g>)
        add_link_options(-fsanitize=${_san_flag})
    else()
        message(WARNING "YAXI_SANITIZER ignored: unrecognized compiler ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()

# ── Dependencies ─────────────────────────────────────────────────────────────
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

find_package(OpenSSL REQUIRED)

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.15.0
    GIT_SHALLOW TRUE
    SYSTEM
)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
    SYSTEM
)

FetchContent_Declare(jwt_cpp
    GIT_REPOSITORY https://github.com/Thalhammer/jwt-cpp.git
    GIT_TAG v0.7.2
    GIT_SHALLOW TRUE
    SYSTEM
)
set(JWT_DISABLE_INSTALL ON CACHE BOOL "" FORCE)

FetchContent_Declare(nameof
    GIT_REPOSITORY https://github.com/Neargye/nameof.git
    GIT_TAG v0.10.5
    GIT_SHALLOW TRUE
    SYSTEM
)
set(NAMEOF_OPT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NAMEOF_OPT_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(NAMEOF_OPT_INSTALL        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG curl-8_20_0
    GIT_SHALLOW TRUE
    SYSTEM
)
set(BUILD_CURL_EXE     OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS  OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS  ON  CACHE BOOL "" FORCE)
set(BUILD_TESTING      OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP  ON  CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAPS ON  CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL    OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2   OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_SRP   ON  CACHE BOOL "" FORCE)
set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)
if(WIN32)
    set(CURL_USE_SCHANNEL ON  CACHE BOOL "" FORCE)
else()
    set(CURL_USE_OPENSSL  ON  CACHE BOOL "" FORCE)
endif()

FetchContent_MakeAvailable(Catch2 nlohmann_json jwt_cpp nameof curl)
list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")

enable_testing()

# ── Rust artifact path derivation ────────────────────────────────────────────
if(YAXI_CARGO_TARGET)
    set(_target_subdir "/${YAXI_CARGO_TARGET}")
else()
    set(_target_subdir "")
endif()

if(WIN32)
    set(_lib_name      "routex_client_cxx.dll.lib")
    set(_dll_name      "routex_client_cxx.dll")
else()
    set(_lib_name      "libroutex_client_cxx.a")
endif()

set(_lib_debug   "${YAXI_REPO_ROOT}/target${_target_subdir}/debug/${_lib_name}")
set(_lib_release "${YAXI_REPO_ROOT}/target${_target_subdir}/release/${_lib_name}")
if(WIN32)
    set(_dll_debug   "${YAXI_REPO_ROOT}/target${_target_subdir}/debug/${_dll_name}")
    set(_dll_release "${YAXI_REPO_ROOT}/target${_target_subdir}/release/${_dll_name}")
endif()

# Single-config fallback (e.g. plain "Ninja"): pick the path matching CMAKE_BUILD_TYPE.
string(TOUPPER "${CMAKE_BUILD_TYPE}" _bt_upper)
if(_bt_upper MATCHES "RELEASE|RELWITHDEBINFO|MINSIZEREL")
    set(_lib_default "${_lib_release}")
    if(WIN32)
        set(_dll_default "${_dll_release}")
    endif()
else()
    set(_lib_default "${_lib_debug}")
    if(WIN32)
        set(_dll_default "${_dll_debug}")
    endif()
endif()

set(YAXI_LIB "${_lib_default}" CACHE FILEPATH "Path to the Rust lib (.a / .dll.lib)")
if(WIN32)
    set(YAXI_DLL "${_dll_default}" CACHE FILEPATH "Path to the Rust DLL (Windows only)")
endif()

# ── Cargo auto-build ─────────────────────────────────────────────────────────
if(YAXI_BUILD_RUST_LIB)
    find_program(CARGO_EXECUTABLE cargo REQUIRED)

    # Per-config BYPRODUCTS so Ninja Multi-Config picks the right path.
    set(_rust_byproducts
        $<$<CONFIG:Debug>:${_lib_debug}>
        $<$<NOT:$<CONFIG:Debug>>:${_lib_release}>)

    add_custom_target(yaxi_rustlib_build ALL
        COMMAND ${CMAKE_COMMAND}
            -DCARGO_EXE=${CARGO_EXECUTABLE}
            -DMANIFEST_PATH=${YAXI_REPO_ROOT}/Cargo.toml
            -DWORKING_DIR=${YAXI_REPO_ROOT}
            $<$<NOT:$<CONFIG:Debug>>:-DRELEASE=1>
            $<$<BOOL:${YAXI_CARGO_TARGET}>:-DCARGO_TARGET=${YAXI_CARGO_TARGET}>
            $<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Debug>>:-DDEBUG_FLAGS_PS1=${YAXI_REPO_ROOT}/scripts/debug-flags.ps1>
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_rust.cmake"
        BYPRODUCTS ${_rust_byproducts}
        WORKING_DIRECTORY "${YAXI_REPO_ROOT}"
        VERBATIM
        COMMAND_EXPAND_LISTS
        COMMENT "cargo build$<$<NOT:$<CONFIG:Debug>>: --release>"
    )
    set_source_files_properties(${_rust_byproducts} PROPERTIES GENERATED TRUE)
endif()

# ── Existence checks ─────────────────────────────────────────────────────────
# When auto-building, artifacts don't exist at configure time - skip the check.
if(NOT YAXI_BUILD_RUST_LIB)
    if(NOT EXISTS "${YAXI_LIB}")
        message(FATAL_ERROR
            "Rust library not found: ${YAXI_LIB}\n"
            "Either run cargo build or pass -DYAXI_LIB=<path>.")
    endif()
    if(WIN32 AND NOT EXISTS "${YAXI_DLL}")
        message(FATAL_ERROR
            "Rust DLL not found: ${YAXI_DLL}\n"
            "Either run cargo build or pass -DYAXI_DLL=<path>.")
    endif()
endif()

if(NOT EXISTS "${YAXI_INCLUDE_DIR}/yaxi/routex-client.h")
    message(FATAL_ERROR "routex-client.h not found under: ${YAXI_INCLUDE_DIR}")
endif()

# ── Imported target ──────────────────────────────────────────────────────────
if(WIN32)
    add_library(YAXI::Client SHARED IMPORTED GLOBAL)
    set_target_properties(YAXI::Client PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE;RELWITHDEBINFO;MINSIZEREL"
        IMPORTED_LOCATION_DEBUG          "${_dll_debug}"
        IMPORTED_LOCATION_RELEASE        "${_dll_release}"
        IMPORTED_LOCATION_RELWITHDEBINFO "${_dll_release}"
        IMPORTED_LOCATION_MINSIZEREL     "${_dll_release}"
        IMPORTED_IMPLIB_DEBUG            "${_lib_debug}"
        IMPORTED_IMPLIB_RELEASE          "${_lib_release}"
        IMPORTED_IMPLIB_RELWITHDEBINFO   "${_lib_release}"
        IMPORTED_IMPLIB_MINSIZEREL       "${_lib_release}"
    )
else()
    add_library(YAXI::Client STATIC IMPORTED GLOBAL)
    set_target_properties(YAXI::Client PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE;RELWITHDEBINFO;MINSIZEREL"
        IMPORTED_LOCATION_DEBUG          "${_lib_debug}"
        IMPORTED_LOCATION_RELEASE        "${_lib_release}"
        IMPORTED_LOCATION_RELWITHDEBINFO "${_lib_release}"
        IMPORTED_LOCATION_MINSIZEREL     "${_lib_release}"
    )
endif()

set_target_properties(YAXI::Client PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${YAXI_INCLUDE_DIR}")

# System deps pulled in by the Rust crate (reqwest/rustls/tokio).
target_link_libraries(YAXI::Client INTERFACE
    "$<$<PLATFORM_ID:Windows>:ws2_32;userenv;bcrypt;advapi32;ntdll>"
    "$<$<NOT:$<PLATFORM_ID:Windows>>:pthread;dl;m>"
    "$<$<PLATFORM_ID:Darwin>:-framework Security;-framework CoreFoundation;-framework CoreServices>"
)

# ── Test executable ──────────────────────────────────────────────────────────
add_executable(yaxi_unit_tests
    test_main.cpp
    test_account_filter.cpp
    test_client_online.cpp
    helpers.cpp
    ticket.cpp
)

if(TARGET yaxi_rustlib_build)
    add_dependencies(yaxi_unit_tests yaxi_rustlib_build)
endif()

target_link_libraries(yaxi_unit_tests PRIVATE
    YAXI::Client
    Catch2::Catch2WithMain
    nlohmann_json::nlohmann_json
    jwt-cpp::jwt-cpp
    nameof::nameof
    CURL::libcurl
    OpenSSL::Crypto
)

# Activate Catch2's StringMaker for std::variant so failed REQUIRE_THAT
# expansions show the held alternative (Error sub-types come out as their
# .what() message via Catch2's std::exception fallback).
target_compile_definitions(yaxi_unit_tests PRIVATE CATCH_CONFIG_ENABLE_VARIANT_STRINGMAKER)

if(MSVC)
    target_compile_options(yaxi_unit_tests PRIVATE
        /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
else()
    target_compile_options(yaxi_unit_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

# ── Windows runtime DLL copy ─────────────────────────────────────────────────
# Pulls in every SHARED IMPORTED transitive dep, current and future.
if(WIN32)
    add_custom_command(TARGET yaxi_unit_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:yaxi_unit_tests>
            $<TARGET_FILE_DIR:yaxi_unit_tests>
        COMMAND_EXPAND_LISTS
        COMMENT "Copying runtime DLLs next to yaxi_unit_tests"
    )
endif()

# ── Test discovery ───────────────────────────────────────────────────────────
include(Catch)
catch_discover_tests(yaxi_unit_tests
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:yaxi_unit_tests>"
    PROPERTIES TIMEOUT 30
)

# ── Summary ──────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "┌─ yaxi_unit_tests ────────────────────────")
message(STATUS "│ Repo root   : ${YAXI_REPO_ROOT}")
message(STATUS "│ Include dir : ${YAXI_INCLUDE_DIR}")
message(STATUS "│ YAXI lib    : ${YAXI_LIB}")
if(WIN32)
    message(STATUS "│ YAXI DLL    : ${YAXI_DLL}")
endif()
if(YAXI_CARGO_TARGET)
    message(STATUS "│ Cargo target: ${YAXI_CARGO_TARGET}")
endif()
message(STATUS "│ Auto-cargo  : ${YAXI_BUILD_RUST_LIB}")
message(STATUS "│ Sanitizer   : ${YAXI_SANITIZER}")
message(STATUS "│ Compiler    : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "└──────────────────────────────────────────")
message(STATUS "")
