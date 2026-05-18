# Populate tests/bench/corpus/ with benchmark inputs.
#
# Sources:
#   - Kubernetes OpenAPI swagger.json (JSON is valid YAML, saved with .yaml ext)
#     pinned at kubernetes/kubernetes v1.30.0.
#   - rapidyaml v0.7.1 bm/cases/*  (style + scalar + real-config inputs).
#
# Existing files are left untouched. Pass -Force to re-download.

[CmdletBinding()]
param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$scriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$corpusRoot  = Join-Path $scriptDir "corpus"
$rapidyamlOut = Join-Path $corpusRoot "rapidyaml"

$K8S_TAG     = "v1.30.0"
$K8S_URL     = "https://raw.githubusercontent.com/kubernetes/kubernetes/$K8S_TAG/api/openapi-spec/swagger.json"
$K8S_DEST    = Join-Path $corpusRoot "k8s-swagger.yaml"

$RYML_TAG    = "v0.7.1"
$RYML_REPO   = "https://github.com/biojppm/rapidyaml.git"

if (-not (Test-Path $corpusRoot)) {
    New-Item -ItemType Directory -Path $corpusRoot | Out-Null
}

# --- Kubernetes swagger -----------------------------------------------------
if ($Force -or -not (Test-Path $K8S_DEST)) {
    Write-Host "Fetching k8s swagger ($K8S_TAG)..."
    Invoke-WebRequest -Uri $K8S_URL -OutFile $K8S_DEST -UseBasicParsing
    $size = (Get-Item $K8S_DEST).Length
    Write-Host ("  -> {0} ({1:N0} bytes)" -f $K8S_DEST, $size)
} else {
    Write-Host "k8s swagger already present, skipping."
}

# --- rapidyaml bm/cases ----------------------------------------------------
if ($Force -or -not (Test-Path $rapidyamlOut) -or -not (Get-ChildItem $rapidyamlOut -ErrorAction SilentlyContinue)) {
    Write-Host "Fetching rapidyaml bm/cases ($RYML_TAG)..."
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("rapidyaml-" + [System.Guid]::NewGuid().ToString())
    try {
        git clone --depth 1 --branch $RYML_TAG $RYML_REPO $tmp 2>&1 | Out-Host
        $bmCases = Join-Path $tmp "bm\cases"
        if (-not (Test-Path $bmCases)) {
            throw "bm/cases not found in clone at $bmCases"
        }
        if (-not (Test-Path $rapidyamlOut)) {
            New-Item -ItemType Directory -Path $rapidyamlOut | Out-Null
        }
        Copy-Item -Path (Join-Path $bmCases "*") -Destination $rapidyamlOut -Recurse -Force
        $count = (Get-ChildItem $rapidyamlOut -File -Recurse).Count
        Write-Host "  -> $count file(s) into $rapidyamlOut"
    } finally {
        if (Test-Path $tmp) {
            Remove-Item -Recurse -Force $tmp
        }
    }
} else {
    Write-Host "rapidyaml cases already present, skipping."
}

Write-Host "Corpus ready at $corpusRoot"
