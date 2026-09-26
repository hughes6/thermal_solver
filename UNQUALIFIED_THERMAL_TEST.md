# Unqualified thermal test

This is an exploratory mode for checking temperature behavior before airflow
qualification finishes. It is not a validated thermal result: the imported
airflow checkpoint is held fixed, airflow acceptance is bypassed, and no
automatic airflow refresh is performed.

Use a fresh export of the target heat-load model. Do not overwrite the donor
qualification case. The donor must have `.cold_flow_seed_started` and a saved
decomposed checkpoint, but it does not need `.cold_flow_seed_complete`.

```bash
SEED_CASE='/absolute/path/to/stopped/qualification-case'
TARGET_CASE="$PWD"
CHECKPOINT='EXACT_PROCESSOR_CHECKPOINT_TIME'

OPENFOAM_LAUNCHER=env bash ./prepare_regions_low_memory.sh "$TARGET_CASE"
OPENFOAM_LAUNCHER=env bash ./create_thermal_branch_from_cold_flow_seed.sh \
  "$SEED_CASE" "$TARGET_CASE" 6 --allow-unqualified "$CHECKPOINT"
```

The importer copies only fluid-flow fields (`U`, `p`, `p_rgh`, `phi`, `rho`
and available turbulence fields). It deliberately does not copy `T`, any solid
field, or watt settings. The target's `0/` temperatures and heat loads remain
in force. It reconstructs from a private snapshot, so the donor is unchanged.

Run only with the explicit opt-in:

```bash
THERMAL_SOLVER_OPENFOAM_ENV_READY=1 OPENFOAM_LAUNCHER=env \
  bash ./run_parallel.sh 6 --unqualified-thermal 0.5 \
  2>&1 | tee -a unqualified-thermal.stdout.log
```

Use a larger end time to continue. If the process stops, rerun the same
explicit mode with a later end time. The case is intentionally blocked from a
normal `--multirate` launch. Absence of `.initial_airflow_converged` is
expected and must remain visible in reports.

For production results, complete the cold-flow run until
`.cold_flow_seed_complete` exists, then import without `--allow-unqualified`.
