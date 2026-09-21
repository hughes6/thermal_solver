#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat >&2 <<'EOF'
Usage:
  create_thermal_branch_from_cold_flow_seed.sh SEED_CASE THERMAL_CASE [PROCESSES]

SEED_CASE must be a completed --cold-flow-seed case. THERMAL_CASE must be a
fresh export of the same geometry, fan configuration, mesh, and ambient state.
The script copies developed fluid fields only; all fluid and solid temperatures
remain those from THERMAL_CASE/0. It then decomposes the branch and prints the
command needed to begin the normal --multirate thermal run.
EOF
    exit 2
}

[[ $# -ge 2 && $# -le 3 ]] || usage
seed_case=$(cd "$1" && pwd -P)
thermal_case=$(cd "$2" && pwd -P)
processes="${3:-${THERMAL_SOLVER_PROCESSES:-4}}"
launcher="${OPENFOAM_LAUNCHER:-openfoam2606}"

if ! [[ "$processes" =~ ^[1-9][0-9]*$ ]] || (( processes < 2 )); then
    echo "Processes must be an integer of at least two." >&2
    exit 2
fi

marker="$seed_case/.cold_flow_seed_complete"
manifest="$seed_case/.cold_flow_seed_manifest"
[[ -f "$marker" && -f "$manifest" ]] || {
    echo "Seed is not a completed cold-flow seed: $seed_case" >&2
    echo "Run it with: bash \"$seed_case/run_parallel.sh\" $processes --cold-flow-seed 3" >&2
    exit 3
}

for required in constant system 0; do
    [[ -e "$thermal_case/$required" ]] || {
        echo "Thermal target is missing $required/: $thermal_case" >&2
        exit 4
    }
done

manifest_value() { awk -v key="$1" '$1 == key { print $2; exit }' "$manifest"; }
seed_time=$(manifest_value seed_time)
[[ -n "$seed_time" ]] || { echo "Seed manifest has no seed_time: $manifest" >&2; exit 5; }

fingerprint_case() {
    local case_dir="$1" file
    local -a files=()
    for file in "$case_dir/constant/regionProperties" "$case_dir/constant/g" "$case_dir/system/decomposeParDict"; do
        [[ -f "$file" ]] && files+=("$file")
    done
    for file in "$case_dir"/constant/*/polyMesh/{points,boundary,faces,owner,neighbour}; do
        [[ -f "$file" ]] && files+=("$file")
    done
    ((${#files[@]} > 0)) || return 1
    for file in "${files[@]}"; do sha256sum "$file"; done | sha256sum | awk '{print $1}'
}

expected_geometry=$(manifest_value geometry_sha256)
actual_geometry=$(fingerprint_case "$thermal_case" || true)
if [[ -n "$expected_geometry" && -n "$actual_geometry" && "$expected_geometry" != "$actual_geometry" ]]; then
    echo "Geometry fingerprint mismatch; refusing to seed a different mesh." >&2
    echo "  seed:   $expected_geometry" >&2
    echo "  target: $actual_geometry" >&2
    exit 6
fi

# The seed runner exits before final reconstruct, so reconstruct the source once.
if [[ ! -f "$seed_case/$seed_time/fluid/U" ]]; then
    echo "Reconstructing cold-flow seed at t=$seed_time before import."
    "$launcher" reconstructPar -case "$seed_case" -allRegions -time "$seed_time"
fi

source_fluid="$seed_case/$seed_time/fluid"
target_fluid="$thermal_case/0/fluid"
[[ -d "$source_fluid" && -d "$target_fluid" ]] || {
    echo "Missing source or target fluid directory for seed import." >&2
    echo "  source: $source_fluid" >&2
    echo "  target: $target_fluid" >&2
    exit 7
}

fields=(U p p_rgh phi rho k omega nut alphat)
for field in "${fields[@]}"; do
    [[ -f "$source_fluid/$field" ]] || {
        echo "Seed checkpoint is missing fluid/$field at t=$seed_time." >&2
        exit 8
    }
done

backup="$thermal_case/0/fluid.beforeColdFlowSeed.$(date -u +%Y%m%dT%H%M%SZ)"
cp -a -- "$target_fluid" "$backup"
for field in "${fields[@]}"; do cp -p -- "$source_fluid/$field" "$target_fluid/$field"; done

# No T field is copied: fluid and solid temperatures remain the target's ambient 0/ state.
if [[ -f "$thermal_case/system/controlDict" ]]; then
    "$launcher" foamDictionary -precision 17 "$thermal_case/system/controlDict" -entry startFrom -set startTime >/dev/null
    "$launcher" foamDictionary -precision 17 "$thermal_case/system/controlDict" -entry startTime -set 0 >/dev/null
fi

rm -f -- "$thermal_case/.initial_airflow_pending" "$thermal_case/.initial_air_exchange_state" "$thermal_case/.airflow_refresh_pending" "$thermal_case/.mapped_initial_state"
touch "$thermal_case/.initial_airflow_converged"
rm -rf -- "$thermal_case/.accepted_airflow_reference"
mkdir -p "$thermal_case/.accepted_airflow_reference"
printf '0\n' > "$thermal_case/.accepted_airflow_reference/time"

for processor_dir in "$thermal_case"/processor[0-9]*; do
    [[ -d "$processor_dir" ]] || continue
    rm -rf -- "$processor_dir"
done
"$launcher" foamDictionary -precision 17 "$thermal_case/system/decomposeParDict" -entry numberOfSubdomains -set "$processes" >/dev/null
"$launcher" decomposePar -case "$thermal_case" -allRegions -latestTime -force

for ((rank=0; rank<processes; ++rank)); do
    u="$thermal_case/processor${rank}/0/fluid/U"
    [[ -f "$u" ]] || { echo "decomposePar did not produce processor${rank}/0/fluid/U." >&2; exit 9; }
    mkdir -p "$thermal_case/.accepted_airflow_reference/processor${rank}"
    cp -p -- "$u" "$thermal_case/.accepted_airflow_reference/processor${rank}/U"
done

cat > "$thermal_case/.cold_flow_seed_import_manifest" <<EOF
version 1
source_case $seed_case
source_seed_time $seed_time
source_geometry_sha256 ${expected_geometry:-unknown}
processes $processes
temperatures_preserved_from_target_zero true
EOF

echo
echo "Cold-flow thermal branch created: $thermal_case"
echo "Imported fields: ${fields[*]}"
echo "Temperatures copied: none (target 0/ remains ambient)."
echo "Backup of target 0/fluid: $backup"
echo
echo "Continue with:"
echo "THERMAL_SOLVER_OPENFOAM_ENV_READY=1 OPENFOAM_LAUNCHER=$launcher bash '$thermal_case/run_parallel.sh' $processes --multirate 30"
