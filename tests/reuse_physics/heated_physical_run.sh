#!/usr/bin/env bash
source /usr/lib/openfoam/openfoam2606/etc/bashrc
set -euo pipefail
export THERMAL_SOLVER_OPENFOAM_ENV_READY=1 OPENFOAM_LAUNCHER=env OMPI_MCA_rmaps_base_oversubscribe=1
assets=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=${REUSE_TEST_REPO:-$(cd "$assets/../.." && pwd)}
export THERMAL_SIM_PROJECT_ROOT="$repo"
work=${1:?pass physical-test work directory}
cd "$work"
checkpoint=$(foamListTimes -case "$work/thermal" -processor -latestTime)
[[ -n "$checkpoint" ]] || exit 1
# Store hashes outside the donor. The full donor tree must remain identical.
find thermal -type f -print0 | sort -z | xargs -0 sha256sum > donor.before.sha256
"$work/export_case" "$work/load60" 60
cd "$work/load60"
bash ./prepare_regions_low_memory.sh "$PWD" > "$work/load60.prepare.log" 2>&1
bash "$repo/tools/prepare_heated_airflow_reuse.sh" "$work/thermal" "$work/load60" "$checkpoint" 2 > "$work/heated.prepare.log" 2>&1
echo HEATED_IMPORT_PREPARED
cd "$work/load60.heated-flow-check"
timeout 120 bash ./run_parallel.sh 2 --cold-flow-seed 0.5 > "$work/heated.qualify.log" 2>&1
echo HEATED_FLOW_QUALIFIED
cd "$work/load60"
bash ./create_thermal_branch_from_cold_flow_seed.sh "$work/load60.heated-flow-check" "$work/load60" 2 > "$work/load60.import.log" 2>&1
timeout 120 bash ./run_parallel.sh 2 --multirate 0.05 > "$work/load60.thermal.log" 2>&1
cd "$work"
find thermal -type f -print0 | sort -z | xargs -0 sha256sum > donor.after.sha256
cmp donor.before.sha256 donor.after.sha256
echo HEATED_DONOR_UNCHANGED_AND_THERMAL_COMPLETE
