#!/usr/bin/env bash
set -euo pipefail

case_dir="$(cd "$(dirname "$0")" && pwd)"
foam_launcher="${OPENFOAM_LAUNCHER:-openfoam2606}"
run_lock="$case_dir/.thermal_solver_run.lock"
if ! command -v flock >/dev/null 2>&1; then
    echo "Required command 'flock' is unavailable." >&2
    exit 4
fi
exec 9>>"$run_lock"
if ! flock -n 9; then
    owner="$(tail -n 1 "$run_lock" 2>/dev/null || true)"
    echo "Another thermal solver is already writing this case${owner:+ (PID $owner)}." >&2
    exit 3
fi
: >"$run_lock"
printf '%s\n' "$$" >&9

bash "$case_dir/prepare_regions.sh"
"$foam_launcher" chtMultiRegionFoam -case "$case_dir" "$@"
