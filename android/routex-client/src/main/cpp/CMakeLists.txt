cmake_minimum_required(VERSION 3.22)

project(routex_client_prefab LANGUAGES CXX)

if(NOT YAXI_LIB_DIR)
    message(FATAL_ERROR
        "YAXI_LIB_DIR is unset. Cross-compile the Rust static libraries first "
        "(see android/README.md) and pass the directory holding one "
        "subdirectory per ABI:\n"
        "  ./gradlew assembleRelease -Pyaxi.libDir=<that directory>")
endif()

set(YAXI_RUST_LIB "${YAXI_LIB_DIR}/${ANDROID_ABI}/libroutex_client_cxx.a")
if(NOT EXISTS "${YAXI_RUST_LIB}")
    message(FATAL_ERROR "No Rust static library for ${ANDROID_ABI}: ${YAXI_RUST_LIB}")
endif()

# The whole public API is already inside the Rust static library - cxx compiles
# src/lib.cc into it - so neither target contributes code of its own. CMake just
# needs a source file to create a target from.
set(_placeholder "${CMAKE_CURRENT_SOURCE_DIR}/placeholder.cpp")

# Shared module. Nothing in this target references the archive, so the members
# have to be pulled in wholesale.
add_library(routex-client SHARED "${_placeholder}")
target_link_libraries(routex-client PRIVATE
    -Wl,--whole-archive "${YAXI_RUST_LIB}" -Wl,--no-whole-archive
    log)
target_link_options(routex-client PRIVATE -Wl,--gc-sections)

# Static module. The cargo archive is already exactly what a consumer needs to
# link against, so the target's own (empty) archive is replaced by it.
add_library(routex-client-static STATIC "${_placeholder}")
add_custom_command(TARGET routex-client-static POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${YAXI_RUST_LIB}" "$<TARGET_FILE:routex-client-static>"
    COMMENT "Publishing the Rust archive as libroutex-client-static.a"
    VERBATIM)
