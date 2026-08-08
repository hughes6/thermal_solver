# Screening and in-depth rack validation - 2026-08-07

## Cases preserved

- Screening: `C:\OpenFOAM\thermal_sim_v2\profile_screening_connected_components_20260807\model_generic_components`
- In-depth: `C:\OpenFOAM\thermal_sim_v2\profile_indepth_connected_components_20260807\model_generic_components`

The cases have separate directories. No prior result directory was reused or
overwritten. The screening mesh has 208,772 total cells and the in-depth mesh
has 335,580 total cells. Both pass full `checkMesh` in the fluid and all four
solid regions.

## Screening result

The corrected screening mesh was mapped from an earlier 19,200 s solution,
re-equilibrated, and advanced with 2,400 s implicit thermal intervals. Two
consecutive thermal and airflow checkpoints passed at 26,400.02 and
28,800.02 s.

- worst internal-cell temperature rate: Trenton, 0.0572 K per 300 s
- screening limit: 0.25 K per 300 s
- final exterior mass imbalance: 0.0041%
- final maximum tracked device-flow change: 0.0444%
- representative final 2,400 s interval plus refresh: 794 s wall time

## Screening-to-in-depth mapping

The converged 28,800.02 s screening fields were interpolated into the clean
in-depth mesh. All five region names matched. After 0.04 s of strict coupled
equilibration, exterior mass imbalance was 0.19%, every exterior fan changed
by at most 0.56%, and every internal fan changed by at most 0.59%.

At the mapped operating point, screening and in-depth volume-weighted solid
means agreed within 0.028 K. Initial hotspot differences were 0.007-0.390 K
for the four solids and 0.398 K for the internal fluid cells. Arithmetic cell
averages must not be used for this comparison because the adaptive cell
volumes differ.

## In-depth convergence

The in-depth mesh required continued thermal evolution through 50,400 s.
Checking only a global/boundary maximum would have stopped early. Convergence
was evaluated from internal-cell maxima and volume-weighted means for the
fluid and every component.

The final two 2,400 s intervals, ending at 48,000.02 and 50,400.02 s, both
passed the 0.10 K per 300 s in-depth limit. Final rates at 50,400.02 s were:

| Region | Volume-average rate (K/300 s) | Internal-maximum rate (K/300 s) | Final internal maximum (K) |
|---|---:|---:|---:|
| Fluid | 0.00126 | 0.01218 | 351.304 |
| Eaton | 0.00008 | 0.01197 | 387.581 |
| Dell | 0.00981 | 0.01717 | 433.131 |
| Trenton | 0.02466 | 0.08606 | 567.421 |
| KVM | 0.00490 | 0.00627 | 325.585 |

One final coupled sample reduced exterior mass imbalance to 0.00143%. The
largest exterior-fan change was 0.0526%. Internal-fan changes were 0.176%,
0.168%, and 0.335%, all below the 1% criterion. A representative final
in-depth 2,400 s interval plus strict refresh required 2,229 s wall time,
2.81 times the representative screening interval.

## Converged profile differences

Comparing each profile at its independently converged endpoint gives the
following in-depth-minus-screening internal-maximum differences:

| Region | Difference (K) |
|---|---:|
| Fluid | +10.212 |
| Eaton | +0.361 |
| Dell | +3.440 |
| Trenton | +2.228 |
| KVM | -0.463 |

These differences include both spatial-resolution and profile-policy effects;
they are not a pure mesh-convergence order estimate. They show that screening
is suitable for workflow iteration and rack-flow diagnosis, but its local
fluid and Dell hotspots should not be treated as in-depth values.

## Defects found and corrected

1. A nonzero mapped restart compared its time against an absolute five-second
   airflow limit. The limit is now measured from the restart checkpoint.
2. Signed mass-weighted temperature on a bidirectional passive opening could
   be nonphysical. The recirculation report now emits face-resolved inward and
   outward flow and temperature data.
3. Small cancellation-dominated passive-opening net flow could block the
   screening convergence test. The screening negligible-flow floor is now
   0.1% of rack throughput.
4. Component hotspot evolution could be hidden by a hotter stable component.
   Every component maximum is now tracked independently.
5. `fieldMinMax` included coupled boundary values, allowing a hot solid
   interface to hide a changing internal fluid-cell maximum. Convergence now
   uses `volFieldValue max` for internal cells; `fieldMinMax` remains available
   for location diagnostics.
