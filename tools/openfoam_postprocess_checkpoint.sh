#!/usr/bin/env bash
set -euo pipefail

if (( $# < 1 || $# > 3 )); then
    echo "Usage: $0 CASE_DIRECTORY [TIME] [SOLVER]" >&2
    exit 2
fi

case_dir="$(cd "$1" && pwd)"
requested_time="${2:-}"
solver="${3:-semiFrozenChtMultiRegionFoam}"
foam_launcher="${OPENFOAM_LAUNCHER:-openfoam2606}"
control_dict="$case_dir/system/controlDict"
run_lock="$case_dir/.thermal_solver_run.lock"

if [[ ! -f "$control_dict" ]]; then
    echo "Missing OpenFOAM control dictionary: $control_dict" >&2
    exit 2
fi
if ! command -v flock >/dev/null 2>&1; then
    echo "Required command 'flock' is unavailable." >&2
    exit 4
fi

exec 9>>"$run_lock"
if ! flock -n 9; then
    echo "Another thermal solver is already writing this case." >&2
    exit 3
fi

if [[ -z "$requested_time" ]]; then
    requested_time=$("$foam_launcher" foamListTimes -case "$case_dir" -latestTime)
    requested_time="${requested_time##*$'\n'}"
fi
if [[ -z "$requested_time" || ! -d "$case_dir/$requested_time" ]]; then
    echo "Missing reconstructed checkpoint time: ${requested_time:-<none>}" >&2
    exit 2
fi

backup=$(mktemp "$case_dir/system/controlDict.reportBackup.XXXXXX")
if ! cp -p -- "$control_dict" "$backup"; then
    rm -f -- "$backup"
    echo "Could not back up $control_dict" >&2
    exit 1
fi
restore_control()
{
    local status=$?
    trap - EXIT INT TERM
    if [[ -f "$backup" ]]; then
        mv -f -- "$backup" "$control_dict"
    fi
    exit "$status"
}
trap restore_control EXIT INT TERM

# Force every enabled function object to execute for this one checkpoint.
sed -i -E \
    -e 's/^([[:space:]]*writeControl[[:space:]]+)[^;]+;/\1timeStep;/' \
    -e 's/^([[:space:]]*writeInterval[[:space:]]+)[^;]+;/\1 1;/' \
    "$control_dict"

"$foam_launcher" "$solver" -case "$case_dir" -postProcess -time "$requested_time"

mv -f -- "$backup" "$control_dict"
trap - EXIT INT TERM
echo "OpenFOAM reports written for t=$requested_time with $solver."
