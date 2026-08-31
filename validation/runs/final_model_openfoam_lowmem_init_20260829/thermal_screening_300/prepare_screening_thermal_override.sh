#!/usr/bin/env bash
set -euo pipefail

case_dir=$(cd "$(dirname "$0")" && pwd -P)
expected_name="final_model_openfoam_lowmem_init_20260829_screening_thermal_from_0p35"
expected_time="0.34999999999999887"

if [[ "$(basename "$case_dir")" != "$expected_name" ]]; then
    echo "Refusing screening override outside the dedicated case '$expected_name'." >&2
    exit 2
fi
if [[ ! -s "$case_dir/.initial_airflow_pending" ]] ||
   [[ ! -f "$case_dir/.initial_airflow_physical_settling" ]]; then
    echo "Expected preserved failed-airflow state is missing." >&2
    exit 2
fi
if [[ -e "$case_dir/.initial_airflow_converged" ]] ||
   [[ -e "$case_dir/.accepted_airflow_reference" ]]; then
    echo "Screening override appears to have already been prepared." >&2
    exit 2
fi
for rank in 0 1; do
    source_u="$case_dir/processor${rank}/${expected_time}/fluid/U"
    if [[ ! -s "$source_u" ]]; then
        echo "Missing decomposed velocity checkpoint: $source_u" >&2
        exit 2
    fi
done
if [[ ! -s "$case_dir/constant/fluid/fvOptions.fullFan" ]]; then
    echo "Missing full heat/fan fvOptions source." >&2
    exit 2
fi

mkdir "$case_dir/.accepted_airflow_reference"
for rank in 0 1; do
    mkdir "$case_dir/.accepted_airflow_reference/processor${rank}"
    cp -p "$case_dir/processor${rank}/${expected_time}/fluid/U" \
        "$case_dir/.accepted_airflow_reference/processor${rank}/U"
done
cp -p "$case_dir/SCREENING_THERMAL_OVERRIDE.time" \
    "$case_dir/.accepted_airflow_reference/time"
cp -p "$case_dir/constant/fluid/fvOptions" \
    "$case_dir/constant/fluid/fvOptions.pre_screening_override"
cp -p "$case_dir/constant/fluid/fvOptions.fullFan" \
    "$case_dir/constant/fluid/fvOptions"

rm -f -- \
    "$case_dir/.initial_airflow_pending" \
    "$case_dir/.initial_air_exchange_state" \
    "$case_dir/.initial_airflow_physical_settling" \
    "$case_dir/.mapped_initial_state" \
    "$case_dir/.airflow_refresh_pending"
touch "$case_dir/.initial_airflow_converged"
cp -p "$case_dir/SCREENING_THERMAL_OVERRIDE.md" \
    "$case_dir/.screening_airflow_override"

sha256sum \
    "$case_dir/.accepted_airflow_reference/processor0/U" \
    "$case_dir/.accepted_airflow_reference/processor1/U" \
    "$case_dir/constant/fluid/fvOptions.pre_screening_override" \
    "$case_dir/constant/fluid/fvOptions" \
    > "$case_dir/SCREENING_THERMAL_OVERRIDE.sha256"
printf '%s\n' \
    "screening_override sourceTime=$expected_time strictAirflowAccepted=false fanDomainFailures=11 velocityRelativeRms=0.0351968" \
    >> "$case_dir/run_summary.log"

echo "Prepared screening-only thermal continuation from t=$expected_time s."
echo "The strict source case was not modified; this case is not validation evidence."
