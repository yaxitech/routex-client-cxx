<#
.SYNOPSIS
    Adds a local NuGet package to a .vcxproj, mirroring what Visual Studio does.

.PARAMETER NugetPackagePath
    Path to the .nupkg file to install.

.PARAMETER VcxprojPath
    Path to the .vcxproj file to modify.
#>
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$NugetPackagePath,

    [Parameter(Mandatory = $true, Position = 1)]
    [string]$VcxprojPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$NugetPackagePath = (Resolve-Path $NugetPackagePath).Path
$VcxprojPath = (Resolve-Path $VcxprojPath).Path
$projectDir = Split-Path $VcxprojPath -Parent

# ---------------------------------------------------------------------------
# 1. Read package ID and version from the .nuspec embedded in the .nupkg zip
# ---------------------------------------------------------------------------
Add-Type -AssemblyName "System.IO.Compression.FileSystem"

$zip = [System.IO.Compression.ZipFile]::OpenRead($NugetPackagePath)
try {
    $nuspecEntry = $zip.Entries |
        Where-Object { $_.Name -like "*.nuspec" -and $_.FullName -notlike "*/*" } |
        Select-Object -First 1
    if (-not $nuspecEntry) {
        # Fall back: accept a nuspec at any depth
        $nuspecEntry = $zip.Entries | Where-Object { $_.Name -like "*.nuspec" } | Select-Object -First 1
    }
    if (-not $nuspecEntry) { throw "No .nuspec file found in '$NugetPackagePath'." }

    $reader = New-Object System.IO.StreamReader($nuspecEntry.Open())
    $nuspecXml = [xml]$reader.ReadToEnd()
    $reader.Dispose()
}
finally {
    $zip.Dispose()
}

$packageId = $nuspecXml.package.metadata.id
$packageVersion = $nuspecXml.package.metadata.version
$packageDirName = "$packageId.$packageVersion"
Write-Verbose "Adding NuGet package: $packageId $packageVersion"

# ---------------------------------------------------------------------------
# 2. Find the solution directory (walk up from project dir looking for .sln)
# ---------------------------------------------------------------------------
$slnDir = $null
$searchDir = $projectDir
while ($searchDir) {
    if (Get-ChildItem -Path $searchDir -Filter "*.sln" -ErrorAction SilentlyContinue) {
        $slnDir = $searchDir
        break
    }
    $parent = Split-Path $searchDir -Parent
    if ($parent -eq $searchDir) { break }
    $searchDir = $parent
}

if ($slnDir) {
    $msbuildPackagesBase = '$(SolutionDir)packages\'
    $physicalPackagesDir = Join-Path $slnDir "packages"
}
else {
    Write-Warning "No .sln found; using project directory as package base."
    $msbuildPackagesBase = '$(ProjectDir)packages\'
    $physicalPackagesDir = Join-Path $projectDir "packages"
}

$packageInstallDir = Join-Path $physicalPackagesDir $packageDirName
# Compose MSBuild-property paths ($(SolutionDir) / $(ProjectDir) remain as literals)
$msbuildPropsPath = "${msbuildPackagesBase}${packageDirName}\build\${packageId}.props"
$msbuildTargetsPath = "${msbuildPackagesBase}${packageDirName}\build\${packageId}.targets"

