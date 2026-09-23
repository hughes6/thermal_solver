#!/usr/bin/env bash
source /usr/lib/openfoam/openfoam2606/etc/bashrc
set -euo pipefail
export THERMAL_SOLVER_OPENFOAM_ENV_READY=1 OPENFOAM_LAUNCHER=env
export OMPI_MCA_rmaps_base_oversubscribe=1
assets=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=${REUSE_TEST_REPO:-$(cd "$assets/../.." && pwd)}
export THERMAL_SIM_PROJECT_ROOT="$repo"
work="${1:?Supply a new empty test directory}"
[[ ! -e "$work" ]] || { echo 'Use a NEW test directory; refusing to overwrite evidence.' >&2; exit 2; }
mkdir -p "$work"
g++ -std=c++20 -O0 -I "$repo/src" "$assets/physical_case.cpp" -o "$work/export_case"
"$work/export_case" "$work/seed" 100
"$work/export_case" "$work/thermal" 100
cd "$work/seed"
timeout 240 bash ./run_parallel.sh 2 --cold-flow-seed 2 > "$work/seed.log" 2>&1
echo SEED_COMPLETE
cd "$work/thermal"
timeout 90 bash ./prepare_regions_low_memory.sh "$work/thermal" > "$work/prepare.log" 2>&1
timeout 90 bash "$repo/tools/create_thermal_branch_from_cold_flow_seed.sh" "$work/seed" "$work/thermal" 2 > "$work/import.log" 2>&1
echo IMPORT_COMPLETE
timeout 120 bash ./run_parallel.sh 2 --multirate 0.05 > "$work/thermal.log" 2>&1
echo THERMAL_COMPLETE
