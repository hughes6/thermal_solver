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
    if (-not $Condition) { throw $Message }
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
    Assert-Condition ($Before.Exists -eq $After.Exists) "$Label existence changed."
    Assert-Condition ($Before.Length -eq $After.Length) "$Label length changed."
    Assert-Condition ($Before.Sha256 -eq $After.Sha256) "$Label content changed."
}

function Get-DirectoryContentState {
    param([Parameter(Mandatory = $true)][string]$Path)
    $root = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $records = foreach ($item in Get-ChildItem -LiteralPath $root -File -Recurse | Sort-Object FullName) {
        $relative = $item.FullName.Substring($root.Length).TrimStart('\', '/')
        "$relative`t$($item.Length)`t$((Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash)"
    }
    return ($records -join "`n")
}

$resolvedCase = [IO.Path]::GetFullPath($CasePath)
$runner = Join-Path $resolvedCase "run_parallel.sh"
Assert-Condition (Test-Path -LiteralPath $runner -PathType Leaf) `
    "Generated runner was not found: $runner"
$runnerText = Get-Content -LiteralPath $runner -Raw
$caseTreeBefore = Get-DirectoryContentState $resolvedCase

$bashCandidates = [Collections.Generic.List[string]]::new()
if ($env:GIT_BASH) { $bashCandidates.Add($env:GIT_BASH) }
$gitCommand = Get-Command git -ErrorAction SilentlyContinue
if ($null -ne $gitCommand) {
    $gitRoot = Split-Path (Split-Path $gitCommand.Source -Parent) -Parent
    $bashCandidates.Add((Join-Path $gitRoot "bin/bash.exe"))
}
if ($env:ProgramFiles) {
    $bashCandidates.Add((Join-Path $env:ProgramFiles "Git/bin/bash.exe"))
}
$bashPath = $bashCandidates |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
Assert-Condition (-not [string]::IsNullOrWhiteSpace($bashPath)) `
    "Git Bash is required for the generated timestep-policy regression."
$bashPath = [IO.Path]::GetFullPath($bashPath)

function Convert-ToGitBashPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    $converted = & $bashPath --login -c 'cygpath -u "$1"' -- ([IO.Path]::GetFullPath($Path))
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($converted)) {
        throw "Unable to convert path for Git Bash: $Path"
    }
    return ([string]($converted | Select-Object -First 1)).Trim()
}

$syntaxInfo = [Diagnostics.ProcessStartInfo]::new()
$syntaxInfo.FileName = $bashPath
$syntaxInfo.UseShellExecute = $false
$syntaxInfo.RedirectStandardOutput = $true
$syntaxInfo.RedirectStandardError = $true
$syntaxInfo.ArgumentList.Add("--login")
$syntaxInfo.ArgumentList.Add("-n")
$syntaxInfo.ArgumentList.Add((Convert-ToGitBashPath $runner))
$syntaxProcess = [Diagnostics.Process]::new()
$syntaxProcess.StartInfo = $syntaxInfo
Assert-Condition $syntaxProcess.Start() "Unable to run generated-runner Bash syntax check."
$syntaxStdout = $syntaxProcess.StandardOutput.ReadToEnd()
$syntaxStderr = $syntaxProcess.StandardError.ReadToEnd()
$syntaxProcess.WaitForExit()
Assert-Condition ($syntaxProcess.ExitCode -eq 0) `
    "Generated runner failed Bash syntax validation. stdout=$syntaxStdout stderr=$syntaxStderr"

# Static structural gates bind the generated script to the fixed-step policy.
foreach ($required in @(
    'mode="${2:---multirate}"',
    'Conventional run mode is disabled for this multirate export',
    '-entry adjustTimeStep -set false',
    '-entry maxDeltaT -set "$ramp_dt"',
    '-entry writeInterval -set "$ramp_steps"',
    'echo "$label Courant limit exceeded or unavailable',
    'actual>=0 && actual<=limit*1.001',
    'if fan_ramp_endpoint_reached "$ramp_current"; then',
    'full scale remains pending',
    'interval_relation=$(classify_stage_interval',
    'Refusing reversed stage target',
    'Missing fan-ramp time metadata for processor${rank}',
    'Missing stage time metadata for processor${rank}',
    'Missing warm-start time metadata for processor${rank}',
    'multirate_pause_reason()',
    'printf ''%s\n'' fan_ramp_pending',
    'printf ''%s\n'' initial_airflow_pending',
    'warm_restart_plan=$(awk -v maximum="$warm_start_maximum_time_step"',
    'warm_window_target=$(next_warm_start_window',
    'run_warm_start_windows()',
    'validate_latest_airflow_courant()',
    'validate_interrupted_fan_ramp_checkpoint()',
    'validate_interrupted_fan_ramp_checkpoint "$warm_current"',
    'fan_ramp_restart_validated=true',
    'set_fan_scale 1',
    'touch "$fan_ramp_complete_marker" || return $?',
    'fan_ramp_restart_recovered time=$actual',
    'refusing unvalidated recovery',
    'validate_latest_airflow_courant "$warm_current" "Warm-start restart checkpoint"',
    'restart Courant validation passed',
    'validate_latest_airflow_courant "$warm_window_target"',
    'validate_latest_airflow_courant "$target"',
    'summary "warm_start_window index=$warm_window_index',
    '-entry maxDeltaT -set "$warm_restart_dt"',
    '-entry writeInterval -set "$warm_restart_steps"',
    'write_airflow_refresh_state()',
    'commit_airflow_refresh_state()',
    'normalize_airflow_refresh_journal()',
    'write_airflow_refresh_state owed "$current" "$frozen_target"',
    'write_airflow_refresh_state active "$current"',
    'terminate_run()',
    'trap ''terminate_run TERM'' TERM',
    'run_tracked()',
    'run_tracked_capture()',
    'run_tracked "$foam_launcher" mpirun',
    'pending_termination_signal=""',
    'trap ''pending_termination_signal=TERM'' TERM',
    'set -m',
    'set +m',
    'kill -s "$signal" -- "-$child_pid"',
    'for ((attempt=0; attempt<50; ++attempt))',
    'for ((attempt=0; attempt<20; ++attempt))',
    'kill -s KILL -- "-$child_pid"',
    'wait "$watchdog_pid"',
    'trap '''' INT TERM',
    'terminal airflow validation remains pending'
)) {
    Assert-Condition ($runnerText.Contains($required)) `
        "Generated runner is missing timestep-policy fragment: $required"
}
Assert-Condition (-not $runnerText.Contains('terminal_requested_end')) `
    "Generated runner still extends requested_end for terminal validation."
Assert-Condition (-not $runnerText.Contains(
    'validate_interrupted_fan_ramp_checkpoint "$warm_current" ||')) `
    "Warm-start fan-ramp checkpoint validation remains in an errexit-suppressing OR-list."
Assert-Condition (-not $runnerText.Contains(
    'validate_latest_airflow_courant "$warm_current" "Warm-start restart checkpoint" ||')) `
    "Warm-start Courant validation remains in an errexit-suppressing OR-list."
Assert-Condition (-not $runnerText.Contains(
    'validate_latest_airflow_courant "$warm_window_target" "Warm-start window $warm_window_index" ||')) `
    "Warm-window Courant validation remains in an errexit-suppressing OR-list."
Assert-Condition ($runnerText.Contains(
    'finite=(v==v && v-v==0)')) `
    "Warm-start timestep validation does not reject non-finite exponents."

$rampCall = $runnerText.IndexOf('run_fan_ramp chtMultiRegionFoam')
$warmPlan = $runnerText.IndexOf(
    'warm_restart_plan=$(awk -v maximum="$warm_start_maximum_time_step"',
    [Math]::Max(0, $rampCall))
Assert-Condition ($rampCall -ge 0 -and $warmPlan -gt $rampCall) `
    "Warm-start timing is not replanned after the optional fan ramp."

$owedJournalIndex = $runnerText.IndexOf(
    'write_airflow_refresh_state owed "$current" "$frozen_target"')
$thermalStageIndex = $runnerText.IndexOf(
    'stage true "$frozen_target"', [Math]::Max(0, $owedJournalIndex))
Assert-Condition ($owedJournalIndex -ge 0 -and $thermalStageIndex -gt $owedJournalIndex) `
    "Thermal-only advancement is not journaled as owing an airflow refresh before the stage call."

# Exercise the exact symmetric endpoint predicate copied from the generated
# runner. This catches both an undershoot and an overshoot, not just text drift.
$endpointMatch = [regex]::Match(
    $runnerText,
    '(?ms)^require_exact_endpoint\(\)\r?\n\{\r?\n.*?^\}\r?\n')
Assert-Condition $endpointMatch.Success `
    "Unable to extract require_exact_endpoint from the generated runner."

function Get-BashFunctionText {
    param([Parameter(Mandatory = $true)][string]$Name)
    $escaped = [regex]::Escape($Name)
    $match = [regex]::Match(
        $runnerText,
        ('(?ms)^[ \t]*' + $escaped +
         '\(\)\r?\n[ \t]*\{\r?\n.*?^[ \t]*\}\r?\n'))
    Assert-Condition $match.Success "Unable to extract $Name from the generated runner."
    return $match.Value
}

