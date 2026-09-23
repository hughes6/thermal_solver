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
[[ "$seed_case" != "$thermal_case" ]] || { echo 'Seed and target must differ.' >&2; exit 4; }
for existing in "$thermal_case"/processor[0-9]* "$thermal_case"/.initial_airflow_converged "$thermal_case"/.cold_flow_seed_import_manifest; do
    [[ ! -e "$existing" && ! -L "$existing" ]] || { echo "Target is already initialized: $existing" >&2; exit 4; }
done
while IFS= read -r existing; do
    if [[ "$existing" =~ ^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$ ]] && awk -v t="$existing" 'BEGIN {exit !(t>0)}'; then
        echo "Target contains saved time $existing; use a fresh export." >&2; exit 4
    fi
done < <(find "$thermal_case" -mindepth 1 -maxdepth 1 -type d -printf '%f\n')

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
[[ "$(manifest_value version)" == 2 ]] || { echo 'Seed requires version 2 metadata; regenerate it with the corrected runner.' >&2; exit 5; }
[[ "$seed_time" =~ ^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$ ]] || exit 5
[[ -n "$seed_time" ]] || { echo "Seed manifest has no seed_time: $manifest" >&2; exit 5; }

fingerprint_case() {
    local case_dir="$1" file
    local -a files=()
    [[ -f "$case_dir/constant/fluid/polyMesh/points" ]] || { echo 'Prepare target region meshes before importing.' >&2; return 1; }
    for file in "$case_dir/constant/regionProperties" "$case_dir/constant/g"; do
        [[ -f "$file" ]] && files+=("$file")
    done
    for file in "$case_dir"/constant/*/polyMesh/{points,boundary,faces,owner,neighbour}; do
        [[ -f "$file" ]] && files+=("$file")
    done
    ((${#files[@]} > 0)) || return 1
    (cd "$case_dir"; for file in "${files[@]}"; do sha256sum "${file#"$case_dir/"}"; done) | sha256sum | awk '{print $1}'
}

expected_geometry=$(manifest_value geometry_sha256)
actual_geometry=$(fingerprint_case "$thermal_case")
if [[ -z "$expected_geometry" || "$expected_geometry" != "$actual_geometry" || "$expected_geometry" != "$(fingerprint_case "$seed_case")" ]]; then
    echo "Geometry fingerprint mismatch; refusing to seed a different mesh." >&2
    echo "  seed:   $expected_geometry" >&2
    echo "  target: $actual_geometry" >&2
    exit 6
fi
exec 8>>"$seed_case/.thermal_solver_run.lock"
flock -n 8 || { echo 'Seed is running.' >&2; exit 4; }
exec 9>>"$thermal_case/.thermal_solver_run.lock"
flock -n 9 || { echo 'Target is running.' >&2; exit 4; }

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

fields=(U p p_rgh phi rho)
for field in k omega nut alphat; do
    if [[ -f "$target_fluid/$field" ]]; then fields+=("$field"); fi
done
for field in "${fields[@]}"; do
    [[ -f "$source_fluid/$field" ]] || {
        echo "Seed checkpoint is missing fluid/$field at t=$seed_time." >&2
        exit 8
    }
done

backup="$thermal_case/.fluid.beforeColdFlowSeed.$(date -u +%Y%m%dT%H%M%SZ)"
cp -a -- "$target_fluid" "$backup"
for field in "${fields[@]}"; do cp -p -- "$source_fluid/$field" "$target_fluid/$field"; done

# No T field is copied: fluid and solid temperatures remain the target's ambient 0/ state.
if [[ -f "$thermal_case/system/controlDict" ]]; then
    "$launcher" foamDictionary -precision 17 "$thermal_case/system/controlDict" -entry startFrom -set startTime >/dev/null
    "$launcher" foamDictionary -precision 17 "$thermal_case/system/controlDict" -entry startTime -set 0 >/dev/null
fi

rm -f -- "$thermal_case/.initial_airflow_pending" "$thermal_case/.initial_air_exchange_state" "$thermal_case/.airflow_refresh_pending" "$thermal_case/.mapped_initial_state"
mkdir -p "$thermal_case/.accepted_airflow_reference"
printf '0\n' > "$thermal_case/.accepted_airflow_reference/time"

"$launcher" foamDictionary -precision 17 "$thermal_case/system/decomposeParDict" -entry numberOfSubdomains -set "$processes" >/dev/null
"$launcher" decomposePar -case "$thermal_case" -allRegions -time 0

for ((rank=0; rank<processes; ++rank)); do
    for field in "${fields[@]}" T; do
        [[ -s "$thermal_case/processor${rank}/0/fluid/$field" ]] || {
            echo "Decomposition missing processor${rank}/0/fluid/$field; no acceptance marker written." >&2; exit 9;
        }
    done
    u="$thermal_case/processor${rank}/0/fluid/U"
    [[ -f "$u" ]] || { echo "decomposePar did not produce processor${rank}/0/fluid/U." >&2; exit 9; }
    mkdir -p "$thermal_case/.accepted_airflow_reference/processor${rank}"
    cp -p -- "$u" "$thermal_case/.accepted_airflow_reference/processor${rank}/U"
done
touch "$thermal_case/.initial_airflow_converged" "$thermal_case/.fan_ramp_complete"

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
