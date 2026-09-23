# Runs the test suite against a build directory (used by CI as well).
#
#   powershell -File scripts/test.ps1                                  # out/build/x64-release
#   powershell -File scripts/test.ps1 -Build out/build/x86-release -Quick
#
# -Quick skips the slower parts (fuzzing, rendering every sample at two widths).
# Exit code: number of failed steps.
param(
    [string]$Build = 'out/build/x64-release',
    [switch]$Quick
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$bin = Resolve-Path $Build
$t = Join-Path $bin 'mdview_test.exe'
$dll = @(Get-ChildItem $bin -Filter 'mdview.wlx*' | Select-Object -First 1)[0].FullName
$tmp = Join-Path $root 'out/test'
New-Item -ItemType Directory -Force $tmp | Out-Null

$failed = New-Object System.Collections.Generic.List[string]
function Step([string]$Name, [scriptblock]$Command) {
    Write-Host "== $Name"
    $global:LASTEXITCODE = 0
    & $Command
    if ($LASTEXITCODE -ne 0) {
        Write-Host "   FAILED (exit code $LASTEXITCODE)" -ForegroundColor Red
        $failed.Add($Name)
    }
}

# Spec conformance. md4c's own files for alerts, footnotes and task lists
# expect md4c's HTML, not GitHub's rendering, so they are not part of this.
Step 'GFM spec' { & $t spec tests/spec/gfm-spec.txt | Select-Object -Last 1 }
Step 'CommonMark spec' { & $t spec tests/spec/commonmark-spec.txt | Select-Object -Last 1 }
Step 'mdview extensions' { & $t spec tests/spec/mdview-extensions.txt --strict | Select-Object -Last 1 }
foreach ($s in 'spec-tables', 'spec-strikethrough', 'spec-permissive-autolinks') {
    Step $s { & $t spec "tests/spec/$s.txt" | Select-Object -Last 1 }
}

Step 'syntax highlighter' { & (Join-Path $bin 'highlight_test.exe') -q | Select-Object -Last 1 }
Step 'emoji table' { & (Join-Path $bin 'emoji_test.exe') | Select-Object -Last 1 }

# Every sample renders (parse + layout + Direct2D) without failing.
$widths = if ($Quick) { @(900) } else { @(320, 900) }
foreach ($f in Get-ChildItem samples/gfm/*.md) {
    foreach ($w in $widths) {
        Step "render $($f.Name) at $w" { & $t render $f.FullName (Join-Path $tmp 'render.png') --width $w --lines | Out-Null }
    }
}

# The plugin DLL driven like Total Commander does, and auto-reload.
foreach ($name in '02-github-readme.md', '04-code-highlighting.md', '16-front-matter.md') {
    Step "plugin $name" { & $t plugin $dll "samples/gfm/$name" (Join-Path $tmp 'plugin.png') | Select-Object -Last 1 }
}
Step 'auto-reload' { & $t reload $dll (Join-Path $tmp 'reload') | Select-Object -Last 1 }
Step 'settings dialog' {
    & $t settings $dll samples/gfm/04-code-highlighting.md (Join-Path $tmp 'settings/s') | Where-Object { $_ -match 'FAIL|settings' }
}

if (-not $Quick) {
    Step 'fuzz' { & $t fuzz 400 | Select-Object -Last 1 }
}

if ($failed.Count) {
    Write-Host "`n$($failed.Count) step(s) failed:" -ForegroundColor Red
    $failed | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
} else {
    Write-Host "`nAll tests passed." -ForegroundColor Green
}
exit $failed.Count
