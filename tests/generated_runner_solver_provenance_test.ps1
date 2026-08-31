param(
    [Parameter(Mandatory = $true)]
    [string]$CasePath
)

$ErrorActionPreference = "Stop"

function Assert-Condition {
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

# Use ArgumentList when available, with a quoted Arguments fallback for
# Windows PowerShell 5.1/.NET Framework.
function Convert-ToProcessStartArgument {
    param([Parameter(Mandatory = $true)][string]$Argument)
    if ($Argument.Length -eq 0) { return '""' }
    if ($Argument -notmatch '[\s"]') { return $Argument }
    $builder = [Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $backslashes = 0
    foreach ($character in $Argument.ToCharArray()) {
        if ($character -eq '\\') { $backslashes++; continue }
        if ($character -eq '"') {
            for ($index = 0; $index -lt (2 * $backslashes + 1); $index++) { [void]$builder.Append('\\') }
            [void]$builder.Append('"')
            $backslashes = 0
            continue
        }
        for ($index = 0; $index -lt $backslashes; $index++) { [void]$builder.Append('\\') }
        $backslashes = 0
        [void]$builder.Append($character)
    }
    for ($index = 0; $index -lt (2 * $backslashes); $index++) { [void]$builder.Append('\\') }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Add-ProcessArgument {
    param([Parameter(Mandatory = $true)][Diagnostics.ProcessStartInfo]$StartInfo,
          [Parameter(Mandatory = $true)][string]$Argument)
    if ($null -ne $StartInfo.ArgumentList) {
        [void]$StartInfo.ArgumentList.Add($Argument)
        return
    }
    $encoded = Convert-ToProcessStartArgument $Argument
    $StartInfo.Arguments = if ([string]::IsNullOrWhiteSpace($StartInfo.Arguments)) {
        $encoded
    } else {
        "$($StartInfo.Arguments) $encoded"
    }
}

function Get-TreeFingerprint {
    param([Parameter(Mandatory = $true)][string]$Path)
    $root = [IO.Path]::GetFullPath($Path)
    $entries = foreach ($item in @(
        Get-ChildItem -LiteralPath $root -Recurse -Force |
            Sort-Object -Property FullName
    )) {
        $relative = [IO.Path]::GetRelativePath($root, $item.FullName).Replace('\', '/')
        if ($item.PSIsContainer) {
            "D`t$relative"
        }
        else {
            $hash = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash
            "F`t$relative`t$($item.Length)`t$hash"
        }
    }
    return ($entries -join "`n")
}

$resolvedCase = [IO.Path]::GetFullPath($CasePath)
$runner = Join-Path $resolvedCase "run_parallel.sh"
Assert-Condition (Test-Path -LiteralPath $runner -PathType Leaf) `
    "Generated runner was not found: $runner"

$bashCandidates = [Collections.Generic.List[string]]::new()
if ($env:GIT_BASH) {
    $bashCandidates.Add($env:GIT_BASH)
}
$gitCommand = Get-Command git -ErrorAction SilentlyContinue
if ($null -ne $gitCommand) {
    $gitRoot = Split-Path (Split-Path $gitCommand.Source -Parent) -Parent
    $bashCandidates.Add((Join-Path $gitRoot "bin/bash.exe"))
}
if ($env:ProgramFiles) {
    $bashCandidates.Add((Join-Path $env:ProgramFiles "Git/bin/bash.exe"))
}
if (${env:ProgramFiles(x86)}) {
    $bashCandidates.Add((Join-Path ${env:ProgramFiles(x86)} "Git/bin/bash.exe"))
}
$bashPath = $bashCandidates |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
Assert-Condition (-not [string]::IsNullOrWhiteSpace($bashPath)) `
    "Git Bash is required to execute the generated OpenFOAM runner regression."
$bashPath = [IO.Path]::GetFullPath($bashPath)

function Convert-ToGitBashPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    $converted = & $bashPath --login -c 'cygpath -u "$1"' -- ([IO.Path]::GetFullPath($Path))
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($converted)) {
        throw "Unable to convert path for Git Bash: $Path"
    }
    return ([string]($converted | Select-Object -First 1)).Trim()
}

$marker = "THERMAL_SIM_SEMIFROZEN_MODE_POLICY_V1"
$runnerText = Get-Content -LiteralPath $runner -Raw
$environmentPosition = $runnerText.IndexOf(
    'Initializing OpenFOAM environment once with $foam_launcher.')
$markerPosition = $runnerText.IndexOf(
    "solver_mode_policy_marker=`"$marker`"")
$sourcePinMatch = [regex]::Match(
    $runnerText,
    'solver_project_source_sha256="(?<sha>[0-9a-f]{64})"')
$locatePosition = $runnerText.IndexOf(
    'semi_frozen_solver="$(command -v semiFrozenChtMultiRegionFoam || true)"')
$absolutePosition = $runnerText.IndexOf(
    'semi_frozen_solver="$(readlink -f "$semi_frozen_solver")"')
$runtimePosition = $runnerText.IndexOf(
    'solver_runtime_attestation=$("$semi_frozen_solver" --thermal-sim-attest')
$lockPosition = $runnerText.IndexOf(
    'run_lock="$case_dir/.thermal_solver_run.lock"')
Assert-Condition ($environmentPosition -ge 0) `
    "Generated runner does not initialize the OpenFOAM environment."
Assert-Condition ($markerPosition -gt $environmentPosition) `
    "Custom-solver marker verification must follow OpenFOAM environment setup."
Assert-Condition ($locatePosition -gt $markerPosition) `
    "Generated runner does not locate the custom solver after declaring its marker."
Assert-Condition $sourcePinMatch.Success `
    "Generated runner does not pin a lowercase project-source SHA-256."
Assert-Condition ($absolutePosition -gt $locatePosition) `
    "Generated runner does not pin the custom solver to an absolute path."
Assert-Condition ($runtimePosition -gt $absolutePosition) `
    "Generated runner does not execute the custom solver runtime attestation."
Assert-Condition ($lockPosition -gt $runtimePosition) `
    "Custom-solver runtime attestation must precede case locking."
Assert-Condition ($runnerText.Contains(
    'project_source_sha256=$solver_project_source_sha256')) `
    "Generated runner does not compare the runtime project-source digest."
Assert-Condition (-not $runnerText.Contains('grep -aFq --')) `
    "Generated runner still relies on a passive binary marker grep."
Assert-Condition (-not $runnerText.Contains(
    'semiFrozenChtMultiRegionFoam -case')) `
    "Generated runner retains a bare custom-solver execution after attestation."
Assert-Condition (-not $runnerText.Contains(
    'run_fan_ramp semiFrozenChtMultiRegionFoam')) `
    "Generated fan ramp does not use the attested absolute solver path."
Assert-Condition ($runnerText.Contains(
    'run_fan_ramp "$semi_frozen_solver"')) `
    "Generated fan ramp is not pinned to the attested solver path."
$absoluteSolverExecutions = [regex]::Matches(
    $runnerText,
    [regex]::Escape('"$semi_frozen_solver" -case')).Count
Assert-Condition ($absoluteSolverExecutions -ge 5) `
    "Expected at least five absolute attested solver executions; found $absoluteSolverExecutions."

$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$testRoot = [IO.Path]::GetFullPath((Join-Path $tempBase (
    "thermal_solver_provenance_test_" + [guid]::NewGuid().ToString("N"))))
Assert-Condition ($testRoot.StartsWith(
    $tempBase, [StringComparison]::OrdinalIgnoreCase)) `
    "Unsafe generated-runner test directory: $testRoot"
[void](New-Item -ItemType Directory -Path $testRoot)

try {
    $fakeBin = Join-Path $testRoot "bin"
    [void](New-Item -ItemType Directory -Path $fakeBin)
    $environmentSentinel = Join-Path $testRoot "environment-launch.log"
    $fakeLauncher = Join-Path $testRoot "fake-openfoam-launcher.sh"
    $fakeLauncherText = @'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$THERMAL_PROVENANCE_ENV_SENTINEL"
export PATH="$THERMAL_PROVENANCE_TEST_PATH"
export FOAM_API=2606
export WM_PROJECT_VERSION=v2606
export WM_OPTIONS=linux64GccDPInt32Opt
exec "$@"
'@
    [IO.File]::WriteAllText(
        $fakeLauncher,
        $fakeLauncherText,
        [Text.UTF8Encoding]::new($false))

    $fakeLauncherBash = Convert-ToGitBashPath $fakeLauncher
    $fakeBinBash = Convert-ToGitBashPath $fakeBin
    & $bashPath --login -c 'chmod +x "$1"' -- $fakeLauncherBash
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to make the fake OpenFOAM environment launcher executable."
    }

    $caseFingerprint = Get-TreeFingerprint $resolvedCase

    function Invoke-RejectedRunner {
        param(
            [Parameter(Mandatory = $true)][string]$Label,
            [Parameter(Mandatory = $true)][string]$ExpectedDiagnostic
        )
        Remove-Item -LiteralPath $environmentSentinel -Force `
            -ErrorAction SilentlyContinue

        $startInfo = [Diagnostics.ProcessStartInfo]::new()
        $startInfo.FileName = $bashPath
        $startInfo.UseShellExecute = $false
        $startInfo.RedirectStandardOutput = $true
        $startInfo.RedirectStandardError = $true
        Add-ProcessArgument $startInfo "--login"
        Add-ProcessArgument $startInfo (Convert-ToGitBashPath $runner)
        Add-ProcessArgument $startInfo "2"
        Add-ProcessArgument $startInfo "--multirate"
        Add-ProcessArgument $startInfo "1"
        $startInfo.Environment["OPENFOAM_LAUNCHER"] = $fakeLauncherBash
        $startInfo.Environment["THERMAL_PROVENANCE_ENV_SENTINEL"] = `
            Convert-ToGitBashPath $environmentSentinel
        $startInfo.Environment["THERMAL_PROVENANCE_TEST_PATH"] = `
            $fakeBinBash + ":/usr/bin:/bin"
        $startInfo.Environment["TMPDIR"] = Convert-ToGitBashPath $testRoot
        foreach ($name in @(
            "THERMAL_ONLY_OUTER_CORRECTORS",
            "THERMAL_SOLVER_SCRIPT_SNAPSHOT",
            "THERMAL_SOLVER_SCRIPT_SNAPSHOT_PATH",
            "THERMAL_SOLVER_CASE_DIR",
            "THERMAL_SOLVER_OPENFOAM_ENV_READY"
        )) {
            [void]$startInfo.Environment.Remove($name)
        }

        $process = [Diagnostics.Process]::new()
        $process.StartInfo = $startInfo
        Assert-Condition $process.Start() `
            "Unable to start the $Label generated-runner regression."
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        $process.WaitForExit()

        Assert-Condition ($process.ExitCode -eq 14) `
            "$Label binary returned $($process.ExitCode), expected 14. stdout=$stdout stderr=$stderr"
        Assert-Condition ($stdout.Contains(
            'Initializing OpenFOAM environment once with')) `
            "$Label binary was rejected before the environment setup path was exercised. stdout=$stdout"
        Assert-Condition ($stderr.Contains($ExpectedDiagnostic)) `
            "$Label binary did not emit the expected diagnostic. stderr=$stderr"
        Assert-Condition (Test-Path -LiteralPath $environmentSentinel -PathType Leaf) `
            "$Label binary did not pass through the fake OpenFOAM environment launcher."
        $environmentCalls = @(Get-Content -LiteralPath $environmentSentinel)
        Assert-Condition ($environmentCalls.Count -eq 1) `
            "$Label binary invoked the environment launcher $($environmentCalls.Count) times, expected once."
        Assert-Condition ((Get-TreeFingerprint $resolvedCase) -eq $caseFingerprint) `
            "$Label binary rejection changed the generated case tree."
        $snapshots = @(Get-ChildItem -LiteralPath $testRoot `
            -Filter "thermal-run-parallel.*" -File)
        Assert-Condition ($snapshots.Count -eq 0) `
            "$Label binary rejection left a script snapshot behind."
        $attestationTemps = @(Get-ChildItem -LiteralPath $testRoot `
            -Filter "thermal-solver-attestation.*" -File)
        Assert-Condition ($attestationTemps.Count -eq 0) `
            "$Label binary rejection left an attestation temporary file behind."
    }

    Invoke-RejectedRunner `
        -Label "missing" `
        -ExpectedDiagnostic "was not found as a regular, non-symlink executable file"

    $staleSolver = Join-Path $fakeBin "semiFrozenChtMultiRegionFoam"
    $staleSolverText = @'
#!/usr/bin/env bash
# Historical stale-policy evidence: Solving isothermal airflow region
exit 97
'@
    [IO.File]::WriteAllText(
        $staleSolver,
        $staleSolverText,
        [Text.UTF8Encoding]::new($false))
    $staleSolverBash = Convert-ToGitBashPath $staleSolver
    & $bashPath --login -c 'chmod +x "$1"' -- $staleSolverBash
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to make the stale custom solver fixture executable."
    }

    Invoke-RejectedRunner `
        -Label "stale" `
        -ExpectedDiagnostic "runtime attestation failed before case locking"

    $wrongSourceSolverText = @"
#!/usr/bin/env bash
printf '%s\n' 'THERMAL_SIM_SOLVER_ATTESTATION_V1 solver=semiFrozenChtMultiRegionFoam project_source_sha256=$('0' * 64) policy=$marker foam_api=2606 wm_project_version=v2606 wm_options=linux64GccDPInt32Opt'
exit 0
"@
    [IO.File]::WriteAllText(
        $staleSolver,
        $wrongSourceSolverText,
        [Text.UTF8Encoding]::new($false))
    & $bashPath --login -c 'chmod +x "$1"' -- $staleSolverBash
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to make the wrong-source custom solver fixture executable."
    }

    Invoke-RejectedRunner `
        -Label "wrong-source" `
        -ExpectedDiagnostic "runtime attestation failed before case locking"

    $validSha = $sourcePinMatch.Groups['sha'].Value
    $newlineStderrSolverText = @"
#!/usr/bin/env bash
printf '%s\n' 'THERMAL_SIM_SOLVER_ATTESTATION_V1 solver=semiFrozenChtMultiRegionFoam project_source_sha256=$validSha policy=$marker foam_api=2606 wm_project_version=v2606 wm_options=linux64GccDPInt32Opt'
printf '\n' >&2
exit 0
"@
    [IO.File]::WriteAllText(
        $staleSolver,
        $newlineStderrSolverText,
        [Text.UTF8Encoding]::new($false))
    & $bashPath --login -c 'chmod +x "$1"' -- $staleSolverBash
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to make the stderr-emitting solver fixture executable."
    }

    Invoke-RejectedRunner `
        -Label "newline-stderr" `
        -ExpectedDiagnostic "runtime attestation failed before case locking"

    Write-Host (
        "generated_runner_solver_provenance_test PASSED: " +
        "missingExit=14; staleExit=14; wrongSourceExit=14; newlineStderrExit=14; " +
        "environmentSetups=4; caseTreeWrites=0; runtimeHandshake=required; " +
        "projectSourceSha256=$($sourcePinMatch.Groups['sha'].Value); marker=$marker")
}
finally {
    if (Test-Path -LiteralPath $testRoot) {
        $cleanupTarget = [IO.Path]::GetFullPath($testRoot)
        Assert-Condition (
            $cleanupTarget.StartsWith(
                $tempBase, [StringComparison]::OrdinalIgnoreCase) -and
            ([IO.Path]::GetFileName($cleanupTarget)).StartsWith(
                "thermal_solver_provenance_test_", [StringComparison]::Ordinal)
        ) "Refusing unsafe generated-runner test cleanup: $cleanupTarget"
        Remove-Item -LiteralPath $cleanupTarget -Recurse -Force
    }
}
