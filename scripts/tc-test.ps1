# Opens a file in Total Commander's Lister with the freshly built plugin,
# using a *separate* TC instance and a copy of the user's wincmd.ini
# (out/tc/wincmd.ini), so the real configuration is never modified.
#
#   powershell -File scripts/tc-test.ps1 -File samples\gfm\02-github-readme.md
#   powershell -File scripts/tc-test.ps1 -Stop          # close the test instance
param(
    [string]$File,
    [string]$Build = 'x64-dev',
    [switch]$Stop,
    [int]$DarkMode = -1
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$tc = 'C:\Program Files\totalcmd\TOTALCMD64.EXE'
$dir = Join-Path $root 'out\tc'
$ini = Join-Path $dir 'wincmd.ini'

# Close a previous test instance (identified by our ini path on its command line).
Get-CimInstance Win32_Process -Filter "Name='TOTALCMD64.EXE'" |
    Where-Object { $_.CommandLine -and $_.CommandLine.Contains($ini) } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force; Start-Sleep -Milliseconds 300 }
if ($Stop) { return }

New-Item -ItemType Directory -Force (Join-Path $dir 'plugin') | Out-Null
Copy-Item (Join-Path $root "out\build\$Build\mdview.wlx64") (Join-Path $dir 'plugin\mdview.wlx64') -Force
if (-not (Test-Path $ini)) {
    Copy-Item (Join-Path $env:APPDATA 'GHISLER\wincmd.ini') $ini
}
# Point [ListerPlugins] at the test build only.
$lines = Get-Content $ini -Encoding Default
$out = New-Object System.Collections.Generic.List[string]
$skip = $false
foreach ($l in $lines) {
    if ($l -match '^\[(.+)\]$') {
        $skip = $matches[1] -eq 'ListerPlugins' -or $matches[1] -eq 'ListerPlugins64'
        if ($skip) { continue }
    }
    if (-not $skip) {
        if ($DarkMode -ge 0 -and $l -match '^DarkMode=') { $l = "DarkMode=$DarkMode" }
        $out.Add($l)
    }
}
$plugin = Join-Path $dir 'plugin\mdview.wlx64'
$out.Add('[ListerPlugins]')
$out.Add("0=$plugin")
$out.Add('0_detect="EXT="MD" | EXT="MARKDOWN" | EXT="MDOWN" | EXT="MKD" | EXT="MKDN" | EXT="MDWN" | EXT="MDTXT" | EXT="MDTEXT" | EXT="RMD""')
Set-Content $ini $out -Encoding Default

$full = (Resolve-Path $File).Path
Start-Process $tc -ArgumentList @('/N', "/i=`"$ini`"", "/S=L", "`"$full`"")
