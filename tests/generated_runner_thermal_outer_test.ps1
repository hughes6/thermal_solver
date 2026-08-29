param(
    [Parameter(Mandatory = $true)]
    [string]$CasePath
)

$ErrorActionPreference = "Stop"

function Assert-Condition {
    param(
        [Parameter(Mandatory = $true)]
        [bool]$Condition,
        [Parameter(Mandatory = $true)]
        [string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function Get-FileState {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return [pscustomobject]@{ Exists = $false; Length = 0L; Sha256 = "" }
    }
    $item = Get-Item -LiteralPath $Path
    return [pscustomobject]@{
        Exists = $true
        Length = $item.Length
        Sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    }
}

function Assert-FileStateUnchanged {
    param(
        [Parameter(Mandatory = $true)]$Before,
        [Parameter(Mandatory = $true)]$After,
        [Parameter(Mandatory = $true)][string]$Label
    )
    Assert-Condition ($Before.Exists -eq $After.Exists) `
        "$Label existence changed during rejected runner invocation."
    Assert-Condition ($Before.Length -eq $After.Length) `
        "$Label length changed during rejected runner invocation."
    Assert-Condition ($Before.Sha256 -eq $After.Sha256) `
        "$Label content changed during rejected runner invocation."
}

$resolvedCase = [IO.Path]::GetFullPath($CasePath)
$runner = Join-Path $resolvedCase "run_parallel.sh"
$solution = Join-Path $resolvedCase "system/fvSolution"
Assert-Condition (Test-Path -LiteralPath $runner -PathType Leaf) `
    "Generated runner was not found: $runner"
Assert-Condition (Test-Path -LiteralPath $solution -PathType Leaf) `
    "Generated fluid fvSolution was not found: $solution"

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
$gitInstallRoot = Split-Path (Split-Path $bashPath -Parent) -Parent
$gitToolPath = (Join-Path $gitInstallRoot "usr/bin") + ";" +
    (Join-Path $gitInstallRoot "mingw64/bin")

function Convert-ToGitBashPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    $converted = & $bashPath --login -c 'cygpath -u "$1"' -- ([IO.Path]::GetFullPath($Path))
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($converted)) {
        throw "Unable to convert path for Git Bash: $Path"
    }
    return ([string]($converted | Select-Object -First 1)).Trim()
}

$runnerText = Get-Content -LiteralPath $runner -Raw
$solutionText = Get-Content -LiteralPath $solution -Raw
Assert-Condition ($solutionText -match 'nOuterCorrectors\s+([1-9][0-9]*)\s*;') `
    "The generated fvSolution does not contain a positive live nOuterCorrectors value."
$liveOuterCorrectors = $Matches[1]

$overrideDeclaration = 'thermal_only_outer_correctors="${THERMAL_ONLY_OUTER_CORRECTORS:-'
$overrideValidationPrefix = 'THERMAL_ONLY_OUTER_CORRECTORS must be'
$minimumTwoValidation = 'THERMAL_ONLY_OUTER_CORRECTORS must be at least 2'
$environmentLaunch = 'Initializing OpenFOAM environment once with $foam_launcher.'
$runLockDeclaration = 'run_lock="$case_dir/.thermal_solver_run.lock"'
$restoreFunction = 'restore_live_outer_correctors()'
$restoreProductionFunction = 'restore_production_solver_controls()'
$restoreStateFunction = 'restore_run_state()'
$invalidateFunction = 'invalidate_airflow_acceptance_state()'
$terminateFunction = 'terminate_run()'
$bootstrapExitTrap = 'trap cleanup_script_snapshot EXIT'
$exitTrap = 'trap restore_run_state EXIT'
$interruptTrap = "trap 'terminate_run INT' INT"
$terminateTrap = "trap 'terminate_run TERM' TERM"
$thermalStageSelection = 'stage_outer_correctors="$thermal_only_outer_correctors"'
$liveStageSelection = "stage_outer_correctors=`"$liveOuterCorrectors`""
$literalLiveRestore = "PIMPLE/nOuterCorrectors -set $liveOuterCorrectors"
$rootSolutionOuterEdit = '"$case_dir/system/fvSolution" -entry PIMPLE/nOuterCorrectors'
$wrongRegionOuterEdit = '"$case_dir/system/fluid/fvSolution" -entry PIMPLE/nOuterCorrectors'

$declarationPosition = $runnerText.IndexOf($overrideDeclaration)
$validationPosition = $runnerText.IndexOf($overrideValidationPrefix)
$minimumTwoValidationPosition = $runnerText.IndexOf($minimumTwoValidation)
$environmentPosition = $runnerText.IndexOf($environmentLaunch)
$runLockPosition = $runnerText.IndexOf($runLockDeclaration)
Assert-Condition ($declarationPosition -ge 0) `
    "Generated runner does not declare THERMAL_ONLY_OUTER_CORRECTORS."
Assert-Condition ($validationPosition -gt $declarationPosition) `
    "Thermal-only outer-corrector validation is missing or precedes its declaration."
Assert-Condition ($environmentPosition -gt $validationPosition) `
    "Thermal-only outer-corrector validation must precede OpenFOAM environment launch."
Assert-Condition ($minimumTwoValidationPosition -gt $validationPosition -and
                  $environmentPosition -gt $minimumTwoValidationPosition) `
    "The explicit-override minimum-two guard must precede OpenFOAM environment launch."
Assert-Condition ($runLockPosition -gt $validationPosition) `
    "Thermal-only outer-corrector validation must precede case locking or writes."
Assert-Condition ($runnerText.Contains($thermalStageSelection)) `
    "Thermal-only stages do not use the validated runtime override."
Assert-Condition ($runnerText.Contains($liveStageSelection)) `
    "Live-flow stages do not explicitly restore the configured live outer-corrector count."
Assert-Condition (-not $runnerText.Contains($wrongRegionOuterEdit)) `
    "Outer-corrector commands target the fluid-region dictionary instead of the governing root fvSolution."
Assert-Condition (([regex]::Matches(
    $runnerText,
    [regex]::Escape($rootSolutionOuterEdit))).Count -ge 3) `
    "Stage, EXIT, and normal-completion outer-corrector edits must target system/fvSolution."

$trackedRunnerPosition = $runnerText.IndexOf('run_tracked()')
$trackedCapturePosition = $runnerText.IndexOf(
    'run_tracked_capture()', $trackedRunnerPosition + 1)
$terminatePosition = $runnerText.IndexOf(
    $terminateFunction, $trackedCapturePosition + 1)
$bootstrapExitTrapPosition = $runnerText.IndexOf(
    $bootstrapExitTrap, $terminatePosition + 1)
$interruptTrapPosition = $runnerText.IndexOf(
    $interruptTrap, $bootstrapExitTrapPosition + 1)
$terminateTrapPosition = $runnerText.IndexOf(
    $terminateTrap, $interruptTrapPosition + 1)
$restoreFunctionPosition = $runnerText.IndexOf(
    $restoreFunction, $terminateTrapPosition + 1)
$restoreProductionPosition = $runnerText.IndexOf(
    $restoreProductionFunction, $restoreFunctionPosition + 1)
$restoreStatePosition = $runnerText.IndexOf(
    $restoreStateFunction, $restoreProductionPosition + 1)
$invalidatePosition = $runnerText.IndexOf(
    $invalidateFunction, $restoreStatePosition + 1)
$exitTrapPosition = $runnerText.IndexOf($exitTrap, $invalidatePosition + 1)
Assert-Condition ($trackedRunnerPosition -ge 0 -and
                  $trackedCapturePosition -gt $trackedRunnerPosition -and
                  $terminatePosition -gt $trackedCapturePosition -and
                  $bootstrapExitTrapPosition -gt $terminatePosition -and
                  $interruptTrapPosition -gt $bootstrapExitTrapPosition -and
                  $terminateTrapPosition -gt $interruptTrapPosition -and
                  $restoreFunctionPosition -gt $terminateTrapPosition -and
                  $restoreProductionPosition -gt $restoreFunctionPosition -and
                  $restoreStatePosition -gt $restoreProductionPosition -and
                  $invalidatePosition -gt $restoreStatePosition -and
                  $exitTrapPosition -gt $invalidatePosition) `
    "Generated runner restoration functions or separate signal traps are missing or out of order."
$trackedRunnerText = $runnerText.Substring(
    $trackedRunnerPosition,
    $trackedCapturePosition - $trackedRunnerPosition)
$deferredIntPosition = $trackedRunnerText.IndexOf(
    "trap 'pending_termination_signal=INT' INT")
$deferredTermPosition = $trackedRunnerText.IndexOf(
    "trap 'pending_termination_signal=TERM' TERM", $deferredIntPosition + 1)
$setsidPosition = $trackedRunnerText.IndexOf('setsid -- "$@" &')
$jobControlFallbackPosition = $trackedRunnerText.IndexOf(
    'set -m', $setsidPosition + 1)
$jobControlResetPosition = $trackedRunnerText.IndexOf(
    'set +m', $jobControlFallbackPosition + 1)
$childRegistrationPosition = $trackedRunnerText.IndexOf(
    'active_child_pid=$!', $jobControlResetPosition + 1)
$restoredIntPosition = $trackedRunnerText.IndexOf(
    "trap 'terminate_run INT' INT", $childRegistrationPosition + 1)
$restoredTermPosition = $trackedRunnerText.IndexOf(
    "trap 'terminate_run TERM' TERM", $restoredIntPosition + 1)
$pendingDeliveryPosition = $trackedRunnerText.IndexOf(
    'terminate_run "$pending_termination_signal"', $restoredTermPosition + 1)
Assert-Condition ($deferredIntPosition -ge 0 -and
                  $deferredTermPosition -gt $deferredIntPosition -and
                  $setsidPosition -gt $deferredTermPosition -and
                  $jobControlFallbackPosition -gt $setsidPosition -and
                  $jobControlResetPosition -gt $jobControlFallbackPosition -and
                  $childRegistrationPosition -gt $jobControlResetPosition -and
                  $restoredIntPosition -gt $childRegistrationPosition -and
                  $restoredTermPosition -gt $restoredIntPosition -and
                  $pendingDeliveryPosition -gt $restoredTermPosition) `
    "run_tracked does not defer signals through child registration, restore terminating traps, and deliver a pending signal."
$restoreFunctionText = $runnerText.Substring(
    $restoreFunctionPosition,
    $restoreProductionPosition - $restoreFunctionPosition)
Assert-Condition ($restoreFunctionText.Contains($literalLiveRestore)) `
    "EXIT restoration does not set live nOuterCorrectors=$liveOuterCorrectors."
$restoreProductionText = $runnerText.Substring(
    $restoreProductionPosition,
    $restoreStatePosition - $restoreProductionPosition)
foreach ($requiredRestore in @(
    'restore_live_outer_correctors || true',
    'PIMPLE/frozenFlow -set false',
    'PIMPLE/semiFrozenFlow -set false',
    'PIMPLE/thermalOnlyFlow -set false',
    'PIMPLE/momentumPredictor -set true',
    'startFrom -set latestTime',
    'stopAt -set endTime',
    'adjustTimeStep -set true',
    '-entry deltaT -set ',
    'writeControl -set adjustableRunTime')) {
    Assert-Condition ($restoreProductionText.Contains($requiredRestore)) `
        "Production-state EXIT restoration is missing: $requiredRestore"
}
$restoreStateText = $runnerText.Substring(
    $restoreStatePosition,
    $invalidatePosition - $restoreStatePosition)
foreach ($bestEffortRestore in @(
    'restore_full_fan_options || true',
    'restore_production_solver_controls || true',
    'cleanup_script_snapshot || true')) {
    Assert-Condition ($restoreStateText.Contains($bestEffortRestore)) `
        "The EXIT trap state restoration is not best-effort for: $bestEffortRestore"
}
$terminateText = $runnerText.Substring(
    $terminatePosition,
    $bootstrapExitTrapPosition - $terminatePosition)
Assert-Condition ($terminateText.Contains('trap - EXIT') -and
    $terminateText.Contains("trap '' INT TERM") -and
                  $terminateText.Contains('declare -F restore_run_state') -and
                  $terminateText.Contains('cleanup_script_snapshot') -and
                  $terminateText.Contains('kill -s "$signal" -- "-$child_pid"') -and
                  $terminateText.Contains('kill -s "$signal" -- "$child_pid"') -and
                  $terminateText.Contains('exit "$status"')) `
    "Signal termination does not disable traps, restore state, and exit with the signal status."
$firstWatchdogPosition = $terminateText.IndexOf(
    'for ((attempt=0; attempt<50; ++attempt))')
$watchdogTermPosition = $terminateText.IndexOf(
    'kill -s TERM -- "-$child_pid"', $firstWatchdogPosition + 1)
$secondWatchdogPosition = $terminateText.IndexOf(
    'for ((attempt=0; attempt<20; ++attempt))', $watchdogTermPosition + 1)
$watchdogKillPosition = $terminateText.IndexOf(
    'kill -s KILL -- "-$child_pid"', $secondWatchdogPosition + 1)
$watchdogWaitPosition = $terminateText.IndexOf(
    'wait "$watchdog_pid"', $watchdogKillPosition + 1)
Assert-Condition ($firstWatchdogPosition -ge 0 -and
                  $watchdogTermPosition -gt $firstWatchdogPosition -and
                  $secondWatchdogPosition -gt $watchdogTermPosition -and
                  $watchdogKillPosition -gt $secondWatchdogPosition -and
                  $watchdogWaitPosition -gt $watchdogKillPosition -and
                  $terminateText.Contains(
                      'declare -F restore_preparation_controls')) `
    "terminate_run is missing its bounded group TERM/KILL watchdog or preparation-state restoration fallback."
Assert-Condition (-not $runnerText.Contains('trap restore_run_state EXIT INT TERM')) `
    "The generated runner still uses a non-terminating shared EXIT/INT/TERM trap."
Assert-Condition (([regex]::Matches(
    $runnerText,
    [regex]::Escape($literalLiveRestore))).Count -ge 2) `
    "The generated runner must restore the live outer-corrector count both on EXIT and normal completion."
$snapshotGatePosition = $runnerText.IndexOf(
    'if [[ "${THERMAL_SOLVER_SCRIPT_SNAPSHOT:-0}" != 1 ]]',
    $terminateTrapPosition)
$trackedPreparationPosition = $runnerText.IndexOf(
    'run_tracked bash "$case_dir/prepare_regions_low_memory.sh"',
    $snapshotGatePosition)
Assert-Condition ($snapshotGatePosition -gt $terminateTrapPosition -and
                  $trackedPreparationPosition -gt $snapshotGatePosition) `
    "Signal forwarding is not installed before snapshot and preparation work."
foreach ($trackedOperation in @(
    'run_tracked "$foam_launcher" decomposePar',
    'run_tracked "$foam_launcher" reconstructPar',
    'run_tracked "$foam_launcher" mpirun -np "$processes"',
    'run_tracked_capture postflight_output')) {
    Assert-Condition ($runnerText.Contains($trackedOperation)) `
        "Generated runner does not track/forward signals for: $trackedOperation"
}

$endpointFunctionPosition = $runnerText.IndexOf('require_exact_endpoint()')
$stageClassifierPosition = $runnerText.IndexOf(
    'classify_stage_interval()', $endpointFunctionPosition + 1)
Assert-Condition ($endpointFunctionPosition -ge 0 -and
                  $stageClassifierPosition -gt $endpointFunctionPosition) `
    "Generated runner endpoint/classifier helpers are missing or out of order."
$endpointFunctionText = $runnerText.Substring(
    $endpointFunctionPosition,
    $stageClassifierPosition - $endpointFunctionPosition)
Assert-Condition ($endpointFunctionText.Contains(
    'if(tolerance>1e-8)tolerance=1e-8')) `
    "Exact-endpoint tolerance is not capped at 1e-8 seconds."

$refreshWriterPosition = $runnerText.IndexOf('write_airflow_refresh_state()')
$refreshCommitterPosition = $runnerText.IndexOf(
    'commit_airflow_refresh_state()', $refreshWriterPosition + 1)
$refreshNormalizerPosition = $runnerText.IndexOf(
    'normalize_airflow_refresh_journal()', $refreshCommitterPosition + 1)
$refreshTempPosition = $runnerText.IndexOf(
    'temporary="${refresh_pending_marker}.tmp.$$"', $refreshWriterPosition)
$refreshPublishPosition = $runnerText.IndexOf(
    'mv -f -- "$temporary" "$refresh_pending_marker"', $refreshTempPosition)
$owedRefreshPosition = $runnerText.IndexOf(
    'write_airflow_refresh_state owed "$current" "$frozen_target"')
$thermalOnlyStagePosition = $runnerText.IndexOf(
    'stage true "$frozen_target"', $owedRefreshPosition)
Assert-Condition ($refreshWriterPosition -ge 0 -and
                  $refreshCommitterPosition -gt $refreshWriterPosition -and
                  $refreshNormalizerPosition -gt $refreshCommitterPosition -and
                  $refreshTempPosition -gt $refreshWriterPosition -and
                  $refreshPublishPosition -gt $refreshTempPosition) `
    "Atomic airflow-refresh journal helpers are missing or out of order."
$refreshCommitterText = $runnerText.Substring(
    $refreshCommitterPosition,
    $refreshNormalizerPosition - $refreshCommitterPosition)
foreach ($commitRequirement in @(
    'if [[ "$state" != owed ]]',
    '[[ ! "$start" =~ ^[0-9]+',
    '[[ ! "$target" =~ ^[0-9]+',
    'require_exact_endpoint "$start" "$expected_start"',
    'require_exact_endpoint "$target" "$expected_target"',
    'require_exact_endpoint "$actual" "$expected_target"',
    'write_airflow_refresh_state active "$actual"')) {
    Assert-Condition ($refreshCommitterText.Contains($commitRequirement)) `
        "Airflow-refresh commit helper is missing: $commitRequirement"
}
Assert-Condition ($owedRefreshPosition -ge 0 -and
                  $thermalOnlyStagePosition -gt $owedRefreshPosition) `
    "Thermal-only advancement is not preceded by an owed airflow-refresh journal write."
$stageFunctionPosition = $runnerText.IndexOf('stage()')
$stageEndpointPosition = $runnerText.IndexOf(
    'require_exact_endpoint "$actual_time" "$target"', $stageFunctionPosition)
$stageSourceRestorePosition = $runnerText.IndexOf(
    'if [[ "$thermal_only" == "true" && ', $stageEndpointPosition)
$stageRefreshCommitPosition = $runnerText.IndexOf(
    'commit_airflow_refresh_state "$actual_time" "$current" "$target"',
    $stageSourceRestorePosition)
$stagePrunePosition = $runnerText.IndexOf(
    'prune_processor_times', $stageRefreshCommitPosition)
$stageSummaryPosition = $runnerText.IndexOf(
    'summary "stage label=$label', $stageRefreshCommitPosition)
$stageCurrentCommitPosition = $runnerText.IndexOf(
    'current="$actual_time"', $stageRefreshCommitPosition)
Assert-Condition ($stageFunctionPosition -ge 0 -and
                  $stageEndpointPosition -gt $stageFunctionPosition -and
                  $stageSourceRestorePosition -gt $stageEndpointPosition -and
                  $stageRefreshCommitPosition -gt $stageSourceRestorePosition -and
                  $stagePrunePosition -gt $stageRefreshCommitPosition -and
                  $stageSummaryPosition -gt $stageRefreshCommitPosition -and
                  $stageCurrentCommitPosition -gt $stageSummaryPosition) `
    "stage() does not commit the exact owed airflow-refresh state after endpoint/source checks and before prune/summary/current advancement."
Assert-Condition ($runnerText.Contains(
    'An uncommitted thermal-only checkpoint advanced from') -and
                  $runnerText.Contains('refusing automatic recovery.') -and
                  -not $runnerText.Contains(
                      'Recovered completed or partial thermal-only checkpoint')) `
    "A progressed uncommitted owed journal does not fail closed."

$emptyInitialPosition = $runnerText.IndexOf(
    'if [[ -f "$initial_pending_marker" && ! -s "$initial_pending_marker" ]]')
$resumeInitialPosition = $runnerText.IndexOf(
    'if [[ -s "$initial_pending_marker" ]]', $emptyInitialPosition)
$newInitialPosition = $runnerText.IndexOf(
    'if [[ ! -f "$initial_pending_marker" ]]', $resumeInitialPosition)
$writeInitialPosition = $runnerText.IndexOf(
    'printf ''%s\n'' "$initial_start" > "$initial_pending_tmp"', $newInitialPosition)
$publishInitialPosition = $runnerText.IndexOf(
    'mv -f "$initial_pending_tmp" "$initial_pending_marker"', $writeInitialPosition)
Assert-Condition ($emptyInitialPosition -ge 0 -and
                  $resumeInitialPosition -gt $emptyInitialPosition -and
                  $newInitialPosition -gt $resumeInitialPosition -and
                  $writeInitialPosition -gt $newInitialPosition -and
                  $publishInitialPosition -gt $writeInitialPosition) `
    "Initial-airflow pending marker cleanup/resume/atomic publication is missing or out of order."

$warmCheckpointPreflightPosition = $runnerText.IndexOf(
    '        preflight_checkpoint_space || exit $?')
$warmInvalidationPosition = $runnerText.IndexOf(
    "        invalidate_airflow_acceptance_state`n", $warmCheckpointPreflightPosition)
$warmRampPosition = $runnerText.IndexOf(
    '        run_fan_ramp ', $warmInvalidationPosition)
$warmFunctionPosition = $runnerText.IndexOf('run_warm_start_windows()')
$warmCallPosition = $runnerText.IndexOf(
    "    run_warm_start_windows`n", $warmFunctionPosition)
Assert-Condition ($warmCheckpointPreflightPosition -ge 0 -and
                  $warmInvalidationPosition -gt $warmCheckpointPreflightPosition -and
                  $warmRampPosition -gt $warmInvalidationPosition -and
                  $warmFunctionPosition -gt $warmInvalidationPosition -and
                  $warmCallPosition -gt $warmFunctionPosition) `
    "Warm-start invalidation, optional ramp, bounded-window function, or invocation is missing or out of order."

$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$testRoot = [IO.Path]::GetFullPath((Join-Path $tempBase (
    "thermal_outer_runner_test_" + [guid]::NewGuid().ToString("N"))))
Assert-Condition ($testRoot.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase)) `
    "Unsafe generated-runner test directory: $testRoot"
[void](New-Item -ItemType Directory -Path $testRoot)

try {
    $sentinel = Join-Path $testRoot "openfoam-launcher-was-called.log"
    $fakeLauncher = Join-Path $testRoot "fake-openfoam-launcher.sh"
    $fakeLauncherText = @'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$THERMAL_OUTER_LAUNCH_SENTINEL"
exit 97
'@
    [IO.File]::WriteAllText(
        $fakeLauncher,
        $fakeLauncherText,
        [Text.UTF8Encoding]::new($false))

    $lockPath = Join-Path $resolvedCase ".thermal_solver_run.lock"
    $summaryPath = Join-Path $resolvedCase "run_summary.log"
    $lockBefore = Get-FileState $lockPath
    $summaryBefore = Get-FileState $summaryPath

    foreach ($unsafeOverride in @("0", "1")) {
        Remove-Item -LiteralPath $sentinel -Force -ErrorAction SilentlyContinue

        $startInfo = [Diagnostics.ProcessStartInfo]::new()
        $startInfo.FileName = $bashPath
        $startInfo.UseShellExecute = $false
        $startInfo.RedirectStandardOutput = $true
        $startInfo.RedirectStandardError = $true
        $startInfo.ArgumentList.Add("--login")
        $startInfo.ArgumentList.Add((Convert-ToGitBashPath $runner))
        $startInfo.ArgumentList.Add("2")
        $startInfo.ArgumentList.Add("--multirate")
        $startInfo.ArgumentList.Add("1")
        $startInfo.Environment["THERMAL_ONLY_OUTER_CORRECTORS"] = $unsafeOverride
        $startInfo.Environment["OPENFOAM_LAUNCHER"] = Convert-ToGitBashPath $fakeLauncher
        $startInfo.Environment["THERMAL_OUTER_LAUNCH_SENTINEL"] = Convert-ToGitBashPath $sentinel
        $startInfo.Environment["TMPDIR"] = Convert-ToGitBashPath $testRoot
        [void]$startInfo.Environment.Remove("THERMAL_SOLVER_SCRIPT_SNAPSHOT")
        [void]$startInfo.Environment.Remove("THERMAL_SOLVER_SCRIPT_SNAPSHOT_PATH")
        [void]$startInfo.Environment.Remove("THERMAL_SOLVER_CASE_DIR")
        [void]$startInfo.Environment.Remove("THERMAL_SOLVER_OPENFOAM_ENV_READY")

        $process = [Diagnostics.Process]::new()
        $process.StartInfo = $startInfo
        Assert-Condition $process.Start() "Unable to start the generated runner under Git Bash."
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        $process.WaitForExit()

        Assert-Condition ($process.ExitCode -eq 2) `
            "Unsafe override $unsafeOverride returned $($process.ExitCode), expected 2. stdout=$stdout stderr=$stderr"
        $expectedDiagnostic = if ($unsafeOverride -eq "0") {
            'THERMAL_ONLY_OUTER_CORRECTORS must be a positive integer.'
        }
        else {
            $minimumTwoValidation
        }
        Assert-Condition ($stderr.Contains($expectedDiagnostic)) `
            "Unsafe override $unsafeOverride did not emit the expected validation message. stderr=$stderr"
        Assert-Condition (-not (Test-Path -LiteralPath $sentinel)) `
            "The OpenFOAM launcher was called before override $unsafeOverride was rejected."
        Assert-FileStateUnchanged $lockBefore (Get-FileState $lockPath) "Run lock"
        Assert-FileStateUnchanged $summaryBefore (Get-FileState $summaryPath) "Run summary"
        $snapshots = @(Get-ChildItem -LiteralPath $testRoot -Filter "thermal-run-parallel.*" -File)
        Assert-Condition ($snapshots.Count -eq 0) `
            "Rejected override $unsafeOverride left a script snapshot behind."
    }

    # Execute the generated EXIT-restoration functions themselves with a
    # sentinel launcher. This exercises the trap path without running any
    # OpenFOAM utility or touching the case dictionaries.
    Remove-Item -LiteralPath $sentinel -Force -ErrorAction SilentlyContinue
    $restoreHarness = Join-Path $testRoot "exercise-generated-restore-trap.sh"
    $restoreHarnessText = @"
#!/usr/bin/env bash
set -euo pipefail
case_dir="`$THERMAL_OUTER_TEST_CASE"
foam_launcher="`$THERMAL_OUTER_TEST_LAUNCHER"
restore_full_fan_options()
{
    :
}
cleanup_script_snapshot()
{
    :
}
$restoreFunctionText
$restoreProductionText
$restoreStateText
trap restore_run_state EXIT
exit 0
"@
    [IO.File]::WriteAllText(
        $restoreHarness,
        $restoreHarnessText,
        [Text.UTF8Encoding]::new($false))

    $restoreStartInfo = [Diagnostics.ProcessStartInfo]::new()
    $restoreStartInfo.FileName = $bashPath
    $restoreStartInfo.UseShellExecute = $false
    $restoreStartInfo.RedirectStandardOutput = $true
    $restoreStartInfo.RedirectStandardError = $true
    $restoreStartInfo.ArgumentList.Add((Convert-ToGitBashPath $restoreHarness))
    $restoreStartInfo.Environment["PATH"] = $gitToolPath + ";" +
        $restoreStartInfo.Environment["PATH"]
    $restoreStartInfo.Environment["THERMAL_OUTER_TEST_CASE"] = Convert-ToGitBashPath $resolvedCase
    $restoreStartInfo.Environment["THERMAL_OUTER_TEST_LAUNCHER"] = Convert-ToGitBashPath $fakeLauncher
    $restoreStartInfo.Environment["THERMAL_OUTER_LAUNCH_SENTINEL"] = Convert-ToGitBashPath $sentinel

    $restoreProcess = [Diagnostics.Process]::new()
    $restoreProcess.StartInfo = $restoreStartInfo
    Assert-Condition $restoreProcess.Start() "Unable to execute the generated EXIT-restoration trap."
    $restoreStdout = $restoreProcess.StandardOutput.ReadToEnd()
    $restoreStderr = $restoreProcess.StandardError.ReadToEnd()
    $restoreProcess.WaitForExit()
    Assert-Condition ($restoreProcess.ExitCode -eq 0) `
        "Generated EXIT-restoration harness returned $($restoreProcess.ExitCode), expected 0. stdout=$restoreStdout stderr=$restoreStderr"
    Assert-Condition (Test-Path -LiteralPath $sentinel -PathType Leaf) `
        "Generated EXIT trap did not invoke the live outer-corrector restoration command."
    $restoreCalls = @(Get-Content -LiteralPath $sentinel)
    Assert-Condition ($restoreCalls.Count -eq 14) `
        "Generated EXIT trap invoked $($restoreCalls.Count) production-control restoration commands, expected 14."
    $expectedRestoreCall = "foamDictionary -precision 17 " +
        (Convert-ToGitBashPath $solution) +
        " -entry PIMPLE/nOuterCorrectors -set $liveOuterCorrectors"
    Assert-Condition ($restoreCalls[0] -eq $expectedRestoreCall) `
        "Generated EXIT trap issued the wrong first restore command. actual=$($restoreCalls[0]) expected=$expectedRestoreCall"
    foreach ($requiredCommand in @(
        'PIMPLE/frozenFlow -set false',
        'PIMPLE/semiFrozenFlow -set false',
        'PIMPLE/thermalOnlyFlow -set false',
        'PIMPLE/momentumPredictor -set true',
        'startFrom -set latestTime',
        'stopAt -set endTime',
        'endTime -set ',
        'adjustTimeStep -set true',
        'maxCo -set ',
        'maxDeltaT -set ',
        '-entry deltaT -set ',
        'writeControl -set adjustableRunTime',
        'writeInterval -set ')) {
        Assert-Condition (($restoreCalls | Where-Object {
            $_.Contains($requiredCommand)
        }).Count -eq 1) `
            "Generated EXIT trap did not issue exactly one production restore containing: $requiredCommand"
    }
    Assert-FileStateUnchanged $lockBefore (Get-FileState $lockPath) "Run lock"
    Assert-FileStateUnchanged $summaryBefore (Get-FileState $summaryPath) "Run summary"

    Write-Host (
        "generated_runner_thermal_outer_test PASSED: overrides=0,1; exit=2; " +
        "prevalidationLauncherCalls=0; caseWrites=0; exitTrapRestoreCalls=14; " +
        "liveOuterCorrectors=$liveOuterCorrectors")
}
finally {
    if (Test-Path -LiteralPath $testRoot) {
        $cleanupTarget = [IO.Path]::GetFullPath($testRoot)
        Assert-Condition (
            $cleanupTarget.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -and
            ([IO.Path]::GetFileName($cleanupTarget)).StartsWith(
                "thermal_outer_runner_test_", [StringComparison]::Ordinal)) `
            "Refusing unsafe generated-runner test cleanup: $cleanupTarget"
        Remove-Item -LiteralPath $cleanupTarget -Recurse -Force
    }
}
