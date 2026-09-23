# Screening-to-fine airflow reuse — integration complete

Implemented and tested for the v2.5 mapping release. Usage: MAPPED_AIRFLOW_REUSE.md.
Rebuild ./model to export the new mapping helpers and printed commands.

Base commit: c8f80a7 on v2.5. Production cases have not been modified.
Implementation is integrated in v2.3. See git history for the release commit.
The separate airflow-only solver optimization is not included in this release.

The separate prepare_mapped_airflow_reuse.sh wrapper selects interpolation in
the heated reuse helper. Four-argument identical-mesh behavior remains intact.
Source fields are reconstructed in a private snapshot; only U and required
turbulence fields are mapped. All target temperatures, pressure and watts stay
at their fresh initial settings. The separate mapped-flow-check must pass the
existing cold-flow acceptance gates before thermal import.

Real OpenFOAM probe passed mapping from 120 fluid cells to 960 fluid cells
(128 to 1024 total), including temperature-dependent air and gravity. Flow
accepted at 0.25 s with maxCo=0.04794 against 0.5 and relative velocity RMS
change 0.00886. This is not a prediction of production convergence time.
Final evidence: /home/hconner158/OpenFOAM/cases/mapping_validation_20260923_final in WSL.

Thermal import and numerical assertions also passed: all initial T=293.15 K,
held velocity delta=0, fine solid energy=2.999993 J versus 3 J input at 0.05 s.
Report: mapping-physics-results.json alongside the mapping and thermal logs.

Compatibility checks, coverage tracer, helper bundling, README and ./model
commands are complete. Full donor file hashes matched before/after mapping.
Real displaced-mesh coverage test correctly refused the target. Changed patch
names, zero/NaN coverage and incomplete preparation were also rejected.
Full exporter, existing cold importer, generated-output and shell syntax tests
passed. New runners refuse incomplete preparation, and preparation prints
progress plus reconstruction/mapping log paths.

Continuation prompt: Read MAPPED_AIRFLOW_REUSE_STATUS.md and help apply the
v2.5 mapping workflow to a fresh in-depth export. The tiny tests are complete;
preserve production cases and do not rerun completed fixtures.

Repeat the completed numerical verification (no solver rerun):
```bash
python3 tests/reuse_physics/verify_mapping.py /home/hconner158/OpenFOAM/cases/mapping_validation_20260923_final
```

Do not rerun mapping_probe.sh or mapping_thermal_probe.sh over this completed
fixture. For a new physical test, run from the repository in WSL:

```bash
TEST_CASE="$HOME/OpenFOAM/cases/mapping_test_$(date +%Y%m%d_%H%M%S)"
bash tests/reuse_physics/mapping_probe.sh "$TEST_CASE" /home/hconner158/OpenFOAM/cases/reuse_validation_20260923_buoyant/thermal
# After successful qualification:
bash tests/reuse_physics/mapping_thermal_probe.sh "$TEST_CASE"
python3 tests/reuse_physics/verify_mapping.py "$TEST_CASE"
bash tests/reuse_physics/mapping_refusal_probe.sh "$TEST_CASE"
```

Keep TEST_CASE for resuming at the next stage. Same physical layout/fan/material
assumptions require matching user configuration; patch/coverage checks cannot
prove physical equivalence of arbitrary models. Mapping/reconstruction need
extra disk and RAM. Fine-mesh adjustment may still be substantial.
