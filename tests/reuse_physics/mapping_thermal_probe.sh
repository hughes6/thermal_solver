#!/usr/bin/env bash
source /usr/lib/openfoam/openfoam2606/etc/bashrc
set -euo pipefail
export THERMAL_SOLVER_OPENFOAM_ENV_READY=1 OPENFOAM_LAUNCHER=env OMPI_MCA_rmaps_base_oversubscribe=1
work=${1:?Supply existing mapping test path}
cd "$work/fine60"
bash ./create_thermal_branch_from_cold_flow_seed.sh "$work/fine60.mapped-flow-check" "$PWD" 2 > "$work/import.log" 2>&1
timeout 120 bash ./run_parallel.sh 2 --multirate 0.05 > "$work/thermal.log" 2>&1
echo MAPPED_THERMAL_COMPLETE
