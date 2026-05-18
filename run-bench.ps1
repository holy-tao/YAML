# Run the YAML benchmark harness on 32- and 64-bit AHK.
#
# Steps:
#   1. Fetch corpus (idempotent).
#   2. Build dist/YAML.ahk if missing.
#   3. Run RunBench.ahk under AutoHotkey64 then AutoHotkey32.

[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$X64Only,
    [switch]$Compare
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path

Write-Host "Fetching corpus..."
& (Join-Path $root "tests\bench\fetch-corpus.ps1")

if ($Compare) {
    Write-Host "Fetching HotKeyIt/Yaml..."
    & (Join-Path $root "tests\bench\fetch-hotkeyit.ps1")
}

$dist = Join-Path $root "dist\YAML.ahk"
if (-not $SkipBuild -and -not (Test-Path $dist)) {
    Write-Host "Building dist/YAML.ahk..."
    AutoHotkey64.exe /ErrorStdOut=UTF-8 (Join-Path $root "build\build.ahk") 2>&1 | Write-Host
}

$runner = Join-Path $root ($Compare ? "tests\bench\CompareBench.ahk" : "tests\bench\RunBench.ahk")

Write-Host "`n64-bit benchmarks:"
AutoHotkey64.exe /ErrorStdOut=UTF-8 $runner 2>&1 | Write-Output

if (-not $X64Only) {
    Write-Host "`n32-bit benchmarks:"
    AutoHotkey32.exe /ErrorStdOut=UTF-8 $runner 2>&1 | Write-Output
}
