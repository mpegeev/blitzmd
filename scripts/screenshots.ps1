# Takes the README screenshots (docs/screenshot-*.png) of the documents in
# docs/screenshots, with the renderer of a build (the plugin's own code):
#
#   powershell -File scripts/screenshots.ps1                  # out/build/x64-release
#   powershell -File scripts/screenshots.ps1 -Build out/build/x64-dev
param([string]$Build = 'out/build/x64-release')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$t = Join-Path (Resolve-Path $Build) 'mdview_test.exe'
& $t render docs/screenshots/overview.md docs/screenshot-light.png --width 900
& $t render docs/screenshots/diagrams.md docs/screenshot-dark.png --width 900 --dark
& $t render docs/screenshots/code.md docs/screenshot-code.png --width 900 --lines
