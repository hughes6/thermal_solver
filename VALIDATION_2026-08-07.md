# Screening and in-depth rack validation - 2026-08-07

## Cases preserved

- Screening: `C:\OpenFOAM\thermal_sim_v2\profile_screening_connected_components_20260807\model_generic_components`
- In-depth: `C:\OpenFOAM\thermal_sim_v2\profile_indepth_connected_components_20260807\model_generic_components`
- In-depth dt=20 study: `C:\OpenFOAM\thermal_sim_v2\profile_indepth_dt20_20260808\model_generic_components`
- Matched-policy 20 mm mesh: `C:\OpenFOAM\thermal_sim_v2\profile_mesh20_matched_20260808\mesh20_matched_model`
- Matched-policy 12.5 mm mesh: `C:\OpenFOAM\thermal_sim_v2\profile_mesh125_matched_20260808\mesh125_matched_model`
- Screening Courant cases: `C:\OpenFOAM\thermal_sim_v2\screening_co_sensitivity_co{10,5,2,1}_20260808`

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
10. Airflow stability baselines previously existed only in the runner process.
    Every segmented or resumed multirate command therefore needed an extra
    coupled check window to reacquire the same flow vector. The runner now
    atomically persists ordered raw and smoothed device flows with their solver
    time. It restores them only when the device topology matches exactly and
    the state is not from a future checkpoint; otherwise it conservatively
    reacquires the baseline.
11. The generated runner accepted one MPI rank even though OpenFOAM aborts
    when invoked with `-parallel -np 1`, and accepted a zero multirate endpoint
    that later left reconstruction with no selected time. Export now requires
    at least two ranks before writing the case, and the runner rejects both
    fewer than two ranks and nonpositive requested endpoints immediately.
12. An adaptive refresh compared its reconstructed time to the requested
    endpoint without tolerance. A value such as `50400.029999999868` was
    treated as earlier than `50400.03`, causing a no-op stage and a second
    airflow evaluation at the same checkpoint. The endpoint decision now uses
    the same scale-aware tolerance as the main multirate loop.
13. The recirculation tool told users to re-export whenever paired boundary
    reports were absent. A valid short endpoint can simply precede the
    function objects' scheduled write. The diagnostic now provides a
    copyable solver-backed `-postProcess -latestTime` command that loads `T`
    and `phi`; generic `postProcess` was runtime-proven to abort on the
    mass-weighted reports because those fields were not loaded.

The full C++ and Python added-feature regression suite passed after these
changes.

## Persisted airflow-baseline restart validation

The 335,580-cell in-depth case was restarted from 50,400.03 s with a valid
persisted two-sample airflow baseline and advanced through one strict coupled
refresh window to 50,400.04 s. The generated runner restored the baseline,
evaluated the endpoint exactly once, and accepted convergence after 0.01 s.

| Refresh metric | Result |
|---|---:|
| Maximum boundary-flow change | 0.0422635% (`Fan_3`) |
| Net boundary-flow imbalance | 0.00709008% |
| Actual maximum Courant number | 0.788938 |
| Direction checks | Passed |
| Solver clock time | 461 s |
| Full runner/reconstruction time | 566 s |

The pending-refresh marker was removed and the state file advanced atomically
to the reconstructed `50400.039999999877` checkpoint. Compared with the
independently generated three-outer/two-pressure reference at 50,400.03 s,
the new endpoint differed by 0.02296 K temperature RMS (0.007637%),
0.01461 m/s velocity RMS (0.7710%), and 0.12487 Pa pressure RMS (0.000150%).
These are consecutive coupled-flow samples rather than identical-time
solutions; the small global differences and passing flow metrics confirm that
the restored baseline avoids an otherwise required extra refresh window
without falsely declaring an unstable boundary-flow state converged.

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
the total speedup.

The original dt=10 evidence runner stopped at its requested 50,400.02 s
endpoint after two coupled samples even though an internal fan's unsmoothed
period-two change was 4.25%. The later pending-refresh and persisted-baseline
fixes prevent production runners from treating that endpoint as airflow
validated. This does not affect the timestep comparison: both candidates use
the same starting fields, mesh, controls, and exact 50,400.02 s comparison
endpoint, so it is an accuracy study of the implicit thermal step rather than
an airflow-convergence claim.

An additional matched run increased only the implicit thermal timestep from
10 s to 20 s. The 20 s thermal-only phase completed in 442 s, 40.2% faster
than the approximately 739 s 10 s phase. Its complete legacy two-sample run
and reconstruction took 1,581 s; strict coupled refreshes dominate that total
and are not accelerated by the thermal timestep.

| Metric | 20 s minus 10 s result |
|---|---:|
| Temperature RMS difference | 0.000114 K (0.000038%) |
| Temperature maximum absolute difference | 0.001831 K |
| Velocity RMS difference | 2.39e-7 m/s (0.000013%) |
| Velocity maximum absolute difference | 1.85e-5 m/s |
| Pressure RMS difference | 0.000275 Pa |
| Pressure maximum absolute difference | 0.007813 Pa |

The tested 20 s cap is now the in-depth profile default. With a valid
persisted airflow baseline, a normal interval can avoid the legacy baseline-
acquisition refresh sample, making the 297 s thermal-phase saving a larger
fraction of end-to-end runtime.

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

## Three-level matched-policy mesh study

Fresh 20 mm and 12.5 mm adaptive meshes were generated with the same model,
geometry, and in-depth solver policy as the production 15 mm case. The
validated 15 mm fields at 50,400.02 s were mapped region by region. Because
`mapFields` cannot map the face-flux field `phi`, each target first ran the
required 0.01 s coupled warm start and then exact-endpoint adaptive refresh
samples. No old case directory was overwritten.

All three meshes passed full multi-region `checkMesh`:

| Fine spacing | Total cells | Fluid cells |
|---:|---:|---:|
| 20 mm | 208,772 | 177,064 |
| 15 mm | 335,580 | 284,396 |
| 12.5 mm | 477,456 | 405,414 |

The 20 mm mapped flow converged after three 0.01 s adaptive samples with
0.163% mass imbalance and 0.442% worst device-flow change. The 12.5 mm case
used four exact-endpoint samples: the second exterior sample was still 1.071%
different and the third sample established the first complete internal-fan
period average. The fourth passed with 0.142% imbalance and 0.272% worst
smoothed device-flow change. Segmented state restoration was exercised at
every fine-mesh endpoint and retained the pending marker until convergence.

Relative to the converged 15 mm operating point:

| Metric | 20 mm minus 15 mm | 12.5 mm minus 15 mm |
|---|---:|---:|
| Fluid volume-mean temperature | +0.0132 K | +0.00277 K |
| Fluid internal maximum temperature | -4.682 K | -1.101 K |
| Fluid volume-mean speed | -0.740% | -0.495% |
| Worst exterior fan-flow magnitude | -2.544% | -1.603% |
| Main vent-flow magnitude | -1.937% | -1.230% |
| Worst smoothed internal-fan flow | 4.09% | 1.35% |
| Largest component mean-temperature difference | -0.0441 K | +0.00950 K |

The finer 12.5 mm solution consistently moves the integrated temperatures
and device flows toward the 15 mm values compared with the 20 mm screening
mesh. The 15 mm in-depth spacing is therefore retained: its remaining
integrated-flow uncertainty is about 1-2%, while 20 mm remains appropriate
only for screening and under-resolves the local fluid hotspot by several
kelvin in this rack.

Pointwise turbulent velocity RMS differences remain 7-12% between unequal
meshes even though volume-mean speed and all boundary flows are much closer.
Those cell-scale comparisons are sensitive to eddy location and piecewise
cell sampling, so they are reported diagnostically rather than used alone as
a mesh-acceptance criterion.

