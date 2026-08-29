#!/usr/bin/env bash
set -euo pipefail

# Low-memory, fidelity-preserving front end for an exported prepare_regions.sh.
# This script must be run inside an initialized OpenFOAM 2606 environment.  It
# does not start WSL or bypass the host resource/provenance gates.

if (( $# != 1 )); then
    echo "Usage: $0 OPENFOAM_CASE" >&2
    exit 64
fi

case_dir="$(cd "$1" && pwd)"
tool_dir="$(cd "$(dirname "$0")" && pwd)"
mapper="$tool_dir/openfoam_stream_region_selectors.py"
foam_launcher="${OPENFOAM_LAUNCHER:-openfoam2606}"
checkpoint_dir="$case_dir/.openfoam_prepare_checkpoints"
split_checkpoint="$checkpoint_dir/splitMeshRegions.state.json"
prepare_lock="$case_dir/.openfoam_prepare.lock"

if [[ ! -f "$case_dir/prepare_regions.sh" ]]; then
    echo "ERROR: missing generated preparation script: $case_dir/prepare_regions.sh" >&2
    exit 2
fi
if [[ ! -f "$mapper" ]]; then
    echo "ERROR: missing bounded-memory selector mapper: $mapper" >&2
    exit 2
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: python3 is required for exact streaming selector projection." >&2
    exit 2
fi
if ! command -v flock >/dev/null 2>&1; then
    echo "ERROR: flock is required to serialize region preparation." >&2
    exit 2
fi
if ! command -v -- "$foam_launcher" >/dev/null 2>&1; then
    echo "ERROR: OpenFOAM launcher is unavailable: $foam_launcher" >&2
    exit 2
fi

exec 8>>"$prepare_lock"
if ! flock -n 8; then
    owner="$(tail -n 1 "$prepare_lock" 2>/dev/null || true)"
    echo "ERROR: another region preparation owns this case${owner:+ (PID $owner)}." >&2
    exit 3
fi
: >"$prepare_lock"
printf '%s\n' "$$" >&8

mkdir -p "$checkpoint_dir"
rm -f "$case_dir/.openfoam_regions_prepared"
python3 "$mapper" stage --case "$case_dir"

if python3 "$mapper" verify-split --case "$case_dir" >/dev/null 2>&1; then
    echo "Reusing content-verified split region meshes."
else
    rm -f "$split_checkpoint" "$checkpoint_dir/splitMeshRegions.done"
    split_log="$case_dir/splitMeshRegions.low_memory.log"
    "$foam_launcher" splitMeshRegions \
        -case "$case_dir" -cellZonesOnly -overwrite </dev/null 2>&1 | tee "$split_log"
    python3 "$mapper" record-split --case "$case_dir" >/dev/null
fi

# This checks every cellRegionAddressing entry used by a selector, requires all
# selected cells to survive in the declared region, and atomically writes each
# small logical field.  The separate PASS audit is invalidated before any
# rebuild and republished only after every derived field succeeds.
python3 "$mapper" materialize --case "$case_dir" \
    --audit selector_mapping_audit.json
python3 "$mapper" verify-materialized --case "$case_dir" \
    --audit selector_mapping_audit.json >/dev/null

# The generated script now skips splitMeshRegions but still runs every topoSet,
# all-region checkMesh, determinant policy, and final prepared-marker gate.
THERMAL_SIM_LOW_MEMORY_PREP_ACTIVE=1 bash "$case_dir/prepare_regions.sh"
