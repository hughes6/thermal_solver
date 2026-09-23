#!/usr/bin/env bash
# Import a developed HEATED velocity field into a separate unheated qualification case.
set -euo pipefail
[[ $# == 4 ]] || { echo "Usage: $0 HEATED_CASE FRESH_TARGET EXACT_TIME PROCESSES" >&2; exit 2; }
source_case=$(realpath -e -- "$1")
target=$(realpath -e -- "$2")
checkpoint=$3
ranks=$4
launcher=${OPENFOAM_LAUNCHER:-openfoam2606}
[[ "$checkpoint" =~ ^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$ ]] || exit 2
[[ "$ranks" =~ ^[1-9][0-9]*$ ]] && ((ranks>=2)) || exit 2
[[ "$source_case" != "$target" && "$target" != "$source_case/"* && "$source_case" != "$target/"* ]] || { echo 'Source and target must be separate, non-nested cases.' >&2; exit 2; }
qualification="${target}.heated-flow-check"
[[ ! -e "$qualification" && ! -L "$qualification" ]] || { echo "Qualification already exists: $qualification. Use its printed continuation commands." >&2; exit 3; }
for case_path in "$source_case" "$target"; do
    [[ -d "$case_path/constant/fluid/polyMesh" && -f "$case_path/system/controlDict" ]] || { echo "Prepare region meshes first: $case_path" >&2; exit 3; }
done
[[ -f "$target/run_parallel.sh" && -f "$target/create_thermal_branch_from_cold_flow_seed.sh" ]] || { echo 'Target requires a new export with reuse helpers.' >&2; exit 3; }
for entry in "$target"/processor[0-9]* "$target"/.initial_airflow_converged "$target"/.cold_flow_seed_started; do
    [[ ! -e "$entry" && ! -L "$entry" ]] || { echo 'Target must be a fresh, unrun export.' >&2; exit 3; }
done
while IFS= read -r name; do
    if [[ "$name" =~ ^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$ ]] && awk -v t="$name" 'BEGIN {exit !(t>0)}'; then
        echo 'Target contains saved times; refusing.' >&2; exit 3
    fi
done < <(find "$target" -mindepth 1 -maxdepth 1 -type d -printf '%f\n')
# Respect runner locks without changing source data or even creating a source lock.
if [[ -f "$source_case/.thermal_solver_run.lock" ]]; then
    exec 8<"$source_case/.thermal_solver_run.lock"
    flock -n 8 || { echo 'Source is running; stop and checkpoint it before importing.' >&2; exit 3; }
fi
exec 9>>"$target/.thermal_solver_run.lock"
flock -n 9 || { echo 'Target is running.' >&2; exit 3; }
mesh_hash() (
    cd "$1"
    sha256sum constant/regionProperties constant/g constant/*/polyMesh/{points,faces,owner,neighbour,boundary} | sha256sum | awk '{print $1}'
)
[[ "$(mesh_hash "$source_case")" == "$(mesh_hash "$target")" ]] || { echo 'Prepared meshes differ; this importer does not interpolate.' >&2; exit 4; }
fields=(U)
for field in k omega nut alphat; do [[ ! -f "$target/0/fluid/$field" ]] || fields+=("$field"); done
# Validate every required donor field before allocating the qualification case.
donor_ranks=0
while [[ -d "$source_case/processor$donor_ranks" ]]; do ((donor_ranks+=1)); done
if ((donor_ranks)); then
    for ((rank=0; rank<donor_ranks; ++rank)); do
        for field in "${fields[@]}"; do
            [[ -s "$source_case/processor$rank/$checkpoint/fluid/$field" ]] || { echo "Missing donor processor$rank/$checkpoint/fluid/$field" >&2; exit 5; }
        done
    done
else
    for field in "${fields[@]}"; do [[ -s "$source_case/$checkpoint/fluid/$field" ]] || { echo "Missing donor $checkpoint/fluid/$field" >&2; exit 5; }; done
fi
mkdir -- "$qualification"
cp -a -- "$target/." "$qualification/"
snapshot="$qualification/.heated-donor-snapshot"
mkdir -- "$snapshot"
cp -aL -- "$source_case/constant" "$source_case/system" "$snapshot/"
if ((donor_ranks)); then
    for ((rank=0; rank<donor_ranks; ++rank)); do
        mkdir -p "$snapshot/processor$rank/$checkpoint/fluid"
        cp -aL -- "$source_case/processor$rank/constant" "$snapshot/processor$rank/constant"
        for field in "${fields[@]}"; do cp -pL -- "$source_case/processor$rank/$checkpoint/fluid/$field" "$snapshot/processor$rank/$checkpoint/fluid/$field"; done
    done
    (cd "$snapshot"; "$launcher" reconstructPar -case "$snapshot" -region fluid -time "$checkpoint" -fields "(${fields[*]})") > "$qualification/reconstruct-donor.log" 2>&1
else
    mkdir -p "$snapshot/$checkpoint/fluid"
    for field in "${fields[@]}"; do cp -pL -- "$source_case/$checkpoint/fluid/$field" "$snapshot/$checkpoint/fluid/$field"; done
fi
for field in "${fields[@]}"; do
    [[ -s "$snapshot/$checkpoint/fluid/$field" ]] || { echo 'Donor reconstruction incomplete; qualification not ready.' >&2; exit 5; }
    cp -p -- "$snapshot/$checkpoint/fluid/$field" "$qualification/0/fluid/$field"
done
# p, p_rgh, and every T stay at the target initial state. No hot rho, phi,
# enthalpy or convergence markers are imported. The solver creates new rho/phi.
touch "$qualification/.fan_ramp_complete"
printf 'source_case %s\nsource_time %s\nsource_kind heated\n' "$source_case" "$checkpoint" > "$qualification/.heated_airflow_origin"
printf '\nHeated velocity imported into %s\n' "$qualification"
echo 'The original heated checkpoint is untouched. Target watts and ambient T are retained.'
echo 'Qualification is required; no convergence marker has been manufactured.'
echo 'Run a bounded unheated check (increase 0.2 only if needed and within airflow_warmup_time):'
printf 'cd %q && THERMAL_SOLVER_OPENFOAM_ENV_READY=1 OPENFOAM_LAUNCHER=%q bash ./run_parallel.sh %q --cold-flow-seed 0.2\n' "$qualification" "$launcher" "$ranks"
echo 'After .cold_flow_seed_complete exists, import into the untouched thermal target:'
printf 'OPENFOAM_LAUNCHER=%q bash %q %q %q %q\n' "$launcher" "$target/create_thermal_branch_from_cold_flow_seed.sh" "$qualification" "$target" "$ranks"
echo 'Then run the --multirate command printed by that importer.'