`tools/openfoam_mesh_comparison.py` was added for unequal-mesh studies. It
reports independent volume-weighted means/maxima and samples the reference
cell containing each target cell centre. An identity test proves that the
method returns zero error for an unchanged mesh; an earlier point-data
interpolation approach was rejected because it produced artificial error even
for identical fields.

## Current-screening Courant compromise

Four byte-identical two-rank cases restarted from the connected-component
20 mm mesh at 28,800.02 s and advanced exactly 0.01 s with only the fixed
coupled timestep changed. Every endpoint was reconstructed and compared with
the conservative 0.0001 s reference.

| Configured ceiling | Fixed step | Actual max(Co) | Solver time | U RMS vs reference | T RMS vs reference | p RMS vs reference |
|---:|---:|---:|---:|---:|---:|---:|
| 10 | 0.0010 s | 4.820 | 49.7 s | 4.076% | 0.01103% | 0.001491% |
| 5 | 0.0005 s | 2.406 | 83.9 s | 3.102% | 0.00876% | 0.001079% |
| 2 | 0.0002 s | 0.959 | 236.4 s | 1.496% | 0.00510% | 0.000520% |
| 1 reference | 0.0001 s | 0.479 | 423.6 s | - | - | - |

Co=5 bought only a modest field improvement. Co=2 reduced velocity RMS error
by 63.3% relative to Co=10 and reduced the worst local velocity difference
from 6.49 to 2.41 m/s. Reconstructing the sample adds similar fixed overhead
to every row. Relative to the representative 794 s screening interval, the
approximately 200 s Co=2 increment is about 25%; periodic refreshes also run
far less often than thermal steps. The screening periodic-refresh ceiling is
therefore reduced from 10 to 2. Initial fan ramp/warmup policy is unchanged,
and in-depth/validation profiles continue to use Co=1.

Solver-backed endpoint reports were also generated for all four cases and
processed through the face-resolved recirculation workflow. Every case found
zero thermal re-ingestion. The KVM opening was bidirectional but its incoming
air remained exactly at 293.15 K, so the near-zero signed net flow was
correctly excluded from the misleading mass-weighted-temperature report.

Relative to the strict reference, Co=2 changed KVM inward traffic by -0.421%,
outward traffic by -0.346%, net sensible heat rejection by +0.00551%, and
aggregate exhaust temperature by +0.000322 K. Heat rejection was 1539.35 W
(99.6346% of applied heat) versus 1539.27 W (99.6291%) in the reference.
This confirms that the selected screening ceiling preserves the engineering
recirculation and energy-balance conclusions, not only global field norms.

## Screening airflow-refresh cadence check

Two isolated copies of the converged 28,800.02 s screening checkpoint were
advanced to the same 31,200 s endpoint. The held-flow branch used the normal
2,400 s screening interval. The comparison branch used a 1,200 s interval,
performed one adaptive refresh at 30,000 s with the current Co=2 ceiling, and
then advanced the accepted flow field through the remaining 1,200 s. The
refresh needed four 0.01 s samples: its third sample still had a 5.61%
internal-fan change, while the fourth reduced the worst change to 0.128%,
with 0.0335% exterior mass imbalance and all directions valid.

At 31,200 s, the refreshed branch changed exterior fan flows by at most
0.195% and main-vent flow by 0.142%. Thermal re-ingestion remained zero.
Aggregate exhaust temperature changed by -0.00786 K and net sensible heat
rejection by -0.0386%. Volume-weighted fluid temperature changed by +0.0307 K.
Component mean-temperature changes were -0.0302 K Eaton, +0.2587 K Dell,
-0.0178 K Trenton, and +0.0444 K KVM. Local turbulent structure was more
sensitive: velocity RMS changed by 3.88% and the largest local fluid
temperature difference was 6.47 K.

The held-flow branch took 13:26 wall time. The current-policy refreshed
workflow took 22:32 to reach and validate the intermediate checkpoint plus
5:46 to resume to the matched endpoint, 2.10 times the measured wall time.
The source checkpoint predated the Co=2 default, so this is deliberately
treated as a conservative combined bound on refresh cadence plus the stricter
Courant correction, not as a pure cadence-only delta. Even that upper bound
left rack throughput, heat rejection, and recirculation conclusions nearly
unchanged. The 2,400 s screening refresh interval is therefore retained;
1,200 s remains appropriate when sub-kelvin component detail during a thermal
transient matters more than screening runtime.

## Fresh-export cold-start and interrupted-restart validation

Fresh screening and in-depth cases were exported from a newly compiled v2.2
runner rather than copied from older evidence directories:

- `C:\OpenFOAM\thermal_sim_v2\fresh_screening_current_20260808`
- `C:\OpenFOAM\thermal_sim_v2\fresh_indepth_current_20260808`

The generated screening runner contains the current 2,400 s / Co=2 / 10 s
policy, while the in-depth runner contains 1,200 s / Co=1 / 20 s. Both fresh
meshes completed region preparation and full `checkMesh`. Screening used
208,772 cells, completed preparation in 1:45, and peaked at 394 MB. In-depth
used 335,580 cells, completed in 1:37, and peaked at 606 MB. Neither swapped.

The clean screening cold start also showed why its 0.30 s minimum airflow
observation must not simply be shortened for speed: velocity RMS still changed
by 12.61% from 0.06 to 0.07 s and 10.61% from 0.07 to 0.08 s. The expensive
startup is live-flow physics, not the thermal-only tiny-timestep defect.

Two restart defects were found by interrupting that cold start. First, the
minimum observation start was local to one runner invocation, so restarting
would repeat already completed live-flow time. The generated runner now stores
the start in `.initial_airflow_pending`, restores it only when it is numeric,
not from the future, and still inside the configured warmup window, and removes
it only after initial airflow is accepted.

Second, an interrupted 0.09 s directory lacked `fluid/phi` and `rho`, but the
runner previously called it a valid processor partition because only directory
timestamps were compared. Restart selection now requires all fluid restart
fields plus every solid-region `T` field on every rank. Numeric processor times
newer than the newest complete common checkpoint are removed as incomplete.

Runtime validation deliberately retained the incomplete 0.09 s checkpoint.
The fixed runner removed it from both ranks, fell back to complete 0.08 s,
passed Courant preflight, and advanced to 0.10 s. A second invocation advanced
to 0.11 s. Both printed `Resuming initial airflow observation window from
t=0.05 s`; the marker stayed at 0.05 s and no false convergence marker was
created. The complete C++ and Python added-feature regression suite passed.

Continuing the same clean case established the first physically eligible
airflow baseline at 0.35 s (0.30 s beyond the 0.05 s fan ramp). At that point
exterior mass imbalance was 0.2815%, all directions were valid, and the
estimated rack air-exchange time was 5.35 s. A segmented 0.36 s invocation
restored the saved convergence state but exposed a third defect: the initial
airflow function immediately cleared the restored flow arrays, so every
invocation could establish a baseline without ever comparing against it.

The reset now occurs only when a new `.initial_airflow_pending` window is
created. On a resumed window, the persisted flow arrays remain authoritative.
Runtime proof at 0.37 s restored the 0.36 s baseline, identified `Fan_5` as the
worst device, measured a real 0.8406% flow change, and accepted initial airflow
after 0.32 s beyond the ramp. Exterior mass imbalance was 0.2550% and all flow
directions remained valid. The pending marker was removed and the accepted
marker was written.

The accepted case then advanced from 0.37 to 10 s in one 9.63 s implicit
thermal step. The solver portion took about 15 s and the complete invocation,
including Windows/WSL launch and reconstruction, took 1:59. This confirms the
intended workflow: cold-start live airflow is expensive but performed once,
while subsequent thermal evolution is much faster and reuses the validated
operating point. The full C++ and Python regression suite passed after the
baseline-preservation correction.

