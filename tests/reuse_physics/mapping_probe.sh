#!/usr/bin/env bash
source /usr/lib/openfoam/openfoam2606/etc/bashrc
set -euo pipefail
export THERMAL_SOLVER_OPENFOAM_ENV_READY=1 OPENFOAM_LAUNCHER=env
export OMPI_MCA_rmaps_base_oversubscribe=1 REUSE_TEST_BUOYANT=1
assets=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=${REUSE_TEST_REPO:-$(cd "$assets/../.." && pwd)}
export THERMAL_SIM_PROJECT_ROOT="$repo"
work=${1:?Supply new Linux test path}
donor=${2:?Supply completed tiny physical_run.sh thermal donor path}
[[ ! -e "$work" ]] || exit 2
mkdir -p "$work"
cd "$work"
g++ -std=c++20 -O0 -I "$repo/src" "$assets/physical_case.cpp" -o "$work/export_case"
REUSE_TEST_FINE=1 "$work/export_case" "$work/fine60" 60
cd "$work/fine60"
bash ./prepare_regions_low_memory.sh "$PWD" > "$work/prepare.log" 2>&1
find "$donor" -type f -print0 | sort -z | xargs -0 sha256sum > "$work/donor.before.sha256"
checkpoint=$(foamListTimes -case "$donor" -processor -latestTime)
bash ./prepare_mapped_airflow_reuse.sh "$donor" "$PWD" "$checkpoint" 2 > "$work/mapping.log" 2>&1
echo MAPPING_COMPLETE
find "$donor" -type f -print0 | sort -z | xargs -0 sha256sum > "$work/donor.after.sha256"
cmp "$work/donor.before.sha256" "$work/donor.after.sha256"
cd "$work/fine60.mapped-flow-check"
timeout 180 bash ./run_parallel.sh 2 --cold-flow-seed 1 > "$work/qualification.log" 2>&1
echo QUALIFICATION_COMPLETE