$fanRampEndpointFunction = Get-BashFunctionText 'fan_ramp_endpoint_reached'
$fanRampPendingFunction = Get-BashFunctionText 'fan_ramp_pending_at'
$stageIntervalFunction = Get-BashFunctionText 'classify_stage_interval'
$stageFunction = Get-BashFunctionText 'stage'
$nextWarmWindowFunction = Get-BashFunctionText 'next_warm_start_window'
$validateLatestAirflowCourantFunction = Get-BashFunctionText 'validate_latest_airflow_courant'
$validateInterruptedFanRampFunction = Get-BashFunctionText 'validate_interrupted_fan_ramp_checkpoint'
$runFanRampFunction = Get-BashFunctionText 'run_fan_ramp'
$runWarmStartWindowsFunction = Get-BashFunctionText 'run_warm_start_windows'
$pauseReasonFunction = Get-BashFunctionText 'multirate_pause_reason'
$writeRefreshStateFunction = Get-BashFunctionText 'write_airflow_refresh_state'
$commitRefreshStateFunction = Get-BashFunctionText 'commit_airflow_refresh_state'
$normalizeRefreshJournalFunction = Get-BashFunctionText 'normalize_airflow_refresh_journal'
$runTrackedFunction = Get-BashFunctionText 'run_tracked'
$runTrackedCaptureFunction = Get-BashFunctionText 'run_tracked_capture'
$terminateRunFunction = Get-BashFunctionText 'terminate_run'
Assert-Condition (-not $validateInterruptedFanRampFunction.Contains(
    'set_fan_scale 1 ||')) `
    "Fan-ramp recovery scale publication remains in an errexit-suppressing OR-list."

$stageEndpointIndex = $stageFunction.IndexOf(
    'require_exact_endpoint "$actual_time" "$target"')
$stageSourceRestoreIndex = $stageFunction.IndexOf(
    'for field in U p p_rgh phi rho k omega nut alphat; do')
$stageRefreshCommitIndex = $stageFunction.IndexOf(
    'commit_airflow_refresh_state "$actual_time" "$current" "$target"')
$stagePruneIndex = $stageFunction.IndexOf(
    'prune_processor_times', [Math]::Max(0, $stageRefreshCommitIndex))
Assert-Condition (
    $stageEndpointIndex -ge 0 -and
    $stageSourceRestoreIndex -gt $stageEndpointIndex -and
    $stageRefreshCommitIndex -gt $stageSourceRestoreIndex -and
    $stagePruneIndex -gt $stageRefreshCommitIndex) `
    "The stage transaction does not commit active airflow refresh after endpoint/source restoration checks and before pruning."

$warmInvalidationIndex = $runnerText.IndexOf(
    'invalidate_airflow_acceptance_state', $runnerText.IndexOf('warm_current=$(latest_processor_restart_time)'))
$warmFanRampValidationIndex = $runnerText.IndexOf(
    'validate_interrupted_fan_ramp_checkpoint "$warm_current"',
    [Math]::Max(0, $warmInvalidationIndex))
$warmRestartValidationIndex = $runnerText.IndexOf(
    'validate_latest_airflow_courant "$warm_current" "Warm-start restart checkpoint"',
    [Math]::Max(0, $warmFanRampValidationIndex))
$warmEqualAcceptanceIndex = $runnerText.IndexOf(
    'if [[ "$warm_restart_relation" == equal ]]',
    [Math]::Max(0, $warmRestartValidationIndex))
$warmRampDecisionIndex = $runnerText.IndexOf(
    '[[ "$mode" == "--warm-start" ]] &&',
    [Math]::Max(0, $warmEqualAcceptanceIndex))
$warmWindowCallIndex = $runnerText.IndexOf(
    "`n    run_warm_start_windows`n",
    [Math]::Max(0, $warmRampDecisionIndex))
Assert-Condition (
    $warmInvalidationIndex -ge 0 -and
    $warmFanRampValidationIndex -gt $warmInvalidationIndex -and
    $warmRestartValidationIndex -gt $warmFanRampValidationIndex -and
    $warmEqualAcceptanceIndex -gt $warmRestartValidationIndex -and
    $warmRampDecisionIndex -gt $warmEqualAcceptanceIndex -and
    $warmWindowCallIndex -gt $warmRampDecisionIndex) `
    "Nonzero warm-start checkpoints are not Courant-revalidated before equal-endpoint acceptance, ramp skipping, and warm-window advancement."

$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$testRoot = [IO.Path]::GetFullPath((Join-Path $tempBase (
    "timestep_policy_runner_test_" + [guid]::NewGuid().ToString("N"))))
Assert-Condition ($testRoot.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase)) `
    "Unsafe generated-runner test directory: $testRoot"
[void](New-Item -ItemType Directory -Path $testRoot)

try {
    $endpointHarness = Join-Path $testRoot "endpoint-harness.sh"
    $endpointHarnessText = "#!/usr/bin/env bash`nset -uo pipefail`n" +
        $endpointMatch.Value +
        'require_exact_endpoint "$1" "$2" "Synthetic endpoint"' + "`n"
    [IO.File]::WriteAllText(
        $endpointHarness,
        $endpointHarnessText,
        [Text.UTF8Encoding]::new($false))

    $endpointCases = @(
        @{ Actual = "1"; Target = "1"; Expected = 0 },
        @{ Actual = "1.0000000005"; Target = "1"; Expected = 0 },
        @{ Actual = "0.999999"; Target = "1"; Expected = 5 },
        @{ Actual = "1.000001"; Target = "1"; Expected = 5 },
        @{ Actual = "17999.999"; Target = "18000"; Expected = 5 },
        @{ Actual = "18000.001"; Target = "18000"; Expected = 5 },
        @{ Actual = "730000"; Target = "730000"; Expected = 0 },
        @{ Actual = "729999.9995"; Target = "730000"; Expected = 5 }
    )
    foreach ($case in $endpointCases) {
        $startInfo = [Diagnostics.ProcessStartInfo]::new()
        $startInfo.FileName = $bashPath
        $startInfo.UseShellExecute = $false
        $startInfo.RedirectStandardOutput = $true
        $startInfo.RedirectStandardError = $true
        $startInfo.ArgumentList.Add("--login")
        $startInfo.ArgumentList.Add((Convert-ToGitBashPath $endpointHarness))
        $startInfo.ArgumentList.Add($case.Actual)
        $startInfo.ArgumentList.Add($case.Target)
        $process = [Diagnostics.Process]::new()
        $process.StartInfo = $startInfo
        Assert-Condition $process.Start() "Unable to start endpoint harness."
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        Assert-Condition ($process.ExitCode -eq $case.Expected) `
            "Endpoint actual=$($case.Actual) target=$($case.Target) returned $($process.ExitCode), expected $($case.Expected). stdout=$stdout stderr=$stderr"
        if ($case.Expected -ne 0) {
            Assert-Condition ($stderr.Contains('failed to reach its exact endpoint')) `
                "Rejected endpoint did not emit the exact-endpoint diagnostic."
        }
    }

    # Execute the actual generated ramp function against a stateful fake
    # OpenFOAM launcher. The first request ends inside the ramp and must not
    # promote full fan scale or publish completion; the continuation must
    # resume from that checkpoint and finish the ramp.
    $rampCase = Join-Path $testRoot "ramp-case"
    [void](New-Item -ItemType Directory -Path $rampCase)
    $rampState = Join-Path $testRoot "ramp-state.txt"
    $rampTarget = Join-Path $testRoot "ramp-target.txt"
    $rampScales = Join-Path $testRoot "ramp-scales.txt"
    $rampLauncher = Join-Path $testRoot "fake-ramp-launcher.sh"
    $rampLauncherText = @'
