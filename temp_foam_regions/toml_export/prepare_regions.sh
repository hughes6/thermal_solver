#!/usr/bin/env bash
set -euo pipefail

case_dir="$(cd "$(dirname "$0")" && pwd)"
foam_launcher="${OPENFOAM_LAUNCHER:-openfoam2606}"

if [[ -f "$case_dir/.openfoam_regions_prepared" ]]; then
    echo "Region meshes already prepared; reusing existing topology."
    exit 0
fi

"$foam_launcher" splitMeshRegions -case "$case_dir" -cellZonesOnly -overwrite

"$foam_launcher" topoSet -case "$case_dir" -region fluid -latestTime -dict "$case_dir/system/topoSetDict_fluid_interfaces"

"$foam_launcher" checkMesh -case "$case_dir" -allRegions -allGeometry -allTopology

touch "$case_dir/.openfoam_regions_prepared"
echo "Region meshes prepared successfully."
