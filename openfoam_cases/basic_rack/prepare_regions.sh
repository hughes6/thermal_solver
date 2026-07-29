#!/usr/bin/env bash
set -euo pipefail

case_dir="$(cd "$(dirname "$0")" && pwd)"
foam_launcher="${OPENFOAM_LAUNCHER:-openfoam2606}"

"$foam_launcher" splitMeshRegions -case "$case_dir" -cellZones -overwrite

"$foam_launcher" topoSet -case "$case_dir" -region example_server_0 -dict "$case_dir/system/topoSetDict_processor_heat_source_0"

"$foam_launcher" checkMesh -case "$case_dir" -allRegions -allGeometry -allTopology

echo "Region meshes prepared successfully."