#!/usr/bin/env bash
set -euo pipefail
if [[ "$1" == foamDictionary ]]; then
    args=("$@")
    for ((i=0; i+3<${#args[@]}; ++i)); do
        if [[ "${args[$i]}" == -entry && "${args[$((i+1))]}" == endTime && "${args[$((i+2))]}" == -set ]]; then
            printf '%s\n' "${args[$((i+3))]}" > "$RAMP_TARGET_FILE"
        fi
    done
    exit 0
fi
if [[ "$1" == mpirun ]]; then
    for argument in "$@"; do
        if [[ "$argument" == postProcess ]]; then
            printf 'max(Co) = 0.1\n'
            exit 0
        fi
    done
    cp "$RAMP_TARGET_FILE" "$RAMP_STATE_FILE"
    target=$(awk 'NF { value=$1 } END { print value }' "$RAMP_TARGET_FILE")
    for rank in 0 1; do
        mkdir -p "$RAMP_CASE_DIR/processor${rank}/${target}/uniform"
        : > "$RAMP_CASE_DIR/processor${rank}/${target}/uniform/time"
    done
    exit 0
fi
exit 0
'@
    [IO.File]::WriteAllText(
        $rampLauncher,
        $rampLauncherText,
        [Text.UTF8Encoding]::new($false))
    $rampHarness = Join-Path $testRoot "ramp-harness.sh"
    $rampHarnessText = "#!/usr/bin/env bash`nset -euo pipefail`n" +
        $endpointMatch.Value +
        $fanRampEndpointFunction +
        $fanRampPendingFunction +
        $runTrackedFunction +
        $runTrackedCaptureFunction +
        $validateLatestAirflowCourantFunction +
        $runFanRampFunction + @'
case_dir="$RAMP_CASE_DIR"
foam_launcher="$RAMP_LAUNCHER"
processes=2
active_child_pid=""
active_child_uses_group=false
tracked_capture_path=""
fan_startup_ramp_time=0.050000000000000003
fan_ramp_complete_marker="$case_dir/.fan_ramp_complete"
full_fan_options="$case_dir/fvOptions.fullFan"
touch "$full_fan_options"
printf '0\n' > "$RAMP_STATE_FILE"
: > "$RAMP_SCALE_FILE"
latest_processor_restart_time() { awk 'NF { value=$1 } END { print value }' "$RAMP_STATE_FILE"; }
set_fan_scale() { printf '%s\n' "$1" >> "$RAMP_SCALE_FILE"; }
prune_processor_times() { :; }
summary() { :; }

run_fan_ramp fakeSolver 0 0.01
require_exact_endpoint "$ramp_current" 0.01 "Partial ramp"
[[ ! -e "$fan_ramp_complete_marker" ]]
if grep -qx '1' "$RAMP_SCALE_FILE"; then
    echo "Partial ramp incorrectly promoted full fan scale." >&2
    exit 31
fi

run_fan_ramp fakeSolver "$ramp_current" "$fan_startup_ramp_time"
require_exact_endpoint "$ramp_current" "$fan_startup_ramp_time" "Resumed ramp"
[[ -f "$fan_ramp_complete_marker" ]]
[[ "$(tail -n 1 "$RAMP_SCALE_FILE")" == 1 ]]
'@
    [IO.File]::WriteAllText(
        $rampHarness,
        $rampHarnessText,
        [Text.UTF8Encoding]::new($false))
    $rampInfo = [Diagnostics.ProcessStartInfo]::new()
    $rampInfo.FileName = $bashPath
    $rampInfo.UseShellExecute = $false
    $rampInfo.RedirectStandardOutput = $true
    $rampInfo.RedirectStandardError = $true
    $rampInfo.ArgumentList.Add("--login")
    $rampInfo.ArgumentList.Add((Convert-ToGitBashPath $rampHarness))
    $rampInfo.Environment["RAMP_CASE_DIR"] = Convert-ToGitBashPath $rampCase
    $rampInfo.Environment["RAMP_LAUNCHER"] = Convert-ToGitBashPath $rampLauncher
    $rampInfo.Environment["RAMP_STATE_FILE"] = Convert-ToGitBashPath $rampState
    $rampInfo.Environment["RAMP_TARGET_FILE"] = Convert-ToGitBashPath $rampTarget
    $rampInfo.Environment["RAMP_SCALE_FILE"] = Convert-ToGitBashPath $rampScales
    $rampProcess = [Diagnostics.Process]::new()
    $rampProcess.StartInfo = $rampInfo
    Assert-Condition $rampProcess.Start() "Unable to execute ramp transition harness."
    $rampStdout = $rampProcess.StandardOutput.ReadToEnd()
    $rampStderr = $rampProcess.StandardError.ReadToEnd()
    $rampProcess.WaitForExit()
    Assert-Condition ($rampProcess.ExitCode -eq 0) `
        "Ramp transition harness failed with $($rampProcess.ExitCode). stdout=$rampStdout stderr=$rampStderr"

    # Exercise the generated final-state classifier. Initial convergence is
    # authoritative; mapped/no-ramp starts still require adaptive acceptance,
    # stale pending auxiliaries cannot override acceptance, and refresh pending
    # always prevents completion.
    $stateCase = Join-Path $testRoot "classifier-case"
    [void](New-Item -ItemType Directory -Path $stateCase)
    $stateHarness = Join-Path $testRoot "classifier-harness.sh"
    $stateHarnessText = "#!/usr/bin/env bash`nset -euo pipefail`n" +
        $fanRampPendingFunction +
        $pauseReasonFunction + @'
case_dir="$CLASSIFIER_CASE_DIR"
initial_convergence_marker="$case_dir/.initial_airflow_converged"
initial_pending_marker="$case_dir/.initial_airflow_pending"
initial_exchange_state="$case_dir/.initial_air_exchange_state"
initial_physical_settling_marker="$case_dir/.initial_airflow_physical_settling"
refresh_pending_marker="$case_dir/.airflow_refresh_pending"
mapped_state_marker="$case_dir/.mapped_initial_state"
fan_startup_ramp_time=0.05
fan_startup_ramp_enabled=true

assert_reason() {
    local actual="$1" expected="$2" reason
    reason=$(multirate_pause_reason "$actual")
    [[ "$reason" == "$expected" ]] || {
        echo "actual=$actual expected=$expected observed=$reason" >&2
        exit 41
    }
}

assert_reason 0.01 fan_ramp_pending
assert_reason 0.05 initial_airflow_pending
touch "$mapped_state_marker"
assert_reason 0 initial_airflow_pending
rm -f "$mapped_state_marker"
fan_startup_ramp_enabled=false
assert_reason 0 initial_airflow_pending
touch "$initial_convergence_marker" "$initial_pending_marker"
if reason=$(multirate_pause_reason 0); then
    echo "Accepted initial airflow was falsely classified as pending: $reason" >&2
    exit 42
fi
touch "$refresh_pending_marker"
assert_reason 0 airflow_refresh_pending
'@
    [IO.File]::WriteAllText(
        $stateHarness,
        $stateHarnessText,
        [Text.UTF8Encoding]::new($false))
    $stateInfo = [Diagnostics.ProcessStartInfo]::new()
    $stateInfo.FileName = $bashPath
    $stateInfo.UseShellExecute = $false
    $stateInfo.RedirectStandardOutput = $true
    $stateInfo.RedirectStandardError = $true
    $stateInfo.ArgumentList.Add("--login")
    $stateInfo.ArgumentList.Add((Convert-ToGitBashPath $stateHarness))
    $stateInfo.Environment["CLASSIFIER_CASE_DIR"] = Convert-ToGitBashPath $stateCase
    $stateProcess = [Diagnostics.Process]::new()
    $stateProcess.StartInfo = $stateInfo
    Assert-Condition $stateProcess.Start() "Unable to execute state-classifier harness."
    $stateStdout = $stateProcess.StandardOutput.ReadToEnd()
    $stateStderr = $stateProcess.StandardError.ReadToEnd()
    $stateProcess.WaitForExit()
    Assert-Condition ($stateProcess.ExitCode -eq 0) `
        "State-classifier harness failed with $($stateProcess.ExitCode). stdout=$stateStdout stderr=$stateStderr"

    # Execute the generated bounded-window planner over a non-divisible
    # interval. A 0.25 s request with a 0.1 s window must produce three exact,
    # strictly advancing checkpoints: 0.1, 0.2, and 0.25.
    $windowHarness = Join-Path $testRoot "window-harness.sh"
    $windowHarnessText = "#!/usr/bin/env bash`nset -euo pipefail`n" +
        $endpointMatch.Value +
        $stageIntervalFunction +
        $nextWarmWindowFunction + @'
warm_start_window_width=0.1
[[ "$(classify_stage_interval 1 1)" == forward ]]
[[ "$(classify_stage_interval -1 1)" == reversed ]]
[[ "$(classify_stage_interval -0.0000000005 1)" == equal ]]
current=0
requested=0.25
count=0
while awk -v a="$current" -v b="$requested" 'BEGIN { exit !(a<b-1e-12) }'; do
    next=$(next_warm_start_window "$current" "$requested")
    awk -v a="$current" -v b="$next" 'BEGIN { exit !(b>a) }'
    current="$next"
    count=$((count+1))
    ((count<=10))
done
require_exact_endpoint "$current" "$requested" "Bounded warm-start planner"
[[ "$count" == 3 ]]
'@
    [IO.File]::WriteAllText(
        $windowHarness,
        $windowHarnessText,
        [Text.UTF8Encoding]::new($false))
    $windowInfo = [Diagnostics.ProcessStartInfo]::new()
    $windowInfo.FileName = $bashPath
    $windowInfo.UseShellExecute = $false
    $windowInfo.RedirectStandardOutput = $true
    $windowInfo.RedirectStandardError = $true
    $windowInfo.ArgumentList.Add("--login")
    $windowInfo.ArgumentList.Add((Convert-ToGitBashPath $windowHarness))
    $windowProcess = [Diagnostics.Process]::new()
    $windowProcess.StartInfo = $windowInfo
    Assert-Condition $windowProcess.Start() "Unable to execute bounded-window harness."
    $windowStdout = $windowProcess.StandardOutput.ReadToEnd()
    $windowStderr = $windowProcess.StandardError.ReadToEnd()
    $windowProcess.WaitForExit()
    Assert-Condition ($windowProcess.ExitCode -eq 0) `
        "Bounded-window harness failed with $($windowProcess.ExitCode). stdout=$windowStdout stderr=$windowStderr"

    # Execute the actual generated bounded warm-start loop. The fake launcher
    # records solver and Courant calls while advancing a two-rank checkpoint
    # tree, so the test covers the loop rather than only its window planner.
    $warmLauncher = Join-Path $testRoot "fake-warm-launcher.sh"
    $warmLauncherText = @'
#!/usr/bin/env bash
set -euo pipefail
if [[ "$1" == foamDictionary ]]; then
    args=("$@")
    for ((i=0; i+3<${#args[@]}; ++i)); do
        if [[ "${args[$i]}" == -entry && "${args[$((i+1))]}" == endTime && "${args[$((i+2))]}" == -set ]]; then
            printf '%s\n' "${args[$((i+3))]}" > "$WARM_TARGET_FILE"
        fi
    done
    exit 0
fi
if [[ "$1" == mpirun ]]; then
    for argument in "$@"; do
        if [[ "$argument" == postProcess ]]; then
            printf '%s\n' postflight >> "$WARM_POSTFLIGHT_LOG"
            printf 'max(Co) = 0.1\n'
            exit 0
        fi
    done
    printf '%s\n' solver >> "$WARM_SOLVER_LOG"
    target=$(awk 'NF { value=$1 } END { print value }' "$WARM_TARGET_FILE")
    printf '%s\n' "$target" > "$WARM_STATE_FILE"
    for rank in 0 1; do
        mkdir -p "$WARM_CASE_DIR/processor${rank}/${target}/uniform"
        : > "$WARM_CASE_DIR/processor${rank}/${target}/uniform/time"
    done
    exit 0
fi
exit 0
'@
    [IO.File]::WriteAllText(
        $warmLauncher,
        $warmLauncherText,
        [Text.UTF8Encoding]::new($false))
    $warmHarness = Join-Path $testRoot "warm-harness.sh"
    $warmHarnessText = "#!/usr/bin/env bash`nset -euo pipefail`n" +
        $endpointMatch.Value +
        $nextWarmWindowFunction +
        $runTrackedFunction +
        $runTrackedCaptureFunction +
        $validateLatestAirflowCourantFunction +
        $runWarmStartWindowsFunction + @'
case_dir="$WARM_CASE_DIR"
foam_launcher="$WARM_LAUNCHER"
processes=2
active_child_pid=""
active_child_uses_group=false
tracked_capture_path=""
trap 'status=$?; echo "$status" > "$WARM_EXIT_STATUS_FILE"' EXIT
warm_start_window_width=0.1
warm_start_maximum_time_step=0.05
warm_current="$WARM_INITIAL_CURRENT"
requested_end="$WARM_REQUESTED_END"
latest_processor_restart_time() { awk 'NF { value=$1 } END { print value }' "$WARM_STATE_FILE"; }
prune_processor_times() { :; }
summary() { printf '%s\n' "$*" >> "$WARM_SUMMARY_LOG"; }
run_warm_start_windows
printf '%s\n' "$warm_current" > "$WARM_FINAL_FILE"
'@
    [IO.File]::WriteAllText(
        $warmHarness,
        $warmHarnessText,
        [Text.UTF8Encoding]::new($false))

    function Invoke-WarmStartScenario {
        param(
            [Parameter(Mandatory = $true)][string]$Name,
            [Parameter(Mandatory = $true)][string]$InitialCurrent,
            [Parameter(Mandatory = $true)][string]$InitialState,
            [Parameter(Mandatory = $true)][string]$RequestedEnd
        )
        $scenarioRoot = Join-Path $testRoot ("warm-" + $Name)
        $scenarioCase = Join-Path $scenarioRoot "case"
        [void](New-Item -ItemType Directory -Path $scenarioCase -Force)
        $statePath = Join-Path $scenarioRoot "state.txt"
        $targetPath = Join-Path $scenarioRoot "target.txt"
        $solverLog = Join-Path $scenarioRoot "solver.log"
        $postflightLog = Join-Path $scenarioRoot "postflight.log"
        $summaryLog = Join-Path $scenarioRoot "summary.log"
        $finalPath = Join-Path $scenarioRoot "final.txt"
        $exitStatusPath = Join-Path $scenarioRoot "exit-status.txt"
        [IO.File]::WriteAllText($statePath, "$InitialState`n", [Text.UTF8Encoding]::new($false))

        $info = [Diagnostics.ProcessStartInfo]::new()
        $info.FileName = $bashPath
        $info.UseShellExecute = $false
        $info.RedirectStandardOutput = $true
        $info.RedirectStandardError = $true
        # Isolated function harnesses deliberately skip user Bash startup
        # files. This keeps the fake OpenFOAM environment hermetic and avoids
        # login-profile traps or shell options changing explicit exit codes.
        $info.ArgumentList.Add("--noprofile")
        $info.ArgumentList.Add("--norc")
        $info.ArgumentList.Add((Convert-ToGitBashPath $warmHarness))
        $info.Environment["WARM_CASE_DIR"] = Convert-ToGitBashPath $scenarioCase
        $info.Environment["WARM_LAUNCHER"] = Convert-ToGitBashPath $warmLauncher
        $info.Environment["WARM_STATE_FILE"] = Convert-ToGitBashPath $statePath
        $info.Environment["WARM_TARGET_FILE"] = Convert-ToGitBashPath $targetPath
        $info.Environment["WARM_SOLVER_LOG"] = Convert-ToGitBashPath $solverLog
        $info.Environment["WARM_POSTFLIGHT_LOG"] = Convert-ToGitBashPath $postflightLog
        $info.Environment["WARM_SUMMARY_LOG"] = Convert-ToGitBashPath $summaryLog
        $info.Environment["WARM_FINAL_FILE"] = Convert-ToGitBashPath $finalPath
        $info.Environment["WARM_EXIT_STATUS_FILE"] = Convert-ToGitBashPath $exitStatusPath
        $info.Environment["WARM_INITIAL_CURRENT"] = $InitialCurrent
        $info.Environment["WARM_REQUESTED_END"] = $RequestedEnd
        $process = [Diagnostics.Process]::new()
        $process.StartInfo = $info
        Assert-Condition $process.Start() "Unable to execute warm-start scenario $Name."
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            Stdout = $stdout
            Stderr = $stderr
            StatePath = $statePath
            SolverLog = $solverLog
            PostflightLog = $postflightLog
            SummaryLog = $summaryLog
            FinalPath = $finalPath
            ExitStatusPath = $exitStatusPath
        }
    }

    $warmSuccess = Invoke-WarmStartScenario `
        -Name "success" -InitialCurrent "0" -InitialState "0" -RequestedEnd "0.25"
    Assert-Condition ($warmSuccess.ExitCode -eq 0) `
        "Warm-start loop failed with $($warmSuccess.ExitCode). stdout=$($warmSuccess.Stdout) stderr=$($warmSuccess.Stderr)"
    Assert-Condition (@(Get-Content -LiteralPath $warmSuccess.SolverLog).Count -eq 3) `
        "Warm-start loop did not execute exactly three solver windows."
    Assert-Condition (@(Get-Content -LiteralPath $warmSuccess.PostflightLog).Count -eq 3) `
        "Warm-start loop did not execute exactly three Courant postflights."
    $warmWindowSummaries = @(
        Get-Content -LiteralPath $warmSuccess.SummaryLog |
            Where-Object { $_.StartsWith("warm_start_window ", [StringComparison]::Ordinal) })
    $warmCourantSummaries = @(
        Get-Content -LiteralPath $warmSuccess.SummaryLog |
            Where-Object { $_.StartsWith("airflow_courant_validation ", [StringComparison]::Ordinal) })
    Assert-Condition ($warmWindowSummaries.Count -eq 3) `
        "Warm-start loop did not publish exactly three window summaries."
    Assert-Condition ($warmCourantSummaries.Count -eq 3) `
        "Warm-start loop did not publish exactly three shared Courant-validation summaries."
    Assert-Condition ((Get-Content -LiteralPath $warmSuccess.FinalPath -Raw).Trim() -eq "0.25") `
        "Warm-start loop did not finish exactly at 0.25 s."

    $warmMissingMetadata = Invoke-WarmStartScenario `
        -Name "missing-metadata" -InitialCurrent "0.1" -InitialState "0.1" -RequestedEnd "0.2"
    Assert-Condition ($warmMissingMetadata.ExitCode -eq 8) `
        "Missing warm-start metadata returned $($warmMissingMetadata.ExitCode), expected 8 (trap observed $((Get-Content -LiteralPath $warmMissingMetadata.ExitStatusPath -Raw).Trim())). stdout=$($warmMissingMetadata.Stdout) stderr=$($warmMissingMetadata.Stderr)"
    Assert-Condition ($warmMissingMetadata.Stderr.Contains(
        'Missing warm-start time metadata for processor0 at t=0.1')) `
        "Missing warm-start metadata did not emit its diagnostic."
    Assert-Condition (-not (Test-Path -LiteralPath $warmMissingMetadata.SolverLog)) `
        "Warm-start solver ran despite missing per-rank source metadata."

    $warmSourceMismatch = Invoke-WarmStartScenario `
        -Name "source-mismatch" -InitialCurrent "0.1" -InitialState "0.09" -RequestedEnd "0.2"
    Assert-Condition ($warmSourceMismatch.ExitCode -eq 5) `
        "Warm-start source mismatch returned $($warmSourceMismatch.ExitCode), expected 5. stdout=$($warmSourceMismatch.Stdout) stderr=$($warmSourceMismatch.Stderr)"
    Assert-Condition ($warmSourceMismatch.Stderr.Contains(
        'Warm-start source checkpoint failed to reach its exact endpoint')) `
        "Warm-start source mismatch did not emit its exact-endpoint diagnostic."
    Assert-Condition (-not (Test-Path -LiteralPath $warmSourceMismatch.SolverLog)) `
        "Warm-start solver ran despite a mismatched source checkpoint."

    # Reproduce the restart gap where a nonzero checkpoint was fully written
    # but the original process stopped before Courant postflight. The shared
    # generated validator must run first when a completed fan-ramp checkpoint
    # is skipped before further warm advancement, and when a completed warm
    # checkpoint already equals the requested endpoint.
    $restartLauncher = Join-Path $testRoot "fake-restart-courant-launcher.sh"
    $restartLauncherText = @'
#!/usr/bin/env bash
set -euo pipefail
if [[ "$1" == foamDictionary ]]; then
    args=("$@")
    for ((i=0; i+3<${#args[@]}; ++i)); do
        if [[ "${args[$i]}" == -entry && "${args[$((i+1))]}" == endTime && "${args[$((i+2))]}" == -set ]]; then
            printf '%s\n' "${args[$((i+3))]}" > "$RESTART_TARGET_FILE"
        fi
    done
    exit 0
fi
if [[ "$1" == mpirun ]]; then
    for argument in "$@"; do
        if [[ "$argument" == postProcess ]]; then
            printf '%s\n' postflight >> "$RESTART_EVENT_LOG"
            printf 'max(Co) = %s\n' "$RESTART_COURANT_VALUE"
            exit 0
        fi
    done
    printf '%s\n' solver >> "$RESTART_EVENT_LOG"
    target=$(awk 'NF { value=$1 } END { print value }' "$RESTART_TARGET_FILE")
    printf '%s\n' "$target" > "$RESTART_STATE_FILE"
    for rank in 0 1; do
        mkdir -p "$RESTART_CASE_DIR/processor${rank}/${target}/uniform"
        : > "$RESTART_CASE_DIR/processor${rank}/${target}/uniform/time"
    done
    exit 0
fi
exit 0
'@
    [IO.File]::WriteAllText(
        $restartLauncher,
        $restartLauncherText,
        [Text.UTF8Encoding]::new($false))
    $restartHarness = Join-Path $testRoot "restart-courant-harness.sh"
    $restartHarnessText = "#!/usr/bin/env bash`nset -euo pipefail`n" +
        $endpointMatch.Value +
        $nextWarmWindowFunction +
        $runTrackedFunction +
        $runTrackedCaptureFunction +
        $fanRampEndpointFunction +
        $fanRampPendingFunction +
        $validateLatestAirflowCourantFunction +
        $validateInterruptedFanRampFunction +
        $runFanRampFunction +
        $runWarmStartWindowsFunction + @'
case_dir="$RESTART_CASE_DIR"
foam_launcher="$RESTART_LAUNCHER"
processes=2
active_child_pid=""
active_child_uses_group=false
pending_termination_signal=""
tracked_capture_path=""
warm_start_window_width=0.1
warm_start_maximum_time_step=0.05
fan_startup_ramp_time=0.050000000000000003
fan_startup_ramp_enabled=true
mapped_state_marker="$case_dir/.mapped_initial_state"
fan_ramp_complete_marker="$case_dir/.fan_ramp_complete"
full_fan_options="$case_dir/fvOptions.fullFan"
fan_ramp_restart_validated=false
warm_current="$RESTART_INITIAL_CURRENT"
requested_end="$RESTART_REQUESTED_END"
latest_processor_restart_time() { awk 'NF { value=$1 } END { print value }' "$RESTART_STATE_FILE"; }
prune_processor_times() { :; }
summary() { printf '%s\n' "$*" >> "$RESTART_SUMMARY_LOG"; }
set_fan_scale()
{
    if [[ "$RESTART_RECOVERY_FAILURE" == scale ]]; then return 42; fi
    printf '%s\n' "$1" >> "$RESTART_SCALE_LOG"
}
touch()
{
    if [[ "$RESTART_RECOVERY_FAILURE" == touch ]] && [[ "$1" == "$fan_ramp_complete_marker" ]]; then
        return 43
    fi
    command touch "$@"
}

validate_interrupted_fan_ramp_checkpoint "$warm_current"
if [[ "$fan_ramp_restart_validated" != true ]] && awk -v t="$warm_current" 'BEGIN { exit !(t>1e-12) }'; then
    validate_latest_airflow_courant "$warm_current" "Warm-start restart checkpoint"
fi
if [[ ! -f "$mapped_state_marker" ]] && [[ ! -f "$fan_ramp_complete_marker" ]] && awk -v a="$warm_current" -v end="$fan_startup_ramp_time" 'BEGIN { exit !(a<end) }'; then
    run_fan_ramp fakeSolver "$warm_current" "$requested_end"
    warm_current="$ramp_current"
fi
run_warm_start_windows
printf '%s\n' "$warm_current" > "$RESTART_FINAL_FILE"
'@
    [IO.File]::WriteAllText(
        $restartHarness,
        $restartHarnessText,
        [Text.UTF8Encoding]::new($false))

    function Invoke-RestartCourantScenario {
        param(
            [Parameter(Mandatory = $true)][string]$Name,
            [Parameter(Mandatory = $true)][string]$InitialCurrent,
            [Parameter(Mandatory = $true)][string]$RequestedEnd,
            [Parameter(Mandatory = $true)][string]$CourantValue,
            [Parameter(Mandatory = $true)][ValidateSet("missing", "present")][string]$FanMarker,
            [Parameter(Mandatory = $true)][ValidateSet("none", "scale", "touch")][string]$RecoveryFailure
        )
        $scenarioRoot = Join-Path $testRoot ("restart-courant-" + $Name)
        $scenarioCase = Join-Path $scenarioRoot "case"
        [void](New-Item -ItemType Directory -Path $scenarioCase -Force)
        foreach ($rank in 0, 1) {
            $metadataDirectory = Join-Path $scenarioCase (
                "processor$rank/$InitialCurrent/uniform")
            [void](New-Item -ItemType Directory -Path $metadataDirectory -Force)
            [IO.File]::WriteAllText(
                (Join-Path $metadataDirectory "time"),
                "",
                [Text.UTF8Encoding]::new($false))
        }
        $statePath = Join-Path $scenarioRoot "state.txt"
        $targetPath = Join-Path $scenarioRoot "target.txt"
        $eventLog = Join-Path $scenarioRoot "events.log"
        $summaryLog = Join-Path $scenarioRoot "summary.log"
        $scaleLog = Join-Path $scenarioRoot "scales.log"
        $finalPath = Join-Path $scenarioRoot "final.txt"
        [IO.File]::WriteAllText(
            $statePath,
            "$InitialCurrent`n",
            [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText(
            (Join-Path $scenarioCase "fvOptions.fullFan"),
            "synthetic full fan options`n",
            [Text.UTF8Encoding]::new($false))
        if ($FanMarker -eq "present") {
            [IO.File]::WriteAllText(
                (Join-Path $scenarioCase ".fan_ramp_complete"),
                "",
                [Text.UTF8Encoding]::new($false))
        }

        $info = [Diagnostics.ProcessStartInfo]::new()
        $info.FileName = $bashPath
        $info.UseShellExecute = $false
        $info.RedirectStandardOutput = $true
        $info.RedirectStandardError = $true
        $info.ArgumentList.Add("--noprofile")
        $info.ArgumentList.Add("--norc")
        $info.ArgumentList.Add((Convert-ToGitBashPath $restartHarness))
        $info.Environment["RESTART_CASE_DIR"] = Convert-ToGitBashPath $scenarioCase
        $info.Environment["RESTART_LAUNCHER"] = Convert-ToGitBashPath $restartLauncher
        $info.Environment["RESTART_STATE_FILE"] = Convert-ToGitBashPath $statePath
        $info.Environment["RESTART_TARGET_FILE"] = Convert-ToGitBashPath $targetPath
        $info.Environment["RESTART_EVENT_LOG"] = Convert-ToGitBashPath $eventLog
        $info.Environment["RESTART_SUMMARY_LOG"] = Convert-ToGitBashPath $summaryLog
        $info.Environment["RESTART_SCALE_LOG"] = Convert-ToGitBashPath $scaleLog
        $info.Environment["RESTART_FINAL_FILE"] = Convert-ToGitBashPath $finalPath
        $info.Environment["RESTART_INITIAL_CURRENT"] = $InitialCurrent
        $info.Environment["RESTART_REQUESTED_END"] = $RequestedEnd
        $info.Environment["RESTART_COURANT_VALUE"] = $CourantValue
        $info.Environment["RESTART_RECOVERY_FAILURE"] = $RecoveryFailure
        $info.Environment["TMPDIR"] = Convert-ToGitBashPath $scenarioRoot
        $process = [Diagnostics.Process]::new()
        $process.StartInfo = $info
        Assert-Condition $process.Start() "Unable to execute restart-Courant scenario $Name."
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            Stdout = $stdout
            Stderr = $stderr
            EventLog = $eventLog
            SummaryLog = $summaryLog
            ScaleLog = $scaleLog
            FanMarkerPath = Join-Path $scenarioCase ".fan_ramp_complete"
            FinalPath = $finalPath
        }
    }

    $partialRampRestart = Invoke-RestartCourantScenario `
        -Name "ramp-partial-resume" `
        -InitialCurrent "0.02" -RequestedEnd "0.03" -CourantValue "0.1" `
        -FanMarker "missing" -RecoveryFailure "none"
    Assert-Condition ($partialRampRestart.ExitCode -eq 0) `
        "Partial ramp-checkpoint restart failed with $($partialRampRestart.ExitCode). stdout=$($partialRampRestart.Stdout) stderr=$($partialRampRestart.Stderr)"
    $partialRampEvents = @(Get-Content -LiteralPath $partialRampRestart.EventLog)
    Assert-Condition (
        $partialRampEvents.Count -eq 3 -and
        $partialRampEvents[0] -eq "postflight" -and
        $partialRampEvents[1] -eq "solver" -and
        $partialRampEvents[2] -eq "postflight") `
        "Interrupted partial ramp checkpoint was not revalidated before ramp resumption. events=$($partialRampEvents -join ',')"
    Assert-Condition (-not (Test-Path -LiteralPath $partialRampRestart.FanMarkerPath)) `
        "Partial fan-ramp recovery incorrectly published the completion marker."
    $partialRampFinal = [double]::Parse(
        (Get-Content -LiteralPath $partialRampRestart.FinalPath -Raw).Trim(),
        [Globalization.CultureInfo]::InvariantCulture)
    Assert-Condition ([Math]::Abs($partialRampFinal - 0.03) -le 1e-12) `
        "Partial fan-ramp recovery did not reach its requested exact checkpoint."

    $rampRestart = Invoke-RestartCourantScenario `
        -Name "ramp-complete-advance" `
        -InitialCurrent "0.05" -RequestedEnd "0.15" -CourantValue "0.1" `
        -FanMarker "missing" -RecoveryFailure "none"
    Assert-Condition ($rampRestart.ExitCode -eq 0) `
        "Completed ramp-checkpoint restart failed with $($rampRestart.ExitCode). stdout=$($rampRestart.Stdout) stderr=$($rampRestart.Stderr)"
    $rampRestartEvents = @(Get-Content -LiteralPath $rampRestart.EventLog)
    Assert-Condition (
        $rampRestartEvents.Count -eq 3 -and
        $rampRestartEvents[0] -eq "postflight" -and
        $rampRestartEvents[1] -eq "solver" -and
        $rampRestartEvents[2] -eq "postflight") `
        "Completed ramp checkpoint was not Courant-revalidated before skipped-ramp warm advancement. events=$($rampRestartEvents -join ',')"
    $rampRestartFinal = [double]::Parse(
        (Get-Content -LiteralPath $rampRestart.FinalPath -Raw).Trim(),
        [Globalization.CultureInfo]::InvariantCulture)
    Assert-Condition ([Math]::Abs($rampRestartFinal - 0.15) -le 1e-12) `
        "Ramp-checkpoint restart did not finish its subsequent exact warm window."
    Assert-Condition (Test-Path -LiteralPath $rampRestart.FanMarkerPath -PathType Leaf) `
        "Validated complete fan-ramp restart did not recover its completion marker."
    Assert-Condition ((Get-Content -LiteralPath $rampRestart.ScaleLog | Select-Object -Last 1) -eq "1") `
        "Validated complete fan-ramp restart did not restore full fan scale."

    foreach ($recoveryFailureCase in @(
        @{ Name = "ramp-recovery-scale-failure"; Failure = "scale"; ExitCode = 42 },
        @{ Name = "ramp-recovery-touch-failure"; Failure = "touch"; ExitCode = 43 }
    )) {
        $failedRecovery = Invoke-RestartCourantScenario `
            -Name $recoveryFailureCase.Name `
            -InitialCurrent "0.05" -RequestedEnd "0.05" -CourantValue "0.1" `
            -FanMarker "missing" -RecoveryFailure $recoveryFailureCase.Failure
        Assert-Condition ($failedRecovery.ExitCode -eq $recoveryFailureCase.ExitCode) `
            "Fan-ramp $($recoveryFailureCase.Failure) failure returned $($failedRecovery.ExitCode), expected $($recoveryFailureCase.ExitCode). stdout=$($failedRecovery.Stdout) stderr=$($failedRecovery.Stderr)"
        Assert-Condition (-not (Test-Path -LiteralPath $failedRecovery.FanMarkerPath)) `
            "Fan-ramp $($recoveryFailureCase.Failure) failure published a completion marker."
        Assert-Condition (-not (Test-Path -LiteralPath $failedRecovery.FinalPath)) `
            "Fan-ramp $($recoveryFailureCase.Failure) failure advanced to restart completion."
        $failedRecoverySummaries = @(
            Get-Content -LiteralPath $failedRecovery.SummaryLog |
                Where-Object { $_.StartsWith("fan_ramp_restart_recovered ", [StringComparison]::Ordinal) })
        Assert-Condition ($failedRecoverySummaries.Count -eq 0) `
            "Fan-ramp $($recoveryFailureCase.Failure) failure published a recovery summary."
        $failedRecoveryEvents = @(Get-Content -LiteralPath $failedRecovery.EventLog)
        Assert-Condition (
            $failedRecoveryEvents.Count -eq 1 -and
            $failedRecoveryEvents[0] -eq "postflight") `
            "Fan-ramp $($recoveryFailureCase.Failure) failure did not stop immediately after checkpoint validation. events=$($failedRecoveryEvents -join ',')"
    }

    $beyondRampRestart = Invoke-RestartCourantScenario `
        -Name "ramp-beyond-end-missing-marker" `
        -InitialCurrent "0.06" -RequestedEnd "0.06" -CourantValue "0.1" `
        -FanMarker "missing" -RecoveryFailure "none"
    Assert-Condition ($beyondRampRestart.ExitCode -eq 7) `
        "Beyond-end ramp restart returned $($beyondRampRestart.ExitCode), expected 7. stdout=$($beyondRampRestart.Stdout) stderr=$($beyondRampRestart.Stderr)"
    Assert-Condition ($beyondRampRestart.Stderr.Contains('refusing unvalidated recovery')) `
        "Beyond-end ramp restart did not emit its fail-closed diagnostic."
    Assert-Condition (-not (Test-Path -LiteralPath $beyondRampRestart.EventLog)) `
        "Beyond-end ramp restart invoked Courant postflight despite missing completion evidence."
    Assert-Condition (-not (Test-Path -LiteralPath $beyondRampRestart.FinalPath)) `
        "Beyond-end ramp restart was incorrectly accepted as finished."

    $warmRestart = Invoke-RestartCourantScenario `
        -Name "warm-complete-finish" `
        -InitialCurrent "0.25" -RequestedEnd "0.25" -CourantValue "0.1" `
        -FanMarker "present" -RecoveryFailure "none"
    Assert-Condition ($warmRestart.ExitCode -eq 0) `
        "Completed warm-checkpoint restart failed with $($warmRestart.ExitCode). stdout=$($warmRestart.Stdout) stderr=$($warmRestart.Stderr)"
    $warmRestartEvents = @(Get-Content -LiteralPath $warmRestart.EventLog)
    Assert-Condition (
        $warmRestartEvents.Count -eq 1 -and
        $warmRestartEvents[0] -eq "postflight") `
        "Completed warm endpoint was not Courant-revalidated before finishing. events=$($warmRestartEvents -join ',')"
    Assert-Condition ((Get-Content -LiteralPath $warmRestart.FinalPath -Raw).Trim() -eq "0.25") `
        "Validated warm restart did not retain its exact endpoint."

    $rejectedRestart = Invoke-RestartCourantScenario `
        -Name "warm-complete-high-courant" `
        -InitialCurrent "0.25" -RequestedEnd "0.25" -CourantValue "1" `
        -FanMarker "present" -RecoveryFailure "none"
    Assert-Condition ($rejectedRestart.ExitCode -eq 7) `
        "Unsafe completed warm checkpoint returned $($rejectedRestart.ExitCode), expected 7. stdout=$($rejectedRestart.Stdout) stderr=$($rejectedRestart.Stderr)"
    Assert-Condition ($rejectedRestart.Stderr.Contains(
        'Warm-start restart checkpoint Courant limit exceeded or unavailable')) `
        "Unsafe completed warm checkpoint did not emit the restart Courant diagnostic."
    Assert-Condition (-not (Test-Path -LiteralPath $rejectedRestart.FinalPath)) `
        "Unsafe completed warm checkpoint was incorrectly accepted as finished."

    foreach ($invalidCourant in @(
        @{ Name = "negative"; Value = "-0.1" },
        @{ Name = "negative-infinity"; Value = "-inf" }
    )) {
        $invalidCourantRestart = Invoke-RestartCourantScenario `
            -Name ("warm-complete-" + $invalidCourant.Name + "-courant") `
            -InitialCurrent "0.25" -RequestedEnd "0.25" `
            -CourantValue $invalidCourant.Value `
            -FanMarker "present" -RecoveryFailure "none"
        Assert-Condition ($invalidCourantRestart.ExitCode -eq 7) `
            "Invalid $($invalidCourant.Name) Courant output returned $($invalidCourantRestart.ExitCode), expected 7. stdout=$($invalidCourantRestart.Stdout) stderr=$($invalidCourantRestart.Stderr)"
        Assert-Condition ($invalidCourantRestart.Stderr.Contains(
            'Warm-start restart checkpoint Courant limit exceeded or unavailable')) `
            "Invalid $($invalidCourant.Name) Courant output did not emit the shared-validator diagnostic."
        Assert-Condition (-not (Test-Path -LiteralPath $invalidCourantRestart.FinalPath)) `
            "Invalid $($invalidCourant.Name) Courant output was incorrectly accepted as finished."
        $invalidCourantEvents = @(Get-Content -LiteralPath $invalidCourantRestart.EventLog)
        Assert-Condition (
            $invalidCourantEvents.Count -eq 1 -and
            $invalidCourantEvents[0] -eq "postflight") `
            "Invalid $($invalidCourant.Name) Courant case did not stop immediately after one restart postflight. events=$($invalidCourantEvents -join ',')"
    }

    # Exercise the generated refresh-journal state machine. An owed record is
    # cleared only when no checkpoint progress exists. Any uncommitted progress
    # and malformed state remain present and return a fail-closed error; only a
    # committed stage may publish an active refresh record.
    $journalCase = Join-Path $testRoot "journal-case"
    [void](New-Item -ItemType Directory -Path $journalCase)
    $journalHarness = Join-Path $testRoot "journal-harness.sh"
    $journalHarnessText = "#!/usr/bin/env bash`nset -euo pipefail`n" +
        $endpointMatch.Value +
        $stageIntervalFunction +
        $writeRefreshStateFunction +
        $commitRefreshStateFunction +
        $normalizeRefreshJournalFunction + @'