## Fresh screening-to-in-depth mapped workflow (2026-08-08)

The accepted fresh screening solution at 10 s was mapped region by region onto
the untouched fresh in-depth mesh with OpenFOAM 2606 `mapFields` using the
supported `interpolate` method. The fluid mapping increased the fluid mesh from
177,064 to 284,396 cells; all four solid regions mapped successfully. Because
`phi` is not transferred by `mapFields`, the target then ran a fully coupled
Co <= 1 warm start from 10 to 10.11 s.

At 10.11 s, the worst top-fan change was 0.774%, exterior mass imbalance was
0.226%, and every fan direction was correct. In-depth intake and exhaust were
0.282315 and 0.281677 kg/s. The accepted screening values were 0.242704 and
0.242086 kg/s, so the finer mesh predicts approximately 16.3% more throughput.
This sensitivity is too large to treat screening airflow magnitudes as final.

Strict 0.01 s fine-mesh segments took roughly 6-9 minutes each with about
484-489 MB peak resident memory and no swaps. A continuous 0.04 s segment took
21.6 minutes. Strict airflow, not implicit thermal evolution, dominates runtime.

The isolated evidence case then advanced to 3600 s in multirate mode. The first
1190 s thermal-only leg took under three minutes. The 1200 s airflow refresh
converged after 0.03 s with 0.451% imbalance and 0.584% worst-device change. At
2400 s, the first refresh found a real 3.85% internal component-fan change from
the hotter air; the next window converged with 0.086% imbalance and 0.521% worst
fan change. Sparse periodic airflow updates are therefore necessary, while
continuous strict airflow is prohibitively expensive.

The complete continuation took 50:07 wall time, used 489 MB peak resident
memory, and incurred no swaps. It was not thermally converged: worst internal
cell change was 10.70 K/300 s and worst component-average change was
2.65 K/300 s. Final temperatures were:

| Region | Mean (C) | Maximum (C) |
|---|---:|---:|
| Fluid | 24.6 | 71.2 |
| Generic Dell R470 1U | 52.9 | 172 |
| Generic Eaton 2U UPS | 38.4 | 106 |
| Generic KVM 1U | 28.8 | 43.1 |
| Generic Trenton 3U | 72.4 | 243 |

At 3600 s the rack rejected 1381 W, or 89.40% of configured heat, with
0.290292 kg/s intake, 0.290041 kg/s exhaust, and no detected hot-air
re-ingestion. The extreme Dell, Eaton, and Trenton maxima show that the current
uncalibrated equivalent heat blocks should be tuned before an 18,000 or
100,000 s final run. Extending this exact load case would measure its continuing
thermal rise, not validate realistic equipment temperatures.

### Effective-conductivity sensitivity

An isolated, identical-mesh in-depth case tested whether the extreme generic
block maxima were primarily caused by the assumed effective conductivity. All
four generic solid regions were changed from 10 to 100 W/(m K); geometry,
watts, density, heat capacity, accepted 10.11 s airflow fields, mesh, and solver
policy were held fixed. The case ran through the same 3600 s multirate endpoint
without overwriting the baseline.

The airflow comparison remained controlled. At 1200 s, refresh convergence was
essentially identical to baseline. At 2400 s, the k=100 case converged in one
0.01 s window with 0.152% imbalance and 0.925% worst flow change. Its final
heat rejection was 1384 W (89.57%), versus 1381 W (89.40%) for k=10.

| Region | k=10 mean (C) | k=100 mean (C) | k=10 max (C) | k=100 max (C) |
|---|---:|---:|---:|---:|
| Fluid | 24.6 | 24.5 | 71.2 | 70.0 |
| Generic Dell R470 1U | 52.9 | 53.2 | 172 | 159 |
| Generic Eaton 2U UPS | 38.4 | 38.4 | 106 | 102 |
| Generic KVM 1U | 28.8 | 28.5 | 43.1 | 41.3 |
| Generic Trenton 3U | 72.4 | 72.1 | 243 | 236 |

Thermal convergence rates also changed little: worst internal-cell change was
10.19 K/300 s and worst component-average change was 2.60 K/300 s, compared
with 10.70 and 2.65 K/300 s for k=10. A tenfold conductivity increase therefore
does not solve the over-temperature behavior and should not be adopted as a
default. The dominant uncertainty is effective convective coupling: one compact
block has much less wetted area and more bypass than distributed boards, heat
sinks, and guided airflow in real equipment. The next calibration study should
hold watts and fan curves fixed while increasing effective heat-transfer area
or using a calibrated air-side source/resistance representation.

### Air-side heat-source runtime proof

The architecture previously discarded `watts` on `state = "air"` during TOML
parsing and could export heat only into component solids. Air-region watts are
now explicit, non-negative, and energy-conservative in both the native mesh and
OpenFOAM export. Each source receives a fluid cell zone and an absolute
enthalpy source; solid heat-source behavior is unchanged.

A generated 5 W runtime case selected one 0.001 m3 fluid cell, loaded the
source as active in OpenFOAM 2606, and completed a coupled solve. Fluid maximum
temperature rose from 293.15 K while unheated solid regions remained at their
initial temperatures. Focused parser/export tests and the complete C++/Python
added-feature suite passed. This capability enables the next controlled rack
comparison: measured `mass flow * cp * (exhaust - intake)` can be applied to
the internal air tunnel without interpreting an uncalibrated compact block
maximum as an equipment temperature.

### Controlled solid-versus-air heat-placement study (2026-08-08)

The same in-depth 335,580-cell rack was rerun with all geometry, solid
properties, vents, fan curves, and the 1545 W total load held fixed. Strict
TOML comparisons confirmed that only source placement changed: the 150, 950,
425, and 20 W loads moved from the four equivalent solid blocks into their
resolved interior-air tunnels. SHA-256 comparisons confirmed identical
`points`, `faces`, `owner`, `neighbour`, and `boundary` files for all five
solver regions. The accepted 10.11 s airflow state was therefore reusable.

The fluid source zones contained 8418, 12004, 8464, and 9512 cells. OpenFOAM
selected every source with the exact absolute load. The first 1200 s airflow
refresh converged after 0.03 s, matching the solid-source baseline; the 2400 s
refresh converged after one 0.01 s window. The run reached 3600 s in 54:50.

| Metric at 3600 s | Solid k=10 | Solid k=100 | Air-side |
|---|---:|---:|---:|
| Worst cell change (K/300 s) | 10.70 | 10.19 | 0.410 |
| Worst component-mean change (K/300 s) | 2.65 | 2.60 | 0.398 |
| Heat rejection fraction | 89.40% | 89.57% | 97.28% |
| Net sensible rejection | 1381 W | 1384 W | 1503 W |
| Fluid maximum | 71.2 C | 70.0 C | 47.7 C |
| Largest component maximum | 243 C | 236 C | 42.6 C |

The air-side case was continued with live airflow refreshes every 3600 s.
The 7200 and 10800 s refreshes each converged in one 0.01 s window. At
10000 s the case rejected 99.15% of its heat but was just outside the strict
thermal limits. By 14400 s it satisfied them over both relevant intervals:

| Interval | Worst cell (K/300 s) | Worst component mean (K/300 s) |
|---|---:|---:|
| 7200 to 14400 s | 0.08195 | 0.04731 |
| 10800 to 14400 s | 0.05399 | 0.02839 |

Final rack intake/exhaust were 0.291207/0.290822 kg/s (0.132% imbalance),
mass-weighted exhaust temperature was 298.396 K, net sensible rejection was
1533.36 W (99.25%), and the main-intake thermal re-ingestion index was zero.
The air-side representation therefore converged by 14400 s and does not need
a 100000 s continuation for this validation target. It is appropriate for
rack airflow, intake/exhaust temperature, and heat-rejection predictions; its
solid temperatures remain chassis thermal responses, not electronics limits.

