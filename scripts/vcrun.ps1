# Runs a command inside the MSVC developer environment.
#
#   powershell -File scripts/vcrun.ps1 [-Arch x64|x86] <command> [args...]
#
# Example:
#   powershell -File scripts/vcrun.ps1 cl /nologo /EHsc test.cpp
[CmdletBinding(PositionalBinding = $false)]
param(
    [ValidateSet('x64', 'x86')] [string]$Arch = 'x64',
    [Parameter(ValueFromRemainingArguments = $true)] [string[]]$Command
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'vsenv.ps1')
Import-VsEnv $Arch
if (-not $Command -or $Command.Count -eq 0) { Write-Error 'No command given'; exit 2 }
$exe = $Command[0]
$rest = @()
if ($Command.Count -gt 1) { $rest = $Command[1..($Command.Count - 1)] }
& $exe @rest
exit $LASTEXITCODE
