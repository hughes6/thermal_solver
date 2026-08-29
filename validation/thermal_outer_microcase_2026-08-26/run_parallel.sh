#!/usr/bin/env bash
set -euo pipefail

echo "ERROR: this persistent OpenFOAM runner predates the required custom-solver mode-policy gate and is quarantined. Regenerate the case runner with the current exporter before any OpenFOAM launch; no environment setup, lock, or case write was attempted." >&2
exit 14

invoked_script="$(readlink -f "$0")"
case_dir="$(cd "$(dirname "$invoked_script")" && pwd)"
script_snapshot_path="${THERMAL_SOLVER_SCRIPT_SNAPSHOT_PATH:-}"
cleanup_script_snapshot()
{
    if [[ -n "$script_snapshot_path" && -f "$script_snapshot_path" ]]; then
        rm -f -- "$script_snapshot_path"
    fi
}
trap cleanup_script_snapshot EXIT INT TERM
if [[ "${THERMAL_SOLVER_SCRIPT_SNAPSHOT:-0}" != 1 ]]; then
    script_snapshot_path=$(mktemp "${TMPDIR:-/tmp}/thermal-run-parallel.XXXXXX")
    cp -- "$invoked_script" "$script_snapshot_path"
    bash -n "$script_snapshot_path"
    exec env THERMAL_SOLVER_SCRIPT_SNAPSHOT=1 THERMAL_SOLVER_SCRIPT_SNAPSHOT_PATH="$script_snapshot_path" THERMAL_SOLVER_CASE_DIR="$case_dir" bash "$script_snapshot_path" "$@"
fi
case_dir="${THERMAL_SOLVER_CASE_DIR:-$case_dir}"
foam_launcher="${OPENFOAM_LAUNCHER:-openfoam2606}"
processes="${1:-2}"
mode="${2:-run}"
requested_end="${3:-12.5}"

airflow_refresh_interval="${4:-300}"
warm_start_maximum_time_step="${THERMAL_WARM_START_MAX_DT:-0.001}"

thermal_only_outer_correctors="${THERMAL_ONLY_OUTER_CORRECTORS:-2}"

if ! [[ "$processes" =~ ^[1-9][0-9]*$ ]] || (( processes < 2 )); then
    echo "Process count must be an integer of at least two for OpenFOAM parallel mode." >&2
    exit 2
fi
if [[ "$mode" != "run" && "$mode" != "--warm-start" && "$mode" != "--multirate" ]]; then
    echo "Usage: $0 [processes] [--warm-start|--multirate [end-time] [airflow-refresh-interval]]" >&2
    exit 2
fi
if [[ "$mode" != "run" ]] && { ! [[ "$requested_end" =~ ^[0-9]+([.][0-9]+)?$ ]] || ! awk -v v="$requested_end" 'BEGIN { exit !(v>0) }'; }; then
    echo "Requested end time must be a positive number." >&2
    exit 2
fi

if [[ "$mode" == "--multirate" ]] && ! [[ "$airflow_refresh_interval" =~ ^[0-9]+([.][0-9]+)?$ ]] || [[ "$mode" == "--multirate" ]] && ! awk -v v="$airflow_refresh_interval" 'BEGIN { exit !(v>0) }'; then
    echo "Airflow refresh interval must be a positive number." >&2
    exit 2
fi

if ! [[ "$warm_start_maximum_time_step" =~ ^[0-9]+([.][0-9]*)?([eE][-+]?[0-9]+)?$ ]] || ! awk -v v="$warm_start_maximum_time_step" 'BEGIN { exit !(v>0) }'; then
    echo "THERMAL_WARM_START_MAX_DT must be a positive finite number." >&2
    exit 2
fi

if ! [[ "$thermal_only_outer_correctors" =~ ^[1-9][0-9]*$ ]]; then
    echo "THERMAL_ONLY_OUTER_CORRECTORS must be a positive integer." >&2
    exit 2
fi

if [[ -n "${THERMAL_ONLY_OUTER_CORRECTORS+x}" ]] && (( thermal_only_outer_correctors < 2 )); then
    echo "THERMAL_ONLY_OUTER_CORRECTORS must be at least 2; one pass disables the additional nonlinear energy-coupling loop." >&2
    exit 2
fi

if [[ "${THERMAL_SOLVER_OPENFOAM_ENV_READY:-0}" != 1 && "$foam_launcher" != env ]]; then
    echo "Initializing OpenFOAM environment once with $foam_launcher."
    exec "$foam_launcher" env THERMAL_SOLVER_OPENFOAM_ENV_READY=1 OPENFOAM_LAUNCHER=env bash "$script_snapshot_path" "$@"
fi

run_lock="$case_dir/.thermal_solver_run.lock"
if ! command -v flock >/dev/null 2>&1; then
    echo "Required command 'flock' is unavailable." >&2
    exit 4
fi
exec 9>>"$run_lock"
if ! flock -n 9; then
    owner="$(tail -n 1 "$run_lock" 2>/dev/null || true)"
    echo "Another thermal solver is already writing this case${owner:+ (PID $owner)}." >&2
    exit 3
fi
: >"$run_lock"
printf '%s\n' "$$" >&9
summary_log="$case_dir/run_summary.log"
summary()
{
    printf '%s | %s\n' "$(date --iso-8601=seconds)" "$*" >> "$summary_log"
}
summary "run_start mode=$mode processes=$processes requestedEnd=$requested_end airflowRefreshInterval=$airflow_refresh_interval warmStartMaxDt=$warm_start_maximum_time_step liveOuterCorrectors=3 thermalOnlyOuterCorrectors=$thermal_only_outer_correctors"