This runtime also exposed two export/controller cleanup issues. Zero-watt
solid regions were incorrectly emitted as active zero-valued OpenFOAM sources;
they are now omitted from source masks, zones, and `fvOptions` while remaining
as geometry. Also, convergence checks at arbitrary end times could see the
last 60 s function-object sample instead of the exact solver checkpoint. The
generated controller now invokes the multi-region solver's non-advancing
`-postProcess -latestTime` mode only when a report is missing or stale. A
runtime replay at 10000 s produced exact fluid and all-solid reports without
advancing time; focused exporter tests cover both fixes.

## Intermediate 17.5 mm mapped-start study (2026-08-09)

An isolated 17.5 mm candidate was generated between the accepted 20 mm
screening mesh and the 15 mm in-depth mesh. All five regions passed
`checkMesh`, retained exactly the same region volumes, and contained 255225
cells (217013 fluid). This is 22.25% more cells than 20 mm and 23.94% fewer
than 15 mm.

The converged 20 mm fields at 14401 s were mapped into the candidate. The
existing runner incorrectly treated those hot, nonuniform mapped fields as a
cold start: it applied the cold fan ramp and then disabled all 1545 W of fluid
heat sources during adaptive initialization. Device-flow change fell below
0.1%, but exterior mass imbalance rose monotonically from 1.612% to 1.627%
and could not satisfy the 1% acceptance limit. This was not a mesh failure.

After restoring the full mapped heat sources and using the normal Co <= 2
accepted-flow refresh, imbalance immediately fell to 0.190%. The refresh
converged after 0.03 s at 0.49 s with 0.181% imbalance and 0.440% worst flow
change. A held-flow thermal step and mandatory terminal refresh then produced
a synchronized 0.96 s endpoint with 0.0256% imbalance, 0.749% worst flow
change, correct directions, and a 4.349 s air-exchange time.

At that endpoint the candidate predicted 0.293265 kg/s intake, 0.293322 kg/s
exhaust, 298.251 K mass-weighted exhaust temperature, zero thermal
re-ingestion, and 1503.65 W net sensible rejection (97.32% of 1545 W). Versus
the converged 15 mm reference, intake differed by +0.707%, exhaust by +0.860%,
exhaust temperature by -0.146 K, and heat rejection by -1.94%. Versus 20 mm,
intake/exhaust were +6.71%/+6.97%.

The full field is not yet mesh-settled despite the close bulk throughput.
Sampling the 15 mm reference onto the 17.5 mm mesh gave 100% coverage, fluid
temperature RMS difference 2.083 K (0.698% of reference RMS), and velocity RMS
difference 0.836 m/s (58.1% of reference RMS). The 17.5 mm profile therefore
remains a candidate, not a replacement default, until longer live-flow
relaxation demonstrates field convergence.

The generated runner now detects a nonuniform mapped velocity field at time
zero during `--warm-start`, records a mapped-state marker, retains the full
fluid heat-source dictionary, and skips the cold fan ramp. Subsequent
`--multirate` initialization keeps the full sources but still requires the
normal adaptive mass-balance, direction, and flow-change checks. The complete
C++ and Python regression suite passed after this correction.

### Extended 17.5 mm live-flow check

The accepted 0.96 s candidate was advanced for another 0.50 s with fully
coupled, full-heat flow at Co approximately 5, then synchronized with Co <= 2
refreshes. The final 1.59 s refresh passed with 0.283% exterior imbalance,
0.0903% worst tracked flow change, and correct directions. Nevertheless, the
spatial field was still moving: direct same-mesh comparison from 0.96 to 1.59 s
gave U RMS change 0.667 m/s (35.7% relative) and T RMS change 0.927 K (0.311%
relative).

The longer endpoint predicted 0.296474 kg/s intake, 0.297417 kg/s exhaust,
297.598 K exhaust temperature, and 1329.46 W instantaneous sensible rejection.
Against the 15 mm reference, fluid U RMS difference increased from 0.836 m/s
(58.1% relative) at 0.96 s to 1.099 m/s (76.4% relative) at 1.59 s. This does
not demonstrate that 17.5 mm is intrinsically worse; it demonstrates that the
initial apparent throughput match was premature. Only 0.63 s of additional
live-flow evolution had elapsed versus a measured rack air-exchange time of
approximately 4.30 s.

The existing adaptive acceptance criteria can therefore certify stable device
flows while a recirculation-sensitive spatial velocity field is still far from
settled. Bulk mass balance and fan-flow stability remain necessary, but they
are not sufficient for mesh or recirculation validation after mapping. The
17.5 mm candidate is rejected as a default, and future mapped-field validation
must include a minimum physical live-flow horizon tied to air-exchange time or
an explicit same-mesh U-field convergence check.

### Air-exchange-aware initial-flow acceptance

The generated controller now implements the physical-horizon requirement with
`minimum_initial_air_exchange_fraction`. After mass balance, directions, and
tracked device-flow changes pass, initial airflow remains live until elapsed
post-ramp time also reaches:

```text
measured air-exchange time * minimum_initial_air_exchange_fraction
```

The default, screening, validation, and in-depth profiles set the fraction to
1.0. Their existing 0.30 s fixed minimum remains a lower bound, while the
existing 5 s `airflow_warmup_time` remains the safety limit. A weakly
ventilated model whose required exchange horizon exceeds that limit now fails
explicitly instead of being certified prematurely; the user must increase the
safety limit. A truly sealed model uses the existing undefined-exchange
sentinel and bypasses only this new horizon, retaining its other checks.

Replaying the preserved 17.5 mm evidence gives a 4.2958 s required horizon.
Both the old 0.30 s acceptance and the extended 1.49 s live-flow state are
therefore rejected, consistent with the measured 35.7% same-mesh U RMS change
between 0.96 and 1.59 s. The generated nonzero and sealed-sentinel branches are
covered by the exporter regression test, and the complete C++ and Python suite
passes.

### Native spatial-velocity acceptance gate

A subsequent same-checkpoint screening benchmark showed why the physical
horizon is necessary but not sufficient on its own. The original 0.01 s live
windows reported approximately 0.1% tracked-flow change while consecutive
decomposed fields still changed by 1.65% RMS in velocity. Over two windows the
velocity difference grew to 3.12% RMS, so this was spatial evolution rather
than harmless cancellation. Two internal `fanMomentumSource` operating-point
reports also showed a stage-end two-cycle that the previous two-sample flow
average could hide.

The generated runner now calculates a native, volume-weighted velocity-field
metric after every live-flow stage:

```text
sqrt(volAverage(|U - U_previous|^2)) / sqrt(volAverage(|U|^2))
```

It uses OpenFOAM's `subtract`, `magSqr`, and `volFieldValue` function objects on
the decomposed fields, so acceptance does not depend on Python, PyVista, or
reconstruction. `maximum_velocity_rms_change_fraction` controls the limit and
is 0.01 in the default, screening, validation, and in-depth profiles. Mass
balance, device-flow stability, direction checks, the air-exchange horizon,
and spatial RMS must all pass. Temporary `UPrevious`, delta, and squared fields
are deleted immediately after evaluation.

The end-to-end generated-runner test measured 0.0280251 m/s RMS delta against
1.81008 m/s RMS velocity, or 1.54828%. The new 1% gate therefore rejected the
spatially unsettled field even though the native calculation completed. No
temporary processor fields remained afterward. An independent Python field
comparison previously matched the same native method to the displayed
precision (5.318468% versus 5.318% on the damping benchmark).

