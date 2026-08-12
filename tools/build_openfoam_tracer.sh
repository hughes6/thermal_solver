#!/usr/bin/env bash
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "$script_dir/.." && pwd)"
foam_bashrc="${OPENFOAM_BASHRC:-/usr/lib/openfoam/openfoam2606/etc/bashrc}"
install_dir="${THERMAL_SIM_FOAM_TOOLS:-/mnt/c/OpenFOAM/thermal_sim_v2_tools}"

if [[ ! -f "$foam_bashrc" ]]; then
    echo "OpenFOAM environment not found: $foam_bashrc" >&2
    echo "Set OPENFOAM_BASHRC to the installed OpenFOAM etc/bashrc." >&2
    exit 1
fi

# shellcheck disable=SC1090
source "$foam_bashrc"
set -eo pipefail
# OpenFOAM rejects a WM_PROJECT_USER_DIR containing spaces at debug level 2.
export WM_PROJECT_USER_DIR="${THERMAL_SIM_FOAM_USER_DIR:-/tmp/thermal_sim_foam_user}"
export FOAM_USER_APPBIN="$install_dir/bin"
mkdir -p "$WM_PROJECT_USER_DIR" "$FOAM_USER_APPBIN"
cd "$project_dir/openfoam_tracer_solver"
wclean
wmake
echo "Installed $FOAM_USER_APPBIN/steadyExhaustTracerFoam"
