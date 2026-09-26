# Quick developer build (x64 Release) into out/build/<dir>.
param([string]$Dir = 'x64-dev', [string]$Highlight = 'src/core/highlight.cpp')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'vsenv.ps1')
Import-VsEnv x64
$args2 = @('-S', $root, '-B', "$root/out/build/$Dir", '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
           "-DMDVIEW_HIGHLIGHT_SOURCE=$Highlight")  # (always: a cached value would stick)
$configure = cmake @args2
if ($LASTEXITCODE -ne 0) {
    $configure
    exit $LASTEXITCODE
}
cmake --build "$root/out/build/$Dir"
exit $LASTEXITCODE
