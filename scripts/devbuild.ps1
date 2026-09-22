# Quick developer build (x64 Release) into out/build/<dir>.
param([string]$Dir = 'x64-dev', [string]$Highlight = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'vsenv.ps1')
Import-VsEnv x64
$args2 = @('-S', $root, '-B', "$root/out/build/$Dir", '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release')
if ($Highlight) { $args2 += "-DMDVIEW_HIGHLIGHT_SOURCE=$Highlight" }
cmake @args2 | Out-Null
cmake --build "$root/out/build/$Dir"
exit $LASTEXITCODE
