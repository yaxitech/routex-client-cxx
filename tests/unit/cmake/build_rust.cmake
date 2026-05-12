# Driver invoked via `cmake -P` to run cargo build with the right environment.
#
# Required inputs:
#   CARGO_EXE        Absolute path to cargo
#   MANIFEST_PATH    Path to Cargo.toml
#   WORKING_DIR      Directory to run cargo in (usually the repo root)
#
# Optional inputs:
#   RELEASE          If defined (1), pass --release to cargo
#   CARGO_TARGET     Triple to pass via --target (cross-compile)
#   DEBUG_FLAGS_PS1  Absolute path to scripts/debug-flags.ps1. When set, cargo
#                    is invoked through Invoke-WithDebugFlags so the Rust
#                    component links against the debug CRT (/MDd). Only set
#                    this on MSVC + Debug.

set(_args build)
if(DEFINED RELEASE)
    list(APPEND _args --release)
endif()
if(CARGO_TARGET)
    list(APPEND _args --target ${CARGO_TARGET})
endif()
list(APPEND _args --manifest-path "${MANIFEST_PATH}")

if(DEBUG_FLAGS_PS1)
    find_program(PWSH_EXE NAMES pwsh powershell REQUIRED)

    # Quote each cargo arg for PowerShell. The script-block is evaluated by
    # pwsh, so we build it as a single string here.
    set(_quoted_args "")
    foreach(_a IN LISTS _args)
        string(APPEND _quoted_args " '${_a}'")
    endforeach()

    set(_inner
        ". '${DEBUG_FLAGS_PS1}'; Invoke-WithDebugFlags { & '${CARGO_EXE}'${_quoted_args} }")

    execute_process(
        COMMAND ${PWSH_EXE} -NoProfile -NonInteractive -Command "${_inner}"
        WORKING_DIRECTORY "${WORKING_DIR}"
        RESULT_VARIABLE _result
    )
else()
    execute_process(
        COMMAND ${CARGO_EXE} ${_args}
        WORKING_DIRECTORY "${WORKING_DIR}"
        RESULT_VARIABLE _result
    )
endif()

if(_result)
    message(FATAL_ERROR "cargo build failed (exit code: ${_result})")
endif()
