#!/usr/bin/env bash
# Install the Courant-validation repair into an already-exported OpenFOAM case.
# This preserves processor time directories and leaves a timestamped runner backup.
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 /absolute/path/to/exported-case" >&2
    exit 64
fi

case_dir=$1
runner="$case_dir/run_parallel.sh"
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

[[ -f "$runner" ]] || { echo "Missing runner: $runner" >&2; exit 66; }
[[ -d "$case_dir/system" ]] || { echo "Missing system directory: $case_dir/system" >&2; exit 66; }
command -v python3 >/dev/null || { echo "python3 is required for this one-time runner repair." >&2; exit 69; }

install -m 0644 "$script_dir/courantValidationDict" "$case_dir/system/courantValidationDict"
backup="$runner.before-courant-validation-repair.$(date +%Y%m%d-%H%M%S)"
cp -p -- "$runner" "$backup"

python3 - "$runner" <<'PY'
from pathlib import Path
import re
import sys

path = Path(sys.argv[1])
text = path.read_text()
helper = '''run_latest_courant_postprocess()
{
    local output_name="$1"
    # Generic postProcess does not initialise phi for a decomposed multi-region
    # CHT restart.  Solver postProcess initialises the same fluid objects without
    # advancing time.
    run_tracked_capture "$output_name" "$foam_launcher" mpirun -np "$processes" "$semi_frozen_solver" -case "$case_dir" -parallel -postProcess -latestTime -dict system/courantValidationDict
}
'''
needle = 'validate_latest_airflow_courant()\n{\n'
already_repaired = helper in text
if not already_repaired:
    if needle not in text:
        raise SystemExit('Unsupported runner: missing validate_latest_airflow_courant().')
    text = text.replace(needle, helper + needle, 1)

pattern = re.compile(
    r'(?m)^(?P<indent>\s*)if ! run_tracked_capture '
    r'(?P<name>output|courant_output|postflight_output) '
    r'"\$foam_launcher" mpirun -np "\$processes" postProcess '
    r'-case "\$case_dir" -parallel -region fluid -latestTime '
    r'-fields \'\(phi rho\)\' -funcs \'\(CourantNo fieldMinMax\(Co\)\)\'; then$'
)
text, replacements = pattern.subn(
    lambda m: f'{m.group("indent")}if ! run_latest_courant_postprocess {m.group("name")}; then',
    text,
)
if replacements == 0 and already_repaired and text.count('run_latest_courant_postprocess ') >= 3:
    pass
elif replacements != 3:
    raise SystemExit(f'Unsupported runner: expected 3 Courant calls, found {replacements}.')
path.write_text(text)
PY

chmod +x "$runner"
echo "Installed Courant-validation repair."
echo "Runner backup: $backup"
echo "Resume normally with:"
echo "  THERMAL_SOLVER_OPENFOAM_ENV_READY=1 OPENFOAM_LAUNCHER=env bash \"$runner\" 4 --multirate 30"
