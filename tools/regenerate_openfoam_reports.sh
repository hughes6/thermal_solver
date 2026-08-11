#!/usr/bin/env bash
set -euo pipefail

case_dir="${1:-.}"
foam_launcher="${OPENFOAM_LAUNCHER:-openfoam2606}"
solver="${OPENFOAM_REPORT_SOLVER:-semiFrozenChtMultiRegionFoam}"
case_dir="$(cd "$case_dir" && pwd)"
control_dict="$case_dir/system/controlDict"
backup="$control_dict.reportBackup.$$"

if [[ ! -f "$control_dict" ]]; then
    echo "Not an OpenFOAM case: $case_dir" >&2
    exit 2
fi

# The OpenFOAM launcher validates its inherited working-directory path even
# when -case is supplied. Entering the case also avoids failures when the
# calling repository path contains spaces.
cd "$case_dir"

restore_control_dict()
{
    if [[ -f "$backup" ]]; then
        mv -f -- "$backup" "$control_dict"
    fi
}
trap restore_control_dict EXIT INT TERM

cp -p -- "$control_dict" "$backup"
sed -i -E \
    -e 's/^([[:space:]]*writeControl[[:space:]]+)[^;]+;/\1timeStep;/' \
    -e 's/^([[:space:]]*writeInterval[[:space:]]+)[^;]+;/\1 1;/' \
    "$control_dict"

"$foam_launcher" "$solver" -case "$case_dir" -postProcess -latestTime
restore_control_dict
trap - EXIT INT TERM
echo "Regenerated final OpenFOAM reports from the latest reconstructed fields."
