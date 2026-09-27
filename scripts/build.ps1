# Builds BlitzMD for one or both architectures and packages the plugin.
#
#   powershell -File scripts/build.ps1                       # x64 Release
#   powershell -File scripts/build.ps1 -Arch all -Package    # x64 + x86, dist/blitzmd.zip
#   powershell -File scripts/build.ps1 -Config Debug
#
# Output: out/build/<arch>-<config>/blitzmd.wlx64 (or blitzmd.wlx for x86).
param(
    [ValidateSet('x64', 'x86', 'all')] [string]$Arch = 'x64',
    [ValidateSet('Release', 'Debug')] [string]$Config = 'Release',
    [switch]$Clean,
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'vsenv.ps1')

$arches = if ($Arch -eq 'all') { @('x64', 'x86') } else { @($Arch) }
$built = @{}
foreach ($a in $arches) {
    # Each architecture needs its own environment -> run in a child process.
    $dir = Join-Path $root "out/build/$a-$($Config.ToLower())"
    if ($Clean -and (Test-Path $dir)) { Remove-Item -Recurse -Force $dir }
    $script = @"
`$ErrorActionPreference = 'Stop'
. '$PSScriptRoot/vsenv.ps1'
Import-VsEnv $a
cmake -S '$root' -B '$dir' -G Ninja -DCMAKE_BUILD_TYPE=$Config
if (`$LASTEXITCODE -ne 0) { exit `$LASTEXITCODE }
cmake --build '$dir'
exit `$LASTEXITCODE
"@
    powershell -NoProfile -ExecutionPolicy Bypass -Command $script
    if ($LASTEXITCODE -ne 0) { throw "Build failed for $a" }
    $built[$a] = $dir
}

if ($Package) {
    $dist = Join-Path $root 'dist'
    $stage = Join-Path $dist 'stage'
    if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
    New-Item -ItemType Directory -Force $stage | Out-Null
    foreach ($a in $built.Keys) {
        $name = if ($a -eq 'x86') { 'blitzmd.wlx' } else { 'blitzmd.wlx64' }
        Copy-Item (Join-Path $built[$a] $name) $stage
    }
    # Total Commander's auto-installer expects a Windows text file (CRLF, ANSI),
    # whatever line endings the checkout has. The version is the one in
    # CMakeLists.txt (project(... VERSION x.y.z)).
    $cmake = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
    if ($cmake -notmatch 'project\(blitzmd VERSION ([0-9.]+)') { throw 'No version in CMakeLists.txt' }
    $version = $matches[1]
    $inf = (Get-Content (Join-Path $root 'pluginst.inf') | ForEach-Object {
        if ($_ -match '^version=') { "version=$version" } else { $_ }
    }) -join "`r`n"
    [System.IO.File]::WriteAllText((Join-Path $stage 'pluginst.inf'), $inf + "`r`n", [System.Text.Encoding]::ASCII)
    Copy-Item (Join-Path $root 'README.md') $stage
    Copy-Item (Join-Path $root 'README_RU.md') $stage
    Copy-Item (Join-Path $root 'LICENSE') $stage
    $zip = Join-Path $dist 'blitzmd.zip'
    if (Test-Path $zip) { Remove-Item -Force $zip }
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
    Write-Host "Packaged: $zip"
}
