#!/usr/bin/env bash
# Interpolate developed screening airflow into a fresh finer-mesh qualification.
set -euo pipefail
[[ $# == 4 ]] || { echo "Usage: $0 SCREENING_CASE FRESH_FINE_TARGET EXACT_TIME PROCESSES" >&2; exit 2; }
helper_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
exec bash "$helper_dir/prepare_heated_airflow_reuse.sh" "$@" --map-mesh
