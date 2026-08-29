#!/usr/bin/env bash
set -euo pipefail

case_dir="$(cd "$(dirname "$0")" && pwd)"
foam_launcher="${OPENFOAM_LAUNCHER:-openfoam2606}"
echo "Serial run_cht.sh is disabled for this multirate export because it bypasses validated live-airflow timestep caps. Use ./run_parallel.sh [processes] --multirate [end-time]." >&2
exit 2
