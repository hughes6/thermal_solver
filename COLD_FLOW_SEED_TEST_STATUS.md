# Cold-flow reuse validation — 2026-09-23

## Real OpenFOAM 2606 test: PASS (limited scope)

The 128-cell fixture uses constant-density air, zero gravity, 120 fluid cells
and 8 solid cells. Evidence is retained in WSL at
`/home/hconner158/OpenFOAM/cases/reuse_validation_20260923_b`.

- Cold seed accepted at 0.45 s; maximum temperature drift 0.00001283 K.
- Heated donor produced by a 100 W, 0.05 s thermal-only run.
- Separate heat-off qualification from donor velocity accepted at 0.2 s;
  maximum temperature drift 0.00000195 K.
- Fresh 60 W branch imported accepted flow, reset every initial region T to
  293.15 K, and completed 0.05 s thermal-only advancement.
- Donor solid energy 4.999994 J vs 5 J input; branch 2.999997 J vs 3 J input.
- Held velocity changed by exactly zero in both thermal stages.
- All donor file hashes matched before and after heated-checkpoint reuse.

Numerical assertions passed; results are in `physics-results.json` alongside
the run logs. A second fixture with temperature-dependent air and gravity also
passed, retained in `reuse_validation_20260923_buoyant` next to the first.
Its maximum heat-off temperature drift was 0.00072 K, donor energy 4.999994 J,
and branch energy 2.999996 J; held velocity and donor hashes remained unchanged.
Both tests use a small thermal perturbation, not a strongly buoyant rack.

The small Python/Bash regression exercises the actual branch-import script with
mock OpenFOAM utilities. It is not a physical CFD test. It reproduced the original
geometry-fingerprint rejection and now passes: developed fields carried into four
fixture partitions, fluid/solid ambient temperatures and target watts preserved,
fan-ramp completion recorded, initialized targets and self-import rejected without
changing their data. Generated run_parallel.sh also passes bash -n.

Corrections: geometry hashes use relative paths and omit decomposition count;
manifest version 2 is required; missing meshes fail closed; imports lock both
cases; existing processor data is refused rather than deleted; acceptance markers
are written after successful decomposition. Seeds require a fresh case and adaptive
airflow validation, and full solid heat dictionaries are restored on exit.

The full exporter test PASSES. Two stale expectations for `run_tracked_capture`
were updated to the existing `run_latest_courant_postprocess` helper. Generated
runner syntax validation also PASSES.

## Rerun in WSL from the repository

For a real 128-cell test, use a NEW Linux-filesystem directory without spaces.
The two physical scripts source OpenFOAM 2606 themselves and require the custom
solver already built. They never run a production case. Each stage is separate
so it can be continued after an agent session ends:

```bash
export REUSE_TEST_BUOYANT=1  # omit/unset for constant-density, zero-gravity variant
TEST_CASE="$HOME/OpenFOAM/cases/reuse_test_$(date +%Y%m%d_%H%M%S)"
bash tests/reuse_physics/physical_run.sh "$TEST_CASE"
# Only after the first script succeeds:
bash tests/reuse_physics/heated_physical_run.sh "$TEST_CASE"
# Only after both scripts succeed:
python3 tests/reuse_physics/verify_physics.py "$TEST_CASE"
```

Keep the printed/chosen TEST_CASE path for continuation. Do not rerun setup
over an existing test directory; inspect logs and resume its runner instead.

```bash
python3 tests/cold_flow_seed_reuse_test.py tools/create_thermal_branch_from_cold_flow_seed.sh
g++ -std=c++20 -O0 -I src tests/openfoam_export_test.cpp -o /tmp/cold_seed_export_test
/tmp/cold_seed_export_test /tmp/cold_seed_generated_case
bash -n /tmp/cold_seed_generated_case/run_parallel.sh
```

## Still required

Full exporter and mock importer regressions passed again on 2026-09-23.
The targeted model-loader output regression also passed: separate cold/heated
reuse commands are printed and the heated helper is bundled in the export.
The older full model_config_test has an unrelated stale default-model-path
assertion and is not claimed as passing.
Review production safeguards and donor/target compatibility before application.
The usable runtime is sourced from
`/usr/lib/openfoam/openfoam2606/etc/bashrc` (source before enabling strict shell
mode). Earlier missing-library findings were an environment setup issue.

Resume request: "Continue cold-flow reuse validation from COLD_FLOW_SEED_TEST_STATUS.md;
review production donor/target compatibility and help apply the workflow.
Preserve all existing simulation cases. Changes remain local and unpushed."