case_dir="$JOURNAL_CASE_DIR"
refresh_pending_marker="$case_dir/.airflow_refresh_pending"

current=10
write_airflow_refresh_state owed 10 11
normalize_airflow_refresh_journal
[[ ! -e "$refresh_pending_marker" ]]

current=10.5
write_airflow_refresh_state owed 10 11
status=0
normalize_airflow_refresh_journal || status=$?
[[ "$status" == 9 ]]
[[ "$(cat "$refresh_pending_marker")" == "owed 10 11" ]]

printf '%s\n' 'owed malformed 11' > "$refresh_pending_marker"
status=0
normalize_airflow_refresh_journal || status=$?
[[ "$status" == 9 ]]
[[ "$(cat "$refresh_pending_marker")" == "owed malformed 11" ]]

printf '%s\n' 10 > "$refresh_pending_marker"
current=10
normalize_airflow_refresh_journal
[[ "$(cat "$refresh_pending_marker")" == "active 10" ]]

write_airflow_refresh_state owed 10 11
commit_airflow_refresh_state 11 10 11
[[ "$(cat "$refresh_pending_marker")" == "active 11" ]]

rm -f "$refresh_pending_marker"
status=0
commit_airflow_refresh_state 11 10 11 || status=$?
[[ "$status" == 9 ]]
[[ ! -e "$refresh_pending_marker" ]]