# Distinct directory spellings can represent the same numeric
# OpenFOAM time (for example 730000.1 and
# 730000.09999999998). Different utilities may select different
# copies, so reject the ambiguity before reading or writing fields.
mapfile -t root_time_dirs < <(find "$case_dir" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | awk '$0 ~ /^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/ { print }' | sort -g)
for ((time_index=1; time_index<${#root_time_dirs[@]}; ++time_index)); do
    previous_time="${root_time_dirs[$((time_index-1))]}"
    candidate_time="${root_time_dirs[$time_index]}"
    if [[ "$previous_time" != "$candidate_time" ]] && awk -v a="$previous_time" -v b="$candidate_time" 'BEGIN { d=a-b; if(d<0)d=-d; s=(a<0?-a:a); t=(b<0?-b:b); if(t>s)s=t; if(s<1)s=1; exit !(d<=1e-12*s) }'; then
        echo "Ambiguous duplicate OpenFOAM root times: $previous_time and $candidate_time. Keep only the mapped/authoritative directory before restarting." >&2
        exit 8
    fi
done

processor_time_complete()
{
    local candidate="$1" rank_count="${2:-$processes}" rank field time_dir region_dir region
    for ((rank=0; rank<rank_count; ++rank)); do
        time_dir="$case_dir/processor${rank}/${candidate}"
        [[ -d "$time_dir/fluid" ]] || return 1
        for field in T U p p_rgh phi rho k omega nut alphat; do
            [[ -f "$time_dir/fluid/$field" ]] || return 1
        done
        for region_dir in "$case_dir/processor${rank}/constant"/*; do
            [[ -d "$region_dir/polyMesh" ]] || continue
            region="${region_dir##*/}"
            [[ "$region" == fluid ]] && continue
            [[ -f "$time_dir/$region/T" ]] || return 1
        done
    done
    return 0
}

latest_complete_processor_time()
{
    local rank_count="${1:-$processes}" candidate
    [[ -d "$case_dir/processor0" ]] || { echo 0; return; }
    while IFS= read -r candidate; do
        if processor_time_complete "$candidate" "$rank_count"; then
            echo "$candidate"
            return
        fi
    done < <(find "$case_dir/processor0" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | awk '$0 ~ /^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/ { print }' | sort -gr)
    echo 0
}

existing_processes=0
while [[ -d "$case_dir/processor${existing_processes}" ]]; do
    ((existing_processes+=1))
done

preflight_warm_start_state()
{
    local require_processors="${1:-false}" scan_reports="${2:-true}" expected=0 index common_time configured_start configured_start_from numeric_dir candidate report_file future_sample
    local invalid=false common_complete=true
    local -a processor_indices=() stale_report_dirs=() stale_report_samples=()
    mapfile -t processor_indices < <(find "$case_dir" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | awk '$0 ~ /^processor[0-9]+$/ { print substr($0,10) }' | sort -n)
    if ((${#processor_indices[@]}==0)); then
        if [[ "$require_processors" == true ]]; then
            echo "Warm-start preflight could not find processor directories after decomposition." >&2
            return 12
        fi
        return 0
    fi
    for index in "${processor_indices[@]}"; do
        if ((10#$index!=expected)) || [[ ! -d "$case_dir/processor${expected}" ]]; then
            echo "Warm-start preflight requires one contiguous processor set (processor0..processor$((${#processor_indices[@]}-1))); found a gap, duplicate numeric rank, or non-canonical directory." >&2
            return 12
        fi
        ((expected+=1))
    done
    common_time=$(latest_complete_processor_time "$expected")
    if ! processor_time_complete "$common_time" "$expected"; then
        common_complete=false
    fi
    configured_start=$(awk '
        /^[[:space:]]*startTime[[:space:]]+/ {
            value=$2; sub(/;.*/,"",value); print value; found=1; exit
        }
        END { if(!found) exit 1 }
    ' "$case_dir/system/controlDict") || {
        echo "Warm-start preflight could not read startTime from system/controlDict." >&2
        return 12
    }
    configured_start_from=$(awk '
        /^[[:space:]]*startFrom[[:space:]]+/ {
            value=$2; sub(/;.*/,"",value); print value; found=1; exit
        }
        END { if(!found) exit 1 }
    ' "$case_dir/system/controlDict") || {
        echo "Warm-start preflight could not read startFrom from system/controlDict." >&2
        return 12
    }
    case "$configured_start_from" in
        startTime|latestTime|firstTime) ;;
        *)
            echo "Warm-start preflight found unsupported startFrom '$configured_start_from' in system/controlDict." >&2
            return 12
            ;;
    esac
    if ! [[ "$configured_start" =~ ^[0-9]+([.][0-9]*)?([eE][-+]?[0-9]+)?$ ]]; then
        echo "Warm-start preflight found non-numeric configured startTime '$configured_start'." >&2
        return 12
    fi
    if [[ "$configured_start_from" == startTime ]] && awk -v configured="$configured_start" -v common="$common_time" 'BEGIN { scale=(configured<0?-configured:configured); other=(common<0?-common:common); if(other>scale)scale=other; if(scale<1)scale=1; exit !(configured>common+1e-9*scale) }'; then
        echo "Warm-start preflight rejected configured startTime $configured_start: it is newer than the latest common complete processor checkpoint $common_time." >&2
        invalid=true
    fi
    if [[ "$scan_reports" == true && -d "$case_dir/postProcessing" ]]; then
        while IFS= read -r numeric_dir; do
            candidate="${numeric_dir##*/}"
            if awk -v report="$candidate" -v common="$common_time" 'BEGIN { scale=(report<0?-report:report); other=(common<0?-common:common); if(other>scale)scale=other; if(scale<1)scale=1; exit !(report>common+1e-9*scale) }'; then
                stale_report_dirs+=("${numeric_dir#"$case_dir/"}")
            fi
        done < <(find "$case_dir/postProcessing" -mindepth 1 -type d -printf '%p\n' | awk -F/ '$NF ~ /^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/ { print }' | sort)
        while IFS=$'\t' read -r report_file future_sample; do
            [[ -n "$report_file" && -n "$future_sample" ]] || continue
            stale_report_samples+=("${report_file#"$case_dir/"} (sample t=$future_sample)")
        done < <(find "$case_dir/postProcessing" -type f \( -name '*.dat' -o -name '*.csv' \) -exec awk -v common="$common_time" '
                {
                    line=$0; sub(/^[[:space:]]+/ ,"",line)
                    count=split(line,column,/[[:space:],]+/)
                    if(count<1) next
                    value=column[1]
                    if(value !~ /^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/) next
                    scale=(value<0?-value:value)
                    other=(common<0?-common:common)
                    if(other>scale)scale=other
                    if(scale<1)scale=1
                    if(value>common+1e-9*scale) {
                        print FILENAME "\t" value
                        nextfile
                    }
                }
            ' {} +)
    fi
    if ((${#stale_report_dirs[@]}>0)); then
        echo "Warm-start preflight rejected postProcessing time directories newer than the latest common complete processor checkpoint $common_time:" >&2
        printf '  %s\n' "${stale_report_dirs[@]}" >&2
        invalid=true
    fi
    if ((${#stale_report_samples[@]}>0)); then
        echo "Warm-start preflight rejected postProcessing data files containing first-column time samples newer than the latest common complete processor checkpoint $common_time:" >&2
        printf '  %s\n' "${stale_report_samples[@]}" >&2
        invalid=true
    fi
    if [[ "$common_complete" != true ]]; then
        if [[ "$invalid" != true ]] && { [[ "$configured_start_from" != startTime ]] || awk -v configured="$configured_start" 'BEGIN { exit !(configured==0) }'; }; then
            echo "Warm-start preflight found only the decomposed t=0 initial state; no completed solver checkpoint exists yet."
            summary "warm_start_preflight_passed commonComplete=none initialState=0 processors=$expected"
            return 0
        fi
        echo "Warm-start preflight found no common complete numeric checkpoint across all $expected processor directories." >&2
        invalid=true
    fi
    if [[ "$invalid" == true ]]; then
        echo "Quarantine or remove rejected/future postProcessing data before continuing. If startFrom is startTime, restore startTime to no later than $common_time. No solver stage was started." >&2
        summary "warm_start_preflight_rejected commonComplete=$common_time startFrom=$configured_start_from configuredStart=$configured_start staleReportDirectories=${#stale_report_dirs[@]} staleReportFiles=${#stale_report_samples[@]}"
        return 12
    fi
    summary "warm_start_preflight_passed commonComplete=$common_time processors=$expected"
}

if [[ "$mode" == "--warm-start" ]]; then
    preflight_warm_start_state false true || exit $?
fi

discard_incomplete_processor_tail()
{
    local accepted="$1" rank candidate time_dir
    for ((rank=0; rank<processes; ++rank)); do
        while IFS= read -r time_dir; do
            candidate="${time_dir##*/}"
            if awk -v t="$candidate" -v a="$accepted" 'BEGIN { exit !(t>a+1e-12) }'; then
                rm -rf -- "$time_dir"
                echo "Discarded incomplete processor${rank} checkpoint: $candidate"
            fi
        done < <(find "$case_dir/processor${rank}" -mindepth 1 -maxdepth 1 -type d -printf '%p\n' | awk -F/ '$NF ~ /^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/')
    done
}

# mapFields maps volume fields but not the face-flux field phi.
# A nonzero mapped case must establish phi with a coupled warm start
# before thermal-only multirate operation. Accept reconstructed or
# current processor phi from an ordinary valid restart.
if [[ "$mode" == "--multirate" ]]; then
    mapped_root_latest=$("$foam_launcher" foamListTimes -case "$case_dir" -latestTime 2>/dev/null || echo 0)
    mapped_root_latest="${mapped_root_latest##*$'\n'}"
    mapped_root_latest="${mapped_root_latest:-0}"
    mapped_phi_present=false
    if [[ -f "$case_dir/$mapped_root_latest/fluid/phi" ]]; then
        mapped_phi_present=true
    elif [[ -d "$case_dir/processor0" ]]; then
        mapped_processor_latest=$(latest_complete_processor_time "$existing_processes")
        if [[ -n "$mapped_processor_latest" && -f "$case_dir/processor0/$mapped_processor_latest/fluid/phi" ]]; then
            mapped_phi_present=true
        fi
    fi
    if awk -v t="$mapped_root_latest" 'BEGIN { exit !(t>0) }' && [[ "$mapped_phi_present" != true ]]; then
        echo "Mapped/nonzero checkpoint t=$mapped_root_latest has no face-flux field fluid/phi. Run a short coupled --warm-start to an end time greater than $mapped_root_latest before --multirate." >&2
        exit 9
    fi
fi

# Reuse a complete, current decomposition. Reconstruct and repartition only when processor state is missing, stale, or uses a different process count.
reuse_decomposition=false
processor_dirs=("$case_dir"/processor[0-9]*)
if [[ -f "$case_dir/.openfoam_regions_prepared" && ${#processor_dirs[@]} -eq "$processes" ]]; then
    reuse_decomposition=true
    for ((rank=0; rank<processes; ++rank)); do
        if [[ ! -d "$case_dir/processor${rank}" ]]; then
            reuse_decomposition=false
            break
        fi
    done
fi
if [[ "$reuse_decomposition" == true ]]; then
    root_latest=$("$foam_launcher" foamListTimes -case "$case_dir" -latestTime 2>/dev/null || echo 0)
    root_latest="${root_latest##*$'\n'}"
    raw_processor_latest=$("$foam_launcher" foamListTimes -case "$case_dir" -processor -latestTime 2>/dev/null || echo 0)
    raw_processor_latest="${raw_processor_latest##*$'\n'}"
    processor_latest=$(latest_complete_processor_time)
    if awk -v raw="${raw_processor_latest:-0}" -v complete="${processor_latest:-0}" 'BEGIN { exit !(raw>complete+1e-12) }'; then
        discard_incomplete_processor_tail "$processor_latest"
    fi
    if awk -v p="${processor_latest:-0}" -v r="${root_latest:-0}" 'BEGIN { exit !(p+1e-9<r) }'; then
        reuse_decomposition=false
    fi
fi
if [[ "$reuse_decomposition" == true ]]; then
    echo "Reusing $processes valid processor partitions at t=${processor_latest:-0}."
else
    if [[ -d "$case_dir/processor0" ]]; then
        root_latest=$("$foam_launcher" foamListTimes -case "$case_dir" -latestTime 2>/dev/null || echo 0)
        root_latest="${root_latest##*$'\n'}"
        processor_latest=$(latest_complete_processor_time "$existing_processes")
        if awk -v p="${processor_latest:-0}" -v r="${root_latest:-0}" 'BEGIN { exit !(p>r) }'; then
        echo "Reconstructing interrupted parallel time $processor_latest before redecomposition."
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry deltaT -set 0.0050000000000000001
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry writeInterval -set 2.5
        "$foam_launcher" reconstructPar -case "$case_dir" -allRegions -latestTime
        fi
    fi
    # decomposePar -force rewrites requested ranks but does not
    # remove surplus processor directories when the rank count
    # decreases. They would make reconstructPar read stale data.
    for processor_dir in "$case_dir"/processor[0-9]*; do
        [[ -d "$processor_dir" ]] || continue
        rm -rf -- "$processor_dir"
    done
    if [[ -f "$case_dir/constant/fluid/pressureJumpFanCurves.json" ]] && grep -q '_pressure_jump_master' "$case_dir/constant/fluid/polyMesh/boundary"; then
        echo "Reusing prepared region mesh with cyclic fan baffles."
    else
        bash "$case_dir/prepare_regions.sh"
    fi
    "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/decomposeParDict" -entry numberOfSubdomains -set "$processes"
    "$foam_launcher" decomposePar -case "$case_dir" -allRegions -latestTime -force

fi

if [[ "$mode" == "--warm-start" ]]; then
    preflight_warm_start_state true false || exit $?
fi

full_fan_options="$case_dir/constant/fluid/fvOptions.fullFan"
flow_only_options="$case_dir/constant/fluid/fvOptions.flowOnly"
fan_ramp_complete_marker="$case_dir/.fan_ramp_complete"
mapped_state_marker="$case_dir/.mapped_initial_state"
fan_options_source="$full_fan_options"
install_fluid_options()
{
    local source="$1" processor_dir
    if [[ ! -f "$source" ]]; then
        echo "Missing fluid options dictionary: $source" >&2
        return 2
    fi
    cp "$source" "$case_dir/constant/fluid/fvOptions"
    for processor_dir in "$case_dir"/processor[0-9]*; do
        [[ -d "$processor_dir" ]] || continue
        mkdir -p "$processor_dir/constant/fluid"
        cp "$source" "$processor_dir/constant/fluid/fvOptions"
    done
}
restore_full_fan_options()
{
    if [[ -f "$full_fan_options" ]]; then
        install_fluid_options "$full_fan_options"
    fi
}
restore_live_outer_correctors()
{
    if [[ -f "$case_dir/system/fvSolution" ]]; then
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/fvSolution" -entry PIMPLE/nOuterCorrectors -set 3 >/dev/null 2>&1 || true
    fi
}
restore_run_state()
{
    restore_full_fan_options
    restore_live_outer_correctors
    cleanup_script_snapshot
}
trap restore_run_state EXIT INT TERM
set_fan_scale()
{
    local scale="$1" processor_dir full_pressure scaled_pressure measured_scale
    if [[ -f "$case_dir/scale_pressure_jump_fans.py" && -f "$case_dir/constant/fluid/pressureJumpFanCurves.json" ]]; then
        python3 "$case_dir/scale_pressure_jump_fans.py" "$case_dir" "$scale"
        return
    fi
    awk -v scale="$scale" '
        NF==2 && substr($1,1,1)=="(" && index($2,")")>0 {
            q=$1; dp=$2; gsub(/[()]/,"",q); gsub(/[()\r]/,"",dp);
            printf "   (%s %.17g)\n", q, dp*scale; next
        }
        { print }
    ' "$fan_options_source" > "$case_dir/constant/fluid/fvOptions"
    full_pressure=$(awk '$1=="(0" { gsub(/[()]/,"",$2); print $2; exit }' "$fan_options_source")
    scaled_pressure=$(awk '$1=="(0" { gsub(/[()]/,"",$2); print $2; exit }' "$case_dir/constant/fluid/fvOptions")
    if [[ -z "$full_pressure" ]]; then
        echo "No curve-driven fan sources require scaling."
        return 0
    fi
    measured_scale=$(awk -v scaled="$scaled_pressure" -v full="$full_pressure" 'BEGIN { if(full==0) print 1; else print scaled/full }')
    if ! awk -v actual="$measured_scale" -v expected="$scale" 'BEGIN { d=actual-expected; if(d<0)d=-d; exit !(d<=1e-6) }'; then
        echo "Fan ramp scaling verification failed: requested=$scale measured=$measured_scale." >&2
        return 4
    fi
    echo "Applied fan pressure scale $scale (first shutoff pressure $scaled_pressure Pa)."
    for processor_dir in "$case_dir"/processor[0-9]*; do
        [[ -d "$processor_dir" ]] || continue
        mkdir -p "$processor_dir/constant/fluid"
        cp "$case_dir/constant/fluid/fvOptions" "$processor_dir/constant/fluid/fvOptions"
    done
}
is_restartable_processor_time()
{
    local candidate="$1" rank root
    [[ "$candidate" =~ ^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$ ]] || return 1
    for ((rank=0; rank<processes; ++rank)); do
        root="$case_dir/processor${rank}/${candidate}/fluid"
        [[ -f "$root/U" && -f "$root/T" ]] || return 1
    done
}
latest_processor_restart_time()
{
    local candidate
    while IFS= read -r candidate; do
        if is_restartable_processor_time "$candidate"; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done < <(find "$case_dir/processor0" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' 2>/dev/null | awk '$0 ~ /^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/ { print }' | sort -gr)
    printf '0\n'
}
preflight_checkpoint_space()
{
    local latest available_kb checkpoint_kb=0 required_kb rank path
    latest=$(latest_processor_restart_time)
    available_kb=$(df -Pk "$case_dir" | awk 'NR==2 { print $4 }')
    if [[ -z "$available_kb" || ! "$available_kb" =~ ^[0-9]+$ ]]; then
        echo "Unable to determine free disk space for $case_dir." >&2
        return 10
    fi
    path="$case_dir/$latest"
    [[ -d "$path" ]] && checkpoint_kb=$((checkpoint_kb+$(du -sk "$path" | awk '{print $1}')))
    for ((rank=0; rank<processes; ++rank)); do
        path="$case_dir/processor${rank}/$latest"
        [[ -d "$path" ]] && checkpoint_kb=$((checkpoint_kb+$(du -sk "$path" | awk '{print $1}')))
    done
    # Reserve two checkpoint equivalents for the new processor and
    # reconstructed fields, plus 512 MiB for temporary/log overhead.
    required_kb=$((2*checkpoint_kb+524288))
    echo "Disk preflight: available=${available_kb}KiB required=${required_kb}KiB checkpoint=${checkpoint_kb}KiB."
    if ((available_kb<required_kb)); then
        echo "Insufficient disk space for a recoverable checkpoint: available=${available_kb}KiB, require at least ${required_kb}KiB. Prune redundant completed times or free host storage before running." >&2
        return 10
    fi
}
prune_processor_times()
{
    local keep="3" processor_root candidate target rank remove_count index
    local -a times=() restart_times=()
    processor_root="$case_dir/processor0"
    [[ -d "$processor_root" ]] || return 0
    mapfile -t times < <(find "$processor_root" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | awk '$0 != "0" && $0 ~ /^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/ { print }' | sort -g)
    for candidate in "${times[@]}"; do
        is_restartable_processor_time "$candidate" && restart_times+=("$candidate")
    done
    times=("${restart_times[@]}")
    remove_count=$((${#times[@]}-keep))
    ((remove_count>0)) || return 0
    for ((index=0; index<remove_count; ++index)); do
        candidate="${times[$index]}"
        for ((rank=0; rank<processes; ++rank)); do
            processor_root="$case_dir/processor${rank}"
            target="$processor_root/$candidate"
            if [[ "$target" != "$processor_root/"* || ! "$candidate" =~ ^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$ ]]; then
                echo "Refusing unsafe checkpoint prune target: $target" >&2
                return 1
            fi
            [[ -d "$target" ]] && rm -rf -- "$target"
        done
        echo "Pruned completed processor checkpoint: $candidate"
    done
}
run_fan_ramp()
{
    local solver="$1" start="$2" limit="$3" step scale target interval ramp_cap ramp_plan ramp_dt ramp_steps saved_time saved_time_file rank
    ramp_current="$start"
    if [[ ! -f "$full_fan_options" ]]; then
        echo "Missing pristine fan options: $full_fan_options" >&2
        return 2
    fi
    echo "Ramping fan pressure from 0 to 100% over 0.050000000000000003 s in 5 stages."
    for step in $(seq 1 5); do
        target=$(awk -v duration="0.050000000000000003" -v i="$step" -v n="5" -v limit="$limit" 'BEGIN { x=duration*i/n; print (x<limit?x:limit) }')
        if ! awk -v a="$target" -v b="$ramp_current" 'BEGIN { exit !(a>b) }'; then continue; fi
        scale=$(awk -v target="$target" -v duration="0.050000000000000003" 'BEGIN { x=target/duration; print (x<1?x:1) }')
        interval=$(awk -v a="$target" -v b="$ramp_current" 'BEGIN { print a-b }')
        # Startup has no established flow field for a Courant
        # preflight. Seed below the requested Courant limit, then
        # let OpenFOAM adapt while clipping each ramp endpoint.
        ramp_cap=$(awk -v flow_max="0.001" -v co="0.40000000000000002" 'BEGIN { scale=(co<2?co/2:1); print flow_max*scale }')
        ramp_plan=$(awk -v maximum="$ramp_cap" -v remaining="$interval" 'BEGIN { n=int(remaining/maximum); if(n*maximum<remaining-1e-12)n++; if(n<1)n=1; printf "%.17g %d", remaining/n,n }')
        read -r ramp_dt ramp_steps <<<"$ramp_plan"
        set_fan_scale "$scale"
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry startFrom -set latestTime
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry endTime -set "$target"
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry adjustTimeStep -set true
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry deltaT -set "$ramp_dt"
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry maxDeltaT -set "$ramp_cap"
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry maxCo -set 0.40000000000000002
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry writeControl -set adjustableRunTime
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry writeInterval -set "$interval"
        saved_time=$(latest_processor_restart_time)
        if [[ -n "$saved_time" ]]; then
            for ((rank=0; rank<processes; ++rank)); do
                saved_time_file="$case_dir/processor${rank}/${saved_time}/uniform/time"
                [[ -f "$saved_time_file" ]] || continue
                "$foam_launcher" foamDictionary -precision 17 "$saved_time_file" -entry deltaT -set "$ramp_dt"
                "$foam_launcher" foamDictionary -precision 17 "$saved_time_file" -entry deltaT0 -set "$ramp_dt"
            done
        fi
        echo "Fan ramp stage $step/5: scale=$scale, t=$ramp_current -> $target, deltaT=$ramp_dt, steps=$ramp_steps"
        "$foam_launcher" mpirun -np "$processes" "$solver" -case "$case_dir" -parallel
        prune_processor_times
        ramp_current="$target"
    done
    set_fan_scale 1
    touch "$fan_ramp_complete_marker"
}

if [[ "$mode" == "--multirate" ]]; then
    current=$(latest_processor_restart_time)
    current="${current:-0}"
    if ! awk -v a="$current" -v b="$requested_end" 'BEGIN { s=(b<0?-b:b); if(s<1)s=1; tol=1e-9*s; exit !(b>a+tol) }'; then
        echo "Multirate end time $requested_end must be greater than the latest processor checkpoint $current; no airflow or thermal stage was run." >&2
        exit 11
    fi
    initial_convergence_marker="$case_dir/.initial_airflow_converged"
    initial_pending_marker="$case_dir/.initial_airflow_pending"
    initial_exchange_state="$case_dir/.initial_air_exchange_state"
    initial_physical_settling_marker="$case_dir/.initial_airflow_physical_settling"
    refresh_pending_marker="$case_dir/.airflow_refresh_pending"
    if [[ ! -f "$initial_convergence_marker" ]]; then
        if [[ -f "$mapped_state_marker" ]]; then
            fan_options_source="$full_fan_options"
            restore_full_fan_options
            echo "Mapped initial airflow retains full fluid heat sources."
        else
            fan_options_source="$flow_only_options"
            install_fluid_options "$flow_only_options"
            echo "Initial airflow uses fans and vents with fluid heat sources disabled."
            echo "Solid-region heat sources remain active during initial airflow; CHT and buoyancy continue to evolve."
        fi
    fi
    if [[ ! -f "$mapped_state_marker" ]] && [[ ! -f "$fan_ramp_complete_marker" ]] && awk -v a="$current" -v end="0.050000000000000003" 'BEGIN { exit !(a<end) }'; then
        run_fan_ramp semiFrozenChtMultiRegionFoam "$current" "$requested_end"
        current="$ramp_current"
    fi
    boundary_flow_names=("test_inlet" "test_outlet" )
    declare -A boundary_flow_lookup=()
    for name in "${boundary_flow_names[@]}"; do
        boundary_flow_lookup["$name"]=1
    done
    tracked_flow_names=("test_inlet" "test_outlet" )
    internal_fan_names=()
    stability_flow_names=("test_inlet" "test_outlet" )
    component_region_names=("test_heater_0" "homogeneous_heater_1" "air_side_heater_2" )
    fan_direction_rules=("test_inlet:-1" )
    fan_positive_pressure_rules=("test_inlet:0.61250000000000004" )
    fan_positive_pressure_names=()
    declare -A fan_positive_pressure_limits=()
    fan_domain_warning_fraction=0.9
    for rule in "${fan_positive_pressure_rules[@]}"; do
        name="${rule%%:*}"
        fan_positive_pressure_names+=("$name")
        fan_positive_pressure_limits["$name"]="${rule#*:}"
    done
    declare -A internal_fan_lookup=()
    for name in "${internal_fan_names[@]}"; do
        internal_fan_lookup["$name"]=1
    done
    declare -A previous_flows=()
    declare -A previous_smoothed_internal_flows=()
    airflow_convergence_state="$case_dir/.airflow_convergence_state"
    velocity_convergence_state="$case_dir/.velocity_convergence_state"
    load_airflow_convergence_state()
    {
        local state_time state_count name raw smoothed index=0 expected_name
        [[ -f "$airflow_convergence_state" ]] || return 1
        read -r state_time state_count < "$airflow_convergence_state" || return 1
        if ! [[ "$state_count" =~ ^[0-9]+$ ]] || (( state_count != ${#stability_flow_names[@]} )); then
            return 1
        fi
        if ! awk -v saved="$state_time" -v now="$current" 'BEGIN { if(saved !~ /^[-+]?[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/) exit 1; scale=(now<0?-now:now); if(scale<1)scale=1; exit !(saved<=now+1e-9*scale) }'; then
            return 1
        fi
        while read -r name raw smoothed; do
            (( index < ${#stability_flow_names[@]} )) || return 1
            expected_name="${stability_flow_names[$index]}"
            [[ "$name" == "$expected_name" ]] || return 1
            if ! awk -v value="$raw" 'BEGIN { exit !(value ~ /^[-+]?[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/) }'; then
                return 1
            fi
            if [[ "$smoothed" != - ]] && ! awk -v value="$smoothed" 'BEGIN { exit !(value ~ /^[-+]?[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/) }'; then
                return 1
            fi
            previous_flows["$name"]="$raw"
            if [[ "$smoothed" != - ]]; then
                previous_smoothed_internal_flows["$name"]="$smoothed"
            fi
            index=$((index+1))
        done < <(tail -n +2 "$airflow_convergence_state")
        (( index == ${#stability_flow_names[@]} )) || return 1
        echo "Restored airflow convergence baseline from t=$state_time s."
        return 0
    }
    if ! load_airflow_convergence_state; then
        previous_flows=()
        previous_smoothed_internal_flows=()
        if [[ -f "$airflow_convergence_state" ]]; then
            echo "Ignoring incompatible, malformed, or future airflow convergence state."
        fi
    fi
    latest_air_exchange_time=""
    latest_one_way_boundary_mass_flow=""
    previous_velocity_relative_rms=""
    latest_velocity_relative_rms=""
    load_velocity_convergence_state()
    {
        local state_time saved_latest saved_previous
        [[ -f "$velocity_convergence_state" ]] || return 1
        read -r state_time saved_latest saved_previous < "$velocity_convergence_state" || return 1
        if ! awk -v saved="$state_time" -v now="$current" 'BEGIN { if(saved !~ /^[-+]?[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/) exit 1; scale=(now<0?-now:now); if(scale<1)scale=1; exit !(saved<=now+1e-9*scale) }'; then return 1; fi
        if ! awk -v value="$saved_latest" 'BEGIN { exit !(value ~ /^[-+]?[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/) }'; then return 1; fi
        latest_velocity_relative_rms="$saved_latest"
        if awk -v value="$saved_previous" 'BEGIN { exit !(value ~ /^[-+]?[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/) }'; then
            previous_velocity_relative_rms="$saved_previous"
        fi
        echo "Restored spatial velocity convergence state from t=$state_time s."
        return 0
    }
    if ! load_velocity_convergence_state; then
        latest_velocity_relative_rms=""
        previous_velocity_relative_rms=""
        if [[ -f "$velocity_convergence_state" ]]; then
            echo "Ignoring incompatible, malformed, or future spatial velocity convergence state."
        fi
    fi
    accepted_airflow_reference="$case_dir/.accepted_airflow_reference"
    accepted_airflow_relative_rms=""
    field_internal_count()
    {
        awk '/^[[:space:]]*internalField[[:space:]]/{ getline; print $1; exit }' "$1" 2>/dev/null
    }
    record_accepted_airflow_reference()
    {
        local latest rank source target
        latest=$(latest_processor_restart_time)
        [[ -n "$latest" ]] || return 1
        rm -rf -- "$accepted_airflow_reference.tmp"
        mkdir -p "$accepted_airflow_reference.tmp"
        for ((rank=0; rank<processes; ++rank)); do
            source="$case_dir/processor${rank}/${latest}/fluid/U"
            target="$accepted_airflow_reference.tmp/processor${rank}"
            [[ -f "$source" ]] || return 1
            mkdir -p "$target"
            cp -p "$source" "$target/U"
        done
        printf '%s\n' "$latest" > "$accepted_airflow_reference.tmp/time"
        rm -rf -- "$accepted_airflow_reference"
        mv "$accepted_airflow_reference.tmp" "$accepted_airflow_reference"
    }
    validate_accepted_airflow_drift()
    {
        local latest reference_time rank source target current_field source_count current_count field spatial_output spatial_status=0 velocity_rms_delta velocity_rms_reference
        accepted_airflow_relative_rms=""
        if [[ ! -f "$accepted_airflow_reference/time" ]]; then
            record_accepted_airflow_reference || return 1
            echo "Accepted airflow baseline recorded; one more refresh is required for coupled convergence."
            return 1
        fi
        reference_time=$(<"$accepted_airflow_reference/time")
        latest=$(latest_processor_restart_time)
        [[ -n "$latest" ]] || return 1
        for ((rank=0; rank<processes; ++rank)); do
            rm -f -- "$case_dir/processor${rank}/${latest}/fluid/UPrevious"
        done
        for ((rank=0; rank<processes; ++rank)); do
            source="$accepted_airflow_reference/processor${rank}/U"
            current_field="$case_dir/processor${rank}/${latest}/fluid/U"
            target="$case_dir/processor${rank}/${latest}/fluid/UPrevious"
            source_count=$(field_internal_count "$source")
            current_count=$(field_internal_count "$current_field")
            if [[ ! "$source_count" =~ ^[0-9]+$ || ! "$current_count" =~ ^[0-9]+$ || "$source_count" != "$current_count" ]]; then
                echo "Accepted airflow reference is incomplete or incompatible with the current decomposition at rank $rank (referenceCells=${source_count:-unknown}, currentCells=${current_count:-unknown}); rebuilding it." >&2
                for ((rank=0; rank<processes; ++rank)); do
                    rm -f -- "$case_dir/processor${rank}/${latest}/fluid/UPrevious"
                done
                record_accepted_airflow_reference || return 1
                return 1
            fi
            cp -p "$source" "$target"
            "$foam_launcher" foamDictionary -precision 17 "$target" -entry FoamFile/object -set UPrevious >/dev/null 2>&1
        done
        if ! spatial_output=$("$foam_launcher" mpirun -np "$processes" semiFrozenChtMultiRegionFoam -case "$case_dir" -parallel -postProcess -latestTime -dict system/spatialConvergenceDict 2>&1); then
            spatial_status=1
        fi
        velocity_rms_delta=$(printf '%s\n' "$spatial_output" | awk '/velocityDeltaSquared =/{value=$NF} END{print value}')
        velocity_rms_reference=$(printf '%s\n' "$spatial_output" | awk '/velocitySquared =/{value=$NF} END{print value}')
        for ((rank=0; rank<processes; ++rank)); do
            for field in UPrevious velocityDelta velocityDeltaSquared velocitySquared; do
                rm -f -- "$case_dir/processor${rank}/${latest}/fluid/${field}"
            done
        done
        if [[ "$spatial_status" != 0 || -z "$velocity_rms_delta" || -z "$velocity_rms_reference" ]]; then
            printf '%s\n' "$spatial_output" >&2
            echo "Accepted airflow drift calculation failed at t=$latest." >&2
            return 1
        fi
        accepted_airflow_relative_rms=$(awk -v delta="$velocity_rms_delta" -v reference="$velocity_rms_reference" 'BEGIN { print (reference>1e-12?delta/reference:(delta<=1e-12?0:1e30)) }')
        echo "Accepted airflow drift: referenceTime=$reference_time, currentTime=$latest, rmsDelta=$velocity_rms_delta m/s, rmsVelocity=$velocity_rms_reference m/s, relativeRms=$accepted_airflow_relative_rms"
        if ! awk -v value="$accepted_airflow_relative_rms" -v limit="0.01" 'BEGIN { exit !(value<=limit) }'; then
            record_accepted_airflow_reference || return 1
            return 1
        fi
        return 0
    }
    airflow_metrics_converged()
    {
        local report name value rule expected net=0 sum_abs=0 flow_time properties air_exchange_time
        local imbalance stable=1 directions_ok=1 maximum_change=0 maximum_change_name=none change boundary_flow_floor=0 flow_floor=0 comparison_value comparison_reference airflow_state_tmp fan_domain_ok=1 fan_domain_warnings=0 fan_domain_failures=0 maximum_fan_domain_utilization=0 maximum_fan_domain_name=none domain_flow domain_limit utilization
        latest_one_way_boundary_mass_flow=""
        if ! report=$("$foam_launcher" mpirun -np "$processes" postProcess -case "$case_dir" -parallel -region fluid -latestTime -field phi 2>&1); then
            echo "$report" >&2
            echo "Unable to evaluate airflow refresh convergence." >&2
            return 2
        fi
        declare -A flows=()
        declare -A current_smoothed_internal_flows=()
        for name in "${tracked_flow_names[@]}"; do
            value=$(awk -v pattern="sum(${name}) of phi =" 'index($0,pattern) { value=$NF; print value; exit }' <<<"$report")
            if [[ -z "$value" ]]; then
                echo "Missing mass-flow result for $name." >&2
                return 2
            fi
            flows["$name"]="$value"
        done
        flow_time=$(latest_processor_restart_time)
        for name in "${internal_fan_names[@]}"; do
            properties="$case_dir/processor0/$flow_time/fluid/uniform/${name}Properties"
            value=$(awk '$1=="flow_rate" { gsub(/;/,"",$2); print $2; exit }' "$properties" 2>/dev/null || true)
            if [[ -z "$value" ]]; then
                echo "Missing fan operating-point output for $name at t=$flow_time." >&2
                return 2
            fi
            flows["$name"]="$value"
            if ! awk -v v="$value" 'BEGIN { exit !(v>0) }'; then
                directions_ok=0
                echo "Internal fan not producing positive through-flow: $name flow_rate=$value m3/s" >&2
            fi
        done
        for name in "${boundary_flow_names[@]}"; do
            value="${flows[$name]}"
            net=$(awk -v a="$net" -v b="$value" 'BEGIN { print a+b }')
            sum_abs=$(awk -v a="$sum_abs" -v b="$value" 'BEGIN { if(b<0)b=-b; print a+b }')
        done
        boundary_flow_floor=$(awk -v s="$sum_abs" -v f="0.0001" 'BEGIN { print 0.5*s*f }')
        for name in "${stability_flow_names[@]}"; do
            value="${flows[$name]}"
            comparison_value="$value"
            comparison_reference=""
            flow_floor=0
            if [[ -n "${boundary_flow_lookup[$name]+set}" ]]; then
                flow_floor="$boundary_flow_floor"
            fi
            if [[ -n "${internal_fan_lookup[$name]+set}" ]]; then
                if [[ -n "${previous_flows[$name]+set}" ]]; then
                    comparison_value=$(awk -v a="$value" -v b="${previous_flows[$name]}" 'BEGIN { print 0.5*(a+b) }')
                    current_smoothed_internal_flows["$name"]="$comparison_value"
                    comparison_reference="${previous_smoothed_internal_flows[$name]-}"
                fi
            elif [[ -n "${previous_flows[$name]+set}" ]]; then
                comparison_reference="${previous_flows[$name]}"
            fi
            if [[ -n "$comparison_reference" ]]; then
                change=$(awk -v a="$comparison_value" -v b="$comparison_reference" -v floor="$flow_floor" 'BEGIN { d=a-b; if(d<0)d=-d; aa=a; if(aa<0)aa=-aa; bb=b; if(bb<0)bb=-bb; if(floor>0 && aa<floor && bb<floor) { print 0; exit } s=bb; if(s<floor)s=floor; if(s<1e-12)s=1e-12; print d/s }')
                if awk -v a="$change" -v b="$maximum_change" 'BEGIN { exit !(a>b) }'; then
                    maximum_change="$change"
                    maximum_change_name="$name"
                fi
            else
                stable=0
            fi
        done
        if [[ ${#boundary_flow_names[@]} -eq 0 ]]; then
            # A sealed domain has no ambient mass-flow balance to evaluate.
            imbalance=0
        else
            imbalance=$(awk -v n="$net" -v s="$sum_abs" 'BEGIN { if(n<0)n=-n; d=0.5*s; print (d>1e-12?n/d:1e30) }')
        fi
        air_exchange_time=$(awk -v volume="0.017000000000000008" -v rho="1.2250000000000001" -v s="$sum_abs" 'BEGIN { one_way=0.5*s; print (one_way>1e-12?volume*rho/one_way:1e30) }')
        latest_air_exchange_time="$air_exchange_time"
        latest_one_way_boundary_mass_flow=$(awk -v s="$sum_abs" 'BEGIN { printf "%.17g", 0.5*s }')
        for rule in "${fan_direction_rules[@]}"; do
            name="${rule%%:*}"
            expected="${rule##*:}"
            value="${flows[$name]}"
            if ! awk -v v="$value" -v e="$expected" 'BEGIN { exit !((e<0 && v<0)||(e>0 && v>0)) }'; then
                directions_ok=0
                echo "Fan direction not settled: $name phi=$value" >&2
            fi
        done
        for name in "${fan_positive_pressure_names[@]}"; do
            value="${flows[$name]-}"
            domain_limit="${fan_positive_pressure_limits[$name]}"
            if [[ -z "$value" || -z "$domain_limit" ]]; then
                fan_domain_ok=0
                fan_domain_failures=$((fan_domain_failures+1))
                echo "Missing fan-domain flow or limit for $name." >&2
                continue
            fi
            domain_flow=$(awk -v v="$value" 'BEGIN { if(v<0)v=-v; printf "%.17g",v }')
            utilization=$(awk -v flow="$domain_flow" -v limit="$domain_limit" 'BEGIN { printf "%.17g",(limit>0?flow/limit:1e30) }')
            if awk -v a="$utilization" -v b="$maximum_fan_domain_utilization" 'BEGIN { exit !(a>b) }'; then
                maximum_fan_domain_utilization="$utilization"
                maximum_fan_domain_name="$name"
            fi
            if awk -v u="$utilization" 'BEGIN { exit !(u>=1) }'; then
                fan_domain_ok=0
                fan_domain_failures=$((fan_domain_failures+1))
                echo "Fan outside positive-pressure curve domain: $name flow=$domain_flow limit=$domain_limit utilization=$utilization" >&2
            elif awk -v u="$utilization" -v warning="$fan_domain_warning_fraction" 'BEGIN { exit !(u>=warning) }'; then
                fan_domain_warnings=$((fan_domain_warnings+1))
                echo "Fan near positive-pressure curve limit: $name flow=$domain_flow limit=$domain_limit utilization=$utilization" >&2
            fi
        done
        for name in "${stability_flow_names[@]}"; do
            previous_flows["$name"]="${flows[$name]}"
            if [[ -n "${current_smoothed_internal_flows[$name]+set}" ]]; then
                previous_smoothed_internal_flows["$name"]="${current_smoothed_internal_flows[$name]}"
            fi
        done
        airflow_state_tmp="${airflow_convergence_state}.tmp.$$"
        {
            printf '%s %s\n' "$flow_time" "${#stability_flow_names[@]}"
            for name in "${stability_flow_names[@]}"; do
                printf '%s %s %s\n' "$name" "${previous_flows[$name]}" "${previous_smoothed_internal_flows[$name]:--}"
            done
        } > "$airflow_state_tmp"
        mv -f "$airflow_state_tmp" "$airflow_convergence_state"
        if ! awk -v v="$imbalance" -v limit="0.01" 'BEGIN { exit !(v<=limit) }'; then stable=0; fi
        if ! awk -v v="$maximum_change" -v limit="0.02" 'BEGIN { exit !(v<=limit) }'; then stable=0; fi
        if [[ "$fan_domain_ok" != 1 ]]; then stable=0; fi
        if [[ -z "$latest_velocity_relative_rms" || -z "$previous_velocity_relative_rms" ]] || ! awk -v v="$latest_velocity_relative_rms" -v limit="0.01" 'BEGIN { exit !(v<=limit) }' || ! awk -v v="$previous_velocity_relative_rms" -v limit="0.01" 'BEGIN { exit !(v<=limit) }'; then stable=0; fi
        echo "Airflow refresh metrics: imbalance=$imbalance, maxFlowChange=$maximum_change, maxFlowDevice=$maximum_change_name, boundaryFlowFloor=$boundary_flow_floor, directionsOK=$directions_ok, fanDomainOK=$fan_domain_ok, fanDomainFailures=$fan_domain_failures, fanDomainWarnings=$fan_domain_warnings, maximumFanDomainUtilization=$maximum_fan_domain_utilization, maximumFanDomainName=$maximum_fan_domain_name, velocityRelativeRms=${latest_velocity_relative_rms:-unavailable}, previousVelocityRelativeRms=${previous_velocity_relative_rms:-unavailable}, estimatedAirExchangeTime=$air_exchange_time s"
        summary "airflow time=$current imbalance=$imbalance maxFlowChange=$maximum_change maxFlowDevice=$maximum_change_name directionsOK=$directions_ok fanDomainOK=$fan_domain_ok fanDomainFailures=$fan_domain_failures fanDomainWarnings=$fan_domain_warnings maximumFanDomainUtilization=$maximum_fan_domain_utilization maximumFanDomainName=$maximum_fan_domain_name velocityRelativeRms=${latest_velocity_relative_rms:-unavailable} previousVelocityRelativeRms=${previous_velocity_relative_rms:-unavailable} estimatedAirExchangeTime=$air_exchange_time"
        [[ "$stable" == 1 && "$directions_ok" == 1 ]]
    }
    thermal_convergence_state="$case_dir/.thermal_convergence_state"
    thermal_convergence_streak="$case_dir/.thermal_convergence_streak"
    if [[ -f "$thermal_convergence_state" ]]; then
        stored_checkpoint=$(awk 'NF { print $1; exit }' "$thermal_convergence_state")
        if [[ -n "$stored_checkpoint" ]] && awk -v saved="$stored_checkpoint" -v now="$current" 'BEGIN { s=(now<0?-now:now); if(s<1)s=1; exit !(saved>now+1e-9*s) }'; then
            echo "Discarding future thermal-convergence state at t=$stored_checkpoint after restart from t=$current."
            rm -f "$thermal_convergence_state" "$thermal_convergence_streak"
        fi
    fi
    thermal_metrics_converged()
    {
        local maximum_root average_root average_file fluid_average_root region line sample_time checkpoint_time peak previous_time previous_peak fluid_average previous_fluid_average elapsed delta scaled_delta value previous index=0 maximum_peak_delta=0 maximum_average_delta=0 scaled_average_delta scaled_fluid_max_delta expected_state_values controlling_peak_region=fluid controlling_average_region=none
        local -a maxima=() averages=() state_values=()
        maximum_root="$case_dir/postProcessing/fluid/fluid_temperature_internal_maximum"
        line=$(find "$maximum_root" -type f -name 'volFieldValue*.dat' -exec awk '!/^#/ && NF>=2 { print $1, $2 }' {} + 2>/dev/null | sort -g -k1,1 | tail -1)
        checkpoint_time=$(awk '{print $1}' <<<"$line")
        peak=$(awk '{print $2}' <<<"$line")
        if [[ -z "$checkpoint_time" || -z "$peak" ]] || ! awk -v sample="$checkpoint_time" -v checkpoint="$current" 'BEGIN { scale=(checkpoint<0?-checkpoint:checkpoint); if(scale<1)scale=1; delta=sample-checkpoint; if(delta<0)delta=-delta; exit !(delta<=1e-9*scale) }'; then
            echo "Refreshing thermal reports at solver checkpoint t=$current."
            if ! "$foam_launcher" mpirun -np "$processes" semiFrozenChtMultiRegionFoam -case "$case_dir" -parallel -postProcess -latestTime; then
                echo "Unable to refresh multi-region thermal convergence reports." >&2
                return 1
            fi
            line=$(find "$maximum_root" -type f -name 'volFieldValue*.dat' -exec awk '!/^#/ && NF>=2 { print $1, $2 }' {} + 2>/dev/null | sort -g -k1,1 | tail -1)
            checkpoint_time=$(awk '{print $1}' <<<"$line")
            peak=$(awk '{print $2}' <<<"$line")
        fi
        if [[ -z "$checkpoint_time" || -z "$peak" ]]; then
            echo "Thermal convergence data is missing or contains no completed fluid internal-maximum sample." >&2
            return 1
        fi
        if ! awk -v sample="$checkpoint_time" -v checkpoint="$current" 'BEGIN { scale=(checkpoint<0?-checkpoint:checkpoint); if(scale<1)scale=1; delta=sample-checkpoint; if(delta<0)delta=-delta; exit !(delta<=1e-9*scale) }'; then
            echo "Thermal convergence fluid report does not match the current solver checkpoint: sample=$checkpoint_time checkpoint=$current." >&2
            return 1
        fi
        fluid_average_root="$case_dir/postProcessing/fluid/fluid_temperature_average"
        line=$(find "$fluid_average_root" -type f -name 'volFieldValue*.dat' -exec awk '!/^#/ && NF>=2 { print $1, $2 }' {} + 2>/dev/null | sort -g -k1,1 | tail -1)
        sample_time=$(awk '{ print $1 }' <<<"$line")
        fluid_average=$(awk '{ print $2 }' <<<"$line")
        if [[ -z "$fluid_average" ]] || ! awk -v sample="$sample_time" -v checkpoint="$checkpoint_time" 'BEGIN { scale=(checkpoint<0?-checkpoint:checkpoint); if(scale<1)scale=1; delta=sample-checkpoint; if(delta<0)delta=-delta; exit !(delta<=1e-9*scale) }'; then
            echo "Thermal convergence fluid-average report is missing or stale: sample=$sample_time checkpoint=$checkpoint_time." >&2
            return 1
        fi
        for region in "${component_region_names[@]}"; do
            maximum_root="$case_dir/postProcessing/$region/${region}_temperature_internal_maximum"
            line=$(find "$maximum_root" -type f -name 'volFieldValue*.dat' -exec awk '!/^#/ && NF>=2 { print $1, $2 }' {} + 2>/dev/null | sort -g -k1,1 | tail -1)
            sample_time=$(awk '{ print $1 }' <<<"$line")
            value=$(awk '{ print $2 }' <<<"$line")
            if [[ -z "$value" ]]; then
                echo "Thermal convergence maximum is missing for component region $region." >&2
                return 1
            fi
            if ! awk -v sample="$sample_time" -v checkpoint="$checkpoint_time" 'BEGIN { scale=(checkpoint<0?-checkpoint:checkpoint); if(scale<1)scale=1; delta=sample-checkpoint; if(delta<0)delta=-delta; exit !(delta<=1e-9*scale) }'; then
                echo "Thermal convergence maximum for component region $region is stale: sample=$sample_time checkpoint=$checkpoint_time." >&2
                return 1
            fi
            maxima+=("$value")
            average_root="$case_dir/postProcessing/$region/${region}_temperature_average"
            line=$(find "$average_root" -type f -name 'volFieldValue*.dat' -exec awk '!/^#/ && NF>=2 { print $1, $2 }' {} + 2>/dev/null | sort -g -k1,1 | tail -1)
            sample_time=$(awk '{ print $1 }' <<<"$line")
            value=$(awk '{ print $2 }' <<<"$line")
            if [[ -z "$value" ]]; then
                echo "Thermal convergence data is missing for component region $region." >&2
                return 1
            fi
            if ! awk -v sample="$sample_time" -v checkpoint="$checkpoint_time" 'BEGIN { scale=(checkpoint<0?-checkpoint:checkpoint); if(scale<1)scale=1; delta=sample-checkpoint; if(delta<0)delta=-delta; exit !(delta<=1e-9*scale) }'; then
                echo "Thermal convergence average for component region $region is stale: sample=$sample_time checkpoint=$checkpoint_time." >&2
                return 1
            fi
            averages+=("$value")
        done
        if [[ ! -f "$thermal_convergence_state" ]]; then
            printf '%s %s %s' "$checkpoint_time" "$peak" "$fluid_average" > "$thermal_convergence_state"
            printf ' %s' "${maxima[@]}" >> "$thermal_convergence_state"
            printf ' %s' "${averages[@]}" >> "$thermal_convergence_state"
            printf '\n' >> "$thermal_convergence_state"
            echo "Thermal convergence baseline recorded at t=$checkpoint_time s."
            return 1
        fi
        read -ra state_values < "$thermal_convergence_state"
        expected_state_values=$((3 + 2*${#component_region_names[@]}))
        if (( ${#state_values[@]} != expected_state_values )); then
            printf '%s %s %s' "$checkpoint_time" "$peak" "$fluid_average" > "$thermal_convergence_state"
            printf ' %s' "${maxima[@]}" >> "$thermal_convergence_state"
            printf ' %s' "${averages[@]}" >> "$thermal_convergence_state"
            printf '\n' >> "$thermal_convergence_state"
            echo "Thermal convergence state format changed; new per-component peak baseline recorded at t=$checkpoint_time s."
            return 1
        fi
        previous_time="${state_values[0]:-}"
        previous_peak="${state_values[1]:-}"
        previous_fluid_average="${state_values[2]:-}"
        printf '%s %s %s' "$checkpoint_time" "$peak" "$fluid_average" > "$thermal_convergence_state"
        printf ' %s' "${maxima[@]}" >> "$thermal_convergence_state"
        printf ' %s' "${averages[@]}" >> "$thermal_convergence_state"
        printf '\n' >> "$thermal_convergence_state"
        elapsed=$(awk -v a="$checkpoint_time" -v b="$previous_time" 'BEGIN { print a-b }')
        if ! awk -v v="$elapsed" 'BEGIN { exit !(v>0) }'; then
            echo "Thermal convergence checkpoint did not advance: previous=$previous_time current=$checkpoint_time." >&2
            return 1
        fi
        scaled_fluid_max_delta=$(awk -v a="$peak" -v b="$previous_peak" -v reference="300" -v elapsed="$elapsed" 'BEGIN { d=a-b; if(d<0)d=-d; print d*reference/elapsed }')
        delta=$(awk -v a="$fluid_average" -v b="$previous_fluid_average" 'BEGIN { d=a-b; if(d<0)d=-d; print d }')
        scaled_delta=$(awk -v d="$delta" -v reference="300" -v elapsed="$elapsed" 'BEGIN { print d*reference/elapsed }')
        maximum_peak_delta="$delta"
        controlling_peak_region=fluidAverage
        for value in "${maxima[@]}"; do
            previous="${state_values[$((index+3))]:-}"
            if [[ -z "$previous" ]]; then return 1; fi
            delta=$(awk -v a="$value" -v b="$previous" 'BEGIN { d=a-b; if(d<0)d=-d; print d }')
            if awk -v a="$delta" -v b="$maximum_peak_delta" 'BEGIN { exit !(a>b) }'; then
                maximum_peak_delta="$delta"
                controlling_peak_region="${component_region_names[$index]}"
            fi
            index=$((index+1))
        done
        scaled_delta=$(awk -v d="$maximum_peak_delta" -v reference="300" -v elapsed="$elapsed" 'BEGIN { print d*reference/elapsed }')
        for value in "${averages[@]}"; do
            previous="${state_values[$((index+3))]:-}"
            if [[ -z "$previous" ]]; then return 1; fi
            delta=$(awk -v a="$value" -v b="$previous" 'BEGIN { d=a-b; if(d<0)d=-d; print d }')
            if awk -v a="$delta" -v b="$maximum_average_delta" 'BEGIN { exit !(a>b) }'; then
                maximum_average_delta="$delta"
                controlling_average_region="${component_region_names[$((index-${#component_region_names[@]}))]}"
            fi
            index=$((index+1))
        done
        scaled_average_delta=$(awk -v d="$maximum_average_delta" -v reference="300" -v elapsed="$elapsed" 'BEGIN { print d*reference/elapsed }')
        echo "Thermal convergence metrics: maxInternalCellChange=$scaled_delta K/300s, maxComponentAverageChange=$scaled_average_delta K/300s, fluidMaximumChange=$scaled_fluid_max_delta K/300s (diagnostic), controllingPeakRegion=$controlling_peak_region, controllingAverageRegion=$controlling_average_region, elapsed=$elapsed s"
        summary "thermal time=$checkpoint_time maxInternalCellChange=$scaled_delta maxComponentAverageChange=$scaled_average_delta fluidMaximumChange=$scaled_fluid_max_delta controllingPeakRegion=$controlling_peak_region controllingAverageRegion=$controlling_average_region elapsed=$elapsed"
        if ! awk -v t="$checkpoint_time" -v minimum="3600" 'BEGIN { scale=(minimum<0?-minimum:minimum); if(scale<1)scale=1; tolerance=1e-9*scale; exit !(t>=minimum-tolerance) }'; then return 1; fi
        if ! awk -v v="$scaled_delta" -v limit="0.10000000000000001" 'BEGIN { exit !(v<=limit) }'; then return 1; fi
        if ! awk -v v="$scaled_average_delta" -v limit="0.050000000000000003" 'BEGIN { exit !(v<=limit) }'; then return 1; fi
        return 0
    }
    stage()
    {
        local thermal_only="$1" target="$2" max_co="$3" max_dt="$4" label="$5" live_dt_cap="$6"
        local interval actual_time saved_time canonical_time restart_dt saved_time_file rank stage_steps stage_dt stage_max_dt stage_write_control stage_write_interval checkpoint_steps field source_field target_field courant_output observed_co courant_safe_dt airflow_hard_cap postflight_output postflight_co spatial_output spatial_status velocity_rms_delta velocity_rms_reference stage_wall_start stage_wall_end stage_wall_seconds stage_velocity_reference stage_velocity_reference_tmp stage_outer_correctors
        interval=$(awk -v end="$target" -v start="$current" 'BEGIN { printf "%.17g", end-start }')
        if awk -v d="$interval" -v target="$target" 'BEGIN { s=(target<0?-target:target); if(s<1)s=1; exit !(d<=1e-9*s) }'; then
            current="$target"
            return 0
        fi
        stage_wall_start=$(date +%s%N)
        if [[ "$thermal_only" == "true" ]]; then
            stage_outer_correctors="$thermal_only_outer_correctors"
        else
            stage_outer_correctors="3"
        fi
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/fvSolution" -entry PIMPLE/nOuterCorrectors -set "$stage_outer_correctors"
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/fluid/fvSolution" -entry PIMPLE/frozenFlow -set false
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/fluid/fvSolution" -entry PIMPLE/semiFrozenFlow -set false
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/fluid/fvSolution" -entry PIMPLE/thermalOnlyFlow -set "$thermal_only"
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/fluid/fvSolution" -entry PIMPLE/momentumPredictor -set true
        if [[ "$thermal_only" == "true" ]]; then
            adjust_time_step=false
            echo "Thermal-only maxCo=$max_co is diagnostic; the fully implicit frozen-flow energy timestep is limited by maxDeltaT=$max_dt s; outerCorrectors=$stage_outer_correctors."
            stage_max_dt="$max_dt"
            stage_write_control=timeStep
            stage_plan=$(awk -v maximum="$max_dt" -v remaining="$interval" 'BEGIN { n=int(remaining/maximum); if(n*maximum<remaining-1e-12)n++; if(n<1)n=1; printf "%.17g %d", remaining/n,n }')
            read -r stage_dt stage_steps <<<"$stage_plan"
            stage_write_interval="$stage_steps"
        else
            # Keep exact, fixed, divisible live-flow steps.
            # adjustableRunTime can enlarge a step to align an
            # adjustable write, bypassing maxDeltaT. Begin with
            # a conservative maxCo-scaled fallback; the saved
            # flow field below then supplies a tighter limit.
            adjust_time_step=false
            airflow_hard_cap=$(awk -v maximum="$max_dt" -v flow_max="$live_dt_cap" 'BEGIN { print (flow_max<maximum?flow_max:maximum) }')
            stage_max_dt=$(awk -v hard="$airflow_hard_cap" -v co="$max_co" 'BEGIN { scale=(co<10?co/10:1); print hard*scale }')
            stage_plan=$(awk -v maximum="$stage_max_dt" -v remaining="$interval" -v checkpoint="0.10000000000000001" 'BEGIN { n=int(remaining/maximum); if(n*maximum<remaining-1e-12)n++; if(n<1)n=1; blocks=int(remaining/checkpoint); if(blocks*checkpoint<remaining-1e-12)blocks++; if(blocks<1)blocks=1; per=int(n/blocks); if(per*blocks<n)per++; if(per<1)per=1; n=per*blocks; printf "%.17g %d %d", remaining/n,n,per }')
            read -r stage_dt stage_steps checkpoint_steps <<<"$stage_plan"
            stage_max_dt="$stage_dt"
            stage_write_control=timeStep
            stage_write_interval="$checkpoint_steps"
        fi
        restart_dt="$stage_dt"
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry adjustTimeStep -set "$adjust_time_step"
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry deltaT -set "$restart_dt"
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry startFrom -set latestTime
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry stopAt -set endTime
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry startTime -set "$current"
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry endTime -set "$target"
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry writeControl -set "$stage_write_control"
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry writeInterval -set "$stage_write_interval"
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry maxCo -set "$max_co"
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry maxDeltaT -set "$stage_max_dt"
        saved_time=$(latest_processor_restart_time)
        if [[ -n "$saved_time" ]]; then
            # Older cases may have directory names written at lower
            # precision. OpenFOAM reconstructs the name at the current
            # timePrecision, so normalize it before attempting restart.
            canonical_time=$(awk -v t="$saved_time" 'BEGIN { printf "%.17g", t }')
            if [[ "$canonical_time" != "$saved_time" ]]; then
                for ((rank=0; rank<processes; ++rank)); do
                    if [[ -e "$case_dir/processor${rank}/$canonical_time" ]]; then
                        echo "Cannot normalize checkpoint: target exists: $case_dir/processor${rank}/$canonical_time" >&2
                        return 1
                    fi
                done
                for ((rank=0; rank<processes; ++rank)); do
                    mv -- "$case_dir/processor${rank}/$saved_time" "$case_dir/processor${rank}/$canonical_time"
                done
                echo "Normalized legacy checkpoint directory: $saved_time -> $canonical_time"
                saved_time="$canonical_time"
            fi
            for ((rank=0; rank<processes; ++rank)); do
                saved_time_file="$case_dir/processor${rank}/${saved_time}/uniform/time"
                if [[ -f "$saved_time_file" ]]; then
                    # Align timeStep writes with this stage's final step.
                    "$foam_launcher" foamDictionary -precision 17 "$saved_time_file" -entry index -set 0
                    "$foam_launcher" foamDictionary -precision 17 "$saved_time_file" -entry deltaT -set "$restart_dt"
                    "$foam_launcher" foamDictionary -precision 17 "$saved_time_file" -entry deltaT0 -set "$restart_dt"
                fi
            done
        fi
        if [[ "$thermal_only" == "false" && -n "$saved_time" ]]; then
            # CourantNo uses the checkpoint's stored deltaT.
            # The restart metadata now contains stage_dt, so the
            # reported Co predicts the proposed first live step.
            if ! courant_output=$("$foam_launcher" mpirun -np "$processes" postProcess -case "$case_dir" -parallel -region fluid -latestTime -fields '(phi rho)' -funcs '(CourantNo fieldMinMax(Co))' 2>&1); then
                printf '%s\n' "$courant_output" >&2
                echo "Courant preflight failed at t=$saved_time." >&2
                return 6
            fi
            observed_co=$(printf '%s\n' "$courant_output" | awk '/max\(Co\) =/{value=$3} END{print value}')
            if [[ -z "$observed_co" ]]; then
                printf '%s\n' "$courant_output" >&2
                echo "Courant preflight did not report max(Co)." >&2
                return 6
            fi
            courant_safe_dt=$(awk -v dt="$stage_dt" -v observed="$observed_co" -v limit="$max_co" -v hard="$airflow_hard_cap" 'BEGIN { safe=(observed>0?dt*0.5*limit/observed:hard); print (safe<hard?safe:hard) }')
            if awk -v safe="$courant_safe_dt" 'BEGIN { exit !(safe>0) }'; then
                stage_plan=$(awk -v maximum="$courant_safe_dt" -v remaining="$interval" -v checkpoint="0.10000000000000001" 'BEGIN { n=int(remaining/maximum); if(n*maximum<remaining-1e-12)n++; if(n<1)n=1; blocks=int(remaining/checkpoint); if(blocks*checkpoint<remaining-1e-12)blocks++; if(blocks<1)blocks=1; per=int(n/blocks); if(per*blocks<n)per++; if(per<1)per=1; n=per*blocks; printf "%.17g %d %d", remaining/n,n,per }')
                read -r stage_dt stage_steps checkpoint_steps <<<"$stage_plan"
                stage_max_dt="$stage_dt"
                stage_write_interval="$checkpoint_steps"
                restart_dt="$stage_dt"
                "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry deltaT -set "$restart_dt"
                "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry maxDeltaT -set "$stage_max_dt"
                "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry writeInterval -set "$stage_write_interval"
                for ((rank=0; rank<processes; ++rank)); do
                    saved_time_file="$case_dir/processor${rank}/${saved_time}/uniform/time"
                    [[ -f "$saved_time_file" ]] || continue
                    "$foam_launcher" foamDictionary -precision 17 "$saved_time_file" -entry deltaT -set "$restart_dt"
                    "$foam_launcher" foamDictionary -precision 17 "$saved_time_file" -entry deltaT0 -set "$restart_dt"
                done
            fi
            echo "Courant preflight: predictedMaxCo=$observed_co, maxCo=$max_co, deltaT=$stage_dt, steps=$stage_steps"
        fi
        if [[ "$thermal_only" == "false" && -n "$saved_time" ]]; then
            stage_velocity_reference="$case_dir/.stage_velocity_reference"
            stage_velocity_reference_tmp="${stage_velocity_reference}.tmp.$$"
            rm -rf -- "$stage_velocity_reference_tmp"
            mkdir -p "$stage_velocity_reference_tmp"
            for ((rank=0; rank<processes; ++rank)); do
                source_field="$case_dir/processor${rank}/${saved_time}/fluid/U"
                if [[ ! -f "$source_field" ]]; then
                    echo "Cannot preserve live-stage velocity reference: $source_field is missing." >&2
                    rm -rf -- "$stage_velocity_reference_tmp"
                    return 8
                fi
                mkdir -p "$stage_velocity_reference_tmp/processor${rank}"
                cp -p "$source_field" "$stage_velocity_reference_tmp/processor${rank}/U"
            done
            rm -rf -- "$stage_velocity_reference"
            mv -f "$stage_velocity_reference_tmp" "$stage_velocity_reference"
        fi
        echo "$label: t=$current -> $target"
        "$foam_launcher" mpirun -np "$processes" semiFrozenChtMultiRegionFoam -case "$case_dir" -parallel
        if [[ "$thermal_only" == "false" ]]; then
            if ! postflight_output=$("$foam_launcher" mpirun -np "$processes" postProcess -case "$case_dir" -parallel -region fluid -latestTime -fields '(phi rho)' -funcs '(CourantNo fieldMinMax(Co))' 2>&1); then
                printf '%s\n' "$postflight_output" >&2
                echo "Courant postflight failed at target=$target." >&2
                return 7
            fi
            postflight_co=$(printf '%s\n' "$postflight_output" | awk '/max\(Co\) =/{value=$3} END{print value}')
            if [[ -z "$postflight_co" ]]; then
                printf '%s\n' "$postflight_output" >&2
                echo "Courant postflight did not report max(Co)." >&2
                return 7
            fi
            echo "Courant postflight: actualMaxCo=$postflight_co, maxCo=$max_co, deltaT=$stage_dt"
            if ! awk -v actual="$postflight_co" -v limit="$max_co" 'BEGIN { exit !(actual<=limit*1.001) }'; then
                echo "Live-flow Courant limit exceeded: actualMaxCo=$postflight_co maxCo=$max_co." >&2
                return 7
            fi
        fi
        actual_time=$(latest_processor_restart_time)
        if ! awk -v actual="$actual_time" -v target="$target" 'BEGIN { scale=(target<0?-target:target); if(scale<1)scale=1; tolerance=1e-6*scale; exit !(actual>=target-tolerance) }'; then
            echo "Solver stage failed to reach target time: target=$target actual=$actual_time." >&2
            return 5
        fi
        if [[ "$thermal_only" == "false" && -n "$saved_time" && "$saved_time" != "$actual_time" ]]; then
            for ((rank=0; rank<processes; ++rank)); do
                source_field="$stage_velocity_reference/processor${rank}/U"
                target_field="$case_dir/processor${rank}/${actual_time}/fluid/UPrevious"
                if [[ ! -f "$source_field" ]]; then
                    echo "Previous velocity field is missing: $source_field" >&2
                    return 8
                fi
                cp -p "$source_field" "$target_field"
                "$foam_launcher" foamDictionary -precision 17 "$target_field" -entry FoamFile/object -set UPrevious >/dev/null 2>&1
            done
            spatial_status=0
            if ! spatial_output=$("$foam_launcher" mpirun -np "$processes" semiFrozenChtMultiRegionFoam -case "$case_dir" -parallel -postProcess -latestTime -dict system/spatialConvergenceDict 2>&1); then
                spatial_status=1
            fi
            velocity_rms_delta=$(printf '%s\n' "$spatial_output" | awk '/velocityDeltaSquared =/{value=$NF} END{print value}')
            velocity_rms_reference=$(printf '%s\n' "$spatial_output" | awk '/velocitySquared =/{value=$NF} END{print value}')
            for ((rank=0; rank<processes; ++rank)); do
                for field in UPrevious velocityDelta velocityDeltaSquared velocitySquared; do
                    rm -f -- "$case_dir/processor${rank}/${actual_time}/fluid/${field}"
                done
            done
            if [[ "$spatial_status" != 0 || -z "$velocity_rms_delta" || -z "$velocity_rms_reference" ]]; then
                printf '%s\n' "$spatial_output" >&2
                echo "Spatial velocity convergence calculation failed at t=$actual_time." >&2
                return 8
            fi
            previous_velocity_relative_rms="$latest_velocity_relative_rms"
            latest_velocity_relative_rms=$(awk -v delta="$velocity_rms_delta" -v reference="$velocity_rms_reference" 'BEGIN { print (reference>1e-12?delta/reference:(delta<=1e-12?0:1e30)) }')
            echo "Spatial velocity change: rmsDelta=$velocity_rms_delta m/s, rmsVelocity=$velocity_rms_reference m/s, relativeRms=$latest_velocity_relative_rms"
            printf '%s %s %s\n' "$actual_time" "$latest_velocity_relative_rms" "${previous_velocity_relative_rms:--}" > "$velocity_convergence_state.tmp.$$"
            mv -f "$velocity_convergence_state.tmp.$$" "$velocity_convergence_state"
            rm -rf -- "$stage_velocity_reference"
        fi
        if [[ "$thermal_only" == "true" && -n "$saved_time" && "$saved_time" != "$actual_time" ]]; then
            for ((rank=0; rank<processes; ++rank)); do
                for field in U p p_rgh phi rho k omega nut alphat; do
                    source_field="$case_dir/processor${rank}/${saved_time}/fluid/${field}"
                    target_field="$case_dir/processor${rank}/${actual_time}/fluid/${field}"
                    if [[ -f "$source_field" && ! -f "$target_field" ]]; then
                        cp -p "$source_field" "$target_field"
                    fi
                done
            done
        fi
        prune_processor_times
        stage_wall_end=$(date +%s%N)
        stage_wall_seconds=$(awk -v start="$stage_wall_start" -v end="$stage_wall_end" 'BEGIN { printf "%.3f", (end-start)/1e9 }')
        echo "Stage wall time: label=$label, thermalOnly=$thermal_only, outerCorrectors=$stage_outer_correctors, start=$current, target=$actual_time, seconds=$stage_wall_seconds"
        summary "stage label=$label thermalOnly=$thermal_only outerCorrectors=$stage_outer_correctors start=$current target=$actual_time seconds=$stage_wall_seconds"
        current="$actual_time"
    }

    adaptive_airflow_refresh()
    {
        local refresh_start="$current" refresh_elapsed=0 refresh_target pending_refresh_start long_lag_failed=0 airflow_metrics_status=0
        airflow_refresh_validated=0
        airflow_refresh_long_lag_validated=0
        if [[ -f "$refresh_pending_marker" ]]; then
            pending_refresh_start=$(awk 'NF { print $1; exit }' "$refresh_pending_marker")
            if [[ "$pending_refresh_start" =~ ^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$ ]] && awk -v start="$pending_refresh_start" -v now="$current" 'BEGIN { scale=(now<0?-now:now); if(scale<1)scale=1; tolerance=1e-9*scale; exit !(start<=now+tolerance) }'; then
                if ! awk -v start="$pending_refresh_start" -v now="$current" -v maximum="0.20000000000000001" 'BEGIN { scale=(now<0?-now:now); if(scale<1)scale=1; tolerance=1e-9*scale; exit !(now<=start+maximum+tolerance) }'; then
                    echo "Pending airflow refresh already exceeded the maximum duration from t=$pending_refresh_start s." >&2
                    return 3
                fi
                refresh_start="$pending_refresh_start"
                echo "Resuming airflow refresh observation window from t=$refresh_start s."
            else
                echo "Discarding incompatible airflow-refresh pending state." >&2
                rm -f "$refresh_pending_marker"
            fi
        fi
        if [[ ! -f "$refresh_pending_marker" ]]; then
            printf '%s
' "$refresh_start" > "$refresh_pending_marker"
        fi
        # Retain the last accepted operating point. The first live window must measure the airflow change caused by the preceding thermal-only interval, not silently establish a fresh baseline. A restarted runner begins with an empty array and therefore conservatively reacquires one.
        while true; do
            refresh_target=$(awk -v a="$current" -v d="0.01" -v start="$refresh_start" -v maximum="0.20000000000000001" -v end="$requested_end" 'BEGIN { x=a+d; limit=start+maximum; if(x>limit)x=limit; if(x>end)x=end; printf "%.17g", x }')
            stage false "$refresh_target" 10 0.25 "Adaptive airflow refresh" 0.0050000000000000001
            refresh_elapsed=$(awk -v a="$current" -v b="$refresh_start" 'BEGIN { print a-b }')
            if awk -v a="$refresh_elapsed" -v b="0.10000000000000001" 'BEGIN { exit !(a>=b) }'; then
                airflow_metrics_status=0
                airflow_metrics_converged || airflow_metrics_status=$?
                if (( airflow_metrics_status > 1 )); then
                    echo "Airflow metrics evaluation failed; aborting refresh." >&2
                    return 3
                fi
                if (( airflow_metrics_status == 0 )); then
                    if validate_accepted_airflow_drift; then
                        if [[ "$long_lag_failed" == 0 ]]; then
                            airflow_refresh_long_lag_validated=1
                        else
                            record_accepted_airflow_reference || return 3
                            echo "Airflow settled after an accepted-field shift; this thermal checkpoint remains ineligible for convergence."
                        fi
                    else
                        long_lag_failed=1
                        echo "Accepted airflow shifted beyond the coupled-convergence limit; continuing live-flow settling at this thermal checkpoint."
                        continue
                    fi
                    echo "Airflow refresh converged after $refresh_elapsed s."
                    airflow_refresh_validated=1
                    rm -f "$refresh_pending_marker"
                    return 0
                fi
            fi
            if awk -v a="$refresh_elapsed" -v b="0.20000000000000001" 'BEGIN { exit !(a>=b) }'; then
                echo "Airflow refresh failed to converge within 0.20000000000000001 s." >&2
                return 3
            fi
            if ! awk -v a="$current" -v b="$requested_end" 'BEGIN { s=(b<0?-b:b); if(s<1)s=1; tol=1e-9*s; exit !(a<b-tol) }'; then
                echo "Airflow refresh reached requested end time without convergence; checkpoint remains unvalidated and the pending refresh will resume before the next thermal-only stage."
                return 0
            fi
        done
    }

    adaptive_initial_airflow()
    {
        local initial_start="$current" initial_elapsed=0 initial_target initial_limit pending_initial_start exchange_target= eligibility_target= air_exchange_fraction=0 air_exchange_last_time= air_exchange_last_flow=0 airflow_metrics_passed=false air_exchange_state_tmp airflow_metrics_status=0 exchange_was_incomplete=false
        if [[ -s "$initial_pending_marker" ]]; then
            pending_initial_start=$(awk 'NF { print $1; exit }' "$initial_pending_marker")
            if [[ "$pending_initial_start" =~ ^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$ ]] && awk -v start="$pending_initial_start" -v now="$current" -v maximum="5" 'BEGIN { scale=(now<0?-now:now); if(scale<1)scale=1; tolerance=1e-9*scale; exit !(start<=now+tolerance && now<=start+maximum+tolerance) }'; then
                initial_start="$pending_initial_start"
                echo "Resuming initial airflow observation window from t=$initial_start s."
            else
                echo "Discarding incompatible initial-airflow pending state." >&2
                rm -f "$initial_pending_marker"
            fi
        fi
        if [[ ! -f "$initial_pending_marker" ]]; then
            printf '%s\n' "$initial_start" > "$initial_pending_marker"
            previous_flows=()
            previous_smoothed_internal_flows=()
            previous_velocity_relative_rms=""
            latest_velocity_relative_rms=""
            rm -f "$velocity_convergence_state"
            rm -f "$initial_physical_settling_marker"
            printf '%s %s %s\n' "$initial_start" 0 0 > "$initial_exchange_state"
        fi
        if read -r air_exchange_last_time air_exchange_last_flow air_exchange_fraction < "$initial_exchange_state" 2>/dev/null && awk -v t="$air_exchange_last_time" -v f="$air_exchange_last_flow" -v x="$air_exchange_fraction" -v start="$initial_start" -v now="$current" 'BEGIN { number="^[-+]?[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$"; scale=(now<0?-now:now); if(scale<1)scale=1; tolerance=1e-9*scale; exit !(t~number && f~number && x~number && t>=start-tolerance && t<=now+tolerance && f>=0 && x>=0) }'; then
            echo "Restored cumulative initial air exchange: fraction=$air_exchange_fraction at t=$air_exchange_last_time s."
        else
            air_exchange_last_time="$initial_start"
            air_exchange_last_flow=0
            air_exchange_fraction=0
            echo "Resetting incompatible cumulative air-exchange state." >&2
        fi
        initial_limit=$(awk -v start="$initial_start" -v maximum="5" -v end="$requested_end" 'BEGIN { limit=start+maximum; print (limit<end?limit:end) }')
        if [[ ! -f "$mapped_state_marker" ]]; then
            eligibility_target=$(awk -v start="$initial_start" -v minimum="0.02" -v interval="0.01" -v limit="$initial_limit" 'BEGIN { lead=minimum-2*interval; if(lead<0)lead=0; x=start+lead; if(x>limit)x=limit; printf "%.17g", x }')
        fi
        if [[ -f "$initial_physical_settling_marker" ]]; then
            exchange_target=$(awk -v now="$current" -v interval="0.10000000000000001" -v limit="$initial_limit" 'BEGIN { x=now+interval; if(x>limit)x=limit; printf "%.17g", x }')
            echo "Resuming full-window initial airflow settling toward t=$exchange_target s."
        fi
        while awk -v a="$current" -v b="$initial_limit" 'BEGIN { s=(b<0?-b:b); if(s<1)s=1; tol=1e-9*s; exit !(a<b-tol) }'; do
            initial_target=$(awk -v a="$current" -v d="0.01" -v checkpoint="0.10000000000000001" -v eligibility_target="$eligibility_target" -v exchange_target="$exchange_target" -v limit="$initial_limit" 'BEGIN { x=a+d; if(eligibility_target!="" && eligibility_target>x)x=eligibility_target; if(eligibility_target!="") { cap=a+checkpoint; if(x>cap)x=cap; } if(exchange_target!="" && exchange_target>x)x=exchange_target; if(x>limit)x=limit; printf "%.17g", x }')
            stage false "$initial_target" 0.40000000000000002 0.25 "Adaptive initial airflow" 0.001
            initial_elapsed=$(awk -v a="$current" -v b="$initial_start" 'BEGIN { print a-b }')
            minimum_observation="0.02"
            if [[ -f "$mapped_state_marker" ]]; then
                minimum_observation="0.01"
            fi
            if awk -v a="$initial_elapsed" -v b="$minimum_observation" 'BEGIN { exit !(a>=b) }'; then
                exchange_was_incomplete=false
                if [[ ! -f "$mapped_state_marker" ]] && ! awk -v completed="$air_exchange_fraction" -v required="1" 'BEGIN { exit !(completed+1e-9>=required) }'; then
                    exchange_was_incomplete=true
                fi
                airflow_metrics_passed=false
                airflow_metrics_status=0
                airflow_metrics_converged || airflow_metrics_status=$?
                if (( airflow_metrics_status > 1 )); then
                    echo "Airflow metrics evaluation failed; aborting initial airflow." >&2
                    return 3
                fi
                if (( airflow_metrics_status == 0 )); then
                    airflow_metrics_passed=true
                fi
                if [[ -n "$latest_one_way_boundary_mass_flow" ]]; then
                    air_exchange_fraction=$(awk -v accumulated="$air_exchange_fraction" -v previous_time="$air_exchange_last_time" -v now="$current" -v previous_flow="$air_exchange_last_flow" -v flow="$latest_one_way_boundary_mass_flow" -v volume="0.017000000000000008" -v rho="1.2250000000000001" 'BEGIN { dt=now-previous_time; if(dt<0)dt=0; increment=0.5*(previous_flow+flow)*dt/(volume*rho); printf "%.17g", accumulated+increment }')
                    air_exchange_last_time="$current"
                    air_exchange_last_flow="$latest_one_way_boundary_mass_flow"
                    air_exchange_state_tmp="${initial_exchange_state}.tmp.$$"
                    printf '%s %s %s\n' "$air_exchange_last_time" "$air_exchange_last_flow" "$air_exchange_fraction" > "$air_exchange_state_tmp"
                    mv -f "$air_exchange_state_tmp" "$initial_exchange_state"
                    echo "Cumulative initial air exchange: fraction=$air_exchange_fraction at t=$current s."
                    summary "initial_air_exchange_progress current=$current fraction=$air_exchange_fraction oneWayMassFlow=$air_exchange_last_flow"
                fi
                if [[ "$airflow_metrics_passed" == true && -f "$mapped_state_marker" ]]; then
                        air_exchange_fraction="1"
                        echo "Mapped airflow skips the cold-start air-exchange horizon after live spatial and device validation."
                fi
                if [[ ! -f "$mapped_state_marker" ]] && ! awk -v completed="$air_exchange_fraction" -v required="1" 'BEGIN { exit !(completed+1e-9>=required) }'; then
                    exchange_target=$(awk -v now="$current" -v interval="0.10000000000000001" -v limit="$initial_limit" 'BEGIN { x=now+interval; if(x>limit)x=limit; printf "%.17g", x }')
                    echo "Cumulative air exchange is $air_exchange_fraction; advancing to t=$exchange_target s before final local convergence checks."
                    summary "initial_air_exchange_advance current=$current target=$exchange_target completedFraction=$air_exchange_fraction"
                    continue
                fi
                if [[ "$exchange_was_incomplete" == true || -f "$initial_physical_settling_marker" ]]; then
                    if [[ "$airflow_metrics_passed" == true ]]; then
                        rm -f "$initial_physical_settling_marker"
                        exchange_target=
                        echo "Initial air-exchange and full-window settling requirements reached; collecting fresh local convergence windows before acceptance."
                    else
                        touch "$initial_physical_settling_marker"
                        exchange_target=$(awk -v now="$current" -v interval="0.10000000000000001" -v limit="$initial_limit" 'BEGIN { x=now+interval; if(x>limit)x=limit; printf "%.17g", x }')
                        echo "Initial air exchange is complete, but the full-window airflow gate has not passed; advancing to t=$exchange_target s before local confirmation."
                        summary "initial_airflow_physical_settling current=$current target=$exchange_target"
                    fi
                    continue
                fi
                if [[ "$airflow_metrics_passed" == true ]]; then
                    echo "Initial airflow converged after $initial_elapsed s beyond the fan ramp; switching to thermal-only mode."
                    if ! record_accepted_airflow_reference; then
                        echo "Unable to preserve the accepted initial airflow reference." >&2
                        return 3
                    fi
                    touch "$initial_convergence_marker"
                    rm -f "$initial_pending_marker" "$mapped_state_marker" "$initial_exchange_state" "$initial_physical_settling_marker"
                    fan_options_source="$full_fan_options"
                    restore_full_fan_options
                    echo "Restored full fluid heat sources for thermal evolution."
                    return 0
                fi
            fi
        done
        if ! awk -v a="$current" -v b="$requested_end" 'BEGIN { s=(b<0?-b:b); if(s<1)s=1; tol=1e-9*s; exit !(a>=b-tol) }'; then
            echo "Initial airflow failed to converge before the airflow_warmup_time safety limit of 5 s." >&2
            return 3
        fi
    }

    if [[ ! -f "$initial_convergence_marker" ]] && awk -v a="$current" -v b="$requested_end" 'BEGIN { exit !(a<b) }'; then
        echo "Adaptively finding initial airflow operating point."
        adaptive_initial_airflow
    fi
    if [[ -f "$refresh_pending_marker" ]] && awk -v a="$current" -v b="$requested_end" 'BEGIN { exit !(a<b) }'; then
        echo "Retrying interrupted airflow refresh."
        adaptive_airflow_refresh
    fi
    while awk -v a="$current" -v b="$requested_end" 'BEGIN { s=(b<0?-b:b); if(s<1)s=1; tol=1e-9*s; exit !(a<b-tol) }'; do
        frozen_target=$(awk -v a="$current" -v d="$airflow_refresh_interval" -v b="$requested_end" 'BEGIN { x=(int(a/d)+1)*d; if(x<=a+1e-9)x+=d; print (x<b ? x : b) }')
        stage true "$frozen_target" 1000 1 "Implicit thermal-only stage (airflow held)" 1
        thermal_candidate=0
        airflow_validated=0
        airflow_long_lag_validated=0
        if thermal_metrics_converged; then
            thermal_candidate=1
        fi
        terminal_requested_end=""
        if ! awk -v a="$current" -v b="$requested_end" 'BEGIN { s=(b<0?-b:b); if(s<1)s=1; tol=1e-9*s; exit !(a<b-tol) }'; then
            terminal_requested_end="$requested_end"
            requested_end=$(awk -v a="$current" -v d="0.20000000000000001" 'BEGIN { printf "%.17g", a+d }')
            echo "Refreshing airflow at terminal thermal checkpoint t=$current s before final reconstruction."
        fi
        adaptive_airflow_refresh
        airflow_validated="$airflow_refresh_validated"
        airflow_long_lag_validated="$airflow_refresh_long_lag_validated"
        if [[ -n "$terminal_requested_end" ]]; then
            requested_end="$terminal_requested_end"
        fi
        if [[ "$thermal_candidate" == 1 && "$airflow_validated" == 1 && "$airflow_long_lag_validated" == 1 ]]; then
            streak=$(cat "$thermal_convergence_streak" 2>/dev/null || echo 0)
            streak=$((streak+1))
            printf '%s\n' "$streak" > "$thermal_convergence_streak"
            echo "Thermal convergence checkpoint $streak/2 accepted with airflow metrics converged."
            summary "checkpoint time=$current streak=$streak required=2 accepted=true"
            record_accepted_airflow_reference || return 3
            echo "Advanced accepted airflow reference after validated checkpoint t=$current s."
            summary "airflow_reference_rebased time=$current reason=validatedCheckpoint"
            if (( streak >= 2 )); then
                echo "Thermal and airflow convergence criteria satisfied at validated checkpoint t=$current s (requested end time $requested_end s)."
                break
            fi
        elif [[ "$airflow_validated" == 1 && "$airflow_long_lag_validated" == 1 ]]; then
            printf '0\n' > "$thermal_convergence_streak"
            record_accepted_airflow_reference || return 3
        elif [[ "$airflow_validated" == 1 ]]; then
            printf '0\n' > "$thermal_convergence_streak"
            echo "Resetting thermal convergence streak: accepted airflow has not converged across refresh cycles."
            summary "checkpoint time=$current streak=0 accepted=false reason=acceptedAirflowLongLag"
        else
            streak=$(cat "$thermal_convergence_streak" 2>/dev/null || echo 0)
            echo "Preserving thermal convergence streak $streak: this terminal partial stage had no airflow validation."
        fi
    done
else
    warm_current=$(latest_processor_restart_time)
    warm_current="${warm_current:-0}"
    if [[ "$mode" == "--warm-start" ]] && awk -v a="$warm_current" 'BEGIN { exit !(a==0) }' && grep -Eq '^[[:space:]]*internalField[[:space:]]+nonuniform' "$case_dir/0/fluid/U"; then
        touch "$mapped_state_marker"
        fan_options_source="$full_fan_options"
        restore_full_fan_options
        echo "Detected mapped nonuniform velocity fields; retaining full heat sources and skipping the cold fan ramp."
    fi
    if [[ "$mode" == "--warm-start" ]]; then
        warm_interval=$(awk -v end="$requested_end" -v start="$warm_current" 'BEGIN { d=end-start; if (d<=0) exit 1; printf "%.17g", d }') || {
            echo "Warm-start end time must be greater than latest time $warm_current." >&2
            exit 2
        }
        warm_restart_dt=$(awk -v interval="$warm_interval" -v maximum="$warm_start_maximum_time_step" 'BEGIN { print (interval<maximum?interval:maximum) }')
        preflight_checkpoint_space || exit $?
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry startFrom -set latestTime
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry stopAt -set endTime
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry endTime -set "$requested_end"
        # Use the authoritative processor checkpoint so the requested endpoint is always written.
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry writeControl -set adjustableRunTime
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry writeInterval -set "$warm_interval"
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry adjustTimeStep -set true
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry deltaT -set "$warm_restart_dt"
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry maxDeltaT -set "$warm_start_maximum_time_step"
        echo "Running airflow/thermal warm start to t=$requested_end s from t=$warm_current s."
    fi
    if [[ "$mode" == "--warm-start" ]] && [[ ! -f "$mapped_state_marker" ]] && [[ ! -f "$fan_ramp_complete_marker" ]] && awk -v a="$warm_current" -v end="0.050000000000000003" 'BEGIN { exit !(a<end) }'; then
        run_fan_ramp chtMultiRegionFoam "$warm_current" "$requested_end"
        warm_current="$ramp_current"
    fi
    if awk -v a="$warm_current" -v b="$requested_end" 'BEGIN { exit !(a<b) }'; then
        "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry endTime -set "$requested_end"
        "$foam_launcher" mpirun -np "$processes" chtMultiRegionFoam -case "$case_dir" -parallel
    fi
fi
write_final_reports()
{
    local control_dict="$case_dir/system/controlDict" backup="$case_dir/system/controlDict.reportBackup.$$" status=0
    cp -p -- "$control_dict" "$backup" || return 1
    if ! sed -i -E -e 's/^([[:space:]]*writeControl[[:space:]]+)[^;]+;/\1timeStep;/' -e 's/^([[:space:]]*writeInterval[[:space:]]+)[^;]+;/\1 1;/' "$control_dict"; then status=1; fi
    if [[ "$status" == 0 ]] && ! "$foam_launcher" semiFrozenChtMultiRegionFoam -case "$case_dir" -postProcess -latestTime; then status=1; fi
    mv -f -- "$backup" "$control_dict" || return 1
    return "$status"
}

reconstruct_time=$(latest_processor_restart_time)
"$foam_launcher" reconstructPar -case "$case_dir" -allRegions -time "$reconstruct_time"

if ! write_final_reports; then
    echo "Final OpenFOAM report generation failed at t=$reconstruct_time." >&2
    exit 3
fi

"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/fluid/fvSolution" -entry PIMPLE/frozenFlow -set false
"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/fluid/fvSolution" -entry PIMPLE/semiFrozenFlow -set false
"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/fluid/fvSolution" -entry PIMPLE/thermalOnlyFlow -set false
"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/fluid/fvSolution" -entry PIMPLE/momentumPredictor -set true
"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/fvSolution" -entry PIMPLE/nOuterCorrectors -set 3
"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry maxCo -set 0.40000000000000002
"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry maxDeltaT -set 0.25
"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry adjustTimeStep -set true
if [[ "$mode" == "--warm-start" ]]; then
    "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry startFrom -set startTime
    "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry startTime -set "$reconstruct_time"
    "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry endTime -set 12.5
    "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry writeControl -set adjustableRunTime
    "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry writeInterval -set 2.5
    rm -rf -- "$case_dir/.accepted_airflow_reference" "$case_dir/.accepted_airflow_reference.tmp"
    rm -f -- "$case_dir/.airflow_convergence_state" "$case_dir/.velocity_convergence_state" "$case_dir/.thermal_convergence_state" "$case_dir/.thermal_convergence_streak" "$case_dir/.airflow_refresh_pending" "$case_dir/.initial_airflow_pending" "$case_dir/.initial_air_exchange_state"
    echo "Warm start invalidated cached airflow and thermal convergence references."
    echo "Warm start complete. The normal transient is configured to resume from latestTime."
elif [[ "$mode" == "--multirate" ]]; then
    "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry startFrom -set startTime
    "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry startTime -set "$reconstruct_time"
    "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry endTime -set 12.5
    "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry writeControl -set adjustableRunTime
    "$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry writeInterval -set 2.5
    if [[ -f "$refresh_pending_marker" ]]; then
        echo "Multirate endpoint reached with an airflow refresh still pending; production controls restored. Continue this case before treating the endpoint as converged."
        summary "run_paused mode=$mode reconstructedTime=$reconstruct_time reason=airflow_refresh_pending"
    else
        echo "Multirate run complete; production controls restored."
        summary "run_complete mode=$mode reconstructedTime=$reconstruct_time"
    fi
else
    echo "Parallel CHT run and latest-time reconstruction complete."
fi