# ---------------------------------------------------------------------------
# 3. Extract the .nupkg into the packages directory
# ---------------------------------------------------------------------------
if (Test-Path $packageInstallDir) {
    Write-Warning "Package directory already exists, skipping extraction: $packageInstallDir"
}
else {
    New-Item -ItemType Directory -Force -Path $packageInstallDir | Out-Null
    $zip = [System.IO.Compression.ZipFile]::OpenRead($NugetPackagePath)
    try {
        foreach ($entry in $zip.Entries) {
            if ($entry.FullName.EndsWith('/')) { continue }
            $destPath = Join-Path $packageInstallDir ($entry.FullName.Replace('/', '\'))
            $destDir = Split-Path $destPath -Parent
            if (-not (Test-Path $destDir)) {
                New-Item -ItemType Directory -Force -Path $destDir | Out-Null
            }
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $destPath, $true)
        }
    }
    finally {
        $zip.Dispose()
    }
    Write-Verbose "Extracted to: $packageInstallDir"
}

# Determine which build-integration files the package provides
$propsExists = Test-Path (Join-Path $packageInstallDir "build\$packageId.props")
$targetsExists = Test-Path (Join-Path $packageInstallDir "build\$packageId.targets")

# ---------------------------------------------------------------------------
# 4. Create / update packages.config
# ---------------------------------------------------------------------------
$packagesConfigPath = Join-Path $projectDir "packages.config"

if (Test-Path $packagesConfigPath) {
    [xml]$pkgConfig = Get-Content $packagesConfigPath -Raw -Encoding UTF8
    $already = $pkgConfig.packages.SelectNodes("package") |
        Where-Object { $_.GetAttribute("id") -eq $packageId }
    if ($already) {
        Write-Warning "Package already listed in packages.config; skipping."
    }
    else {
        $elem = $pkgConfig.CreateElement("package")
        $elem.SetAttribute("id", $packageId)
        $elem.SetAttribute("version", $packageVersion)
        $elem.SetAttribute("targetFramework", "native")
        $pkgConfig.packages.AppendChild($elem) | Out-Null
        $pkgConfig.Save($packagesConfigPath)
        Write-Verbose "Updated packages.config"
    }
}
else {
    $pkgConfig = New-Object System.Xml.XmlDocument
    $pkgConfig.AppendChild($pkgConfig.CreateXmlDeclaration("1.0", "utf-8", $null)) | Out-Null
    $root = $pkgConfig.CreateElement("packages")
    $elem = $pkgConfig.CreateElement("package")
    $elem.SetAttribute("id", $packageId)
    $elem.SetAttribute("version", $packageVersion)
    $elem.SetAttribute("targetFramework", "native")
    $root.AppendChild($elem) | Out-Null
    $pkgConfig.AppendChild($root) | Out-Null
    $pkgConfig.Save($packagesConfigPath)
    Write-Verbose "Created packages.config"
}

# ---------------------------------------------------------------------------
# 5. Modify the .vcxproj
# ---------------------------------------------------------------------------
[xml]$proj = Get-Content $VcxprojPath -Raw -Encoding UTF8
$ns = "http://schemas.microsoft.com/developer/msbuild/2003"
$nsm = New-Object System.Xml.XmlNamespaceManager($proj.NameTable)
$nsm.AddNamespace("ms", $ns)

function Test-ImportExist([string]$hint) {
    foreach ($imp in $proj.SelectNodes("//ms:Import", $nsm)) {
        if ($imp.GetAttribute("Project") -like "*$hint*") { return $true }
    }
    return $false
}

# -- .props import inside <ImportGroup Label="ExtensionSettings"> ------------
if ($propsExists -and -not (Test-ImportExist $packageId)) {
    $extSettings = $proj.SelectSingleNode("//ms:ImportGroup[@Label='ExtensionSettings']", $nsm)
    if (-not $extSettings) { throw "ImportGroup 'ExtensionSettings' not found in vcxproj." }
    $imp = $proj.CreateElement("Import", $ns)
    $imp.SetAttribute("Project", $msbuildPropsPath)
    $imp.SetAttribute("Condition", "Exists('$msbuildPropsPath')")
    $extSettings.AppendChild($imp) | Out-Null
}

# -- .targets import inside <ImportGroup Label="ExtensionTargets"> -----------
if ($targetsExists -and -not (Test-ImportExist "$packageId.targets")) {
    $extTargets = $proj.SelectSingleNode("//ms:ImportGroup[@Label='ExtensionTargets']", $nsm)
    if (-not $extTargets) { throw "ImportGroup 'ExtensionTargets' not found in vcxproj." }
    $imp = $proj.CreateElement("Import", $ns)
    $imp.SetAttribute("Project", $msbuildTargetsPath)
    $imp.SetAttribute("Condition", "Exists('$msbuildTargetsPath')")
    $extTargets.AppendChild($imp) | Out-Null
}

# -- EnsureNuGetPackageBuildImports target -----------------------------------
$ensureTarget = $proj.SelectSingleNode(
    "//ms:Target[@Name='EnsureNuGetPackageBuildImports']", $nsm)

if (-not $ensureTarget) {
    $ensureTarget = $proj.CreateElement("Target", $ns)
    $ensureTarget.SetAttribute("Name", "EnsureNuGetPackageBuildImports")
    $ensureTarget.SetAttribute("BeforeTargets", "PrepareForBuild")

    $pg = $proj.CreateElement("PropertyGroup", $ns)
    $errText = $proj.CreateElement("ErrorText", $ns)
    $errText.InnerText = ("This project references NuGet package(s) that are missing on this " +
        "computer. Use NuGet Package Restore to download them.  For more information, see " +
        "http://go.microsoft.com/fwlink/?LinkID=322105. The missing file is {0}.")
    $pg.AppendChild($errText) | Out-Null
    $ensureTarget.AppendChild($pg) | Out-Null
    $proj.DocumentElement.AppendChild($ensureTarget) | Out-Null
}

$errorTextValue = '$(ErrorText)'   # MSBuild property reference, not a PS variable
if ($propsExists) {
    $errEl = $proj.CreateElement("Error", $ns)
    $errEl.SetAttribute("Condition", "!Exists('$msbuildPropsPath')")
    $errEl.SetAttribute("Text", "`$([System.String]::Format('$errorTextValue', '$msbuildPropsPath'))")
    $ensureTarget.AppendChild($errEl) | Out-Null
}
if ($targetsExists) {
    $errEl = $proj.CreateElement("Error", $ns)
    $errEl.SetAttribute("Condition", "!Exists('$msbuildTargetsPath')")
    $errEl.SetAttribute("Text", "`$([System.String]::Format('$errorTextValue', '$msbuildTargetsPath'))")
    $ensureTarget.AppendChild($errEl) | Out-Null
}

# -- Save vcxproj with consistent UTF-8 (no BOM), 2-space indentation -------
$writerSettings = New-Object System.Xml.XmlWriterSettings
$writerSettings.Indent = $true
$writerSettings.IndentChars = "  "
$writerSettings.Encoding = New-Object System.Text.UTF8Encoding($false)
$writerSettings.NewLineChars = "`r`n"
$writerSettings.NewLineHandling = [System.Xml.NewLineHandling]::Replace

$writer = [System.Xml.XmlWriter]::Create($VcxprojPath, $writerSettings)
try {
    $proj.Save($writer)
}
finally {
    $writer.Dispose()
}

Write-Verbose "Updated: $(Split-Path $VcxprojPath -Leaf)"
Write-Verbose "Done."
