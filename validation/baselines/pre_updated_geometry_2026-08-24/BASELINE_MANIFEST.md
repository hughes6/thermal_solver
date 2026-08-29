# Pre-update research-lab model baseline

Captured on 2026-08-24 before integrating the user's revised geometry and fan
curves.

## Preserved inputs and reports

- `new_model_baseline.toml`: model definition used for checkpoints through
  0.35 s and the continuing 0.35--0.40 s run.
- `updated_model_raw_paste.txt`: exact revised input supplied by the user,
  unchanged.
- `NEW_LAB_MODEL_SCREENING_baseline.md`: screening and validation narrative at
  the time of the update.
- `validation_0p10.*` through `validation_0p35.*`: independent checkpoint
  mass/energy/connectivity reports.
- `airflow_field_convergence_0p05_0p10.csv` through
  `airflow_field_convergence_0p35_0p40.csv`: spatial field comparisons.
- `fan_flow_checkpoint_0p40.md`: signed cyclic pressure-jump operating points
  for all 13 internal fans at the final checkpoint.
- `cyclic_warm_0p40.stdout.log`: complete four-rank 0.35--0.40 solver,
  reconstruction, and final-report log.

## External OpenFOAM case

The complete baseline case remains at
`C:/OpenFOAM/thermal_sim_SOLAR_ATB/thermal_lab_cyclic_pressure_jump_screen`.
It has not been overwritten. The four-rank 0.35--0.40 s continuation completed
with exit code 0, reconstructed every fluid and solid field at exactly
0.40000000000000002 s, generated final function-object reports, and restored
the production controls. Its evidence is copied into this folder.

The revised model is intentionally named `new_model_updated.toml`, so its
OpenFOAM export receives a separate case directory and cannot silently reuse
this baseline solution.

## Final baseline result (0.40 s)

- Fluid connectivity: 3 intended regions / 1,538,474 fluid cells, pass.
- Exterior mass mismatch: 0.07358%, pass (improved from 0.10482% at 0.35 s).
- Outlet reverse-flow share: 0%.
- Cyclic fan directions: all 13 positive.
- Net transported thermal power: -13.9291 W versus 1,515 W applied, expected
  fail for the cold subsecond transient.
- Solid range: 293.14979--293.21552 K.
- 0.35--0.40 s whole-fluid velocity change: 17.45% relative RMS, fail.
- 0.35--0.40 s exterior-air velocity change: 19.51% relative RMS, fail.
- 0.35--0.40 s whole-fluid gauge-corrected pressure change: 2.08%, settling
  materially faster than velocity; exterior-air pressure changed 9.17%.
- Solver execution/clock time for this continuation: 5,785.65/5,870 s
  (about 97.8 minutes wall clock for 0.05 simulated seconds).

This baseline is numerically stable and mass-balanced, but is not a converged
flow field, developed thermal solution, calibrated model, or industry-ready
validation result.