write_airflow_refresh_state active 10
status=0
commit_airflow_refresh_state 11 10 11 || status=$?
[[ "$status" == 9 ]]
[[ "$(cat "$refresh_pending_marker")" == "active 10" ]]
[[ ! -e "${refresh_pending_marker}.tmp.$$" ]]
'@
    [IO.File]::WriteAllText(
        $journalHarness,
        $journalHarnessText,
        [Text.UTF8Encoding]::new($false))
    $journalInfo = [Diagnostics.ProcessStartInfo]::new()
    $journalInfo.FileName = $bashPath
    $journalInfo.UseShellExecute = $false
    $journalInfo.RedirectStandardOutput = $true
    $journalInfo.RedirectStandardError = $true
    $journalInfo.ArgumentList.Add("--login")
    $journalInfo.ArgumentList.Add((Convert-ToGitBashPath $journalHarness))
    $journalInfo.Environment["JOURNAL_CASE_DIR"] = Convert-ToGitBashPath $journalCase
    $journalProcess = [Diagnostics.Process]::new()
    $journalProcess.StartInfo = $journalInfo
    Assert-Condition $journalProcess.Start() "Unable to execute airflow-refresh journal harness."
    $journalStdout = $journalProcess.StandardOutput.ReadToEnd()
    $journalStderr = $journalProcess.StandardError.ReadToEnd()
    $journalProcess.WaitForExit()
    Assert-Condition ($journalProcess.ExitCode -eq 0) `
        "Airflow-refresh journal harness failed with $($journalProcess.ExitCode). stdout=$journalStdout stderr=$journalStderr"

    # Use the generated tracked-child launcher and signal handler with a
    # one-shot restore stub. TERM is delivered while the parent is waiting on
    # a long-lived tracked child. It must reach and stop that child promptly,
    # terminate the runner as 143, restore once, and skip subsequent work.
    $terminateRestoreLog = Join-Path $testRoot "terminate-restore.log"
    $terminateSentinel = Join-Path $testRoot "terminate-post-signal.log"
    $terminateHarness = Join-Path $testRoot "terminate-harness.sh"
    $terminateHarnessText = "#!/usr/bin/env bash`nset -euo pipefail`n" + @'
restore_run_state()
{
    printf '%s\n' restore >> "$TERMINATE_RESTORE_LOG"
}
'@ + "`n" + $runTrackedFunction + "`n" + $terminateRunFunction + "`n" + @'
active_child_pid=""
active_child_uses_group=false
export PATH="/usr/bin:/bin:$PATH"
trap restore_run_state EXIT
trap 'terminate_run INT' INT
trap 'terminate_run TERM' TERM
( sleep 0.2; kill -TERM "$$" ) &
run_tracked sleep 30
printf '%s\n' reached > "$TERMINATE_SENTINEL"
'@
    [IO.File]::WriteAllText(
        $terminateHarness,
        $terminateHarnessText,
        [Text.UTF8Encoding]::new($false))
    $terminateInfo = [Diagnostics.ProcessStartInfo]::new()
    $terminateInfo.FileName = $bashPath
    $terminateInfo.UseShellExecute = $false
    $terminateInfo.RedirectStandardOutput = $true
    $terminateInfo.RedirectStandardError = $true
    $terminateInfo.ArgumentList.Add("--noprofile")
    $terminateInfo.ArgumentList.Add("--norc")
    $terminateInfo.ArgumentList.Add((Convert-ToGitBashPath $terminateHarness))
    $terminateInfo.Environment["TERMINATE_RESTORE_LOG"] = Convert-ToGitBashPath $terminateRestoreLog
    $terminateInfo.Environment["TERMINATE_SENTINEL"] = Convert-ToGitBashPath $terminateSentinel
    $terminateProcess = [Diagnostics.Process]::new()
    $terminateProcess.StartInfo = $terminateInfo
    $terminateStopwatch = [Diagnostics.Stopwatch]::StartNew()
    Assert-Condition $terminateProcess.Start() "Unable to execute TERM signal harness."
    $terminateStdout = $terminateProcess.StandardOutput.ReadToEnd()
    $terminateStderr = $terminateProcess.StandardError.ReadToEnd()
    $terminateProcess.WaitForExit()
    $terminateStopwatch.Stop()
    Assert-Condition ($terminateProcess.ExitCode -eq 143) `
        "TERM signal harness returned $($terminateProcess.ExitCode), expected 143. stdout=$terminateStdout stderr=$terminateStderr"
    Assert-Condition ((Get-Content -LiteralPath $terminateRestoreLog).Count -eq 1) `
        "TERM signal handler did not restore run state exactly once."
    Assert-Condition ($terminateStopwatch.Elapsed.TotalSeconds -lt 5) `
        "TERM signal did not reach and stop the tracked 30-second child promptly (elapsed=$($terminateStopwatch.Elapsed.TotalSeconds) s)."
    Assert-Condition (-not (Test-Path -LiteralPath $terminateSentinel)) `
        "TERM signal handler continued into the post-signal sentinel."

    # Force the real watchdog path with a dedicated tracked process group whose
    # leader and descendant both ignore INT/TERM. The generated handler must
    # escalate to KILL, reap the group, restore once, and still report the
    # caller-facing TERM status. Timeout cleanup independently kills the exact
    # recorded group so a regression cannot leak this synthetic child.
    $stubbornRestoreLog = Join-Path $testRoot "stubborn-restore.log"
    $stubbornLeaderPid = Join-Path $testRoot "stubborn-leader.pid"
    $stubbornDescendantPid = Join-Path $testRoot "stubborn-descendant.pid"
    $stubbornLeaderWindowsPid = Join-Path $testRoot "stubborn-leader-winpid.txt"
    $stubbornDescendantWindowsPid = Join-Path $testRoot "stubborn-descendant-winpid.txt"
    $stubbornSentinel = Join-Path $testRoot "stubborn-post-signal.log"
    $stubbornChild = Join-Path $testRoot "stubborn-child.sh"
    $stubbornChildText = @'
