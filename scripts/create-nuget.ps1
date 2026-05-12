[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Version,

    [string]$Commit = $env:GITHUB_SHA ?? (git rev-parse HEAD)
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Clean up previous run
if (Test-Path "nuget-pkg") {
    Remove-Item "nuget-pkg" -Recurse -Force
}
New-Item -ItemType Directory -Path "nuget-pkg" | Out-Null

# Prepare contents
$runtimeIdToTriplet = @{
    "win-arm64" = "aarch64-pc-windows-msvc"
    "win-x86"   = "i686-pc-windows-msvc"
    "win-x64"   = "x86_64-pc-windows-msvc"
}
foreach ($runtimeId in $runtimeIdToTriplet.Keys) {
    $triplet = $runtimeIdToTriplet[$runtimeId]

    $dst = "nuget-pkg/runtimes/$runtimeId/native"
    New-Item -ItemType Directory -Path $dst -Force | Out-Null
    New-Item -ItemType Directory -Path "$dst/debug" -Force | Out-Null

    Get-ChildItem -Path "target/$triplet/release/*"  -Include *.dll, *.dll.lib |
        Copy-Item -Destination $dst

    Get-ChildItem -Path "target/$triplet/debug/*"  -Include *.pdb, *.dll, *.dll.lib |
        Copy-Item -Destination "$dst/debug"
}

Copy-Item "include" "nuget-pkg/include" -Recurse
Copy-Item "README.md" "nuget-pkg/README.md"
Join-Path $PSScriptRoot "NuGetTemplate/build" | Copy-Item -Destination "nuget-pkg" -Recurse

# Generate nuspec
$NuspecTemplate = Join-Path $PSScriptRoot "NuGetTemplate/routex-client-cxx.nuspec"

$nuspec = (Get-Content $NuspecTemplate -Raw) `
    -replace '\$version\$', $Version `
    -replace '\$commit\$', $Commit

$nuspecPath = "nuget-pkg/routex-client-cxx.nuspec"
$nuspec | Out-File $nuspecPath -Encoding UTF8

# Finally: pack
nuget pack $nuspecPath -OutputDirectory . -BasePath "nuget-pkg"
