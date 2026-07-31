#!/usr/bin/env bash
set -euo pipefail

case_dir="$(cd "$(dirname "$0")" && pwd)"
foam_launcher="${OPENFOAM_LAUNCHER:-openfoam2606}"
processes="${1:-4}"
mode="${2:-run}"
requested_end="${3:-10}"

if ! [[ "$processes" =~ ^[1-9][0-9]*$ ]]; then
    echo "Process count must be a positive integer." >&2
    exit 2
fi
if [[ "$mode" != "run" && "$mode" != "--warm-start" && "$mode" != "--multirate" ]]; then
    echo "Usage: $0 [processes] [--warm-start|--multirate [end-time]]" >&2
    exit 2
fi
if [[ "$mode" != "run" ]] && ! [[ "$requested_end" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "Requested end time must be a positive number." >&2
    exit 2
fi

if [[ "$mode" == "--warm-start" ]]; then
    latest_time=$("$foam_launcher" foamListTimes -case "$case_dir" -latestTime 2>/dev/null || echo 0)
    latest_time="${latest_time##*$'\n'}"
    warm_interval=$(awk -v end="$requested_end" -v start="${latest_time:-0}" 'BEGIN { d=end-start; if (d<=0) exit 1; printf "%.17g", d }') || {
        echo "Warm-start end time must be greater than latest time ${latest_time:-0}." >&2
        exit 2
    }
    "$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry startFrom -set latestTime
    "$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry endTime -set "$requested_end"
    "$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry writeInterval -set "$warm_interval"
    echo "Running airflow/thermal warm start to t=$requested_end s."
fi

# Preserve an interrupted parallel stage before rebuilding the decomposition. Otherwise decomposePar -force can leave later processor fields addressed to an obsolete partition.
if [[ -d "$case_dir/processor0" ]]; then
    root_latest=$("$foam_launcher" foamListTimes -case "$case_dir" -latestTime 2>/dev/null || echo 0)
    root_latest="${root_latest##*$'\n'}"
    processor_latest=$("$foam_launcher" foamListTimes -case "$case_dir" -processor -latestTime 2>/dev/null || echo 0)
    processor_latest="${processor_latest##*$'\n'}"
    if awk -v p="${processor_latest:-0}" -v r="${root_latest:-0}" 'BEGIN { exit !(p>r) }'; then
        echo "Reconstructing interrupted parallel time $processor_latest before redecomposition."
        "$foam_launcher" reconstructPar -case "$case_dir" -allRegions -latestTime
    fi
fi

bash "$case_dir/prepare_regions.sh"
"$foam_launcher" foamDictionary "$case_dir/system/decomposeParDict" -entry numberOfSubdomains -set "$processes"
"$foam_launcher" decomposePar -case "$case_dir" -allRegions -latestTime -force

full_fan_options="$case_dir/constant/fluid/fvOptions.fullFan"
restore_full_fan_options()
{
    local processor_dir
    if [[ -f "$full_fan_options" ]]; then
        cp "$full_fan_options" "$case_dir/constant/fluid/fvOptions"
        for processor_dir in "$case_dir"/processor[0-9]*; do
            [[ -d "$processor_dir" ]] || continue
            mkdir -p "$processor_dir/constant/fluid"
            cp "$full_fan_options" "$processor_dir/constant/fluid/fvOptions"
        done
    fi
}
trap restore_full_fan_options EXIT INT TERM
set_fan_scale()
{
    local scale="$1" processor_dir full_pressure scaled_pressure measured_scale
    awk -v scale="$scale" '
        NF==2 && substr($1,1,1)=="(" && index($2,")")>0 {
            q=$1; dp=$2; gsub(/[()]/,"",q); gsub(/[()\r]/,"",dp);
            printf "   (%s %.17g)\n", q, dp*scale; next
        }
        { print }
    ' "$full_fan_options" > "$case_dir/constant/fluid/fvOptions"
    full_pressure=$(awk '$1=="(0" { gsub(/[()]/,"",$2); print $2; exit }' "$full_fan_options")
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
run_fan_ramp()
{
    local solver="$1" start="$2" limit="$3" step scale target interval
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
        set_fan_scale "$scale"
        "$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry startFrom -set latestTime
        "$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry endTime -set "$target"
        "$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry writeInterval -set "$interval"
        echo "Fan ramp stage $step/5: scale=$scale, t=$ramp_current -> $target"
        "$foam_launcher" mpirun -np "$processes" "$solver" -case "$case_dir" -parallel
        ramp_current="$target"
    done
    set_fan_scale 1
}

if [[ "$mode" == "--multirate" ]]; then
    current=$("$foam_launcher" foamListTimes -case "$case_dir" -processor -latestTime 2>/dev/null || echo 0)
    current="${current##*$'\n'}"
    current="${current:-0}"
    if awk -v a="$current" -v end="0.050000000000000003" 'BEGIN { exit !(a<end) }'; then
        run_fan_ramp semiFrozenChtMultiRegionFoam "$current" "$requested_end"
        current="$ramp_current"
    fi
    boundary_flow_names=("Fan_1" "Fan_2" "Fan_3" "Fan_4" "Fan_5" "Fan_6" "Fan_7" "Fan_8" "Fan_9" "Vent_main_intake" )
    tracked_flow_names=("Fan_1" "Fan_2" "Fan_3" "Fan_4" "Fan_5" "Fan_6" "Fan_7" "Fan_8" "Fan_9" "Vent_main_intake" )
    internal_fan_names=("internal_Rear_exhaust_fan_2" "internal_Cooling_fan_1_5" "internal_Cooling_fan_2_6" "internal_Cooling_fan_3_7" "internal_Cooling_fan_4_8" "internal_Cooling_fan_5_9" "internal_Cooling_fan_6_10" )
    stability_flow_names=("Fan_1" "Fan_2" "Fan_3" "Fan_4" "Fan_5" "Fan_6" "Fan_7" "Fan_8" "Fan_9" "Vent_main_intake" "internal_Rear_exhaust_fan_2" "internal_Cooling_fan_1_5" "internal_Cooling_fan_2_6" "internal_Cooling_fan_3_7" "internal_Cooling_fan_4_8" "internal_Cooling_fan_5_9" "internal_Cooling_fan_6_10" )
    fan_direction_rules=("Fan_1:1" "Fan_2:1" "Fan_3:1" "Fan_4:1" "Fan_5:1" "Fan_6:1" "Fan_7:1" "Fan_8:1" "Fan_9:1" )
    declare -A previous_flows=()
    airflow_metrics_converged()
    {
        local report name value rule expected net=0 sum_abs=0 flow_time properties
        local imbalance stable=1 directions_ok=1 maximum_change=0 change
        if ! report=$("$foam_launcher" mpirun -np "$processes" postProcess -case "$case_dir" -parallel -region fluid -latestTime -field phi 2>&1); then
            echo "$report" >&2
            echo "Unable to evaluate airflow refresh convergence." >&2
            return 1
        fi
        declare -A flows=()
        for name in "${tracked_flow_names[@]}"; do
            value=$(awk -v pattern="sum(${name}) of phi =" 'index($0,pattern) { value=$NF; print value; exit }' <<<"$report")
            if [[ -z "$value" ]]; then
                echo "Missing mass-flow result for $name." >&2
                return 1
            fi
            flows["$name"]="$value"
        done
        flow_time=$("$foam_launcher" foamListTimes -case "$case_dir" -processor -latestTime 2>/dev/null || echo 0)
        flow_time="${flow_time##*$'\n'}"
        for name in "${internal_fan_names[@]}"; do
            properties="$case_dir/processor0/$flow_time/fluid/uniform/${name}Properties"
            value=$(awk '$1=="flow_rate" { gsub(/;/,"",$2); print $2; exit }' "$properties" 2>/dev/null || true)
            if [[ -z "$value" ]]; then
                echo "Missing fan operating-point output for $name at t=$flow_time." >&2
                return 1
            fi
            flows["$name"]="$value"
            if ! awk -v v="$value" 'BEGIN { exit !(v>0) }'; then
                directions_ok=0
                echo "Internal fan not producing positive through-flow: $name flow_rate=$value m3/s" >&2
            fi
        done
        for name in "${stability_flow_names[@]}"; do
            value="${flows[$name]}"
            if [[ -n "${previous_flows[$name]+set}" ]]; then
                change=$(awk -v a="$value" -v b="${previous_flows[$name]}" 'BEGIN { d=a-b; if(d<0)d=-d; s=b; if(s<0)s=-s; if(s<1e-12)s=1e-12; print d/s }')
                maximum_change=$(awk -v a="$maximum_change" -v b="$change" 'BEGIN { print (a>b?a:b) }')
            else
                stable=0
            fi
        done
        for name in "${boundary_flow_names[@]}"; do
            value="${flows[$name]}"
            net=$(awk -v a="$net" -v b="$value" 'BEGIN { print a+b }')
            sum_abs=$(awk -v a="$sum_abs" -v b="$value" 'BEGIN { if(b<0)b=-b; print a+b }')
        done
        imbalance=$(awk -v n="$net" -v s="$sum_abs" 'BEGIN { if(n<0)n=-n; d=0.5*s; print (d>1e-12?n/d:1e30) }')
        for rule in "${fan_direction_rules[@]}"; do
            name="${rule%%:*}"
            expected="${rule##*:}"
            value="${flows[$name]}"
            if ! awk -v v="$value" -v e="$expected" 'BEGIN { exit !((e<0 && v<0)||(e>0 && v>0)) }'; then
                directions_ok=0
                echo "Fan direction not settled: $name phi=$value" >&2
            fi
        done
        for name in "${stability_flow_names[@]}"; do
            previous_flows["$name"]="${flows[$name]}"
        done
        if ! awk -v v="$imbalance" -v limit="0.01" 'BEGIN { exit !(v<=limit) }'; then stable=0; fi
        if ! awk -v v="$maximum_change" -v limit="0.02" 'BEGIN { exit !(v<=limit) }'; then stable=0; fi
        echo "Airflow refresh metrics: imbalance=$imbalance, maxFlowChange=$maximum_change, directionsOK=$directions_ok"
        [[ "$stable" == 1 && "$directions_ok" == 1 ]]
    }
    stage()
    {
        local thermal_only="$1" target="$2" max_co="$3" max_dt="$4" label="$5"
        local interval
        interval=$(awk -v end="$target" -v start="$current" 'BEGIN { printf "%.17g", end-start }')
        "$foam_launcher" foamDictionary "$case_dir/system/fluid/fvSolution" -entry PIMPLE/frozenFlow -set false
        "$foam_launcher" foamDictionary "$case_dir/system/fluid/fvSolution" -entry PIMPLE/semiFrozenFlow -set false
        "$foam_launcher" foamDictionary "$case_dir/system/fluid/fvSolution" -entry PIMPLE/thermalOnlyFlow -set "$thermal_only"
        "$foam_launcher" foamDictionary "$case_dir/system/fluid/fvSolution" -entry PIMPLE/momentumPredictor -set true
        if [[ "$thermal_only" == "true" ]]; then
            adjust_time_step=false
            stage_dt=$(awk -v maximum="$max_dt" -v remaining="$interval" 'BEGIN { print (remaining<maximum?remaining:maximum) }')
        else
            adjust_time_step=true
            stage_dt="0.10000000000000001"
        fi
        "$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry adjustTimeStep -set "$adjust_time_step"
        "$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry deltaT -set "$stage_dt"
        "$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry startFrom -set latestTime
        "$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry endTime -set "$target"
        "$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry writeInterval -set "$interval"
        "$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry maxCo -set "$max_co"
        "$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry maxDeltaT -set "$max_dt"
        echo "$label: t=$current -> $target"
        "$foam_launcher" mpirun -np "$processes" semiFrozenChtMultiRegionFoam -case "$case_dir" -parallel
        current="$target"
    }

    adaptive_airflow_refresh()
    {
        local refresh_start="$current" refresh_elapsed=0 refresh_target
        previous_flows=()
        while true; do
            refresh_target=$(awk -v a="$current" -v d="1" -v start="$refresh_start" -v maximum="20" -v end="$requested_end" 'BEGIN { x=a+d; limit=start+maximum; if(x>limit)x=limit; if(x>end)x=end; print x }')
            stage false "$refresh_target" 1 1 "Adaptive airflow refresh"
            refresh_elapsed=$(awk -v a="$current" -v b="$refresh_start" 'BEGIN { print a-b }')
            if awk -v a="$refresh_elapsed" -v b="1" 'BEGIN { exit !(a>=b) }'; then
                if airflow_metrics_converged; then
                    echo "Airflow refresh converged after $refresh_elapsed s."
                    return 0
                fi
            fi
            if ! awk -v a="$current" -v b="$requested_end" 'BEGIN { exit !(a<b) }'; then return 0; fi
            if awk -v a="$refresh_elapsed" -v b="20" 'BEGIN { exit !(a>=b) }'; then
                echo "Airflow refresh failed to converge within 20 s." >&2
                return 3
            fi
        done
    }

    adaptive_initial_airflow()
    {
        local initial_start="$current" initial_elapsed=0 initial_target initial_limit
        initial_limit=$(awk -v maximum="5" -v end="$requested_end" 'BEGIN { print (maximum<end?maximum:end) }')
        previous_flows=()
        while awk -v a="$current" -v b="$initial_limit" 'BEGIN { exit !(a<b) }'; do
            initial_target=$(awk -v a="$current" -v d="0.01" -v limit="$initial_limit" 'BEGIN { x=a+d; print (x<limit?x:limit) }')
            stage false "$initial_target" 1 1 "Adaptive initial airflow"
            initial_elapsed=$(awk -v a="$current" -v b="$initial_start" 'BEGIN { print a-b }')
            if awk -v a="$initial_elapsed" -v b="0.02" 'BEGIN { exit !(a>=b) }'; then
                if airflow_metrics_converged; then
                    echo "Initial airflow converged after $initial_elapsed s beyond the fan ramp; switching to thermal-only mode."
                    return 0
                fi
            fi
        done
        if ! awk -v a="$current" -v b="$requested_end" 'BEGIN { exit !(a>=b) }'; then
            echo "Initial airflow failed to converge before the airflow_warmup_time safety limit of 5 s." >&2
            return 3
        fi
    }

    if awk -v a="$current" -v b="$requested_end" 'BEGIN { exit !(a<b) }'; then
        echo "Adaptively finding initial airflow operating point."
        adaptive_initial_airflow
    fi
    while awk -v a="$current" -v b="$requested_end" 'BEGIN { exit !(a<b) }'; do
        frozen_target=$(awk -v a="$current" -v d="300" -v b="$requested_end" 'BEGIN { x=a+d; print (x<b ? x : b) }')
        stage true "$frozen_target" 1000 1 "Implicit thermal-only stage (airflow held)"
        if awk -v a="$current" -v b="$requested_end" 'BEGIN { exit !(a<b) }'; then
            adaptive_airflow_refresh
        fi
    done
else
    warm_current=$("$foam_launcher" foamListTimes -case "$case_dir" -processor -latestTime 2>/dev/null || echo 0)
    warm_current="${warm_current##*$'\n'}"
    warm_current="${warm_current:-0}"
    if [[ "$mode" == "--warm-start" ]] && awk -v a="$warm_current" -v end="0.050000000000000003" 'BEGIN { exit !(a<end) }'; then
        run_fan_ramp chtMultiRegionFoam "$warm_current" "$requested_end"
        warm_current="$ramp_current"
    fi
    if awk -v a="$warm_current" -v b="$requested_end" 'BEGIN { exit !(a<b) }'; then
        "$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry endTime -set "$requested_end"
        "$foam_launcher" mpirun -np "$processes" chtMultiRegionFoam -case "$case_dir" -parallel
    fi
fi
"$foam_launcher" reconstructPar -case "$case_dir" -allRegions -latestTime

"$foam_launcher" foamDictionary "$case_dir/system/fluid/fvSolution" -entry PIMPLE/frozenFlow -set false
"$foam_launcher" foamDictionary "$case_dir/system/fluid/fvSolution" -entry PIMPLE/semiFrozenFlow -set false
"$foam_launcher" foamDictionary "$case_dir/system/fluid/fvSolution" -entry PIMPLE/thermalOnlyFlow -set false
"$foam_launcher" foamDictionary "$case_dir/system/fluid/fvSolution" -entry PIMPLE/momentumPredictor -set true
"$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry maxCo -set 1
"$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry maxDeltaT -set 1
"$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry adjustTimeStep -set true
if [[ "$mode" == "--warm-start" ]]; then
    "$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry startFrom -set latestTime
    "$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry endTime -set 10
    "$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry writeInterval -set 60
    echo "Warm start complete. The normal transient is configured to resume from latestTime."
elif [[ "$mode" == "--multirate" ]]; then
    "$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry startFrom -set latestTime
    "$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry endTime -set 10
    "$foam_launcher" foamDictionary "$case_dir/system/controlDict" -entry writeInterval -set 60
    echo "Multirate run complete; production controls restored."
else
    echo "Parallel CHT run and latest-time reconstruction complete."
fi
