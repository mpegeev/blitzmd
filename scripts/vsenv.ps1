# Imports the MSVC developer environment (vcvarsall.bat) into the current
# PowerShell process. Dot-source this file, then call Import-VsEnv x64|x86.

function Import-VsEnv([string]$Arch = 'x64') {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found: $vswhere" }
    $vs = & $vswhere -latest -prerelease -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath | Select-Object -First 1
    if (-not $vs) { throw 'No Visual Studio with C++ tools found' }
    $bat = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
    $vcArch = if ($Arch -eq 'x86') { 'x64_x86' } else { 'x64' }
    $lines = cmd /c "`"$bat`" $vcArch >nul 2>&1 && set"
    if ($LASTEXITCODE -ne 0) { throw "vcvarsall.bat $vcArch failed" }
    foreach ($line in $lines) {
        $i = $line.IndexOf('=')
        if ($i -gt 0) {
            [Environment]::SetEnvironmentVariable($line.Substring(0, $i), $line.Substring($i + 1))
        }
    }
}
