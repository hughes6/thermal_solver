#!/usr/bin/env bash
set -euo pipefail

# Run one fixed-step, thermal-only OpenFOAM continuation from an existing
# decomposed checkpoint.  This deliberately performs no airflow refresh so
# timestep variants can be compared at the same physical end time.

case_dir="$(readlink -f "${1:?case directory is required}")"
start_time="${2:?start time is required}"
duration="${3:?duration is required}"
maximum_dt="${4:?maximum thermal timestep is required}"
processes="${5:-4}"
log_path="${6:-$case_dir/thermal_dt_benchmark.stdout.log}"
foam_launcher="${OPENFOAM_LAUNCHER:-openfoam2606}"

number='^[0-9]+([.][0-9]*)?([eE][-+]?[0-9]+)?$'
for value in "$start_time" "$duration" "$maximum_dt"; do
    if ! [[ "$value" =~ $number ]] || ! awk -v v="$value" 'BEGIN { exit !(v>0) }'; then
        echo "Times and timestep must be positive finite numbers: $value" >&2
        exit 2
    fi
done
if ! [[ "$processes" =~ ^[1-9][0-9]*$ ]] || (( processes < 2 )); then
    echo "Process count must be an integer of at least two." >&2
    exit 2
fi
if [[ ! -f "$case_dir/system/controlDict" || ! -f "$case_dir/system/fluid/fvSolution" ]]; then
    echo "Not a prepared multi-region OpenFOAM case: $case_dir" >&2
    exit 2
fi

if [[ "${THERMAL_BENCHMARK_OPENFOAM_ENV_READY:-0}" != 1 && "$foam_launcher" != env ]]; then
    exec "$foam_launcher" env \
        THERMAL_BENCHMARK_OPENFOAM_ENV_READY=1 \
        OPENFOAM_LAUNCHER=env \
        bash "$(readlink -f "$0")" "$@"
fi

target_time=$(awk -v start="$start_time" -v duration="$duration" \
    'BEGIN { printf "%.17g", start+duration }')
read -r stage_dt stage_steps < <(awk -v maximum="$maximum_dt" -v duration="$duration" '
    BEGIN {
        steps=int(duration/maximum)
        if (steps*maximum < duration-1e-12) steps++
        if (steps < 1) steps=1
        printf "%.17g %d\n", duration/steps, steps
    }')

for ((rank=0; rank<processes; ++rank)); do
    checkpoint="$case_dir/processor${rank}/${start_time}"
    if [[ ! -d "$checkpoint/fluid" ]]; then
        echo "Missing decomposed start checkpoint: $checkpoint/fluid" >&2
        exit 3
    fi
done

# Reject a case containing future processor times.  A benchmark branch must
# start from exactly one preserved checkpoint so latest-time selection cannot
# silently change the initial state.
future_time=$(find "$case_dir/processor0" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' \
    | awk -v start="$start_time" '$0 ~ /^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/ && $0>start+1e-9 { print; exit }')
if [[ -n "$future_time" ]]; then
    echo "Future processor checkpoint $future_time exists; use a clean benchmark branch." >&2
    exit 3
fi

"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/fluid/fvSolution" \
    -entry PIMPLE/frozenFlow -set false
"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/fluid/fvSolution" \
    -entry PIMPLE/semiFrozenFlow -set false
"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/fluid/fvSolution" \
    -entry PIMPLE/thermalOnlyFlow -set true
"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/fluid/fvSolution" \
    -entry PIMPLE/momentumPredictor -set true

"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry startFrom -set startTime
"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry startTime -set "$start_time"
"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry stopAt -set endTime
"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry endTime -set "$target_time"
"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry adjustTimeStep -set false
"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry deltaT -set "$stage_dt"
"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry maxCo -set 1000
"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry maxDeltaT -set "$stage_dt"
"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry writeControl -set timeStep
"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry writeInterval -set "$stage_steps"
"$foam_launcher" foamDictionary -precision 17 "$case_dir/system/controlDict" -entry purgeWrite -set 0

for ((rank=0; rank<processes; ++rank)); do
    time_file="$case_dir/processor${rank}/${start_time}/uniform/time"
    [[ -f "$time_file" ]] || continue
    "$foam_launcher" foamDictionary -precision 17 "$time_file" -entry index -set 0
    "$foam_launcher" foamDictionary -precision 17 "$time_file" -entry deltaT -set "$stage_dt"
    "$foam_launcher" foamDictionary -precision 17 "$time_file" -entry deltaT0 -set "$stage_dt"
done

mkdir -p "$(dirname "$log_path")"
wall_start=$(date +%s%N)
echo "THERMAL_DT_BENCHMARK start=$start_time target=$target_time duration=$duration requestedMaxDt=$maximum_dt actualDt=$stage_dt steps=$stage_steps processes=$processes"
"$foam_launcher" mpirun -np "$processes" semiFrozenChtMultiRegionFoam \
    -case "$case_dir" -parallel 2>&1 | tee "$log_path"
wall_end=$(date +%s%N)
wall_seconds=$(awk -v start="$wall_start" -v end="$wall_end" \
    'BEGIN { printf "%.6f", (end-start)/1e9 }')

actual_time=$(find "$case_dir/processor0" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' \
    | awk -v target="$target_time" '
        $0 ~ /^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/ {
            difference=$0-target
            if (difference<0) difference=-difference
            scale=(target<0?-target:target)
            if (scale<1) scale=1
            if (difference<=1e-12*scale) { print; exit }
        }')
if [[ -z "$actual_time" ]]; then
    echo "Solver did not write a checkpoint numerically equal to target $target_time." >&2
    exit 5
fi
for ((rank=0; rank<processes; ++rank)); do
    if [[ ! -d "$case_dir/processor${rank}/${actual_time}" ]]; then
        echo "Target checkpoint $actual_time is missing on processor $rank." >&2
        exit 5
    fi
done

# Preserve frozen flow fields if this OpenFOAM build omits an unchanged field
# at the new thermal-only checkpoint.
for ((rank=0; rank<processes; ++rank)); do
    for field in U p phi k omega nut alphat; do
        source_field="$case_dir/processor${rank}/${start_time}/fluid/$field"
        target_field="$case_dir/processor${rank}/${actual_time}/fluid/$field"
        if [[ -f "$source_field" && ! -f "$target_field" ]]; then
            cp -p "$source_field" "$target_field"
        fi
    done
done

"$foam_launcher" reconstructPar -case "$case_dir" -allRegions -time "$actual_time" \
    >"${log_path%.log}.reconstruct.log" 2>&1

result_path="${log_path%.log}.result.tsv"
printf 'start_time\ttarget_time\tduration\trequested_max_dt\tactual_dt\tsteps\tprocesses\twall_seconds\tsimulated_seconds_per_wall_second\n' > "$result_path"
awk -v start="$start_time" -v target="$actual_time" -v duration="$duration" \
    -v requested="$maximum_dt" -v actual="$stage_dt" -v steps="$stage_steps" \
    -v processes="$processes" -v wall="$wall_seconds" \
    'BEGIN { printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%.9g\n", start,target,duration,requested,actual,steps,processes,wall,duration/wall }' \
    >> "$result_path"
echo "THERMAL_DT_BENCHMARK_COMPLETE target=$actual_time wallSeconds=$wall_seconds result=$result_path"
