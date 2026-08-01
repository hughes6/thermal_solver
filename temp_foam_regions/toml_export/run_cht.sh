#!/usr/bin/env bash
set -euo pipefail

case_dir="$(cd "$(dirname "$0")" && pwd)"
foam_launcher="${OPENFOAM_LAUNCHER:-openfoam2606}"

bash "$case_dir/prepare_regions.sh"
"$foam_launcher" chtMultiRegionFoam -case "$case_dir" "$@"
