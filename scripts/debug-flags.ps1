<#
.SYNOPSIS
    Provides Invoke-WithDebugFlags for running commands with debug CRT link flags.
#>

function Invoke-WithDebugFlags {
    <#
    .SYNOPSIS
        Runs a script block with RUSTFLAGS, CFLAGS, and CXXFLAGS configured for debug CRT linkage.
    .PARAMETER ScriptBlock
        Script block to execute. Wrap the invocation in braces so PowerShell's
        parameter binder does not consume tokens such as `--` (end of options).
    .EXAMPLE
        Invoke-WithDebugFlags { cargo build --locked --target $env:TARGET }
    .EXAMPLE
        Invoke-WithDebugFlags { cargo clippy --all-targets --locked -- -W clippy::pedantic -D warnings }
    #>
    [Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSUseSingularNouns', '')]
    param(
        [Parameter(Mandatory, Position = 0)]
        [scriptblock]$ScriptBlock
    )

    $addRustFlags = '-C target-feature=-crt-static -C link-arg=/nodefaultlib:msvcrt -C link-arg=/defaultlib:msvcrtd'
    # The `__builtin_bswap16` is added because aws-lc determines whether the
    # compiler supports some intrinsics by compiling test programs (see
    # `tests/compiler_features_tests/builtin_swap_check.c`). With `/MDd`, this
    # compilation results in a false positive. The added define makes sure the
    # compilation fails instead
    $addCFlags = '/MDd /D__builtin_bswap16=#error"'
    $addCxxFlags = '/MDd /D__builtin_bswap16=#error"'

    $savedRustFlags = $env:RUSTFLAGS
    $savedCFlags = $env:CFLAGS
    $savedCxxFlags = $env:CXXFLAGS

    try {
        $env:RUSTFLAGS = if ($savedRustFlags) { "$savedRustFlags $addRustFlags" } else { $addRustFlags }
        $env:CFLAGS = if ($savedCFlags) { "$savedCFlags $addCFlags" } else { $addCFlags }
        $env:CXXFLAGS = if ($savedCxxFlags) { "$savedCxxFlags $addCxxFlags" } else { $addCxxFlags }

        & $ScriptBlock
    }
    finally {
        $env:RUSTFLAGS = $savedRustFlags
        $env:CFLAGS = $savedCFlags
        $env:CXXFLAGS = $savedCxxFlags
    }
}