The reference trajectory later crossed the 1% limit for one 0.01 s window
(0.9956%) and immediately rebounded to 1.0354%. To avoid phase-sensitive
acceptance of an oscillation band that straddles the limit, both the current
and immediately previous spatial RMS windows must now pass. A restarted runner
conservatively reacquires these two live windows.

Runtime tuning remains provisional. Increasing the live-flow limit to Co=10
and checking every 0.05 s reduced a 0.10 s benchmark from 865.24 s to 410.00 s
(2.11x), but the resulting field was not spatially settled. Reducing U equation
relaxation from 0.7 to 0.3 reduced second-window velocity drift from 7.77% to
5.32% while preserving bulk imbalance and exchange time, but produced a 7.24%
cross-case velocity difference and an 8.13 K worst local temperature
difference at 0.6 s. Neither tuning is promoted to a production profile until
the new spatial gate certifies a complete air-exchange run.

## Pending adaptive-refresh restart audit

The periodic adaptive-refresh marker stored the original refresh start time,
but the generated `adaptive_airflow_refresh` function previously overwrote it
with the current time whenever a runner retried an interrupted or endpoint-
split refresh. That discarded accumulated refresh time, reset the maximum-
duration safety window, and could permit unbounded retries across runner
invocations while claiming to resume the pending refresh.

The runner now reads and validates the stored start, resumes its observation
window with cumulative elapsed time, and returns an explicit failure if the
persisted refresh already exceeded `maximum_airflow_refresh_duration`.
Malformed or future-dated markers are discarded as incompatible. The focused
C++ exporter test passed, and the preserved generated runner passed WSL
`bash -n` syntax validation. An isolated generated-function harness split a
refresh at 5.08 s after a stored 5.00 s start: the first invocation returned
with the marker still at 5.00 s, and the second resumed that start and returned
status 3 at the original 0.20 s cumulative cap. The harness then removed its
temporary marker and script.

## Regression executable cleanup

The official `tests/run_added_feature_tests.ps1` script previously linked its
six C++ test programs directly into `tests/`. Although ignored by Git, these
binaries accumulated, caused linker locks after interrupted test runs, and
contradicted the repository's generated-executable cleanup policy. Thirteen
older ignored diagnostic executables totaling 24,401,283 bytes were verified
inside `tests/` and removed; they remain rebuildable from source.

The suite now compiles C++ tests into a unique directory beneath the system
temporary directory. A guarded `finally` block deletes only a path beneath
that temp root whose directory name begins with
`thermal_solver_added_feature_tests_`. The complete official suite passed, its
specific temp directory no longer existed afterward, and `tests/` contained
zero `.exe` files.

## Full air-exchange screening reference

The preserved screening reference
`model_generic_airside_screening_spatial_gate_runtime_20260809` completed a
full measured air-exchange horizon without being restarted or regenerated.
Initial airflow was accepted after 4.30 s beyond the mapped fan-ramp state;
the estimated exchange time at acceptance was 4.29280 s. The accepting window
reported 0.84529% volume-weighted velocity RMS change, 0.034326% tracked
boundary imbalance, 0.0491704% maximum device-flow change, valid directions,
and a maximum Courant number below its limit. Fluid and solid temperatures
remained bounded throughout.

Native OpenFOAM comparisons against the final accepted decomposed field at
5.1299999999999724 s quantify how misleading the early bulk-flow passes were:

| Comparison | RMS delta U | RMS final U | Relative RMS |
| --- | ---: | ---: | ---: |
| 0.54 s to final | 0.615582 m/s | 1.90871 m/s | 32.25% |
| 2.85 s to final | 0.413492 m/s | 1.90871 m/s | 21.66% |
| 5.11 s to 5.13 s | 0.0312253 m/s | 1.90871 m/s | 1.636% |

The terminal thermal-checkpoint refresh also demonstrated why spatial and
device gates must both remain active. Its first live window changed the field
by 1.60165% RMS while device flow changed only 0.301821%. The second window
passed the spatial gate at 0.922019% but narrowly failed the device gate at
1.03105%. The third window passed both at 0.851518% spatial RMS and 0.155930%
device change, with 0.035005% imbalance and valid directions. This reference
runner was generated before the consecutive-pass correction; newly generated
runners conservatively require the current and previous spatial windows to
pass.

The early, midpoint, and final `U` fields are preserved under the case's
`validation_snapshots` directory. Native comparisons used temporary
`UPrevious`, `velocityDelta`, `velocityDeltaSquared`, and `velocitySquared`
fields with cleanup traps; none remain in the accepted processor checkpoint.

## Windows-mounted versus WSL-native field I/O

Process inspection during the reference repeatedly caught an OpenFOAM rank in
uninterruptible `p9_client_rpc` waits while spatial post-processing read and
wrote decomposed fields on `/mnt/c`. A compact 214 MiB copy containing the
identical `constant`, `system`, and processor checkpoint trees was therefore
benchmarked on WSL-native temporary storage.

Two identical spatial post-processing trials took 18.177 s and 11.628 s on
`/mnt/c`, versus 4.227 s and 4.616 s on native storage. The averages are
14.903 s and 4.422 s respectively, a 3.37x native-storage speedup for this
field-reduction phase. This does not yet prove the same factor for the full
solver window, but it directly confirms that Windows-mounted decomposed-field
I/O is a major source of runtime and timing variability.

Generated `stage()` functions now emit a `Stage wall time` record containing
the label, flow mode, start time, target time, and total seconds after pruning.
This measures dictionary setup, Courant preflight, the solver, Courant and
spatial postflight, field propagation, and checkpoint cleanup as one unit;
individual solver `ClockTime` lines cannot represent that cost.

## Test artifact isolation

Python syntax checks previously wrote bytecode into repository
`__pycache__` directories, so a stale permission-locked `.pyc` could fail an
otherwise clean suite and leave generated files behind. The test runner now
sets `PYTHONPYCACHEPREFIX` to a uniquely named guarded temporary directory,
restores the caller's environment, and deletes that directory in `finally`.
The optional mesh-comparison module is run only when both NumPy and PyVista are
available; otherwise the runner reports an explicit dependency skip instead
of treating `unittest`'s zero-test exit code as a product failure.

## Single OpenFOAM environment initialization

Whole-stage timing on a separate 600-cell validation-fan case isolated a much
larger process-launch penalty than solver `ClockTime` suggested. The identical
0.05 to 0.06 s airflow stage on `/mnt/c` took 89.126 s wall time even though
the solver reported only 10 s `ClockTime`. Approximately 79.1 s, or 88.8% of
the stage, was therefore outside the solver process. The stage produced
0.013669 m/s RMS velocity delta, 0.102832 m/s RMS velocity, 13.2926% relative
RMS, and maximum Courant number 0.00523598.

Moving the identical checkpoint to WSL-native storage reduced the stage to
48.335 s (1.84x faster) with the same displayed physics. A second controlled
`/mnt/c` trial initialized the OpenFOAM environment once and then ran all
internal utilities directly; that reduced the stage to 18.213 s (4.89x faster
than the original `/mnt/c` runner), with the same Courant and velocity values.

Generated parallel runners now validate their arguments and then perform one
guarded self-`exec` through the configured OpenFOAM launcher. The re-entered
process sets `THERMAL_SOLVER_OPENFOAM_ENV_READY=1` and
`OPENFOAM_LAUNCHER=env`, so `foamDictionary`, `foamListTimes`, MPI solvers,
post-processing, reconstruction, and the preparation script reuse one already
initialized environment. Lock acquisition occurs only after re-entry, avoiding
a false self-lock. Users who explicitly provide `OPENFOAM_LAUNCHER=env` retain
direct control for a shell where OpenFOAM is already initialized.

