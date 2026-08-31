[CmdletBinding()]
param(
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = $PSScriptRoot
$powerShellHost = (Get-Process -Id $PID).Path
$binary = Join-Path $projectRoot "validation\bin\model_final_openfoam_screen.exe"
$model = Join-Path $projectRoot "validation\run_configs\final_model_openfoam_screen_20260829.toml"
$fanCurves = Join-Path $projectRoot "library\fan_curves\fan_curves.toml"
$resourceGate = Join-Path $projectRoot "tools\openfoam_resource_gate.ps1"
$evidenceRoot = Join-Path $projectRoot "validation\runs\openfoam_screen_launch_evidence"

foreach($required in @($binary, $model, $fanCurves, $resourceGate)) {
    if(-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required launch file is missing: $required"
    }
}

$sourceSha = & python (Join-Path $projectRoot "tools\openfoam_semifrozen_attestation.py") `
    --print-source-sha --repo-root $projectRoot
if($LASTEXITCODE -ne 0) {
    throw "Unable to fingerprint the semi-frozen OpenFOAM solver source."
}
if($sourceSha.Trim() -ne "c5b8c82093c2017b295d43a9b23984cb88416ea168545d1886e26fa968e03c4f") {
    throw "Solver-source fingerprint changed after launcher preparation: $sourceSha"
}

if($ValidateOnly) {
    Write-Host "OpenFOAM screening launcher validation passed."
    Write-Host "Model: $model"
    Write-Host "Binary: $binary"
    Write-Host "Close Codex/ChatGPT before running without -ValidateOnly."
    exit 0
}

New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$caseName = "final_model_openfoam_screen_$stamp"
$hostEvidence = Join-Path $evidenceRoot "${caseName}_host_gate.json"
$wslEvidence = Join-Path $evidenceRoot "${caseName}_host_wsl_gate.json"
$exportLog = Join-Path $evidenceRoot "${caseName}_export.log"

Write-Host "Running the mandatory 60-second host resource gate..."
& $powerShellHost -NoLogo -NoProfile -ExecutionPolicy Bypass -File $resourceGate `
    -DiskPath "C:\OpenFOAM" -EvidencePath $hostEvidence
if($LASTEXITCODE -ne 0) {
    throw "Host resource gate failed. Evidence: $hostEvidence"
}

Push-Location $projectRoot
try {
    Write-Host "Exporting isolated OpenFOAM case '$caseName'..."
    & $binary --case-name $caseName $model $fanCurves 2>&1 |
        Tee-Object -FilePath $exportLog
    if($LASTEXITCODE -ne 0) {
        throw "OpenFOAM export failed. Log: $exportLog"
    }
} finally {
    Pop-Location
}

Write-Host "Running the mandatory host/WSL resource gate..."
& $powerShellHost -NoLogo -NoProfile -ExecutionPolicy Bypass -File $resourceGate `
    -DiskPath "C:\OpenFOAM" -QueryWsl -WslDistribution "Ubuntu" `
    -EvidencePath $wslEvidence
if($LASTEXITCODE -ne 0) {
    throw "Host/WSL resource gate failed. Evidence: $wslEvidence"
}

$windowsCase = "C:\OpenFOAM\thermal_model_final\$caseName"
$wslCase = "/mnt/c/OpenFOAM/thermal_model_final/$caseName"
$wslCommand = "source /usr/lib/openfoam/openfoam2606/etc/bashrc >/dev/null 2>&1 && cd '$wslCase' && set -o pipefail && ./run_parallel.sh 2 --multirate 30 2>&1 | tee -a thermal_solver.stdout.log"

Write-Host "Starting the two-rank OpenFOAM screening solve..."
Write-Host "Case: $windowsCase"
& wsl.exe -d Ubuntu -- bash -lc $wslCommand
if($LASTEXITCODE -ne 0) {
    throw "OpenFOAM screening solve failed with exit code $LASTEXITCODE. Case: $windowsCase"
}

Write-Host "OpenFOAM screening solve completed."
Write-Host "Case: $windowsCase"
