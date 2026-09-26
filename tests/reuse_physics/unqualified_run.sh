#!/usr/bin/env bash
source /usr/lib/openfoam/openfoam2606/etc/bashrc
set -euo pipefail
export THERMAL_SOLVER_OPENFOAM_ENV_READY=1 OPENFOAM_LAUNCHER=env OMPI_MCA_rmaps_base_oversubscribe=1
repo='/mnt/c/Users/hconn/Downloads/Thermal Sim/v2.3'
export THERMAL_SIM_PROJECT_ROOT="$repo"
work=${1:?New test directory required}
[[ ! -e "$work" ]] || exit 2
mkdir -p "$work"
cd "$work"
g++ -std=c++20 -O0 -I "$repo/src" "$repo/tests/reuse_physics/physical_case.cpp" -o export_case
./export_case "$work/seed" 100
./export_case "$work/thermal" 60
cd seed
set +e
timeout 180 bash ./run_parallel.sh 2 --cold-flow-seed 0.05 > ../seed.log 2>&1
result=$?
set -e
[[ $result == 12 ]] || { tail -40 ../seed.log; exit 3; }
[[ ! -e .cold_flow_seed_complete ]]
checkpoint=$(find processor0 -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | awk '/^[0-9.]+$/' | sort -g | tail -1)
find . -type f -print0 | sort -z | xargs -0 sha256sum > ../source.before
cd ../thermal
timeout 90 bash ./prepare_regions_low_memory.sh "$PWD" > ../prepare.log 2>&1
timeout 90 bash "$repo/tools/create_thermal_branch_from_cold_flow_seed.sh" "$work/seed" "$PWD" 2 --allow-unqualified "$checkpoint" > ../import.log 2>&1
(cd ../seed; find . -type f -print0 | sort -z | xargs -0 sha256sum) > ../source.after
cmp ../source.before ../source.after
[[ ! -e .initial_airflow_converged && ! -e .accepted_airflow_reference ]]
if bash ./run_parallel.sh 2 --multirate 0.05 > ../reject-normal.log 2>&1; then exit 4; fi
timeout 180 bash ./run_parallel.sh 2 --unqualified-thermal 0.05 > ../thermal.log 2>&1
timeout 180 bash ./run_parallel.sh 2 --unqualified-thermal 0.1 > ../resume.log 2>&1
[[ ! -e .initial_airflow_converged && ! -e .accepted_airflow_reference && ! -e .cold_flow_seed_complete ]]
reconstructPar -allRegions -latestTime > ../reconstruct.log 2>&1
echo 'PASS: pending seed imported without source mutation; normal mode rejected; explicit thermal test and restart completed; no acceptance fabricated.'
