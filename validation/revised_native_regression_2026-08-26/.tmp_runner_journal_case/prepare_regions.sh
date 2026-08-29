#!/usr/bin/env bash
set -euo pipefail

case_dir="$(cd "$(dirname "$0")" && pwd)"
foam_launcher="${OPENFOAM_LAUNCHER:-openfoam2606}"

run_toposet()
{
    "$foam_launcher" topoSet "$@" </dev/null
}

if [[ "${THERMAL_SIM_LOW_MEMORY_PREP_ACTIVE:-0}" != 1 ]]; then
    echo "ERROR: component-region preparation must enter through $case_dir/prepare_regions_low_memory.sh." >&2
    exit 2
fi
python3 "$case_dir/openfoam_stream_region_selectors.py" verify-split --case "$case_dir" >/dev/null
python3 "$case_dir/openfoam_stream_region_selectors.py" verify-materialized --case "$case_dir" --audit selector_mapping_audit.json >/dev/null

rm -f "$case_dir/.openfoam_regions_prepared" "$case_dir/.openfoam_mesh_determinant_warning"
run_toposet -case "$case_dir" -region fluid -latestTime -dict "$case_dir/system/topoSetDict_fluid_interfaces"
run_toposet -case "$case_dir" -region test_heater_0 -time 0 -dict "$case_dir/system/topoSetDict_test_heat_source_0"
run_toposet -case "$case_dir" -region homogeneous_heater_1 -time 0 -dict "$case_dir/system/topoSetDict_homogeneous_heater_load_1"
run_toposet -case "$case_dir" -region fluid -time 0 -dict "$case_dir/system/topoSetDict_heated_internal_air_2"
run_toposet -case "$case_dir" -region fluid -time 0 -dict "$case_dir/system/topoSetDict_porous_perforated_tray_0"

check_mesh_log="$case_dir/checkMesh.prepare.log"
allow_determinant_warnings="true"
"$foam_launcher" checkMesh -case "$case_dir" -allRegions -allGeometry -allTopology 2>&1 | tee "$check_mesh_log"

failed_checks=$(awk '/^Failed [1-9][0-9]* mesh checks/ { total += $2 } END { print total+0 }' "$check_mesh_log")
determinant_failures=$(grep -Ec '^[[:space:]]*\*\*\*Cells with small determinant' "$check_mesh_log" || true)
unexpected_diagnostics=$(grep -E '^[[:space:]]*\*\*\*' "$check_mesh_log" | grep -Ev 'Cells with small determinant' || true)
if (( failed_checks != determinant_failures )) || [[ -n "$unexpected_diagnostics" ]]; then
    echo "ERROR: full checkMesh reported non-determinant failures or inconsistent failure diagnostics. Review $check_mesh_log; the solver will not run." >&2
    exit 1
fi
if (( determinant_failures > 0 )); then
    touch "$case_dir/.openfoam_mesh_determinant_warning"
fi
if (( determinant_failures > 0 )) && [[ "$allow_determinant_warnings" != true ]]; then
    echo "ERROR: $determinant_failures region(s) contain cells with determinant below 0.001. This export's mesh quality policy rejects determinant warnings; the solver will not run. Review $check_mesh_log." >&2
    exit 1
fi
if (( determinant_failures > 0 )); then
    echo "SCREENING WARNING: $determinant_failures region(s) contain reduced-order cells with determinant below 0.001. All other full checkMesh checks passed. This explicitly permissive mesh is exploratory only; see $check_mesh_log." >&2
fi
touch "$case_dir/.openfoam_regions_prepared"
echo "Region meshes prepared; accepted determinant warnings: $determinant_failures."
