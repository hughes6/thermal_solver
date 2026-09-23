#!/usr/bin/env bash
source /usr/lib/openfoam/openfoam2606/etc/bashrc
set -euo pipefail
assets=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=${REUSE_TEST_REPO:-$(cd "$assets/../.." && pwd)}
work=${1:?Supply existing mapping test}
cd "$work"
source_case="$work/fine60.mapped-flow-check/.heated-donor-snapshot"
checkpoint=$(foamListTimes -case "$source_case" -latestTime)
target="$work/disjoint-negative"
[[ ! -e "$target" ]] || exit 2
cp -a "$work/fine60" "$target"
cd "$target"
transformPoints -case "$target" -region fluid -translate '(1 0 0)' > "$work/disjoint-transform.log" 2>&1
python3 "$repo/tools/mapped_airflow_checks.py" seed "$source_case" "$target" --time "$checkpoint"
mapFields "$source_case" -case "$target" -sourceRegion fluid -targetRegion fluid -sourceTime "$checkpoint" -consistent -mapMethod interpolate > "$work/disjoint-map.log" 2>&1
if python3 "$repo/tools/mapped_airflow_checks.py" verify "$source_case" "$target" > "$work/disjoint-result.log" 2>&1; then
    echo 'ERROR: disjoint target passed coverage' >&2; exit 1
fi
cat "$work/disjoint-result.log"
echo PASS_DISJOINT_MAPPING_REJECTED
