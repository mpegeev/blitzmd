# Builds mdview for one or both architectures and packages the plugin.
#
#   powershell -File scripts/build.ps1                       # x64 Release
#   powershell -File scripts/build.ps1 -Arch all -Package    # x64 + x86, dist/mdview.zip
#   powershell -File scripts/build.ps1 -Config Debug
#
# Output: out/build/<arch>-<config>/mdview.wlx64 (or mdview.wlx for x86).
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
        $name = if ($a -eq 'x86') { 'mdview.wlx' } else { 'mdview.wlx64' }
        Copy-Item (Join-Path $built[$a] $name) $stage
    }
    Copy-Item (Join-Path $root 'pluginst.inf') $stage
    Copy-Item (Join-Path $root 'README.md') $stage
    Copy-Item (Join-Path $root 'LICENSE') $stage
    $zip = Join-Path $dist 'mdview.zip'
    if (Test-Path $zip) { Remove-Item -Force $zip }
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
    Write-Host "Packaged: $zip"
}