6. Reaching a requested endpoint without passing airflow metrics was
   unconditionally counted as airflow validation. The refresh routine now
   propagates an explicit validation flag, and an endpoint-exhausted refresh
   cannot increment the convergence streak.
7. Component maximum and average reports independently selected their latest
   value without proving it was written at the fluid checkpoint time. A
   partially written function-object cycle could therefore mix current fluid
   data with stale component data. Every component maximum and average must
   now match the fluid checkpoint timestamp before convergence is evaluated.
8. Rolling back to an older solver checkpoint could leave newer abandoned
   samples under `postProcessing`. The fluid report timestamp must now match
   the authoritative current processor checkpoint, preventing future data
   from being reused after a rollback.
9. An adaptive refresh that reached the requested endpoint without converging
   removed its pending marker. Extending that case could therefore begin a
   new frozen thermal interval before repairing the unvalidated airflow.
   Endpoint exhaustion now retains the marker so the next multirate invocation
   resumes coupled airflow before any thermal-only advancement.

The full C++ and Python added-feature regression suite passed after these
changes.

## In-depth implicit timestep study

An isolated copy of the 335,580-cell in-depth case was restarted from the
48,000.02 s checkpoint and advanced to 50,400.02 s with a 10 s implicit
thermal-only timestep. The reference endpoint used the prior 5 s timestep.
Both runs used the same 0.02 s strict-Co coupled airflow refresh.

| Metric | 10 s minus 5 s result |
|---|---:|
| Temperature RMS difference | 0.000992 K |
| Temperature maximum absolute difference | 0.017517 K |
| Velocity RMS difference | 8.45e-7 m/s |
| Velocity maximum absolute difference | 8.64e-5 m/s |
| Pressure RMS difference | 0.000635 Pa |
| Pressure maximum absolute difference | 0.007813 Pa |

The complete 10 s interval, coupled refresh, and reconstruction took 1,681 s
by `/usr/bin/time` (1,702 s including the Windows launcher), compared with
2,229 s for the representative 5 s reference: a 24.6% solver-timed or 23.6%
launcher-timed reduction. The thermal-only portion completed in about 739 s;
the strict coupled refresh is unaffected by this setting and therefore limits
the total speedup. The tested 10 s cap is now the in-depth profile default.

## Coupled-refresh outer-corrector study

A second isolated case restarted from the validated 50,400.02 s endpoint and
advanced by 0.01 s with two PIMPLE outer correctors and two pressure
correctors. It was compared against the existing three-outer/two-pressure
reference at 50,400.03 s.

| Metric | 2x2 minus 3x2 result |
|---|---:|
| Temperature RMS difference | 0.01596 K |
| Temperature maximum absolute difference | 1.24768 K |
| Velocity RMS difference | 0.03595 m/s (1.8978%) |
| Velocity maximum absolute difference | 1.22583 m/s |
| Pressure RMS difference | 1.07193 Pa |
| Pressure maximum absolute difference | 26.3203 Pa |

The 2x2 run took 315 s versus approximately 332 s for the 3x2 reference, only
about a 5% reduction. That saving does not justify the large local velocity
and temperature deviations. The in-depth profile therefore retains three
outer correctors and two pressure correctors.

## Coupled-refresh Courant sensitivity

An isolated three-outer/two-pressure case was restarted from 50,400.02 s and
advanced to 50,400.03 s with the coupled-refresh Courant ceiling increased
from 1.0 to 1.25. This preserved the validated corrector structure and changed
only the adaptive live-flow timestep limit.

| Metric | Co 1.25 minus Co 1 result |
|---|---:|
| Temperature RMS difference | 0.00204 K |
| Temperature maximum absolute difference | 0.25269 K |
| Velocity RMS difference | 0.00694 m/s (0.3664%) |
| Velocity maximum absolute difference | 0.58539 m/s |
| Pressure RMS difference | 0.79507 Pa |
| Pressure maximum absolute difference | 18.2422 Pa |

The measured solver clock time was approximately 263 s for both ceilings on
this workstation, so the modestly larger timestep produced no repeatable
runtime benefit while increasing local field differences. The in-depth
profile retains `airflow_refresh_maximum_courant_number = 1.0`.
