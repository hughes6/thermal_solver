# Transient first-law audit status for 1.50--1.60 s

Date: 2026-08-26

## Verdict

**NOT EVALUABLE from the retained evidence.** This is neither a numerical
closure pass nor a closure failure.

The retained 1.50 and 1.60 s binary checkpoints can support new high-precision
region storage integrals. They cannot reconstruct the missing time integral of
external-boundary energy transport and other equation-source work across all
200 accepted solver steps. Existing function-object summaries also contain
approximately six reported digits even though time directory naming used high
precision. Endpoint interpolation or a two-sample trapezoid would therefore be
an unsupported substitute for the solver-time-step ledger.

The existing `validation_1p60.md` comparison of 8.9274 W instantaneous outlet
sensible transport with 2,315 W applied heat remains only a steady-state heat-
removal/development gate. It is not a transient first-law residual: at this
early checkpoint most energy is expected to enter storage.

## Fail-closed audit contract

`tools/openfoam_transient_energy_audit.py` now separates numerical first-law
closure from the gross regional storage indicator. Before calculating either,
it requires:

- 15--64 digit decimal values with explicit uncertainties;
- byte counts and SHA-256 hashes for both checkpoint manifests and every
  storage dependency;
- identical run UUID, mesh hash, case-configuration hash, and region inventory
  at both endpoints;
- a monotone, gap-free start/end/`deltaT` row for every accepted solver step;
- per-step external-boundary, applied-heat, and nonheat-source energy increments
  that sum exactly to the audited interval totals;
- explicit inventories for every external patch, heat source, nonheat energy
  term, and internally cancelled CHT interface; and
- provenance for the exporter metadata and extraction definition.

The balance includes sensible enthalpy for every region, kinetic energy for
fluid regions, applied heat, other signed equation sources, and all outward
external-boundary enthalpy/kinetic/conductive transport. The closure scale is
cancellation-safe and the reported upper bound includes declared input
uncertainty. Low storage is reported only as an indicator; it cannot prove
local thermal convergence.

Focused verification passed 20/20 tests. The retained 124-byte stdout log is
`transient_energy_audit_tests_2026-08-26.stdout.log`, SHA-256
`671846ab8d47c38507a71bc7749822f663e54adafb4596b474aa90294216c140`.

## Required next evidence

A formal 1.50--1.60 s closure verdict requires an instrumented rerun from the
preserved 1.50 s checkpoint. The exporter/solver instrumentation must write
17-digit per-step energy increments, complete inventories, and the hashed
checkpoint/configuration/mesh manifests described above. Offline endpoint
post-processing is insufficient.

No OpenFOAM solver was launched for this audit. The rerun remains subject to
the campaign resource gate and must not contend with Fluent or another CFD/MPI
process.