A freshly generated case passed `bash -n`, printed exactly one environment
initialization record, and completed preparation, the five-step fan ramp, one
adaptive airflow stage, reconstruction, and control restoration in 124 s. Its
timed stage was 21.665 s, a 4.11x speedup over the original generated runner,
and its Courant and spatial metrics matched the baseline exactly at displayed
precision.

## Coupled thermal/airflow convergence gate

The preserved screening case was continued through 12,000.01 s. The old
logic declared screening convergence after two thermal checkpoints below the
0.25 K/300 s limit, but a native comparison of accepted velocity fields from
7,200.01 to 12,000.01 s still showed 0.0274398 m/s RMS change against
1.90755 m/s RMS velocity, or 1.4385%. Adjacent live refresh windows were each
below 1%, so the short-window spatial test alone could hide accumulating
accepted-field drift.

Generated runners now preserve an accepted decomposed `U` reference in
`.accepted_airflow_reference`, outside pruned numeric time directories. A
thermal convergence streak can advance only when the thermal limits, local
airflow metrics, and accepted-reference velocity RMS limit all pass. The
reference remains anchored throughout the required checkpoint streak; a
failed comparison rebuilds the anchor and resets the streak. A restarted old
case without a reference conservatively records a baseline and requires
another refresh.

The corrected runner was installed without regenerating the preserved mesh or
results. At 14,400.02 s it recorded the missing baseline and reset the stale
2/2 streak. At 16,800.01 s, accepted-field drift was 0.0154058 m/s against
1.90644 m/s, or 0.808093%, so checkpoint 1/2 passed. The fully anchored test
at 19,200.02 s then measured 0.0279756 m/s against 1.90587 m/s, or 1.46787%,
and correctly reset the streak even though the two local refresh windows
passed at 0.821285% and 0.782063%. This reproduces and prevents the prior
premature-convergence failure.

Thermal diagnostics now identify the controlling region. At 14,400 s the
fluid peak controlled at 0.2312 K/300 s and `Generic_Trenton_3U_2` controlled
component-average drift at 0.007675 K/300 s. At 19,200 s the corresponding
values were 0.2206 K/300 s and 0.0096875 K/300 s, again controlled by the
fluid and `Generic_Trenton_3U_2`. These names make an apparent convergence
oscillation traceable to the actual region rather than the former ambiguous
`maxInternalCellChange` label.

### Continued coupled-flow settling and restart cost

Continuation through 24,000 s showed a repeatable alternating pattern. The
21,600 s accepted-field comparison changed by 1.46836% and reset the streak;
the 24,000 s comparison changed by 0.754098% and reached checkpoint 1/2. This
proved that returning immediately after a large accepted-field shift deferred
the remaining live-flow evolution across another 2,400 s frozen-flow interval.

The adaptive controller now continues live flow at the same thermal
checkpoint after such a shift. It rebases the spatial reference, requires a
subsequent locally converged window, stores that final field as the next
baseline, and keeps the shifted checkpoint ineligible for convergence. At
26,400 s the accumulated shift was 2.03297%; the controller immediately ran
another window, which changed by 0.795135%, then stored the settled field and
left the streak at zero. Previously that window was deferred until 28,800 s.

Spatial-window state now persists in `.velocity_convergence_state`. A fresh
runner at 26,400.03 s restored the preceding 0.795135% and 0.735016%
observations. The 28,800 s refresh therefore needed one 0.01 s window
(194.672 s wall time), rather than another roughly 160-200 s reacquisition
window. Its local and accepted-field change was 0.762552%, so checkpoint 1/2
passed. The case is not yet declared coupled-converged; the remaining spatial
drift is being characterized rather than hidden by stable bulk-flow metrics.

### Recirculation topology despite instantaneous field drift

Exact reconstructed OpenFOAM comparisons confirmed that the residual velocity
motion is real: RMS changes were 2.1206% from 19,200.02 to 24,000.01 s,
2.0780% from 24,000.01 to 26,400.03 s, and 0.76255% from 26,400.03 to
28,800.01 s. Face-resolved boundary audits nevertheless found unchanged
engineering topology at all four checkpoints. Every fan patch was purely
outward, the main vent was purely inward, and the fanless KVM opening retained
a nearly balanced bidirectional exchange. Bidirectional traffic was
0.12911-0.12914% of total gross boundary flow, thermal re-ingestion was zero,
and net sensible rejection remained 1539.91-1540.66 W for 1545 W applied.

The standard recirculation report became impractically slow while scanning the
case's many restart-fragmented function-object files. It now accepts
`--snapshot-times`; this mode reads reconstructed `phi` and `T` boundary faces
directly, bypasses report-history scans, and writes a checkpoint summary CSV,
a per-patch face-flow CSV, and a four-panel topology plot. The four-checkpoint
208,772-cell screening report completed in 21.9 s and is preserved under the
case's `validation_snapshots` directory. A three-checkpoint preserved in-depth
case completed in 4.5 s and likewise showed zero thermal re-ingestion, although
its older discretized fan/porosity sources differ from the current screening
export and therefore are not treated as a controlled mesh-accuracy comparison.

## Current-config screening-to-in-depth comparison (2026-08-10)

A new non-overwriting case,
`model_generic_airside_indepth_current_20260810`, was exported from the current
`model_generic_airside.toml`. Its 335,580-cell mesh passed full all-region
geometry and topology checks (284,396 fluid cells). The preserved screening
case has 208,772 cells. All four applied loads (150, 950, 425, and 20 W) and
every fan-curve table value matched; only expected mesh-dependent selected
thicknesses, cell zones, and porous-source discretization differed.

The screening 28,800.01 s solution was mapped with `mapFields interpolate
-consistent` into fluid and all four solid regions. The first attempted map
was safely rejected because the freshly exported target had not yet been
split into region meshes. Running the generated `prepare_regions.sh` first
resolved this ordering requirement. The runner then detected mapped
nonuniform velocity, retained full heat sources, skipped the cold fan ramp,
and performed strict `Co <= 1` coupled warm starts to construct consistent
target-mesh flow fields.

Mapped velocity adjustment decayed from 2.876% RMS over 0.01-0.03 s, to
1.108% over 0.03-0.04 s, 1.00066% over 0.04-0.05 s, and finally 0.94184% over
0.05-0.06 s. The accepted 0.06 s endpoint had 0.0254% exterior mass
imbalance, zero thermal re-ingestion, and the same fan/vent/KVM bidirectional
topology as screening. Strict windows cost approximately 377-394 s per 0.01 s
after preparation; mapping all five regions took 43.7 s.

| Metric | Screening 28,800.01 s | Current in-depth mapped 0.06 s | Screening difference |
|---|---:|---:|---:|
| Intake mass flow | 0.297692 kg/s | 0.293510 kg/s | +1.425% |
| Exhaust mass flow | 0.297691 kg/s | 0.293584 kg/s | +1.399% |
| Exhaust mass-weighted temperature | 298.2980 K | 298.2919 K | +0.0061 K |
| Bidirectional mass fraction | 0.12914% | 0.12485% | +0.00430 percentage point |
| Thermal re-ingestion index | 0 | 0 | unchanged |

The mapped in-depth temperature field has not undergone a long independent
thermal transient, so its instantaneous 1517 W sensible rejection is not used
as a thermal mesh-accuracy acceptance metric. This experiment isolates the
current mesh/strict-flow sensitivity: screening airflow magnitude is within
about 1.5% of the spatially settled current in-depth result, while topology
and intake/exhaust temperature rise agree much more closely.

### Guarded mapping workflow and 1,200 s in-depth continuation

`tools/map_openfoam_case.py` now automates the required preparation, mapping,
and strict warm-start order. It accepts only a distinct, freshly exported
target with no nonzero reconstructed results or processor partitions, verifies
the requested reconstructed source checkpoint and every target region, runs
`prepare_regions.sh`, maps each fluid/solid region with `mapFields interpolate
-consistent`, and finishes with the generated strict coupled warm start. Use
`--dry-run` to inspect every command without changing either case. For example:

