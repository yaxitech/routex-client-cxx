<#
.SYNOPSIS
    Removes a NuGet package's references from a .vcxproj (inverse of
    add-nuget-package.ps1).

.DESCRIPTION
    Strips every <Import> whose Project attribute mentions the given package
    id from the .vcxproj, and removes the matching <package id="..."/> entry
    from the sibling packages.config. The packages.config file is deleted
    when its last entry is removed.

    Idempotent: silently does nothing if no matching references exist.

.PARAMETER PackageId
    The NuGet package id to remove (for example, routex-client-cxx).

.PARAMETER VcxprojPath
    Path to the .vcxproj file to modify.
#>
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$PackageId,

    [Parameter(Mandatory = $true, Position = 1)]
    [string]$VcxprojPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$VcxprojPath = (Resolve-Path $VcxprojPath).Path
$projectDir = Split-Path $VcxprojPath -Parent

$writerSettings = New-Object System.Xml.XmlWriterSettings
$writerSettings.Indent = $true
$writerSettings.IndentChars = "  "
$writerSettings.Encoding = New-Object System.Text.UTF8Encoding($false)
$writerSettings.NewLineChars = "`r`n"
$writerSettings.NewLineHandling = [System.Xml.NewLineHandling]::Replace

# ---------------------------------------------------------------------------
# 1. Strip <Import> references from the .vcxproj
# ---------------------------------------------------------------------------
[xml]$proj = Get-Content $VcxprojPath -Raw -Encoding UTF8
$ns = "http://schemas.microsoft.com/developer/msbuild/2003"
$nsm = New-Object System.Xml.XmlNamespaceManager($proj.NameTable)
$nsm.AddNamespace("ms", $ns)

$imports = $proj.SelectNodes("//ms:Import[contains(@Project, '$PackageId')]", $nsm)
foreach ($imp in @($imports)) {
    $imp.ParentNode.RemoveChild($imp) | Out-Null
}

$writer = [System.Xml.XmlWriter]::Create($VcxprojPath, $writerSettings)
try {
    $proj.Save($writer)
}
finally {
    $writer.Dispose()
}

# ---------------------------------------------------------------------------
# 2. Strip <package id="..."/> entry from packages.config (delete if empty)
# ---------------------------------------------------------------------------
$packagesConfigPath = Join-Path $projectDir "packages.config"
if (Test-Path $packagesConfigPath) {
    [xml]$pkgConfig = Get-Content $packagesConfigPath -Raw -Encoding UTF8

    $entries = $pkgConfig.SelectNodes("/packages/package[@id='$PackageId']")
    foreach ($entry in @($entries)) {
        $entry.ParentNode.RemoveChild($entry) | Out-Null
    }

    $remaining = @($pkgConfig.SelectNodes("/packages/package"))
    if ($remaining.Count -eq 0) {
        Remove-Item $packagesConfigPath
    }
    else {
        $writer = [System.Xml.XmlWriter]::Create($packagesConfigPath, $writerSettings)
        try {
            $pkgConfig.Save($writer)
        }
        finally {
            $writer.Dispose()
        }
    }
}
