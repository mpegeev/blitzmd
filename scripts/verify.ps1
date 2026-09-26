# Builds and tests the working tree the way CI does, plus an ASan build: for
# checking a branch locally (e.g. when Actions minutes are out).
#
#   powershell -File scripts/verify.ps1                    # x64, x86 and ASan
#   powershell -File scripts/verify.ps1 -Skip asan         # without the ASan build
#   powershell -File scripts/verify.ps1 -Asan { & $t mermaid samples/gfm/29-mermaid-beta.md }
#
# x64: out/build/x64-dev and the full test.ps1; x86: out/build/x86-release and
# test.ps1 -Quick; ASan: out/build/x64-asan-full running fuzz, modal and the
# -Asan script block ($t: the ASan mdview_test.exe, $dll: the ASan plugin).
# Each step's output goes to out/verify/<step>.log. Exit code: failed steps.
param([string[]]$Skip = @(), [scriptblock]$Asan = $null)
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$logs = Join-Path $root 'out/verify'
New-Item -ItemType Directory -Force $logs | Out-Null
$failed = @()
# A step: its output to its log, a line with its exit code, time and last lines.
# (A step sets $global:LASTEXITCODE; `exit` in it would end the whole script.)
function Run([string]$Name, [scriptblock]$Body) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $global:LASTEXITCODE = 0
    & $Body *>&1 | Out-File -Encoding utf8 (Join-Path $logs "$Name.log")
    $rc = $LASTEXITCODE
    $tail = Get-Content (Join-Path $logs "$Name.log") -Tail 3
    "{0,-12} rc={1} {2:N0}s  {3}" -f $Name, $rc, $sw.Elapsed.TotalSeconds, ($tail -join ' | ')
    if ($rc -ne 0) { $script:failed += $Name }
}
if ('x64' -notin $Skip) {
    Run 'build-x64' { & scripts/devbuild.ps1 -Dir x64-dev }
    Run 'test-x64' { & scripts/test.ps1 -Build out/build/x64-dev }
}
if ('x86' -notin $Skip) {
    Run 'build-x86' { & scripts/build.ps1 -Arch x86 }
    Run 'test-x86' { & scripts/test.ps1 -Build out/build/x86-release -Quick }
}
if ('asan' -notin $Skip) {
    Run 'build-asan' {
        . scripts/vsenv.ps1
        Import-VsEnv x64
        # (Quoted: PowerShell splits an unquoted -D...=a.cpp at the dot.)
        $configure = cmake -S . -B out/build/x64-asan-full -G Ninja -DCMAKE_BUILD_TYPE=Release -DMDVIEW_ASAN=ON `
                           "-DMDVIEW_HIGHLIGHT_SOURCE=src/core/highlight.cpp"
        if ($LASTEXITCODE -ne 0) {
            $configure
            return
        }
        cmake --build out/build/x64-asan-full
    }
    if ('build-asan' -notin $failed) {
        Run 'test-asan' {
            . scripts/vsenv.ps1
            Import-VsEnv x64
            $d = Get-ChildItem $env:VCToolsInstallDir -Recurse -Filter clang_rt.asan_dynamic-x86_64.dll | Select-Object -First 1
            $env:PATH = $d.DirectoryName + ';' + $env:PATH
            $t = (Resolve-Path out/build/x64-asan-full/mdview_test.exe).Path
            $dll = (Resolve-Path out/build/x64-asan-full/mdview.wlx64).Path
            $bad = 0
            & $t fuzz 400 | Select-Object -Last 1
            if ($LASTEXITCODE -ne 0) { $bad++ }
            & $t modal $dll out/test/asan-modal | Select-Object -Last 1
            if ($LASTEXITCODE -ne 0) { $bad++ }
            if ($Asan) {
                & $Asan
                if ($LASTEXITCODE -ne 0) { $bad++ }
            }
            "asan failures: $bad"
            $global:LASTEXITCODE = $bad
        }
    }
}
if ($failed.Count) { "FAILED: $($failed -join ', ')" } else { 'ALL OK' }
exit $failed.Count
