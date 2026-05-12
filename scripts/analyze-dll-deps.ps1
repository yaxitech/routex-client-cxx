#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Analyzes the direct dependencies of a DLL and verifies they match the
    expected build configuration (Debug or Release).

.PARAMETER DllPath
    Path to the DLL file to inspect.

.PARAMETER Configuration
    Expected build configuration: "Debug" or "Release".
    The script exits with code 1 if the imported runtime DLLs do not match.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$DllPath,

    [Parameter(Mandatory)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# Locate dumpbin.exe
# ---------------------------------------------------------------------------
function Find-Dumpbin {
    # Check PATH first - works when running from a Developer PowerShell prompt.
    $inPath = Get-Command dumpbin -ErrorAction SilentlyContinue
    if ($inPath) { return $inPath.Source }

    # Fall back to vswhere so the script also works from a plain PowerShell
    # window, including Visual Studio Build Tools installations.
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $null }

    # -products * includes Build Tools; -find searches inside the installation.
    $candidates = & $vswhere -products * -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe'
    if ($candidates) {
        # Multiple versions may be returned; take the last (highest version).
        return ($candidates | Select-Object -Last 1)
    }

    return $null
}

$dumpbin = Find-Dumpbin
if (-not $dumpbin) {
    Write-Error ("dumpbin.exe not found. Run this script from a Developer PowerShell prompt, " +
        "or ensure Visual Studio / Build Tools are installed.")
    exit 1
}

# ---------------------------------------------------------------------------
# Validate the DLL path
# ---------------------------------------------------------------------------
$DllPath = (Resolve-Path $DllPath).Path
$dllName = Split-Path $DllPath -Leaf

# ---------------------------------------------------------------------------
# Run dumpbin and parse the dependency list
# ---------------------------------------------------------------------------

# dumpbin output contains a section like:
#
#   Image has the following dependencies:
#
#     KERNEL32.dll
#     VCRUNTIME140.dll
#
#   Summary
#
$output = & $dumpbin /dependents $DllPath

$inDepsSection = $false
$imports = [System.Collections.Generic.List[string]]::new()

foreach ($line in $output) {
    if ($line -match 'has the following dependencies') {
        $inDepsSection = $true
        continue
    }
    if ($inDepsSection -and $line -match '^\s*Summary') {
        break
    }
    if ($inDepsSection) {
        $trimmed = $line.Trim()
        if ($trimmed -ne '') {
            $imports.Add($trimmed.ToLower())
        }
    }
}

# ---------------------------------------------------------------------------
# Run dumpbin /exports and parse the symbol list
# ---------------------------------------------------------------------------

# dumpbin /exports output contains a section like:
#
#     ordinal hint RVA      name
#
#           1    0 00001000 some_symbol
#           2    1 00001020 another_symbol
#
#   Summary
#
# When the DLL has no exports at all, dumpbin prints:
#   "File contains no exports."
$exportOutput = & $dumpbin /exports $DllPath

$exports = [System.Collections.Generic.List[string]]::new()
$inExportsSection = $false

foreach ($line in $exportOutput) {
    if ($line -match 'ordinal hint RVA\s+name') {
        $inExportsSection = $true
        continue
    }
    if ($inExportsSection) {
        if ($line -match '^\s*Summary') { break }
        if ($line -match '^\s*$') { continue }
        # Each export line: ordinal  hint  RVA  name
        # RVA may be absent for forwarded symbols, so match 0 or more hex chars.
        if ($line -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]*\s+(\S+)') {
            $exports.Add($Matches[1])
        }
        else {
            throw "Unexpected line in dumpbin exports output: '$line'"
        }
    }
}

# ---------------------------------------------------------------------------
# Print dependencies
# ---------------------------------------------------------------------------
Write-Output ""
Write-Output "=== Dependencies of $dllName ==="
if ($imports.Count -eq 0) {
    Write-Output "  (none - statically linked or no import table)"
}
else {
    foreach ($dep in $imports) {
        Write-Output "  $dep"
    }
}

# ---------------------------------------------------------------------------
# Print exports
# ---------------------------------------------------------------------------
Write-Output ""
Write-Output "=== Exports of $dllName ==="
if ($exports.Count -eq 0) {
    Write-Output "  (none - DLL exports no symbols)"
}
else {
    Write-Output "  $($exports.Count) symbol(s):"
    foreach ($sym in $exports) {
        Write-Output "  $sym"
    }
}

# ---------------------------------------------------------------------------
# Configuration check
# ---------------------------------------------------------------------------

# Regex patterns for MSVC runtime DLLs.
# Versioned family: vcruntime, msvcp, concrt, vcomp - version number in name.
# Unversioned:      ucrtbase - the Universal CRT never carried a version number.
$versionedDebugPattern = '^(vcruntime|msvcp|concrt|vcomp)(\d+)(_\d+)?d\.dll$'
$versionedReleasePattern = '^(vcruntime|msvcp|concrt|vcomp)(\d+)(_\d+)?\.dll$'
$ucrtDebugPattern = '^ucrtbased\.dll$'
$ucrtReleasePattern = '^ucrtbase\.dll$'

$debugRuntimeFound = [System.Collections.Generic.List[string]]::new()
$releaseRuntimeFound = [System.Collections.Generic.List[string]]::new()
# Collect version numbers from the versioned DLLs (e.g. "140", "141").
$versions = [System.Collections.Generic.HashSet[string]]::new()

foreach ($dll in $imports) {
    if ($dll -match $versionedDebugPattern) {
        $debugRuntimeFound.Add($dll)
        [void]$versions.Add($Matches[2])
    }
    elseif ($dll -match $versionedReleasePattern) {
        $releaseRuntimeFound.Add($dll)
        [void]$versions.Add($Matches[2])
    }
    elseif ($dll -match $ucrtDebugPattern) {
        $debugRuntimeFound.Add($dll)
    }
    elseif ($dll -match $ucrtReleasePattern) {
        $releaseRuntimeFound.Add($dll)
    }
}

Write-Output ""
Write-Output "=== Configuration Check ($Configuration) ==="

# Warn about unexpected or mixed runtime versions.
foreach ($ver in $versions) {
    if ($ver -ne '140') {
        Write-Warning "Unexpected MSVC runtime version: $ver (expected 140)"
    }
}
if ($versions.Count -gt 1) {
    Write-Warning "Mixed MSVC runtime versions found: $($versions -join ', ')"
}

$failed = $false

if ($Configuration -eq 'Release') {
    # A release DLL must not carry debug runtime dependencies.
    if ($debugRuntimeFound.Count -gt 0) {
        Write-Output "  [FAIL] Debug runtime DLLs found in a Release build:"
        foreach ($d in $debugRuntimeFound) { Write-Output "         $d" }
        $failed = $true
    }
    else {
        Write-Output "  [OK]  No debug runtime DLLs found."
    }
}
else {
    # A debug DLL must not use the release runtime instead of the debug one.
    if ($releaseRuntimeFound.Count -gt 0) {
        Write-Output "  [FAIL] Release runtime DLLs found in a Debug build:"
        foreach ($d in $releaseRuntimeFound) { Write-Output "         $d" }
        $failed = $true
    }
    else {
        Write-Output "  [OK]  No release runtime DLLs found."
    }
}

if ($failed) { exit 1 }
