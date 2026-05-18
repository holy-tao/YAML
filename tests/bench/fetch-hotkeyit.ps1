# Fetch HotKeyIt/Yaml (ahkV2 branch) into tests/bench/vendor/ for comparison
# benchmarks. Existing file left untouched; pass -Force to re-download.

[CmdletBinding()]
param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$vendorDir = Join-Path $scriptDir "vendor"
$dest      = Join-Path $vendorDir "HotKeyIt-Yaml.ahk"
$url       = "https://raw.githubusercontent.com/HotKeyIt/Yaml/ahkV2/Yaml.ahk"

if (-not (Test-Path $vendorDir)) {
    New-Item -ItemType Directory -Path $vendorDir | Out-Null
}

if ($Force -or -not (Test-Path $dest)) {
    Write-Host "Fetching HotKeyIt/Yaml (ahkV2)..."
    Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing
    $size = (Get-Item $dest).Length
    Write-Host ("  -> {0} ({1:N0} bytes)" -f $dest, $size)
} else {
    Write-Host "HotKeyIt/Yaml already present, skipping."
}