#!/usr/bin/env bash
set -u
trap '' INT TERM
set +m
printf '%s\n' "$$" > "$STUBBORN_LEADER_PID_FILE"
ps -lp "$$" | awk 'NR==2 { print $4 }' > "$STUBBORN_LEADER_WINPID_FILE"
sleep 30 &
descendant=$!
printf '%s\n' "$descendant" > "$STUBBORN_DESCENDANT_PID_FILE"
ps -lp "$descendant" | awk 'NR==2 { print $4 }' > "$STUBBORN_DESCENDANT_WINPID_FILE"
wait "$descendant"
'@
    [IO.File]::WriteAllText(
        $stubbornChild,
        $stubbornChildText,
        [Text.UTF8Encoding]::new($false))
    $stubbornHarness = Join-Path $testRoot "stubborn-harness.sh"
    $stubbornHarnessText = "#!/usr/bin/env bash`nset -euo pipefail`n" + @'
restore_run_state()
{
    printf '%s\n' restore >> "$STUBBORN_RESTORE_LOG"
}
'@ + "`n" + $runTrackedFunction + "`n" + $terminateRunFunction + "`n" + @'
active_child_pid=""
active_child_uses_group=false
pending_termination_signal=""
export PATH="/usr/bin:/bin:$PATH"
trap restore_run_state EXIT
trap 'terminate_run INT' INT
trap 'terminate_run TERM' TERM
( sleep 0.2; kill -TERM "$$"; sleep 0.2; kill -TERM "$$" 2>/dev/null || true ) &
run_tracked bash "$STUBBORN_CHILD_SCRIPT"
printf '%s\n' reached > "$STUBBORN_SENTINEL"
'@
    [IO.File]::WriteAllText(
        $stubbornHarness,
        $stubbornHarnessText,
        [Text.UTF8Encoding]::new($false))
    $stubbornInfo = [Diagnostics.ProcessStartInfo]::new()
    $stubbornInfo.FileName = $bashPath
    $stubbornInfo.UseShellExecute = $false
    $stubbornInfo.RedirectStandardOutput = $true
    $stubbornInfo.RedirectStandardError = $true
    $stubbornInfo.ArgumentList.Add("--noprofile")
    $stubbornInfo.ArgumentList.Add("--norc")
    $stubbornInfo.ArgumentList.Add((Convert-ToGitBashPath $stubbornHarness))
    $stubbornInfo.Environment["STUBBORN_CHILD_SCRIPT"] = Convert-ToGitBashPath $stubbornChild
    $stubbornInfo.Environment["STUBBORN_RESTORE_LOG"] = Convert-ToGitBashPath $stubbornRestoreLog
    $stubbornInfo.Environment["STUBBORN_LEADER_PID_FILE"] = Convert-ToGitBashPath $stubbornLeaderPid
    $stubbornInfo.Environment["STUBBORN_DESCENDANT_PID_FILE"] = Convert-ToGitBashPath $stubbornDescendantPid
    $stubbornInfo.Environment["STUBBORN_LEADER_WINPID_FILE"] = Convert-ToGitBashPath $stubbornLeaderWindowsPid
    $stubbornInfo.Environment["STUBBORN_DESCENDANT_WINPID_FILE"] = Convert-ToGitBashPath $stubbornDescendantWindowsPid
    $stubbornInfo.Environment["STUBBORN_SENTINEL"] = Convert-ToGitBashPath $stubbornSentinel
    $stubbornProcess = [Diagnostics.Process]::new()
    $stubbornProcess.StartInfo = $stubbornInfo
    $stubbornStopwatch = [Diagnostics.Stopwatch]::StartNew()
    Assert-Condition $stubbornProcess.Start() "Unable to execute stubborn-child watchdog harness."
    $stubbornFinished = $stubbornProcess.WaitForExit(15000)
    $stubbornStopwatch.Stop()
    if (-not $stubbornFinished) {
        if (Test-Path -LiteralPath $stubbornLeaderPid -PathType Leaf) {
            $groupIdentifier = (Get-Content -LiteralPath $stubbornLeaderPid -Raw).Trim()
            if ($groupIdentifier -match '^[0-9]+$') {
                $cleanupInfo = [Diagnostics.ProcessStartInfo]::new()
                $cleanupInfo.FileName = $bashPath
                $cleanupInfo.UseShellExecute = $false
                $cleanupInfo.ArgumentList.Add("--noprofile")
                $cleanupInfo.ArgumentList.Add("--norc")
                $cleanupInfo.ArgumentList.Add("-c")
                $cleanupInfo.ArgumentList.Add('kill -KILL -- "-$1" 2>/dev/null || true')
                $cleanupInfo.ArgumentList.Add("--")
                $cleanupInfo.ArgumentList.Add($groupIdentifier)
                $cleanupProcess = [Diagnostics.Process]::Start($cleanupInfo)
                [void]$cleanupProcess.WaitForExit(3000)
            }
        }
        foreach ($windowsPidFile in @(
            $stubbornLeaderWindowsPid,
            $stubbornDescendantWindowsPid)) {
            if (Test-Path -LiteralPath $windowsPidFile -PathType Leaf) {
                $windowsIdentifier = (Get-Content -LiteralPath $windowsPidFile -Raw).Trim()
                if ($windowsIdentifier -match '^[0-9]+$') {
                    $syntheticWindowsProcess = Get-Process -Id ([int]$windowsIdentifier) -ErrorAction SilentlyContinue
                    if ($null -ne $syntheticWindowsProcess) {
                        $syntheticWindowsProcess.Kill($true)
                        [void]$syntheticWindowsProcess.WaitForExit(3000)
                    }
                }
            }
        }
        if (-not $stubbornProcess.HasExited) {
            $stubbornProcess.Kill($true)
            [void]$stubbornProcess.WaitForExit(3000)
        }
        throw "Stubborn-child watchdog harness exceeded its 15-second fail-safe timeout."
    }
    $stubbornStdout = $stubbornProcess.StandardOutput.ReadToEnd()
    $stubbornStderr = $stubbornProcess.StandardError.ReadToEnd()
    Assert-Condition ($stubbornProcess.ExitCode -eq 143) `
        "Stubborn-child watchdog returned $($stubbornProcess.ExitCode), expected 143. stdout=$stubbornStdout stderr=$stubbornStderr"
    Assert-Condition ((Get-Content -LiteralPath $stubbornRestoreLog).Count -eq 1) `
        "Stubborn-child watchdog did not restore run state exactly once."
    Assert-Condition (-not (Test-Path -LiteralPath $stubbornSentinel)) `
        "Stubborn-child watchdog continued into the post-signal sentinel."
    Assert-Condition (
        $stubbornStopwatch.Elapsed.TotalSeconds -ge 6 -and
        $stubbornStopwatch.Elapsed.TotalSeconds -lt 12) `
        "Stubborn-child watchdog did not exercise bounded TERM-to-KILL escalation (elapsed=$($stubbornStopwatch.Elapsed.TotalSeconds) s)."
    Assert-Condition (
        (Test-Path -LiteralPath $stubbornLeaderPid -PathType Leaf) -and
        (Test-Path -LiteralPath $stubbornDescendantPid -PathType Leaf) -and
        (Test-Path -LiteralPath $stubbornLeaderWindowsPid -PathType Leaf) -and
        (Test-Path -LiteralPath $stubbornDescendantWindowsPid -PathType Leaf)) `
        "Stubborn-child watchdog did not record both POSIX and Windows group members."
    $groupIdentifier = (Get-Content -LiteralPath $stubbornLeaderPid -Raw).Trim()
    $descendantIdentifier = (Get-Content -LiteralPath $stubbornDescendantPid -Raw).Trim()
    $leaderWindowsIdentifier = (Get-Content -LiteralPath $stubbornLeaderWindowsPid -Raw).Trim()
    $descendantWindowsIdentifier = (Get-Content -LiteralPath $stubbornDescendantWindowsPid -Raw).Trim()
    Assert-Condition (
        $groupIdentifier -match '^[0-9]+$' -and
        $descendantIdentifier -match '^[0-9]+$' -and
        $leaderWindowsIdentifier -match '^[0-9]+$' -and
        $descendantWindowsIdentifier -match '^[0-9]+$') `
        "Stubborn-child watchdog recorded invalid process identifiers."
    $reapInfo = [Diagnostics.ProcessStartInfo]::new()
    $reapInfo.FileName = $bashPath
    $reapInfo.UseShellExecute = $false
    $reapInfo.RedirectStandardOutput = $true
    $reapInfo.RedirectStandardError = $true
    $reapInfo.ArgumentList.Add("--noprofile")
    $reapInfo.ArgumentList.Add("--norc")
    $reapInfo.ArgumentList.Add("-c")
    $reapInfo.ArgumentList.Add(
        'if kill -0 -- "-$1" 2>/dev/null || kill -0 -- "$2" 2>/dev/null; then exit 1; fi')
    $reapInfo.ArgumentList.Add("--")
    $reapInfo.ArgumentList.Add($groupIdentifier)
    $reapInfo.ArgumentList.Add($descendantIdentifier)
    $reapProcess = [Diagnostics.Process]::new()
    $reapProcess.StartInfo = $reapInfo
    Assert-Condition $reapProcess.Start() "Unable to verify stubborn process-group cleanup."
    $reapStdout = $reapProcess.StandardOutput.ReadToEnd()
    $reapStderr = $reapProcess.StandardError.ReadToEnd()
    $reapProcess.WaitForExit()
    if ($reapProcess.ExitCode -ne 0) {
        $cleanupInfo = [Diagnostics.ProcessStartInfo]::new()
        $cleanupInfo.FileName = $bashPath
        $cleanupInfo.UseShellExecute = $false
        $cleanupInfo.ArgumentList.Add("--noprofile")
        $cleanupInfo.ArgumentList.Add("--norc")
        $cleanupInfo.ArgumentList.Add("-c")
        $cleanupInfo.ArgumentList.Add('kill -KILL -- "-$1" 2>/dev/null || true')
        $cleanupInfo.ArgumentList.Add("--")
        $cleanupInfo.ArgumentList.Add($groupIdentifier)
        $cleanupProcess = [Diagnostics.Process]::Start($cleanupInfo)
        [void]$cleanupProcess.WaitForExit(3000)
    }
    Assert-Condition ($reapProcess.ExitCode -eq 0) `
        "Stubborn process group or descendant survived watchdog cleanup. stdout=$reapStdout stderr=$reapStderr"
    $windowsSurvivors = [Collections.Generic.List[int]]::new()
    foreach ($windowsIdentifier in @(
        $leaderWindowsIdentifier,
        $descendantWindowsIdentifier)) {
        $syntheticWindowsProcess = Get-Process -Id ([int]$windowsIdentifier) -ErrorAction SilentlyContinue
        if ($null -ne $syntheticWindowsProcess) {
            $windowsSurvivors.Add([int]$windowsIdentifier)
            $syntheticWindowsProcess.Kill($true)
            [void]$syntheticWindowsProcess.WaitForExit(3000)
        }
    }
    Assert-Condition ($windowsSurvivors.Count -eq 0) `
        "Windows-level audit found surviving stubborn synthetic processes: $($windowsSurvivors -join ','). Exact fail-safe cleanup was applied."

    # An explicit conventional run must be rejected before environment launch,
    # case lock, preparation, or summary mutation on a multirate export.
    $sentinel = Join-Path $testRoot "launcher-called.log"
    $fakeLauncher = Join-Path $testRoot "fake-openfoam-launcher.sh"
    $fakeLauncherText = @'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$TIMESTEP_POLICY_LAUNCH_SENTINEL"
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

    $rejectInfo = [Diagnostics.ProcessStartInfo]::new()
    $rejectInfo.FileName = $bashPath
    $rejectInfo.UseShellExecute = $false
    $rejectInfo.RedirectStandardOutput = $true
    $rejectInfo.RedirectStandardError = $true
    $rejectInfo.ArgumentList.Add("--login")
    $rejectInfo.ArgumentList.Add((Convert-ToGitBashPath $runner))
    $rejectInfo.ArgumentList.Add("2")
    $rejectInfo.ArgumentList.Add("run")
    $rejectInfo.ArgumentList.Add("1")
    $rejectInfo.Environment["OPENFOAM_LAUNCHER"] = Convert-ToGitBashPath $fakeLauncher
    $rejectInfo.Environment["TIMESTEP_POLICY_LAUNCH_SENTINEL"] = Convert-ToGitBashPath $sentinel
    $rejectInfo.Environment["TMPDIR"] = Convert-ToGitBashPath $testRoot
    [void]$rejectInfo.Environment.Remove("THERMAL_SOLVER_SCRIPT_SNAPSHOT")
    [void]$rejectInfo.Environment.Remove("THERMAL_SOLVER_SCRIPT_SNAPSHOT_PATH")
    [void]$rejectInfo.Environment.Remove("THERMAL_SOLVER_CASE_DIR")
    [void]$rejectInfo.Environment.Remove("THERMAL_SOLVER_OPENFOAM_ENV_READY")

    $rejectProcess = [Diagnostics.Process]::new()
    $rejectProcess.StartInfo = $rejectInfo
    Assert-Condition $rejectProcess.Start() "Unable to execute generated runner."
    $rejectStdout = $rejectProcess.StandardOutput.ReadToEnd()
    $rejectStderr = $rejectProcess.StandardError.ReadToEnd()
    $rejectProcess.WaitForExit()
    Assert-Condition ($rejectProcess.ExitCode -eq 2) `
        "Explicit run returned $($rejectProcess.ExitCode), expected 2. stdout=$rejectStdout stderr=$rejectStderr"
    Assert-Condition ($rejectStderr.Contains(
        'Conventional run mode is disabled for this multirate export')) `
        "Explicit run did not emit the bypass diagnostic. stderr=$rejectStderr"
    Assert-Condition (-not (Test-Path -LiteralPath $sentinel)) `
        "OpenFOAM launcher was called before conventional run was rejected."
    Assert-FileStateUnchanged $lockBefore (Get-FileState $lockPath) "Run lock"
    Assert-FileStateUnchanged $summaryBefore (Get-FileState $summaryPath) "Run summary"
    Assert-Condition (@(Get-ChildItem -LiteralPath $testRoot -Filter "thermal-run-parallel.*" -File).Count -eq 0) `
        "Rejected conventional run left a script snapshot behind."

    # A syntactically valid but non-finite exponent must also be rejected before
    # environment setup or launcher use.
    $nonfiniteSentinel = Join-Path $testRoot "nonfinite-launcher-called.log"
    $nonfiniteInfo = [Diagnostics.ProcessStartInfo]::new()
    $nonfiniteInfo.FileName = $bashPath
    $nonfiniteInfo.UseShellExecute = $false
    $nonfiniteInfo.RedirectStandardOutput = $true
    $nonfiniteInfo.RedirectStandardError = $true
    $nonfiniteInfo.ArgumentList.Add("--login")
    $nonfiniteInfo.ArgumentList.Add((Convert-ToGitBashPath $runner))
    $nonfiniteInfo.ArgumentList.Add("2")
    $nonfiniteInfo.ArgumentList.Add("--warm-start")
    $nonfiniteInfo.ArgumentList.Add("1")
    $nonfiniteInfo.Environment["OPENFOAM_LAUNCHER"] = Convert-ToGitBashPath $fakeLauncher
    $nonfiniteInfo.Environment["TIMESTEP_POLICY_LAUNCH_SENTINEL"] = Convert-ToGitBashPath $nonfiniteSentinel
    $nonfiniteInfo.Environment["THERMAL_WARM_START_MAX_DT"] = "1e999"
    $nonfiniteInfo.Environment["TMPDIR"] = Convert-ToGitBashPath $testRoot
    [void]$nonfiniteInfo.Environment.Remove("THERMAL_SOLVER_SCRIPT_SNAPSHOT")
    [void]$nonfiniteInfo.Environment.Remove("THERMAL_SOLVER_SCRIPT_SNAPSHOT_PATH")
    [void]$nonfiniteInfo.Environment.Remove("THERMAL_SOLVER_CASE_DIR")
    [void]$nonfiniteInfo.Environment.Remove("THERMAL_SOLVER_OPENFOAM_ENV_READY")
    $nonfiniteProcess = [Diagnostics.Process]::new()
    $nonfiniteProcess.StartInfo = $nonfiniteInfo
    Assert-Condition $nonfiniteProcess.Start() "Unable to execute non-finite prevalidation case."
    $nonfiniteStdout = $nonfiniteProcess.StandardOutput.ReadToEnd()
    $nonfiniteStderr = $nonfiniteProcess.StandardError.ReadToEnd()
    $nonfiniteProcess.WaitForExit()
    Assert-Condition ($nonfiniteProcess.ExitCode -eq 2) `
        "Non-finite timestep returned $($nonfiniteProcess.ExitCode), expected 2. stdout=$nonfiniteStdout stderr=$nonfiniteStderr"
    Assert-Condition ($nonfiniteStderr.Contains(
        'THERMAL_WARM_START_MAX_DT must be a positive finite number')) `
        "Non-finite timestep did not emit the finite-number diagnostic. stderr=$nonfiniteStderr"
    Assert-Condition (-not (Test-Path -LiteralPath $nonfiniteSentinel)) `
        "OpenFOAM launcher was called before non-finite timestep rejection."

    $caseTreeAfter = Get-DirectoryContentState $resolvedCase
    Assert-Condition ($caseTreeAfter -ceq $caseTreeBefore) `
        "Generated-runner policy tests mutated the supplied case tree."

    Write-Host (
        "generated_runner_timestep_policy_test PASSED: exactEndpointCases=8; " +
        "undershootRejected=3; overshootRejected=2; largeTimeCapRejected=1; " +
        "partialRampResumed=1; pauseClassifierCases=6; stageOrderCases=3; " +
        "warmPlannerWindows=3; warmExecutedWindows=3; warmPostflights=3; " +
        "warmMetadataRejected=1; warmSourceMismatchRejected=1; " +
        "restartCourantRechecks=8; fanRampRestartCases=3; " +
        "fanRampRecoveryFailures=2; restartUnsafeRejected=4; " +
        "refreshJournalCases=7; termExit=143; restoreCalls=1; " +
        "stubbornWatchdogKill=1; stubbornDescendantsRemaining=0; " +
        "nonFiniteRejected=1; conventionalRunExit=2; bashSyntax=pass; " +
        "prevalidationLauncherCalls=0; caseWrites=0")
}
finally {
    if (Test-Path -LiteralPath $testRoot) {
        $cleanupTarget = [IO.Path]::GetFullPath($testRoot)
        Assert-Condition (
            $cleanupTarget.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -and
            ([IO.Path]::GetFileName($cleanupTarget)).StartsWith(
                "timestep_policy_runner_test_", [StringComparison]::Ordinal)) `
            "Refusing unsafe generated-runner test cleanup: $cleanupTarget"
        Remove-Item -LiteralPath $cleanupTarget -Recurse -Force
    }
}