```powershell
python tools/map_openfoam_case.py `
  --source C:\OpenFOAM\thermal_sim_v2\source_case `
  --target C:\OpenFOAM\thermal_sim_v2\fresh_target_case `
  --source-time 28800.01 --processes 2 --dry-run
```

A real safety check against the already-used in-depth target was rejected
before execution because nonzero reconstructed results existed. Unit tests
cover tolerant exact-time selection, region discovery, command ordering, and
rejection of non-fresh targets and existing partitions.

Runtime testing exposed a second mapped-state penalty: although mapped cases
already skipped the cold fan ramp, they still inherited the cold-start minimum
0.30 s airflow-observation duration. On the current fine mesh, each strict
0.01 s window costs roughly 7-10 minutes. Generated runners now apply the
configured cold-start minimum only to cold fields. Mapped fields may be
accepted after the normal live spatial, mass-balance, direction, and smoothed
device-flow checks establish sufficient history; they also skip the separate
air-exchange horizon only after those checks pass.

The preserved in-depth case validated this path. Its final mapped checks had
0.0261% exterior mass imbalance, 0.1269% maximum device-flow change, and
0.8256% velocity RMS change. The runner explicitly accepted the mapped field
at 0.12 s and advanced thermally to 1,200 s in 266.8 s wall time. The terminal
coupled refresh then passed in one 0.01 s window: accepted-field velocity drift
was 0.8245%, exterior imbalance was 0.0264%, and maximum device-flow change was
0.2545%. This confirms that holding airflow during the thermal segment did not
hide a material flow shift at this checkpoint.

Face-resolved snapshots at 0.06 and 1,200.01 s show the thermal field moving
from its mapped initial state toward energy balance while preserving topology:

| Metric | Mapped 0.06 s | Continued 1,200.01 s |
|---|---:|---:|
| Intake mass flow | 0.293510 kg/s | 0.296245 kg/s |
| Exhaust mass flow | 0.293584 kg/s | 0.296167 kg/s |
| Exhaust mass-weighted temperature | 298.2919 K | 298.3315 K |
| Net sensible heat rejection | 1517.14 W | 1542.26 W |
| Rejection / 1545 W applied | 98.20% | 99.82% |
| Bidirectional mass fraction | 0.12485% | 0.12692% |
| Thermal re-ingestion index | 0 | approximately 0 |

All nine fan patches remained outward, the main vent remained inward, and the
fanless KVM opening remained a small balanced bidirectional exchange. Snapshot
reports now create a missing output directory automatically; the evidence is
preserved in the case's `validation_snapshots_1200` directory.

### Long in-depth convergence and floating-time threshold

Continuation to 2,400 and 3,600 s remained thermally stable. At 2,400 s the
fluid-controlled peak rate was 0.05165 K/300 s, the Eaton-controlled component
average rate was 0.0200 K/300 s, and net sensible rejection was 1541.88 W
(99.80% of 1545 W). At 3,600 s the rates fell to 0.031625 and 0.011725 K/300 s.
The corresponding accepted-field airflow drifts were 0.8148% and 0.8359%, with
exterior mass imbalance below 0.025% and device-flow change below 0.11%.

Despite those passes, the 3,600 s convergence streak remained zero. The
checkpoint was represented as `3599.9999999999927`, while the minimum thermal
convergence gate compared it to 3,600 with exact `>=`. Generated runners now
apply a scale-aware time tolerance to this threshold, matching the tolerance
already used for checkpoint and requested-end comparisons. The real preserved
case then accepted nominal 4,800 s (`4799.9999999999945`) as coupled checkpoint
1/2, directly validating the correction.

The anchored two-checkpoint airflow test continued to prevent premature
convergence. Local velocity RMS changes at 4,800, 6,000, 7,200, and 8,400 s
were approximately 0.83%, but two-interval accumulated drift reached about
1.6%, above the 1% coupled limit. Each such shift triggered an immediate
same-thermal-time settling window and rebased the accepted reference. At
9,600 s a separate 4.25% change in one internal rear fan was caught despite
only 0.77% spatial RMS change; the next live window reduced device change to
0.065%, demonstrating why both spatial and fan operating-point gates are
required. The requested 18,000 s continuation remains non-overwriting and
prunes completed processor checkpoints while characterizing this slow flow
settling.

### Completed 18,000 s current in-depth run

The preserved current-config in-depth case completed the requested 18,000 s
stage without overwriting earlier reconstructed endpoints. Production controls
were restored cleanly. It did not declare coupled convergence: the final
18,000.01 s endpoint was valid checkpoint 1/2, so proceeding directly to the
100,000 s stage with a 10,000 s airflow interval is not yet justified for this
case.

From 3,600 to 18,000 s, twelve implicit thermal stages consumed 2,260 s wall
time while seventeen strict live-flow stages consumed 7,857 s. Thus airflow
validation represented 77.7% of measured solver-stage time. Each 1,200 s
thermal advance generally took 141-267 s; each 0.01 s fine-mesh airflow window
took 383-573 s. The long-lag gate repeatedly found approximately 1.5-1.6%
accumulated velocity drift over two thermal checkpoints even though individual
windows were typically 0.76-0.84%. Immediate same-checkpoint settling therefore
prevented false convergence and rebased the accepted field.

The strict fluid-hotspot criterion exposed a damped alternating response at a
rear-side cell. Consecutive hotspot values were 324.165 K at 10,800 s,
323.569 K at 12,000 s, 324.012 K at 13,200 s, and 323.394 K at 14,400 s.
Component-average changes remained below 0.014 K/300 s throughout. The hotspot
rate first passed the 0.10 K/300 s in-depth limit at 15,600 s, failed again at
16,800 s, and passed strongly at 18,000 s (0.030675 K/300 s). The required
second checkpoint correctly prevented the isolated 15,600 pass from being
reported as convergence.

Face-resolved reconstructed endpoints remained physically consistent:

| Metric | 1,200.01 s | 3,600.01 s | 18,000.01 s |
|---|---:|---:|---:|
| Intake mass flow | 0.296245 kg/s | 0.296615 kg/s | 0.297498 kg/s |
| Exhaust mass flow | 0.296167 kg/s | 0.296550 kg/s | 0.297489 kg/s |
| Exhaust mass-weighted temperature | 298.3315 K | 298.3221 K | 298.3031 K |
| Net sensible heat rejection | 1542.26 W | 1541.45 W | 1540.67 W |
| Rejection / 1545 W applied | 99.82% | 99.77% | 99.72% |
| Bidirectional mass fraction | 0.12692% | 0.12738% | 0.12886% |
| Thermal re-ingestion index | approximately 0 | 0 | 0 |

All nine fan patches remained outward, the main vent remained inward, and the
fanless KVM opening remained the only small bidirectional exchange. Evidence is
preserved under `validation_snapshots_18000`. The engineering conclusion is
that the current in-depth mesh produces stable bulk energy and component
temperatures, but its airflow field has not yet met the deliberately strict
two-checkpoint spatial criterion. Continue with the 1,200 s refresh cadence or
use the validated screening workflow for longer exploration; do not jump this
case to sparse 10,000 s refreshes solely because 18,000 s was reached.

### Controlled screening continuation to 36,000 s

The preserved current-config screening case
`model_generic_airside_screening_spatial_gate_runtime_20260809` was continued
without overwriting from 28,800.01 to 36,000.02 s using the production
2,400 s refresh cadence, 10 s thermal-only ceiling, and strict `Co <= 2`
airflow windows. Production controls were restored after completion. The case
did not declare coupled convergence.

At 31,200 s, the thermal rates passed screening limits, but accepted airflow
had moved 1.425% since 26,400 s. A same-checkpoint settling window reduced the
local velocity change to 0.760% and rebased the accepted reference, leaving
that checkpoint ineligible. At 33,600 s, peak and component-average rates were
only 0.00489 and 0.00268 K/300 s; the accepted airflow change was 0.754%, so
checkpoint 1/2 was accepted. At 36,000 s the peak rate rose to
0.21383 K/300 s while component-average drift remained 0.01188 K/300 s. Both
still passed the screening thermal limits, but the anchored airflow change
across 4,800 s was 1.395%. The runner rejected that checkpoint. Its immediate
settling window passed at 0.727% local velocity change and 0.0228% maximum
device-flow change, but correctly remained ineligible. Exterior mass
imbalance stayed below 0.0007% and all tracked directions remained valid.

The 7,200 s continuation used 1,033.5 s in three thermal stages and 765.5 s in
five live-flow stages, excluding orchestration overhead. Airflow therefore
represented 42.6% of measured solver-stage time. Screening flow windows cost
137-191 s, versus 383-573 s in the current in-depth case: approximately three
times faster at representative averages. Thermal stages cost 248-407 s per
2,400 simulated seconds. The screening thermal leg is not proportionally
faster than in-depth because screening still uses a conservative 10 s
thermal-only ceiling while the validated in-depth profile uses 20 s.

Face-resolved endpoints confirm stable bulk engineering results despite the
slow spatial airflow mode:

| Metric | 28,800.01 s | 36,000.02 s | Change |
|---|---:|---:|---:|
| Intake mass flow | 0.297692 kg/s | 0.297668 kg/s | -0.0080% |
| Exhaust mass flow | 0.297691 kg/s | 0.297670 kg/s | -0.0073% |
| Exhaust mass-weighted temperature | 298.2980 K | 298.2975 K | -0.00052 K |
| Net sensible heat rejection | 1540.18 W | 1539.91 W | -0.27 W |
| Rejection / 1545 W applied | 99.688% | 99.671% | -0.017 percentage point |
| Bidirectional mass fraction | 0.12914% | 0.12916% | +0.00002 percentage point |
| Thermal re-ingestion index | 0 | 0 | unchanged |

All fan patches remained outward, the main rack vent remained inward, and the
fanless KVM opening remained the only small bidirectional exchange. The
snapshot plot and face-flow tables are preserved under
`validation_snapshots_36000`. The result strengthens the recommended workflow:
use screening for long settling and operating-point exploration, retain the
anchored two-checkpoint airflow gate, then map a selected screening checkpoint
to in-depth for final spatial validation. A controlled same-checkpoint
screening comparison is still required before raising its 10 s thermal-only
ceiling.

### Controlled screening thermal-timestep comparison

That remaining comparison used two independent 208,772-cell cases copied from
the same freshly mapped 36,000.02 s screening state. Both branches performed
the same live-flow validation before advancing from 0.04 to 2,400.00 s with
airflow held. The reference retained a 10 s thermal-only ceiling; the trial
used 20 s. Both then ran the same strict terminal airflow window to
2,400.01 s. The initial spatial metrics were bit-for-bit identical across the
branches: 1.0254%, 0.88335%, and 0.848834% over successive 0.01 s windows.

The endpoint comparison on the identical mesh found:

| Metric | 10 s ceiling | 20 s ceiling | Difference |
|---|---:|---:|---:|
| Thermal-stage wall time | 380.696 s | 287.274 s | -24.5% |
| All-region temperature RMS | reference | - | 0.0000202 K |
| Maximum cell-temperature difference | reference | - | 0.000244 K |
| Velocity RMS difference | reference | - | 0.000002% |
| Maximum cell-velocity difference | reference | - | 0.000000956 m/s |
| Exhaust mass flow | 0.297673732 kg/s | 0.297673732 kg/s | +0.00000000019 kg/s |
| Exhaust mass-weighted temperature | 298.2992513 K | 298.2992536 K | +0.0000023 K |
| Net sensible heat rejection | 1540.46085 W | 1540.46153 W | +0.00068 W |
| Bidirectional mass fraction | 0.1291283% | 0.1291283% | negligible |
| Thermal re-ingestion index | 0 | 0 | unchanged |

The first terminal flow refresh changed 1.59807% in both cases, so both
endpoints were correctly left pending rather than called converged. That
shared airflow response does not contaminate the timestep conclusion: the
post-refresh velocity fields differed by only 2.93e-8 m/s RMS. The 20 s
screening thermal ceiling is therefore adopted. It preserves engineering and
cell-field accuracy at this controlled operating point while removing roughly
one quarter of thermal-stage runtime; all existing airflow cadence and
convergence safeguards remain unchanged. The cases and reports are preserved
as `screening_dt10_controlled_20260810` and
`screening_dt20_controlled_20260810`.

### Long screening confirmation with the 20 s ceiling

The generated 20 s runner differed from the previously installed preserved
screening runner at exactly one line: the implicit frozen-flow stage ceiling.
After SHA-256 verification, it was installed in
`model_generic_airside_screening_spatial_gate_runtime_20260809`, and that case
was continued non-overwriting from 36,000.02 to 43,200.01 s. Production
controls were restored after completion.

The three 2,400 s thermal stages cost 184.4, 189.1, and 126.0 s, for 499.5 s
total. The preceding 10 s-ceiling continuation over the same 7,200 simulated
seconds used 1,033.5 s of thermal-stage time. The real long-case reduction was
51.7%, while the complete measured solver-stage time fell from 1,799.0 to
1,161.5 s (35.4%) despite normal flow-window runtime variability. Live-flow
validation became 57.0% of measured stage time, confirming that airflow is now
the dominant screening cost rather than the implicit energy solve.

The accelerated continuation preserved the slow-mode behavior caught by the
convergence guards. At 38,400 s, thermal peak and component-average rates were
0.01383 and 0.00511 K/300 s, and accepted airflow drift was 0.744%; checkpoint
1/2 passed. At 40,800 s, the thermal rates still passed at 0.20546 and
0.00965 K/300 s, while local airflow changed 0.723%. The anchored change over
4,800 s was 1.365%, so the checkpoint was rejected. A same-time window settled
at 0.711% and rebased the accepted field. At 43,200 s, thermal rates were
0.00519 and 0.00190 K/300 s and airflow drift was 0.718%, producing a new
checkpoint 1/2. Exterior imbalance remained below 0.0007%, device-flow change
below 0.036%, and all directions remained valid.

Face-resolved results also remained stable from 36,000.02 to 43,200.01 s:

| Metric | 36,000.02 s | 43,200.01 s | Change |
|---|---:|---:|---:|
| Intake mass flow | 0.297668 kg/s | 0.297649 kg/s | -0.0062% |
| Exhaust mass flow | 0.297670 kg/s | 0.297648 kg/s | -0.0072% |
| Exhaust mass-weighted temperature | 298.29749 K | 298.29971 K | +0.00223 K |
| Net sensible heat rejection | 1539.91 W | 1540.47 W | +0.55 W |
| Rejection / 1545 W applied | 99.671% | 99.707% | +0.036 percentage point |
| Bidirectional mass fraction | 0.129159% | 0.129171% | +0.000012 percentage point |
| Thermal re-ingestion index | 0 | 0 | unchanged |

Evidence is preserved under `validation_snapshots_43200`. The long run
confirms the 20 s ceiling is both materially faster and behaviorally
consistent. It does not justify weakening the 2,400 s airflow cadence or the
anchored two-checkpoint gate; those controls continue to catch a genuine
alternating airflow mode after bulk mass flow, heat rejection, and component
temperatures appear stationary.
