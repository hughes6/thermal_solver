# Validation completion — 2026-08-13

## Status

The v2.2 rack workflow has completed its broad validation campaign. Further
unbounded long runs are not justified by the evidence collected so far. New
long cases should be triggered by a geometry, physics, boundary-condition, fan
curve, solver, or mesh-policy change rather than run continuously.

The final regression audit passed all native solver/export tests. The Python
audit passed 136 tests with one optional test skipped when run with the bundled
NumPy/PyVista-capable Python runtime. The default system Python lacks NumPy, so
two recirculation-array tests cannot run under that interpreter; this is an
environment limitation, not a model failure.

## Accepted operating envelope

- Use the screening profile for fast layout iteration and qualitative flow,
  hotspot, and recirculation screening.
- Use the in-depth profile for final quantitative temperature and flow results.
- Use the validated 15 mm in-depth case as the principal mesh reference. The
  12.5 mm and 10 mm probes bound mesh sensitivity but do not justify replacing
  the default for routine work given their storage/runtime cost.
- Use warm-start flow to establish the airflow field, then thermal-only or
  multirate continuation only after the generated validation gates accept the
  mapped airflow checkpoint.
- Treat a result as accepted only when the generated mass, energy, heat-source,
  field-convergence, and airflow-device reports pass. Recirculation conclusions
  additionally require the tracer/matrix report.
- The Spalding wall-function experiment is rejected as the default: it did not
  remove the mixed near-wall y+ limitation and changed velocity RMS by 5.956%
  versus the control. Its endpoint still passed mass (0.00369%) and energy
  (0.2876%) closure, with outlet flow 0.297991 kg/s and temperature 298.2941 K.
- Wall heat-transfer accuracy remains the principal unresolved fidelity limit.
  Report y+ validity with final quantitative cases and calibrate uncertain
  equipment models against measured intake/exhaust temperature and mass flow.

## Final maintenance fixes

- Fan-curve provenance is now optional for models that do not load a fan-curve
  library. A supplied-but-missing provenance source remains an error.
- The in-depth thermal-only maximum time-step regression now matches the
  validated 30 s profile.
- The mesh-preparation regression now checks the current hard-stop wording for
  non-determinant `checkMesh` failures.

## Case retention manifest

No cases were deleted during this audit.

| Case | Size (GiB) | Disposition |
|---|---:|---|
| `model_generic_airside_screening_20260813` | 0.317 | Keep: authoritative long screening evidence |
| `model_generic_airside_control1200_20260813` | 0.580 | Keep: authoritative 15 mm in-depth reference |
| `model_generic_airside_mesh10_probe_20260813` | 1.170 | Keep for now: finest mesh-sensitivity evidence; archive if reports are sufficient |
| `model_generic_airside_mesh125_current_20260813` | 0.760 | Cleanup candidate: intermediate mesh probe |
| `model_generic_airside_screening_spalding_20260813` | 0.367 | Cleanup candidate: rejected wall-function experiment |

Deleting the two cleanup candidates would recover approximately 1.127 GiB.
Preserve their validation reports and provenance before deletion if the raw
OpenFOAM fields are no longer required.

## Revalidation triggers

Repeat the targeted validation matrix—not the entire historical campaign—when
changing mesh spacing/refinement rules, fan placement or curve handling, vent
mapping, turbulence/wall treatment, heat-source mapping, restart/multirate
policy, or OpenFOAM solver code. Pure documentation and plotting-only changes
need regression tests but do not require new long CFD cases.
