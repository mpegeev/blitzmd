# Runs the test suite against a build directory (used by CI as well).
#
#   powershell -File scripts/test.ps1                                  # out/build/x64-release
#   powershell -File scripts/test.ps1 -Build out/build/x86-release -Quick
#
# -Quick skips the slower parts (rendering every sample at two widths, most of
# the fuzzing). Exit code: number of failed steps.
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
# A step's last line (its summary), and the FAIL lines before it.
function Summary {
    begin { $last = $null }
    process {
        if ($null -ne $last -and "$last" -match 'FAIL') { $last }
        $last = $_
    }
    end { $last }
}

# Spec conformance. md4c's own files for alerts, footnotes and task lists
# expect md4c's HTML, not GitHub's rendering, so they are not part of this.
Step 'GFM spec' { & $t spec tests/spec/gfm-spec.txt | Summary }
Step 'CommonMark spec' { & $t spec tests/spec/commonmark-spec.txt | Summary }
Step 'mdview extensions' { & $t spec tests/spec/mdview-extensions.txt --strict | Summary }
foreach ($s in 'spec-tables', 'spec-strikethrough', 'spec-permissive-autolinks') {
    Step $s { & $t spec "tests/spec/$s.txt" | Summary }
}

Step 'syntax highlighter' { & (Join-Path $bin 'highlight_test.exe') -q | Summary }
Step 'emoji table' { & (Join-Path $bin 'emoji_test.exe') | Summary }
Step 'flag emoji' { & $t flags samples/gfm/17-flags.md | Summary }
Step 'collapsed details' {
    & $t details (Get-ChildItem samples/gfm/*.md | ForEach-Object FullName) | Where-Object { $_ -match 'FAIL|details' }
}
Step 'math' { & $t math samples/gfm/18-math.md | Where-Object { $_ -match 'FAIL|math' } }
# Every diagram of the samples; the parse checks, hostile input and fuzzing once.
Step 'mermaid' {
    & $t mermaid (Get-ChildItem samples/gfm/2*-mermaid-*.md | ForEach-Object FullName) | Where-Object { $_ -match 'FAIL|mermaid' }
}

# Every sample renders (parse + layout + Direct2D) without failing.
$widths = if ($Quick) { @(900) } else { @(320, 900) }
foreach ($f in Get-ChildItem samples/gfm/*.md) {
    foreach ($w in $widths) {
        Step "render $($f.Name) at $w" { & $t render $f.FullName (Join-Path $tmp 'render.png') --width $w --lines | Out-Null }
    }
}

# The plugin DLL driven like Total Commander does, and auto-reload.
# (Not $name: inside Step that is its $Name parameter, PowerShell names ignoring case.)
# (Each with the default settings: none left in out/test/mdview.ini.)
foreach ($sample in '02-github-readme.md', '04-code-highlighting.md', '16-front-matter.md') {
    Remove-Item (Join-Path $tmp 'mdview.ini') -ErrorAction SilentlyContinue
    Step "plugin $sample" { & $t plugin $dll "samples/gfm/$sample" (Join-Path $tmp 'plugin.png') | Summary }
}
Step 'auto-reload' { & $t reload $dll (Join-Path $tmp 'reload') | Summary }
Step 'settings dialog' {
    & $t settings $dll samples/gfm/04-code-highlighting.md (Join-Path $tmp 'settings/s') | Where-Object { $_ -match 'FAIL|settings' }
}
Step 'image zoom' { & $t zoom $dll tests/zoom.md (Join-Path $tmp 'zoom/z') | Where-Object { $_ -match 'FAIL|zoom' } }
Step 'inputs' { & $t inputs all | Where-Object { $_ -match 'FAIL|inputs' } }
Step 'long paths' { & $t longpath (Join-Path $tmp 'longpath') | Where-Object { $_ -match 'FAIL|longpath' } }
Step 'remote pictures' { & $t remote $dll (Join-Path $tmp 'remote') | Where-Object { $_ -match 'FAIL|remote' } }
Step 'search' { & $t search $dll (Join-Path $tmp 'search') | Where-Object { $_ -match 'FAIL|search' } }
Step 'Windows theme' { & $t theme $dll (Join-Path $tmp 'theme') | Where-Object { $_ -match 'FAIL|theme' } }
Step 'printer' { & $t printer dialog | Where-Object { $_ -match 'FAIL|printer' } }
Step 'heavy paragraphs' { & $t heavy 5000 | Where-Object { $_ -match 'FAIL|heavy' } }
Step 'closed in a modal loop' { & $t modal $dll (Join-Path $tmp 'modal') | Where-Object { $_ -match 'FAIL|modal' } }

$fuzz = if ($Quick) { 100 } else { 400 }
Step 'fuzz' { & $t fuzz $fuzz | Summary }

if ($failed.Count) {
    Write-Host "`n$($failed.Count) step(s) failed:" -ForegroundColor Red
    $failed | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
} else {
    Write-Host "`nAll tests passed." -ForegroundColor Green
}
exit $failed.Count
