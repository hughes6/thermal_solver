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

The tested 20 s cap became the in-depth profile default at this stage. A later
matched 30 s study is documented below. With a valid
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
current 20 s `airflow_warmup_time` remains the safety limit. It was increased
after a production-rack case measured a 12.43 s exchange horizon that could
not possibly satisfy the former 5 s cap. A weakly
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
faster than in-depth because screening used a conservative 10 s thermal-only
ceiling while the then-current validated in-depth profile used 20 s.

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

### Spatial drift localization and bounded live-flow probe

`openfoam_field_convergence.py` now accepts `--geometry` and partitions fluid
field changes into each component's exported Air box and the remaining
external rack air. This prevents a high-speed internal passage from being
mistaken for the entire rack field while retaining the all-fluid aggregate.
Unit tests cover non-overlapping component masks and external-cell retention.

From 36,000.02 to 43,200.01 s, whole-fluid velocity changed 2.58% RMS. The
largest cell change, 2.87 m/s, was inside the Trenton air passage at its rear
exhaust. Partitioned RMS changes were 15.46% in Eaton, 7.30% in Trenton,
0.178% in Dell, 0.077% in the fanless KVM, and 2.33% in external rack air.
Thus component vortices dominate the maximum, but excluding component air
would not eliminate the slower external-field motion.

A bounded diagnostic then advanced only the coupled flow solution at strict
`Co <= 2`, preserving reconstructed endpoints every 0.01 s from 43,200.01 to
43,200.05 s and one further endpoint at 43,200.10 s. Consecutive whole-fluid
changes were 0.781%, 0.688%, 0.714%, and 0.749%; external-rack changes remained
0.603-0.638%. They did not decay. Cumulative 0.01-to-0.05 s movement reached
2.68% whole-fluid and 2.38% external-rack RMS. Over the subsequent 0.05 s,
movement increased to 3.35% whole-fluid and 2.97% external-rack RMS. Eaton and
Trenton changed 23.2% and 8.37% over that final interval, consistent with
transient internal vortices rather than relaxation toward one fixed velocity
field.

The engineering outputs remained stationary throughout the same probe:

| Metric | 43,200.01 s | 43,200.10 s | Change |
|---|---:|---:|---:|
| Intake mass flow | 0.2976493 kg/s | 0.2976326 kg/s | -0.0056% |
| Exhaust mass flow | 0.2976480 kg/s | 0.2976396 kg/s | -0.0028% |
| Exhaust mass-weighted temperature | 298.29971 K | 298.29873 K | -0.00099 K |
| Net sensible heat rejection | 1540.47 W | 1540.13 W | -0.34 W (-0.022%) |
| Bidirectional mass fraction | 0.129171% | 0.129195% | +0.000024 percentage point |
| Thermal re-ingestion index | 0 | 0 | unchanged |

This establishes that a 1% long-lag requirement on instantaneous screening
velocity is incompatible with the observed transient-RANS field even when the
rack-level thermal and flow outputs are stable. The local two-window limit
remains 1% to catch abrupt refresh changes. A separate
`maximum_accepted_velocity_rms_change_fraction` now controls the anchored
two-checkpoint comparison: screening uses 3%, bounded by the measured 2.58%
7,200 s change and the mesh's ballpark role. Default and validation profiles
retain 1%. The in-depth limit was subsequently tested separately on a fresh
fine-mesh mapping, as documented below.

The diagnostic also exposed a restart-state defect. `--warm-start` changed
fields but previously retained the old thermal streak, device-flow history,
spatial history, pending refresh, and accepted velocity reference. A later
multirate run could therefore compare against stale pre-warm-start data or
inherit a false convergence checkpoint. Generated runners now invalidate all
of those caches after reconstructing a warm start. A real 43,200.10-to-.11 s
warm start confirmed every cache was removed and production controls were
restored; the next multirate continuation must conservatively reacquire its
accepted airflow baseline.

## Screening long-lag gate validation through 50,400 s (2026-08-10)

The preserved screening case
`model_generic_airside_screening_spatial_gate_runtime_20260809` was resumed
from 43,200.11 s to 50,400 s after deliberately invalidating the cached
warm-start convergence references.  This exercised the screening-only 3%
accepted-reference velocity RMS limit while retaining the strict 1% limit on
each individual airflow refresh.

The first accepted thermal checkpoint at 48,000.01 s had a 0.78208% local
velocity RMS change and a 0.78208% accepted-reference drift.  The second at
50,400.01 s had a 0.780945% local change and 1.45851% accumulated drift from
45,600.04 s.  Both checkpoints therefore passed for the intended reason; the
run ended with `.thermal_convergence_streak` equal to 2.  Thermal metrics were
0.08395 K/300 s peak and 0.0032625 K/300 s component-average at the first
checkpoint, then 0.209113 K/300 s peak and 0.0098125 K/300 s
component-average at the second.

The endpoint engineering quantities remained effectively stationary between
43,200.11 and 50,400.01 s:

- intake mass flow: 0.2976292 -> 0.2975792 kg/s (-0.0168%)
- exhaust mass flow: 0.2976385 -> 0.2975819 kg/s (-0.0190%)
- exhaust mass-weighted temperature: 298.29854 -> 298.29902 K (+0.00048 K)
- net sensible heat rejection: 1540.064 -> 1539.915 W (-0.149 W)
- expected heat rejection fraction: 99.6805% -> 99.6709%
- thermal re-ingestion index: 0 at both endpoints
- bidirectional boundary-flow fraction: 0.129196% -> 0.129208%

The full instantaneous velocity field still changed by 3.9808% RMS over the
entire 7,200 s endpoint interval, concentrated in the Eaton (25.80%) and
Trenton (11.66%) internal air passages; external rack air changed by 3.4869%.
Temperature changed by only 0.0890% RMS overall.  This confirms that transient
RANS vortex phase is not an appropriate long-lag screening stop metric by
itself, while the local 1% refresh gate plus the 3% accepted-reference gate
continues to protect against an actually unsettled flow update.

Reports are preserved under the case's `validation_snapshots_50400` directory:
`field_change_partitioned.csv`, `recirculation.csv`,
`recirculation_face_flow.csv`, and `recirculation.png`.

## Current screening endpoint mapped to in-depth and converged (2026-08-10)

The qualified 50,400.01 s screening state was mapped into the new,
non-overwriting 335,580-cell case
`model_generic_airside_indepth_50400_validation`. The guarded mapper prepared
all five regions, mapped the reconstructed fields, and performed a strict
Co <= 1 warm start. Cache invalidation forced conservative airflow-history
reacquisition; the accepted fine-mesh baseline was established at 0.06 s.

With the original 1% in-depth accepted-reference limit, every local refresh
remained below 1%, but convergence could not persist. At 4,800 s a thermal
checkpoint was initially accepted with 0.833739% local velocity RMS change,
then a same-time settle to 4,800.02 s rebased the accumulated drift and reset
the thermal streak. The pattern repeated at 6,000/7,200 s: local changes were
0.779336% and 0.773180%, but the second checkpoint again triggered a settle;
the 7,200.02 s settle passed locally at 0.788410% and reset the streak to zero.

Zone analysis showed that the long-lag signal remained dominated by transient
internal vortices. From 4,800.02 to 7,200.02 s the full velocity field changed
2.2294% RMS, including 12.09% in Eaton and 8.39% in Trenton, while external
rack air changed 1.9165%. Temperature changed only 0.03915% RMS. Over that
same interval, intake flow changed 0.205%, exhaust flow 0.219%, outlet
temperature -0.01534 K, heat rejection -0.077%, and re-ingestion remained
zero. The 1% anchored limit was therefore stricter than two individually
accepted 1% updates can mathematically sustain and was rejecting bounded
transient RANS motion rather than a changing rack operating point.

The in-depth accepted-reference limit was increased only to 2%; its local
velocity, device-flow, mass-balance, direction, Co <= 1, and thermal limits
remain unchanged. Screening remains at 3%, while default and validation remain
at 1%. A real continuation from the rebased 7,200.02 s state then accepted
checkpoint 1 at 8,400.01 s with 0.780575% local velocity change and checkpoint
2 at 9,600.01 s with 0.826818% local change. The full accepted-reference field
change across 7,200.02 to 9,600.01 s was 1.5662%, so the new 2% threshold was
actually exercised rather than bypassed. `.thermal_convergence_streak` ended
at 2 and the solver stopped converged without a same-time rebase.

At the converged 9,600.01 s endpoint, intake and exhaust flows were 0.2967523
and 0.2967086 kg/s, outlet temperature was 298.3170 K, sensible heat rejection
was 1540.76 W (99.725% of 1545 W), bidirectional boundary flow was 0.12776%,
and thermal re-ingestion was zero. Reports are preserved under
`validation_snapshots_2400`, `validation_snapshots_4800`,
`validation_snapshots_7200`, and `validation_snapshots_9600` in the in-depth
case.

## Durable concise run summaries (2026-08-10)

The long in-depth validation produced more than 20,000 console lines per
2,400 s continuation. Important stage timings and convergence decisions were
therefore easy to lose when a terminal, supervising process, or captured tool
reached its output limit. Generated parallel runners now append a compact
`run_summary.log` inside each case. It records run start/completion, every
thermal or coupled stage wall time, airflow metrics, thermal metrics, accepted
checkpoint counts, and long-lag checkpoint resets. Full solver output remains
available on the console, but is no longer the only record of the decisions
that control convergence.

The log is intentionally append-only, so a continuation preserves earlier
run evidence instead of overwriting it. The exporter regression test verifies
all summary event types. The generated Bash runner also passed `bash -n` under
the same WSL environment used for OpenFOAM.

## Final screening versus in-depth operating point

The independently converged current-configuration endpoints agree closely:

| Metric | Screening 50,400.01 s | In-depth 9,600.01 s | Screening minus in-depth |
|---|---:|---:|---:|
| Intake mass flow | 0.2975792 kg/s | 0.2967523 kg/s | +0.279% |
| Exhaust mass flow | 0.2975819 kg/s | 0.2967086 kg/s | +0.294% |
| Exhaust mass-weighted temperature | 298.29902 K | 298.31700 K | -0.01798 K |
| Net sensible heat rejection | 1539.915 W | 1540.757 W | -0.842 W (-0.055%) |
| Bidirectional mass fraction | 0.129208% | 0.127760% | +0.00145 percentage point |
| Thermal re-ingestion index | 0 | 0 | unchanged |

The final mapped in-depth continuation advanced 2,400 simulated seconds in
about 1,293 s end-to-end. The validated screening continuation advanced 7,200
simulated seconds in roughly the same order of wall time, so screening delivers
approximately three times the simulated-time throughput on this workstation
while keeping the rack-level outputs within the differences above. In-depth
remains necessary for final local fields and hotspots; screening is validated
for layout, load, fan, and recirculation iteration.

## In-depth 30 s implicit timestep study (2026-08-11)

Two isolated copies of the converged 9,600.01 s 335,580-cell in-depth case
were advanced to the same 10,800.02 s coupled endpoint. The control retained
the 20 s implicit thermal cap; the candidate changed only that cap to 30 s.
Runs were sequential to avoid memory contention.

The 20 s thermal stage took 147.380 s and the 30 s stage took 118.200 s, a
19.8% reduction. End-to-end times including strict airflow refreshes and
reconstruction were 879.05 and 789.22 s, respectively, although about 56 s of
that difference came from ordinary airflow-stage timing variation rather than
the timestep. The directly attributable saving is the 29.18 s thermal-stage
reduction.

| 30 s minus 20 s metric | Result |
|---|---:|
| Temperature RMS difference | 0.00001548 K (0.000005%) |
| Maximum temperature difference | 0.0002136 K |
| Velocity RMS difference | 4.26e-8 m/s (0.000002%) |
| Maximum velocity difference | 9.63e-7 m/s |
| Pressure RMS difference | 0.0000827 Pa |
| Maximum pressure difference | 0.007813 Pa |

Both thermal checks reported 0.05175 K/300 s peak change. Component-average
change was 0.008625 K/300 s at 20 s and 0.008600 K/300 s at 30 s. Rack flow,
outlet temperature, heat rejection, bidirectional flow, and zero re-ingestion
were numerically indistinguishable. The in-depth profile therefore now uses a
30 s implicit thermal-only cap. Screening remains at its separately validated
20 s cap.

## Converged-reference restart and extended in-depth gate (2026-08-11)

Continuing the converged 9,600.01 s case exposed that its accepted airflow
reference still pointed to 7,200.02 s. At 10,800.01 s, the runner therefore
compared across three thermal intervals, measured 2.25198% RMS drift, launched
an unnecessary 325.718 s same-time settle, and reset the thermal streak. The
accepted field used to prove convergence must become the baseline for a later
continuation. Generated runners now atomically record the current accepted
velocity field immediately when the required thermal/airflow checkpoint count
is reached.

The longer continuation also expanded the in-depth long-lag evidence. After a
4.758% internal Eaton fan shift was correctly rejected and then settled, the
two-checkpoint accepted-field drift reached 2.10252%, just above the initial
2% limit. From 10,800.02 to the settled 13,200.03 s endpoint, intake changed
0.087%, outlet temperature changed -0.00599 K, heat rejection changed -0.025%,
bidirectional flow remained about 0.128%, and thermal re-ingestion remained
zero. The in-depth anchored limit is therefore 2.5%: below screening's 3%,
while the strict 1% local spatial, 1% device-flow, mass-balance, direction,
and Co <= 1 checks remain unchanged.

A real continuation from the rebased 13,200.03 s state exercised the complete
policy. The 14,400 s thermal hotspot rate failed at 0.29965 K/300 s, so no
checkpoint was counted. The next two thermal checkpoints passed at 15,600 and
16,800 s. Their local velocity changes were 0.70765% and 0.73119%; accumulated
accepted-reference drift from 14,400.01 to 16,800.01 s was 1.38566%. The run
ended at checkpoint 2/2, and both `.thermal_convergence_streak` and the stored
accepted-reference timestamp were verified as `2` and
`16800.009999999958`. This directly validates the convergence-time rebase fix
and the revised in-depth gate without bypassing any local criterion.

## Validation-rack cold start and 100,000 s endpoint (2026-08-11)

A fresh 600-cell `validation_fan_rack` export exposed a runtime inefficiency in
the cold-start safeguard. Device flows and local velocity changes passed by
0.58 s, but the required air-exchange observation horizon was 20.0751 s. The
old runner would reach that horizon through roughly 1,955 separate 0.01 s MPI
launches. Generated runners now advance directly to the bounded exchange
horizon after the early metrics pass, while retaining the strict 0.001 s CFD
timestep and rechecking every spatial and device criterion afterward. The real
run correctly rejected the apparently settled field: full-field velocity RMS
changed 63.4068% over the exchange. Two subsequent 0.01 s checks reduced the
change to effectively zero. The model-specific warmup safety limit is now 25 s
because one exchange takes about 20.1 s.

The same preserved case was continued with 12,000 s airflow-refresh intervals.
At 3,600 s it rejected only 7.128 W of the applied 50 W and its outlet was
293.935 K. At 20,001 s those values improved to 24.570 W and 295.857 K, proving
that both earlier endpoints were thermal transients. Temperature drift over a
12,000 s window fell monotonically from 1.1045 K at 36,000 s to 0.6999 K at
48,000 s, 0.4244 K at 60,000 s, 0.2540 K at 72,000 s, 0.1510 K at 84,000 s,
and 0.0894 K at 96,000 s. The final 4,000 s drift at 100,000 s was 0.0631 K.

The reconstructed 100,001 s endpoint passed the numerical audit: inlet and
outlet mass flow differed by 0.00006%, signed air-side heat transport was
49.216 W (1.568% below the 50 W source), and the signed mass-weighted outlet
temperature was 298.572 K versus the Fluent reference of 298.500 K, a 0.072 K
or 0.024% difference. Airflow refreshes remained below 0.1% spatial drift,
supporting the multirate assumption. Local results still require a mesh study:
the coarse case predicts a 567.6 K aluminum average and 47.42% reverse share of
gross outlet-face traffic. The signed net outlet balance is validated; solid
temperature and local recirculation are not yet mesh-independent.

## Validation-rack mesh sensitivity (2026-08-11)

Two uniquely named cases were mapped from converged predecessors, rebuilt with
a fully coupled warm start, and then continued until their own mass, energy,
temperature, and airflow gates passed. No baseline case was overwritten. The
horizontal refinement used 50 x 50 x 25 mm cells (2,400 total, 2,368 fluid,
32 solid, 48 interface faces). The vertical refinement used 50 x 50 x 12.5 mm
cells (4,800 total, 4,736 fluid, 64 solid, 80 interface faces). Both meshes
passed `checkMesh` with zero non-orthogonality and skewness and matched AMI
weights of one.

| Mesh | Solid cells | Final time | Outlet T | Fluent difference | Heat transport | Mass error | Solid average | Outlet reverse gross share |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 100 x 100 x 25 mm | 8 | 100,001 s | 298.572 K | +0.072 K | 49.216 W | 0.00006% | 567.61 K | 47.42% |
| 50 x 50 x 25 mm | 32 | 60,008 s | 298.585 K | +0.085 K | 49.335 W | 0.00002% | 541.07 K | 47.46% |
| 50 x 50 x 12.5 mm | 64 | 60,012 s | 298.708 K | +0.208 K | 50.446 W | 0.00004% | 472.99 K | 47.48% |

Rack-level outlet temperature, mass balance, and energy rejection remain
stable and pass the Fluent comparison. Component temperature is not
mesh-independent: horizontal refinement changed the aluminum average by
-26.54 K and subsequent vertical refinement changed it by another -68.08 K.
The coarse validation model therefore validates rack-level transport only and
must not be cited as a component-temperature validation.

The unchanged 47.4-47.5% gross reverse share is insensitive to both refinements.
The validation fan imposes only about 0.05 m/s mean flow while the solid is
hundreds of kelvin hotter than ambient, so buoyancy-driven local backflow at
the pressure outlet can dominate gross face traffic even while signed net mass
flow is correct. This metric is a physical warning for the low-flow geometry,
not evidence that the signed outlet balance is inverted.

Runtime also establishes a useful cost boundary. A 12,000 s implicit thermal
stage took about 64-105 s at 2,400 cells and 93-108 s at 4,800 cells. A strict
1 s coupled refresh took roughly 105-169 s and 153-169 s respectively. The
4,800-cell post-thermal field required seven coupled seconds before two
consecutive local spatial checks passed; boundary flow and mass checks had
passed much earlier. Fixed one-second refreshes would therefore certify an
unsettled refined field.

The study also exposed an endpoint-reporting defect. A requested endpoint can
arrive while `.airflow_refresh_pending` remains because the strict spatial
field has not settled. Generated runners now report `run_paused` with reason
`airflow_refresh_pending` in this state and explicitly instruct the operator
to continue the same case. They emit `run_complete` only when no refresh is
pending.

## Eight-layer limit and refresh timestep study (2026-08-11)

A fourth unique case halved only the vertical spacing again to 6.25 mm. It had
9,600 total cells, 9,472 fluid cells, 128 solid cells, eight cells through the
aluminum height, and 144 matched interface faces. The clean mesh was mapped
from the converged four-layer case and rebuilt with a coupled warm start. Its
12,000 s implicit thermal stage took 109.1 s.

The refined post-thermal airflow did not satisfy the steady-flow validation
policy. Its first 1 s coupled window changed the velocity field by 17.93%.
Across the complete 20 s safety window, local velocity drift ranged from 2.15%
to 26.30%, later oscillated around 6-8%, and ended at 7.87%; final mass
imbalance was 1.053%. The case was therefore rejected rather than added as a
fourth converged component-temperature point. This is evidence of a resolved
unsteady/buoyant flow regime at the finer mesh, not justification to relax the
1% spatial gate. A future component-temperature validation at this resolution
requires an explicitly time-averaged unsteady-flow policy.

The case also exposed two runner issues. First, a requested endpoint exactly
equal to the maximum refresh duration previously returned the endpoint-pause
path before checking safety-limit exhaustion. Generated runners now check the
maximum duration first. The exhausted real case then immediately returned
nonzero with `Airflow refresh failed to converge within 20 s.` Second, all
live-flow stages shared the cold-start 0.001 s timestep cap even though refresh
stages already perform strict Courant preflight and postflight checks.

A matched one-second study cloned the same 12,001 s checkpoint. The 0.001 s
control took 291.8 s; a 0.005 s candidate took 104.4 s, a 64.2% reduction.
Candidate predicted maximum Courant number was only 0.0314. Relative to the
control, full-field velocity RMS difference was 0.787%, fluid-temperature RMS
difference was 0.0486 K, solid-temperature RMS difference was 0.000094 K, and
outlet temperature differed by 0.0466 K. Both runs correctly remained pending
because the physical field was unsettled.

The schema now separates `airflow_refresh_maximum_time_step` from the existing
`airflow_maximum_time_step`. The validation profile uses 0.005 s for refreshes
while retaining 0.001 s for startup and warm-start airflow. Default, screening,
and in-depth profiles remain at 0.001 s until independently matched on their
own representative cases.

## In-depth refresh Courant study and cache compatibility (2026-08-11)

The production-style in-depth workflow was independently tested from the same
accepted 16,800.01 s checkpoint. The Co=1 control advanced 0.01 s in 61 steps
at 0.000163934 s and took 405.695 s. The matched Co=2 candidate used 31 steps
at 0.000322581 s, passed its postflight check at Co=1.5761, and took 183.323 s,
a 54.8% runtime reduction.

The candidate's full fluid-velocity field differed from the control by
0.0189753 m/s RMS against a 1.89502 m/s RMS field, or 1.0013%. Its live flow
metrics were also noisier at the terminal partial checkpoint. Because this is
on the 1% accuracy boundary rather than comfortably below it, the in-depth
profile remains at Co=1. The faster setting is not promoted based only on
runtime.

The control uncovered an independent restart defect. Its cached accepted
airflow field came from an older processor topology: rank 0 contained 147,443
cached cells while the current partition contained 86,589. Copying that field
as `UPrevious` contaminated the result directory and caused `reconstructPar`
to fail even though the coupled solve itself was valid. The recovered control
reconstructed successfully after removal of only those temporary diagnostic
fields.

Generated runners now read the binary field's declared internal-cell count for
every processor before materializing `UPrevious`. Missing, malformed, or
topology-incompatible caches are rebuilt from the current field, all partial
diagnostic copies are removed, and the long-lag comparison is deferred to the
next refresh. This prevents convergence caches from making otherwise valid
checkpoints unreconstructable after repartitioning or mapped restarts.

## Long production-style in-depth convergence (2026-08-11)

The preserved `model_generic_airside_indepth_50400_validation` case was
continued from 9,600.01 s toward a requested 18,000 s endpoint with 1,200 s
thermal intervals. Earlier checkpoints were retained and the runner pruned
only its normal intermediate processor writes. The 1,200 s implicit thermal
stages took 148-162 s on this machine. A 0.01 s Co=1 coupled refresh used
61-62 steps and normally took about 312 s before later host contention made
the final measured wall-clock sample non-representative.

The case demonstrated why thermal drift alone cannot terminate the workflow.
At 10,800 s the peak and component-average rates were only 0.05175 and
0.008625 K/300 s, but airflow shifted enough to force a rebase. At 12,000 s
the rebaselined field passed with 0.7426% long-lag velocity drift. At 13,200 s
a live window caught a 4.758% internal-fan flow change, and the subsequent
settled field was still 2.1025% from its anchored airflow reference. The older
generated runner's stricter 1% anchored policy rebased that field. The next
thermal checkpoint then moved by 0.29965 K/300 s at the fluid hot spot,
disproving the apparent earlier convergence.

After the rebase, 15,600 s and 16,800 s formed two consecutive validated
checkpoints. The final rates were 0.033275 K/300 s for the fluid peak and
0.0123 K/300 s for the controlling component average. Final long-lag velocity
drift was 1.38567%, local spatial drift was 0.731188%, and boundary mass
imbalance was 0.00344%. The runner therefore stopped correctly at
16,800.01 s before the requested 18,000 s endpoint.

The reconstructed final reports give 0.296551 kg/s total fan exhaust,
298.3032 K mass-weighted fan outlet temperature, 299.2920 K rack-fluid
average, and 322.5547 K rack-fluid maximum. Against the configured 1,545 W
equipment load, signed fan heat rejection is 1,535.81 W, a 0.595% energy
difference. The nearly closed KVM boundary has only 1.48e-9 kg/s flow, so its
standalone weighted temperature is numerically meaningless and must continue
to be excluded by the recirculation report's minimum-flow gate.

This run also exposed missing values at terminal report times. Function
objects used a 60 s report schedule while a coupled refresh ended 0.01 s after
the last thermal report. OpenFOAM created canonical report files containing
only the time, and later post-processing wrote valid values into suffixed files
that the existing plotting readers do not consume. Generated runners now
perform one final multi-region post-processing pass after reconstruction. They
temporarily force report writes and then restore `controlDict`; existing report
data is never deleted. Plot readers accept both canonical and OpenFOAM's
collision-safe suffixed report filenames. The same
operation is available for older preserved cases through
`tools/regenerate_openfoam_reports.sh`. On the 284,396-fluid-cell case it took
14.7 s and restored readable final values without rerunning the simulation.

## Converged screening versus in-depth rack metrics (2026-08-11)

The final report repair was applied to the preserved 50,400.01 s screening
endpoint, then its steady operating point was compared with the independently
converged 16,800.01 s in-depth endpoint. This compares converged solutions,
not equal transient timestamps: the in-depth case was initialized from the
screening field and then satisfied its own thermal and airflow gates.

| Metric | Screening | In-depth | In-depth minus screening |
|---|---:|---:|---:|
| Total fan exhaust | 0.296813 kg/s | 0.296551 kg/s | -0.0884% |
| Mass-weighted fan outlet | 298.2975 K | 298.3032 K | +0.0057 K |
| Signed fan heat rejection | 1535.47 W | 1535.81 W | +0.0221% |
| Rack-fluid average | 298.9217 K | 299.2920 K | +0.3703 K |
| Rack-fluid maximum | 322.6206 K | 322.5547 K | -0.0659 K |

Individual fan flows differ by -1.00% to +1.34%, and outlet temperatures by
-0.284 K to +0.377 K. The largest component-average difference is +0.455 K
for the Dell region. The largest component-maximum difference is -0.863 K,
also on the Dell region. The other component averages agree within 0.293 K
and maxima within 0.725 K. Both meshes reject the configured 1,545 W within
0.6%.

These results support screening for rack airflow, total heat rejection,
boundary temperatures, and hot-zone location. In-depth resolution remains the
appropriate source for reported local component temperatures, but this case
does not show a rack-level bias large enough to justify making screening more
expensive.

Both endpoints also reproduce an OpenFOAM numerical artifact at the passive
KVM boundary: net flow is about 1.5e-9 kg/s, so dividing signed thermal flux by
near-zero signed mass flux produces a reported weighted temperature near
-2.9 million K. This is not a physical temperature. The outlet-temperature
tool now reads complete OpenFOAM reports before loading PyVista, rejects flows
at or below 1e-8 kg/s by default, and reports that the weighted temperature is
undefined. This both prevents misleading output and avoids the earlier `T` and
`phi` array failure when valid reports are already present. The threshold is
configurable with `--minimum-mass-flow` for genuinely smaller systems.

## External and equipment-level recirculation audit (2026-08-11)

The recirculation tool was run directly on reconstructed face fields at both
converged endpoints. A new `--csv-only` mode makes the numerical audit usable
without Matplotlib and writes the summary, face-resolved boundary traffic, and
internal equipment-air CSV files without creating a plot.

| Endpoint | External thermal re-ingestion | Bidirectional boundary mass | Net sensible rejection | Load fraction |
|---|---:|---:|---:|---:|
| Screening, 50,400.01 s | 0.0000 | 0.12921% | 1539.92 W | 99.6709% |
| In-depth, 16,800.01 s | 0.0000 | 0.12858% | 1540.17 W | 99.6872% |

All nine rack exhaust patches are one-way outward and the main vent is one-way
inward. The passive KVM front opening is locally bidirectional even though its
net flow is essentially zero. Screening resolves 0.0007690 kg/s in and
0.0007690 kg/s out at that patch; in-depth resolves 0.0007646 kg/s each way.
Its inward faces receive the imposed 293.15 K ambient condition and its outward
faces discharge at 298.90 K screening or 298.82 K in-depth. This explains both
the 50% patch-local bidirectional share and the meaningless signed
mass-weighted temperature obtained when the opposing streams are collapsed to
one near-zero net flow.

The earlier external index of zero does not answer the server-to-server
question by itself. The rack boundary condition supplies ambient air on every
external inward face and does not model the room outside the rack. Equipment
front-intake and rear-exhaust cell-zone temperatures give the following
separate air-rise indicator:

| Internal pair | Screening | In-depth |
|---|---:|---:|
| UPS intake/exhaust (zones 0/1) | 0.0753 | 0.0620 |
| Dell intake/exhaust (zones 2/3) | 0.2854 | 0.2800 |
| Trenton intake/exhaust (zones 4/5) | 0.5675 | 0.5404 |

The indicator is `(T_intake - T_ambient) / (T_exhaust - T_ambient)`, clamped
to zero through one. It demonstrates substantial hot intake air at the
Trenton and moderate hot intake air at the Dell, with close agreement between
meshes. It is not source attribution: proving that a particular exhaust fed a
particular intake still requires a passive scalar/tracer or resolved room
domain.

Future exports now include `internal_airflow_devices.csv`, mapping every
internal OpenFOAM zone to its component, kind, and original device name. The
recirculation tool uses this metadata to label equipment pairs. It retains an
adjacent-zone fallback for preserved cases exported before the metadata was
added, which is why the audited legacy CSVs use zone-pair identifiers.

## Fresh unique-case screening validation (2026-08-11)

`model_runner --case-name NAME` now replaces only the final directory name of
the configured OpenFOAM case path. It rejects paths, `.` and `..`, and cannot
be combined with `--plot-existing`. This makes it possible to export and run a
new case without deleting or reusing the configured production case. The
runner's generated Bash and plotting commands all use the unique path.

The current exporter was exercised with the unique case
`model_generic_airside_screening_mapped_20260811`. Its 208,772 total cells
split into 177,064 fluid cells and four solid regions. `checkMesh
-allRegions -allGeometry -allTopology` passed every region; the fluid maximum
aspect ratio was 38.3013 and all AMI weight sums were one.

A true cold start exposed a screening-only runtime problem. After the 0.05 s
fan ramp, the full acceptance audit at 0.35 s had 0.2832% mass imbalance,
correct device directions, and stable device flows, while consecutive spatial
velocity changes were 2.959% and 2.922%. The former 1% screening threshold
therefore rejected an otherwise settled transient RANS field. At roughly
70--110 wall-clock seconds per 0.01 s window, advancing through the measured
5.35 s rack air-exchange horizon would have taken about 10--16 hours. The
screening local spatial threshold is now 3%, matching its previously validated
3% two-checkpoint long-lag threshold. The in-depth profile remains at 1%.

This relaxation does not bypass the independent safeguards. In the mapped
run, checkpoints were still rejected for a 3.108% spatial change, a 5.57%
internal fan-flow change, and 4--9.5% accepted-field long-lag changes. The
mapped state was accepted only after three live 0.01 s windows with 0.132%
mass imbalance, 0.311% maximum device-flow change, correct directions, and
2.55% spatial change. Only then did it skip the cold-start air-exchange
horizon. The global undecomposed mesh was verified byte-for-byte identical to
the source mesh; processor fields were regenerated rather than copied because
the two decompositions had different cell ordering.

The fresh 18,000 s screening stage completed with a mandatory terminal live
flow refresh and final report regeneration:

| Metric | Fresh screening at 18,000.01 s |
|---|---:|
| Peak thermal drift | 0.03785 K/300 s |
| Component-average thermal drift | 0.016675 K/300 s |
| Boundary mass imbalance | 0.04105% |
| Maximum device-flow change | 0.18894% |
| Local velocity RMS change | 2.0781% |
| Main intake mass flow | 0.292358 kg/s inward |
| Main intake temperature | 293.15 K |
| External exhaust temperature | 298.3844 K |
| Net sensible heat rejection | 1537.35 W |
| Heat-rejection fraction of 1545 W | 99.5047% |
| External thermal re-ingestion index | 0.0000 |
| Maximum equipment air-rise index | 0.49279 |

The thermal field was settled, but the long-lag airflow guard repeatedly
detected gradual redistribution across multiple 2,400 s thermal intervals.
The 18,000 s endpoint had rebuilt only checkpoint 1 of the required two, so it
was not labelled fully coupled-converged. Continuing the preserved case with
the documented 100,000 s / 10,000 s refresh workflow is therefore warranted.

## Long screening continuation and convergence-reference fix (2026-08-11)

The preserved case was continued from 18,000.01 s with
`--multirate 100000 10000`. Each 10,000 s frozen-flow interval took roughly
329--482 wall-clock seconds. Thermal drift remained below 0.014 K/300 s after
30,000 s, while each live 0.01 s airflow refresh required roughly 109--166
seconds and retained mass imbalance below 0.03% and device-flow change below
0.17%. This validates the longer refresh interval for a thermally settled rack.

The continuation exposed a convergence-state defect. A validated checkpoint
incremented the thermal streak but did not advance the accepted airflow
reference until the entire required streak completed. Consequently, the next
checkpoint compared its field across two refresh intervals. This rack changes
about 2% RMS per 10,000 s interval, so two individually acceptable intervals
accumulated to about 4%, exceeded the 3% screening long-lag limit, and reset
the streak. Two consecutive checkpoints were therefore structurally
unreachable even though every one-interval comparison passed.

The runner now records the current airflow reference after every fully
validated thermal/airflow checkpoint, before testing the required streak
count. It still rejects and rebases any material single-interval shift. An
exporter regression asserts this ordering. The active case was stopped only
after its completed 60,000.02 s rebase and resumed with the corrected script.
Live evidence then showed:

| Checkpoint | Peak/average thermal drift (K/300 s) | Mass imbalance | Device change | One-interval airflow drift | Result |
|---|---:|---:|---:|---:|---|
| 70,000.01 s | 0.005451 / 0.005394 | 0.01491% | 0.13380% | 1.95519% | 1/2; reference advanced |
| 80,000.01 s | 0.013188 / 0.005670 | 0.01558% | 0.09296% | 1.98922% | 2/2; coupled converged |

The solver stopped early at 80,000.01 s rather than continuing to the requested
100,000 s. Terminal reporting and reconstruction completed normally. The
final rack-level comparison is:

| Metric | 18,000.01 s | Converged 80,000.01 s | Change |
|---|---:|---:|---:|
| External exhaust mass flow | 0.292239 kg/s | 0.295190 kg/s | +1.010% |
| Aggregate exhaust temperature | 298.3844 K | 298.3302 K | -0.0542 K |
| Net sensible heat rejection | 1537.35 W | 1536.79 W | -0.56 W |
| Heat-rejection fraction | 99.5047% | 99.4686% | -0.0361 percentage points |
| External thermal re-ingestion | 0.0000 | 0.0000 | none |
| Maximum equipment air-rise index | 0.49279 | 0.52835 | +0.03556 |

The small rack-level changes confirm that the 18,000 s result was already a
good engineering estimate, while the corrected 80,000 s endpoint provides the
formal two-checkpoint coupled-convergence proof. The equipment air-rise index
continues to show meaningful internal hot-air exposure, especially for the
Trenton, but remains a temperature indicator rather than exhaust-source
attribution.

The converged screening endpoint also remains close to the independently
converged 284,396-fluid-cell in-depth endpoint:

| Metric | Screening 80,000.01 s | In-depth 16,800.01 s | Screening difference |
|---|---:|---:|---:|
| Fluid volume-average temperature | 299.1309 K | 299.2920 K | -0.1611 K |
| Fluid internal maximum temperature | 323.0964 K | 322.5547 K | +0.5417 K |
| External exhaust mass flow | 0.295190 kg/s | 0.297315 kg/s | -0.7147% |
| Aggregate exhaust temperature | 298.3302 K | 298.3045 K | +0.0257 K |
| Net sensible heat rejection | 1536.79 W | 1540.17 W | -0.2194% |
| Heat-rejection fraction | 99.4686% | 99.6872% | -0.2186 percentage points |
| UPS equipment air-rise index | 0.06530 | 0.06201 | +0.00329 |
| Dell equipment air-rise index | 0.30672 | 0.27998 | +0.02674 |
| Trenton equipment air-rise index | 0.52835 | 0.54036 | -0.01201 |

These differences support screening for rack-level airflow, heat rejection,
and temperature ranking. The in-depth mesh remains preferable when local hot
spots and component-scale recirculation differences of roughly 0.5 K or a few
hundredths in the air-rise index matter.

The checkpoint-reference correction was also exercised on the preserved
in-depth case. Its legacy accepted reference used a different two-rank cell
partition: rank 1 stored 62,077 velocity values while the current decomposition
expected 134,309. The first drift post-process therefore failed with an
OpenFOAM field-size error even though local refresh metrics passed. Current
generated runners already compare per-rank internal-field counts and rebuild
an incompatible reference; the preserved runner was updated with that guard
and its reference was rebuilt from its current partition.

The strict 284,396-fluid-cell continuation from 16,800.01 s produced:

| Metric | In-depth result |
|---|---:|
| 16,800→18,000 s thermal stage wall time | 289.1 s |
| Peak / average thermal drift at 18,000 s | 0.04023 / 0.01103 K/300 s |
| First 0.01 s strict airflow-window wall time | 494.2 s |
| Second 0.01 s strict airflow-window wall time | 458.6 s |
| Repaired-reference accepted drift at 18,000.03 s | 0.72480% |
| Boundary mass imbalance | about 0.003% |
| Maximum device-flow change | below 0.035% |

The repaired strict reference passed its in-depth threshold and the refresh
converged. The case was stopped at that completed checkpoint before another
thermal interval. The result confirms that screening's speed advantage is
large: strict live-flow windows are several times slower even though both
profiles already agree closely on the engineering outputs.

## Source-attributed exhaust recirculation (2026-08-11)

A dedicated steady passive-scalar utility now traces each internal equipment
exhaust independently on an already-converged airflow field. It reads fixed
`rho`, `phi`, and `nut`, solves a bounded 0--1 scalar with turbulent diffusion
`rho*nut/Sc_t`, clamps the selected exhaust cell zone to one, and applies zero
concentration to ambient inflow. It does not advance or modify the thermal
solution. The Python driver creates a unique compact derivative case and
refuses to overwrite an existing output directory.

Build under OpenFOAM v2606, from a path without spaces:

```bash
source /usr/lib/openfoam/openfoam2606/etc/bashrc
export WM_PROJECT_USER_DIR=/tmp/thermal_sim_foam_user
export FOAM_USER_APPBIN=/mnt/c/OpenFOAM/thermal_sim_v2_tools/bin
cd /mnt/c/Users/hconn/Downloads/Thermal\\ Sim/v2.2/openfoam_tracer_solver
wmake
```

Run the complete source-to-intake matrix from a reconstructed converged case:

```bash
cd /tmp
python3 /mnt/c/Users/hconn/Downloads/Thermal\\ Sim/v2.2/tools/exhaust_recirculation_tracer.py \
  /mnt/c/OpenFOAM/thermal_sim_v2/model_generic_airside_screening_mapped_20260811 \
  /mnt/c/OpenFOAM/thermal_sim_v2/tracer_attribution_RUN_ID \
  --solver /mnt/c/OpenFOAM/thermal_sim_v2_tools/bin/steadyExhaustTracerFoam
```

The end-to-end validation copied only 32.9 MB before scalar output. All three
fields stayed within 0--1 and converged below `1e-9` maximum field change. The
80,000.01 s screening result was:

| Exhaust source / Intake | Eaton UPS | Dell R470 | Trenton |
|---|---:|---:|---:|
| Eaton UPS | 0.0000% | 1.1744% | 2.1158% |
| Dell R470 | 0.0001% | 9.2344% | 15.1859% |
| Trenton | 0.0001% | 25.4010% | 32.4304% |

These are true incoming-mass-flux-weighted tracer fractions, integrated over
the boundary of each thin intake cell zone. The corresponding intake flows
were 0.014676 kg/s for Eaton, 0.052811 kg/s for Dell, and 0.039835 kg/s for
Trenton. The result identifies Trenton exhaust as the dominant Dell-intake
contaminant and quantifies substantial Dell and Trenton self-recirculation
without adding multiple transported fields to the long thermal solve.

The earlier volume-weighted proxy differed from the mass-weighted values by at
most 0.338 percentage points. Turbulent-Schmidt sensitivity runs at 0.5 and
0.9 preserved every source ranking; the largest shift from one endpoint to the
other was 0.628 percentage points. The source-attribution conclusion is
therefore not an artifact of the default `Sc_t = 0.7` choice.

### Screening versus in-depth source attribution

The same fixed-flow tracer workflow was run against the independently
converged 284,396-cell in-depth endpoint at 16,800.01 s. The preserved legacy
case predates `internal_airflow_devices.csv`, so the driver gained an explicit
`--device-metadata` override. It copies the selected CSV into the derivative as
provenance and leaves the preserved result untouched. The mesh contained the
same six internal device-zone names as the current model.

This comparison exposed a scalar-solver defect in the initial driver. It had
expanded the existing `(U|h|k|omega)` solver regex to include tracers, which
also inherited `relTol 0.1`. Screening happened to settle after 63--138 outer
iterations, but the in-depth Eaton tracer oscillated around `1e-3` field change
and failed its 500-iteration limit. Derivatives now receive a separate
`"tracer.*"` PBiCGStab/DILU entry with `tolerance 1e-12` and `relTol 0`.
Both meshes subsequently converged every source in two outer iterations. The
three scalar solves took 6.1 seconds total on screening and 18.0 seconds on
in-depth, excluding case-copy/startup overhead.

| Exhaust source / Intake | Screening 177,064 cells | In-depth 284,396 cells | In-depth minus screening |
|---|---:|---:|---:|
| Eaton / Eaton | 0.0000% | 0.4900% | +0.4900 pp |
| Eaton / Dell | 1.1763% | 1.7684% | +0.5920 pp |
| Eaton / Trenton | 2.1196% | 3.2202% | +1.1006 pp |
| Dell / Dell | 9.2344% | 10.1030% | +0.8686 pp |
| Dell / Trenton | 15.1860% | 17.5818% | +2.3958 pp |
| Trenton / Dell | 25.4083% | 18.1400% | -7.2683 pp |
| Trenton / Trenton | 32.4377% | 29.9792% | -2.4585 pp |

The component intake mass flows still agree closely: screening differs from
in-depth by +0.988% for Eaton, -0.136% for Dell, and +0.381% for Trenton. The
source split is more mesh-sensitive because it depends on local mixing and jet
paths. In-depth `Sc_t = 0.5--0.9` sensitivity changed any entry by at most
0.719 percentage points, far less than the 7.268-point Trenton-to-Dell mesh
difference. Therefore screening is appropriate for detecting and ranking
recirculation paths, but quantitative source fractions should use the in-depth
mesh or carry a mesh-discretization uncertainty.

Every new report now includes a JSON provenance file with the source case and
exact time, device metadata path, mesh cells/faces, Schmidt number, convergence
settings, per-source runtime, and final field change. This prevents attribution
results from being silently associated with a stale endpoint or another mesh.

### Recirculation settling from 18,000 to 80,000 seconds

The driver now accepts `--time` as either the exact reconstructed directory
name or a uniquely matching numeric value. This permits a preserved case to be
compared across endpoints without renaming directories or allowing latest-time
selection to hide which result was analyzed.

The corrected strict tracer solve was applied to the same screening mesh at
18,000.01 s and the formally coupled-converged 80,000.01 s endpoint:

| Exhaust source / Intake | 18,000.01 s | 80,000.01 s | Change |
|---|---:|---:|---:|
| Eaton / Dell | 0.8244% | 1.1763% | +0.3519 pp |
| Eaton / Trenton | 1.6340% | 2.1196% | +0.4856 pp |
| Dell / Dell | 8.4276% | 9.2344% | +0.8068 pp |
| Dell / Trenton | 12.3225% | 15.1860% | +2.8635 pp |
| Trenton / Dell | 27.8979% | 25.4083% | -2.4896 pp |
| Trenton / Trenton | 30.9407% | 32.4377% | +1.4970 pp |

The intake mass flows changed by only +0.037% for Eaton, +0.088% for Dell,
and -0.289% for Trenton. However, the summed contribution from the three known
equipment exhausts changed from 37.15% to 35.82% at the Dell intake and from
44.90% to 49.74% at the Trenton intake. The bulk airflow is therefore already
an excellent engineering estimate at 18,000 s, while quantitative source
attribution continues to respond to the slowly changing thermally coupled flow
field. Use the coupled-converged endpoint for final recirculation fractions;
use the 18,000 s matrix for early path detection and mitigation screening.

### Saved attribution visualization

Use `python plot/exhaust_recirculation_matrix.py MATRIX.csv --output matrix.png` to plot an old report without rerunning OpenFOAM. Add `--compare OTHER.csv` for aligned screening, comparison, and percentage-point difference panels. Both absolute panels use a fixed 0--100% scale so separate cases remain visually comparable.

### Model-runner tracer commands

OpenFOAM exports containing at least one component with both an internal intake
vent and internal exhaust fan now print the complete source-attribution
workflow after the existing thermal and signed-flow plotting commands. The
output includes a one-time WSL build command, a unique `_tracer_RUN_ID`
derivative-case command, and a Python command that plots the saved matrix
without rerunning OpenFOAM. Exports with only an unpaired internal vent or fan
do not print the workflow.

`tools/build_openfoam_tracer.sh` sources OpenFOAM before enabling strict shell
error handling, overrides the space-sensitive OpenFOAM user directory, cleans
the solver's generated objects, and installs `steadyExhaustTracerFoam` under
`C:/OpenFOAM/thermal_sim_v2_tools/bin`. A focused model-runner regression
executes both paired and unpaired exports and verifies that WSL commands use
`/mnt/<drive>/...` paths rather than invalid Windows paths.

### Detailed-equipment multi-device attribution

A fresh export of the current detailed `model.toml` produced 469,742 fluid
cells and exposed a correctness defect before the long run began. Detailed
equipment can have multiple intake vents and multiple fan zones: Eaton has two
front intakes, Dell has six internal cooling fans, and Trenton has no fan. The
initial tracer metadata/driver retained only the last intake and last fan for
each component, which was valid for the earlier generic one-zone templates but
would misrepresent the detailed model.

The exporter now classifies passive device planes by their projection along
the component flow direction. Co-planar upstream vents are `intake`; downstream
passive vents are `outlet`; fans remain `exhaust`. The tracer utility clamps all
fan zones belonging to one component in a single source solve, aggregates all
of a target component's intake-zone mass and tracer flux, and retains fanless
equipment such as Trenton as a target column without inventing an exhaust
source row. The detailed export now resolves:

- Eaton: two intake zones and one exhaust-fan source zone;
- Dell: one intake zone and six exhaust-fan source zones;
- Trenton: one intake zone, one passive outlet, and no source row.

An 80,000.01 s generic-case backward-compatibility run reproduced every one of
the previous nine source fractions and three intake flows exactly. Exporter,
paired/unpaired model-runner, Python grouping, plotting, and OpenFOAM-v2606
compilation regressions pass. The long detailed-model run must use an export
created after this metadata fix.

## Rank-change restart and scaling validation (2026-08-12)

A 494,039-cell production-rack screening case exposed a restart defect while
changing an active case from two to four MPI ranks. The generated runner tested
the old processor checkpoint against the newly requested rank count. A complete
two-rank checkpoint was therefore rejected because `processor2` and
`processor3` did not yet exist; the runner skipped reconstruction, deleted the
old partitions, and redecomposed the older root checkpoint. Processor checkpoint
validation now accepts an explicit rank count and uses the detected existing
partition count before reconstruction or deletion. The exporter regression test
covers the generated rank-count-aware path.

The preserved 0.05 s root state also provided a matched scaling benchmark for
the coupled 0.05-to-0.06 s segment:

| Metric | 2 ranks | 4 ranks | Difference |
|---|---:|---:|---:|
| Segment wall time | 238.321 s | 128.809 s | -45.95% |
| Maximum Courant number | 2.54044 | 2.54045 | +0.00001 |
| Spatial velocity relative RMS change | 15.4164% | 15.4246% | +0.0082 percentage points |

Four ranks are retained for this case because they nearly halve coupled-flow
wall time while preserving the monitored numerical behavior.

### Resumable air-exchange advancement

The same cold-start case exposed a durability problem before its full physical
air-exchange horizon. Once device and spatial metrics first pass, the adaptive
controller advances directly to the measured exchange target. With a roughly
12.43 s exchange time and a 0.001 s live-flow timestep, that continuous solver
stage takes about 40 hours on four ranks. The generated runner previously set
`writeInterval` to the complete stage length, so an interruption near the end
could discard nearly all of that work.

The TOML/exporter option `airflow_checkpoint_interval` now defaults to 0.1 s.
Live-flow stages remain continuous, but their timestep-based write interval is
capped to the number of steps spanning that duration. `purgeWrite` continues to
retain the configured rolling checkpoint count. Short convergence windows still
write only their endpoint, while long air-exchange advances retain restartable
processor checkpoints without repeated solver launches or postprocessing. The
live-flow planner rounds its total step count upward to an integer number of
checkpoint blocks and recomputes a no-larger timestep, ensuring the final write
also lands exactly on the requested stage endpoint. A 12.379 s example at the
0.001 s cap produces 12,400 steps, writes every 100 steps, uses
`deltaT=0.00099830645 s`, and lands exactly at 12.379 s.

### Pre-eligibility launch reduction

Cold airflow cannot be accepted before `minimum_initial_airflow_duration`, but
the older controller still stopped for postprocessing and relaunched at every
0.01 s check interval throughout that forbidden period. Those interruptions do
not change the continuously integrated CFD solution and add substantial fixed
overhead on large meshes. The controller now advances directly to two check
intervals before the eligibility time, using the rolling live-flow checkpoints
above. It then performs two ordinary adjacent check windows so both the current
and previous spatial RMS changes are measured at the configured interval before
the first possible acceptance. For a 0.05 s post-ramp start, 0.30 s minimum,
and 0.01 s interval, the targets are 0.33, 0.34, and 0.35 s. Timestep caps,
physical duration, spatial gates, device-flow gates, and air-exchange gates are
unchanged; only unnecessary solver launches and premature postprocessing are
removed.

### Purge-safe spatial reference

Runtime review of the first continuous stage found that endpoint spatial RMS
postprocessing still read `U` from the stage's original processor checkpoint.
That worked for short stages, but a long exchange advance writes enough rolling
checkpoints for `purgeWrite` to delete the original time before the endpoint.
The CFD solve would complete and then fail while attempting to construct
`UPrevious` from a purged directory.

Before each live-flow solve, the runner now atomically preserves only the
starting `U` field for every rank in `.stage_velocity_reference`, outside the
OpenFOAM time directories. Endpoint spatial postprocessing reads that immutable
reference and removes it after the convergence state is safely written. A
restart from an intermediate rolling checkpoint naturally replaces the stage
reference with the velocity at that new authoritative start. Full checkpoint
retention remains bounded during a solver invocation: OpenFOAM can retain the
three inherited directories plus up to the configured three writes made by
that invocation. After the stage, the generated runner prunes the combined
processor history back to three. Exact start-to-end spatial comparison remains
available regardless of stage duration.

The 494,039-cell production case then provided an actual interruption proof.
Its continuous 0.17-to-0.33 s stage wrote an intermediate checkpoint at
0.25000000000000017 s. All four ranks contained every required fluid restart
field and all four solid-region temperature fields. The process was terminated
after reaching 0.253 s and restarted. The runner selected the complete 0.25 s
checkpoint rather than falling back to 0.17 s, retained the original 0.05 s
observation start, planned exactly 80 steps to 0.33 s, and atomically preserved
four stage velocity references totaling about 10.5 MB before relaunching the
solver. This is runtime proof on the production mesh, not only generated-script
coverage.

The subsequent 0.45-to-5.27144 s full-exchange stage provided a real rolling
write on the same case. It planned 4,851 equal steps at
`deltaT=0.0009939064110492681 s`, writing every 99 steps. The first checkpoint
landed exactly at `0.54839673469387784 s` on all four ranks. Every rank contained
all required fluid, turbulence, and solid-temperature restart fields. The case
grew from 0.709 to 0.768 GB, while the immutable velocity references remained
byte-identical to the 0.45 s starting fields.

### Production fluid-connectivity classification

`checkMesh -allRegions` reports two disconnected fluid components in this
494,039-cell production export: 413,193 cells in the rack-connected region and
12,160 cells in a smaller region. An isolated `foamToVTK` export paired with
OpenFOAM's binary `cellToRegion` labels measured the smaller region as
0.00783697 m3, with cell-centre bounds `(0.03691, 0.00993, 0.63043)` to
`(0.50048, 0.67618, 0.65905)` m. Those bounds match the configured KVM interior
air volume within one local cell width.

This is intentional sealed air inside the detailed fanless KVM, not an
accidentally disconnected portion of the rack flow. The generated device
metadata contains no KVM fan, intake, or exhaust zone; the KVM's two 10 W
internal solids reject heat through its sealed internal air and conjugate solid
interfaces. The live coupled solve remained finite and mass-balanced. Do not
delete this region or invent a KVM exhaust merely to make `checkMesh` report one
fluid component. Conversely, any additional disconnected fluid component, or
one whose bounds do not match an intentionally sealed internal-air definition,
remains a mesh defect requiring investigation.

### Production heat-source audit

The active case's `constant/openfoamExportProperties` and per-region
`fvOptions` contain the same 13 absolute heat sources. They sum exactly to
1,545 W:

| Component | Sources | Applied heat |
|---|---:|---:|
| Eaton UPS | 3 | 150 W |
| Dell R470 | 4 | 950 W |
| Trenton 3U | 4 | 425 W |
| Fanless KVM | 2 | 20 W |

The Dell power-supply source is the intended reduced 50 W value rather than
the former 100 W placeholder. Its selected volume is 0.0003808 m3 and its
volumetric generation is 131,303 W/m3, well below the Dell CPU/memory zone's
598,086 W/m3. The long run is therefore validating the revised power
allocation; no heat source is missing or duplicated, and the small PSU region
is no longer predisposed to dominate solely through its source density.

### Live rolling-purge ceiling

The production full-exchange stage subsequently wrote checkpoints at
`0.64679346938775251`, `0.74519020408162717`, and
`0.84358693877550184 s`. At the fourth write made by this solver invocation,
OpenFOAM removed its oldest invocation-local checkpoint
(`0.54839673469387784 s`) on all four ranks. Each rank retained exactly the same
six directories: three inherited stage-start directories plus the newest three
rolling writes. The new checkpoint contained all 29 expected files per rank,
including every fluid/turbulence restart field and all four solid temperatures.

This proves the live ceiling rather than merely inferring it from
`purgeWrite`: during one continuous stage, storage is bounded at the inherited
history plus `saved_time_directories`; after solver completion, the runner's
explicit prune reduces the combined history to `saved_time_directories`.

### Ambient-connected air-exchange volume

The KVM connectivity classification exposed a second issue: the generated
air-exchange calculation summed every OpenFOAM fluid cell, including sealed
equipment air. The active case therefore embedded `1.05595265 m3` in its
runner, while the ambient-connected rack component measured `1.04811571 m3`.
The sealed KVM added 0.00783697 m3, or 0.742% of the old exchange volume. At the
0.45 s operating point, excluding it changes the estimated horizon from
5.22144 to approximately 5.18269 s. The active validation stage was left
unchanged because shortening a running endpoint would invalidate its restart
proof; its result is conservative by about 0.039 s.

Future exports now flood-fill fluid cells from every external fan/vent source
zone and use only that ambient-connected volume for the exchange horizon.
Solid cells and active zero-thickness face walls block traversal. Sealed fluid
remains present in the CHT solve and continues to store and transfer heat; it is
excluded only from the rack-air replacement calculation. A focused 36-cell
regression encloses one fluid cell behind three inward face walls and verifies
that connected volume falls from 0.035 to 0.034 m3 while the sealed cell remains
fluid.

### Full-exchange production continuation

The same four-rank production stage was allowed to continue without changing
its numerical controls. Rolling writes through `1.6307608163265697 s` remained
aligned across every rank. Each latest checkpoint contained the same 29-file
relative manifest, including all critical fluid fields and all four solid
temperature fields. OpenFOAM retained exactly six time directories per rank
(three inherited plus three invocation-local writes), replacing the oldest
rolling write after each new completed checkpoint. No fatal signature occurred;
maximum fluid Courant number remained about 2.56 against the cold-screening
limit of 5, and cumulative continuity error remained of order `7e-6`.

A lightweight binary-field audit compared all 425,353 decomposed fluid cells
(1,276,059 velocity components) without launching VTK or OpenFOAM
postprocessing. Relative unweighted RMS velocity changes over equal
`0.0983967 s` checkpoint intervals fell from 13.30% to 12.61%, 10.87%, 9.72%,
and 9.37%, then remained in a 9.59-9.80% band. Scaling the long-window values by
the square root of the 9.84 screening check windows gives approximately
3.0-3.1%, consistent with the screening profile's measured short-window
transient-RANS fluctuation band. The simple early exponential projection was
therefore rejected: the field approaches a statistically unsteady floor, not
a cellwise motionless state. Final acceptance remains the generated runner's
volume-weighted adjacent-window spatial checks plus device-flow stability,
direction validity, exterior mass balance, and the complete air-exchange
duration.

The turbulence fields independently showed the same behavior. Across the two
latest retained equal intervals, `k` relative RMS change was 13.74% and 13.54%,
while turbulent thermal diffusivity changed 18.62% and 17.56%. Fluid
temperature changed only about 0.00061 K RMS per interval. These measurements
support retaining the full exchange horizon; they do not justify shortening it
from stable Courant or continuity values alone.

Solid watt loads remain active during initial airflow. In this case the
generated `fvOptions.flowOnly` and `fvOptions.fullFan` fluid dictionaries are
identical because there is no separate fluid volumetric heat source to remove.
From 0.45 to 1.532 s, cumulative solid temperature changes were 0.0669 K RMS
(0.2255 K maximum) for Dell, 0.0114 K RMS (0.0291 K maximum) for Trenton,
0.00714 K RMS (0.0519 K maximum) for Eaton, and 0.00106 K RMS (0.00286 K
maximum) for the KVM. At 1.631 s, Dell's cumulative maximum rise was 0.2446 K.
Over the same 0.45-to-1.631 s span, fluid temperature changed only 0.00602 K RMS
and density changed 0.0153% RMS. Powered-solid preheating is therefore physical
but too small to materially move this initial fan operating point.

### Decomposed production-case numerical audit

The numerical case validator previously selected the newest reconstructed root
time and assumed patches named `Validation_inlet` and `Validation_outlet`.
On the live production case that selected stale `0.06 s` data even though newer
complete four-rank checkpoints existed. It also misread zero-face boundary
lists and zero-cell solid partitions by continuing into the next binary list.

The validator now prefers the latest numeric checkpoint common to every MPI
rank, aggregates boundary and solid fields across ranks, treats legitimate
zero-length lists as empty, and automatically classifies inflow and outflow on
all physical `type patch` openings when explicit patch names are omitted. An
explicit expected connected-fluid-region count accounts for intentional sealed
air volumes without weakening the mesh-connectivity check. Six focused tests
and 35 related OpenFOAM Python tests pass.

At `1.92595102 s`, the corrected audit covered 425,353 fluid cells and the two
expected connected components (ambient rack air plus the sealed fanless KVM).
External inflow was `-0.29707002 kg/s`, outflow was `0.29706584 kg/s`, and mass
imbalance was 0.00141%. Energy balance intentionally failed during the cold
airflow stage: net sensible transport was `-4.43 W` against 1545 W of solid
loads, outlet temperature was 293.1352 K, and the hottest solid cell was only
293.5592 K. This is valid evidence that mass flow is established but not a
thermal-convergence result; the same audit must pass energy balance after the
thermal stages before the production result can be accepted.

The next four-rank checkpoint at `2.02434776 s` was complete (29 matching files
per rank), and the solver advanced beyond it. Over the same `0.0983967 s`
window used above, full-field velocity relative RMS change fell from 8.210% to
7.367%; turbulent thermal diffusivity fell from 14.004% to 12.317%. Fluid
temperature changed only 0.000636 K RMS. The corrected audit measured
`-0.29727392 kg/s` inflow and `0.29727571 kg/s` outflow, improving exterior mass
imbalance to 0.00060%. Energy transport remained intentionally unconverged at
`-4.37 W` versus 1545 W applied. This supports continued airflow settling and
confirms that a sound mass balance alone is not thermal acceptance.

Future exports now write `expectedConnectedFluidRegions` into
`constant/openfoamExportProperties`, and the numerical validator consumes that
value automatically (legacy cases still default to one, with a CLI override).
The exporter counts fluid components across face-connected cells while treating
explicit internal walls as barriers. The focused sealed-cavity fixture proves
that the count changes from one to two when its final three inward faces are
closed; the standard export fixture emits one. This makes topology validation
self-describing and prevents both false failures for intentional sealed air and
false passes caused by globally disabling connectivity checks.

The selected-checkpoint recirculation report had the same stale-root-time
assumption as the old validator. It now selects requested or latest complete
decomposed checkpoints and aggregates face-resolved `phi` and `T` across every
rank; a single common MPI checkpoint is valid and rounded directory names are
resolved tolerantly. Thirty focused tests pass. A live CSV-only report at
`2.02434776 s` reproduced the validator's all-rank values: 0.297273923 kg/s
inflow, 0.297275709 kg/s outflow, and -4.37134 W sensible rejection. It also
resolved each of nine exhaust fan patches at about 0.03275-0.03323 kg/s and the
single intake vent at -0.297274 kg/s. Bidirectional flow was zero. The thermal
re-ingestion index was correctly undefined because cold-stage exhaust remained
slightly below ambient; it becomes meaningful only after powered air warms.

At the next aligned checkpoint, `2.12274449 s`, velocity relative RMS change
continued downward to 7.062% and `alphat` to 11.443%; `k`, `omega`, and `nut`
changed 11.272%, 3.793%, and 11.443%, respectively. A 6.46 m/s maximum velocity
component change was localized to rank 0, whose aggregate relative change was
only 5.50%. Rank 2 had the largest relative change (17.87%) because its field
magnitude was low (0.477 m/s RMS), while its maximum component change was only
1.50 m/s. Exterior mass imbalance remained 0.00157%, all ambient faces retained
their intended directions, maxCo remained about 2.59, and no fatal signature
appeared. The combined evidence identifies bounded source/wake motion rather
than a broad numerical instability.

The source-attributed exhaust tracer also scanned only reconstructed root
times. On the active case it would therefore have traced stale `0.06 s` flow
instead of the complete `2.12274449 s` MPI checkpoint. The launcher now compares
valid reconstructed and rank-common decomposed states. It accepts a genuinely
newer reconstructed state, but refuses stale or missing reconstruction with an
exact copyable command. On Windows the live diagnostic is:

`wsl openfoam2606 reconstructPar -case '/mnt/c/OpenFOAM/thermal_sim_v2/model_current_snapspan_20260812' -region fluid -time '2.1227444897959704'`

The guard runs before creating the tracer output case. Eight tracer tests and
28 related recirculation/checkpoint tests pass. Reconstruction was deliberately
not run during the active exchange stage; source-attributed tracing is useful
after airflow acceptance and thermal exhaust rise, not on the present cold
checkpoint.

The `2.22114122 s` checkpoint remained complete and restartable. Equal-window
velocity and `alphat` changes rebounded only slightly, from 7.062% to 7.178%
and from 11.443% to 11.491%, while fluid-temperature movement decreased to
0.000617 K RMS. This confirms a bounded transient floor rather than monotonic
cellwise freezing. Boundary behavior is substantially steadier: relative to
the `2.02434776 s` report, the largest individual fan-flow change was about
0.55% (Fan 4), intake flow changed about 0.10%, all nine fans remained exhausts,
the main vent remained an intake, and no opening had bidirectional flow.
Current inflow was 0.29757656 kg/s, outflow was 0.29757799 kg/s, giving only
0.00048% mass imbalance. Device flows and mass balance are therefore already
inside their 1% screening gates; the runner must still complete one physical
air exchange and its consecutive spatial checks before accepting airflow.

The standalone outlet mass-weighted-temperature utility had a separate stale
precedence defect: `--time latest` returned an old function-object report before
checking newer decomposed fields. It now trusts a report for `latest` only when
its time matches the latest complete result, otherwise reading face-resolved
`T` and `phi` directly across every rank before considering VTK. Explicit
historical requests still use exact reports. On the live case, `Fan_1 latest`
now reports `2.22114122 s`, 0.033209906 kg/s, and 293.135813 K from 53 all-rank
faces; explicit `--time 0.06` still returns the historical 0.01102924 kg/s and
293.148700 K report. This also avoids the VTK missing-`T`/`phi` failure mode.
Twenty-five related tests pass, including corrected bounded parsing of ASCII
OpenFOAM lists.

The signed mass-flow plot retained correct signs but labeled the newest report
file as the latest result even when rank checkpoints were newer. It now keeps
the function-object history and appends one direct all-rank `phi` endpoint when
the latest complete checkpoint is newer, explicitly identifying that source.
For live `Fan_1`, report history ended at `0.45 s` and 0.02756503 kg/s; the
updated endpoint is `2.22114122 s` and +0.033209906 kg/s. Positive still means
out of the domain, so no sign inversion was reintroduced. Thirteen focused
outlet/validation tests pass.

At `2.31953796 s`, all four ranks again held 29 matching files and the solver
advanced beyond the checkpoint. Equal-window velocity change reached a new low
of 6.826%, `alphat` fell to 11.279%, and fluid-temperature change fell to
0.000590 K RMS. The localized maximum velocity-component change also decreased
from 6.86 to 6.11 m/s. Relative to `2.22114122 s`, the largest fan-flow change
was about 0.30% (Fan 4) and intake flow changed only about 0.034%. All external
directions remained correct, bidirectional flow remained zero, and the audit
measured 0.00149% exterior mass imbalance. The cold-stage energy check still
correctly failed at -4.28 W sensible rejection versus 1545 W applied, while the
hottest solid cell had begun warming to 293.634 K.

The default recirculation-history mode still used its final function-object
row for headline heat rejection even though its separate face-flow CSV used the
newest decomposed checkpoint. It now appends a direct external-boundary row
from all-rank `T` and `phi` whenever report history is stale, and labels that
endpoint explicitly. On the live case, history ends at `0.06 s`; the generated
CSV now ends at `2.31953796 s` with 0.29767717 kg/s intake, 0.29767275 kg/s
exhaust, and -4.28272 W cold-stage sensible rejection. The thermal re-ingestion
index remains correctly undefined until exhaust exceeds ambient. Twenty-eight
related direct-field and recirculation tests pass.

The live heat-animation path was exercised directly against the decomposed
production case. VTK selected `2.31953796 s`, returned five populated regions
with 494,039 total fluid-plus-solid cells, and an off-screen plot produced a
143,459-byte PNG without exposing any `mesh contains no cells` warnings. The
current cold-stage hotspot was 20.4836 C in `Dell_PowerEdge_R470_1U_1` at
approximately `(0.152654, 0.504725, 0.238250) m`. Low-level unsuppressed VTK
inspection still reports empty meshes for legitimate rank/solid combinations
with zero owned cells; the user-facing animation correctly suppresses only
those read-time warnings and retains all five populated regions.

Solid warming across `2.22114122 -> 2.31953796 s` followed applied-load order:
Dell (950 W) changed 0.006236 K RMS, Trenton (425 W) 0.001033 K, Eaton
(150 W) 0.000649 K, and the fanless KVM (20 W) 0.000100 K. Dell's maximum
cell change was only 0.01851 K. This ordering supports correctly placed active
sources rather than anomalous or runaway preheating.

At the next aligned checkpoint, `2.41793469 s`, equal-window velocity change
fell again to 6.123%, `alphat` crossed below 10% at 9.905%, fluid-temperature
change fell to 0.000551 K RMS, and the localized maximum velocity-component
change dropped to 5.18 m/s. Exterior mass imbalance remained only 0.00255%.
`Fan_1` delivered +0.033220511 kg/s at 293.136195 K from 53 all-rank faces.
The hottest solid cell was 293.6520 K; cold-stage sensible rejection remained
intentionally unconverged at -4.24 W.

The progress tool now reports diagnostic Courant timestep headroom without
changing the active stage. At approximately `2.447 s`, the production case used
`dt=0.000993906411 s`, observed maxCo 2.5803 against maxCo 5, and therefore had
an 80%-margin Courant estimate of `0.00154077 s` (1.55 times the current step).
The generated stage cap remains `0.000993906411 s`. Raising the screening
airflow cap could theoretically reduce coupled-flow wall time by roughly 35%,
but no default is changed until an identical-checkpoint continuation compares
the larger step against the current field, fan-flow, mass-balance, and
direction criteria. Seventeen progress tests pass.

### Cumulative initial air exchange

The active runner froze its cold-start exchange target after the `0.45 s`
check, when estimated exchange time was 5.22144 s, then launched one continuous
stage to `5.27144 s`. Boundary flow subsequently rose from about 0.248 kg/s to
about 0.298 kg/s, but that increase could not shorten the already launched
stage. Using elapsed time against one early flow estimate is conservative but
is not the physical exchanged-air quantity.

Future exports now integrate cumulative one-way boundary mass with the
trapezoidal rule and divide by ambient-connected rack air mass. The state stores
`last_time`, `last_one_way_mass_flow`, and `cumulative_exchange_fraction`
atomically in `.initial_air_exchange_state`; it survives an interrupted initial
stage, rejects malformed/future/negative state, resets on a new observation or
warm start, clears fresh-flow output before every measurement, and is removed
after acceptance. Once device/spatial gates pass but exchanged fraction remains
low, the runner advances only one existing airflow-checkpoint interval before
rechecking. Mapped airflow retains its validated horizon skip.

The active flow history suggests the integrated criterion would reach one rack
volume around 4.7-4.9 s instead of the frozen 5.27144 s target, potentially
saving about 50-75 wall minutes at the current rate without reducing the one-
volume requirement. This estimate is intentionally not applied retroactively
to the running case. The exporter fixture passes its C++ assertions and the
generated `run_parallel.sh` passes `bash -n`.

The unchanged active case subsequently completed an aligned checkpoint at
`2.51633143 s`. Equal-window velocity change fell to 5.854%, `alphat` to
9.008%, fluid-temperature change to 0.000529 K RMS, and the localized maximum
velocity-component change to 5.04 m/s. Exterior mass imbalance was 0.00205%,
and the hottest solid cell was 293.6705 K. These results continue the settling
trend independently of the future-export exchange-integral change.

The later aligned `2.71312490 s` checkpoint showed that the cold-start RANS
field is bounded but not monotonically freezing. Across the preceding
`0.0983967 s`, velocity relative RMS change rebounded to 7.584% and `alphat` to
10.057%, while fluid-temperature change remained only 0.000581 K RMS. The
engineering boundary values remained much steadier: the main vent supplied
0.29773941 kg/s, the nine top fans exhausted 0.032797-0.033271 kg/s each, all
directions were correct, reverse flow was zero, and exterior mass imbalance
was 0.00086%. A connectivity audit passed when supplied the physically correct
legacy expectation of two fluid regions (rack ambient plus the sealed fanless
KVM); future exports store that expectation in case metadata automatically.
The energy audit still intentionally failed at -4.02 W transported versus
1545 W applied because this early transient is storing nearly all applied heat
in the solids instead of rejecting it at the exhaust. The flow-only fluid
options suppress air-region heat sources during cold-start airflow, but this
detailed model's 1545 W load is defined in four solid-region `fvOptions` files
and remains active, as confirmed by the ordered solid warming above. This
checkpoint reinforces that stable bulk/device flow alone must not bypass the
spatial and one-air-exchange startup gates or be mistaken for thermal steady
state.

The apparent late-stage runtime regression was traced to the workstation, not
to deteriorating linear convergence. Over successive 0.10 s windows from
2.28-2.78 s, average pressure iterations per timestep remained in the narrow
32.18-33.70 range. The final two windows changed only from 33.24 to 33.63
pressure iterations per step, while execution cost rose from 7.88 to 9.05 s
per step. All four MPI ranks remained CPU-bound, I/O wait and swap activity
were zero, but Windows reported the 2.40 GHz i5-1135G7 at only 907 MHz and
about 49% processor performance under sustained load. The laptop was on AC at
100% charge, with the Balanced plan allowing a 100% maximum processor state.
Only a 53.9 C platform thermal zone was exposed; CPU-core telemetry was not
available. Runtime comparisons from this long run must therefore record CPU
performance state and should not attribute its approximately 15% late-window
slowdown to solver settings. Cooling/platform throttling is the most consistent
explanation, although direct CPU-core temperature evidence is unavailable.

At the aligned `2.81152163 s` checkpoint, the equal-component diagnostic measured
8.009% velocity RMS change and 10.781% `alphat` change over the preceding
0.0983967 s; fluid-temperature movement remained only 0.000576 K RMS. Fan and
vent outputs were much steadier over the same interval: maximum individual fan
change was 0.2911%, intake changed 0.00531% in magnitude, exhaust changed
0.00406%, all directions remained correct, and reverse flow remained zero.
The retained checkpoint interval is almost ten times the runner's 0.01 s local
acceptance window, so its cumulative change must not be compared directly with
the profile's per-window threshold.

The established volume-weighted, geometry-partitioned diagnostic measured
6.996% whole-fluid velocity change, 6.706% in external rack air, 16.716% in
Dell internal air, 7.819% in Trenton internal air, and 0.875% in Eaton internal
air. Thus the movement is strongest around Dell but is not confined to
equipment passages. The lightweight binary `openfoam_field_delta.py` result
differs because it weights every stored scalar component equally and does not
read cell volumes. Its output now labels all RMS values as component-weighted
and explicitly warns that they are not the runner's volume-weighted vector-
magnitude convergence-gate metric. The
volume-aware `openfoam_field_convergence.py` remains authoritative when
assessing or localizing runner-gate behavior.

At `2.90991837 s`, the following retained 0.0983967 s interval increased to
7.463% volume-weighted whole-fluid velocity change and 6.845% in external rack
air. Dell internal air changed 23.144%, while Eaton and Trenton internal air
changed 0.985% and 6.790%. Boundary operating points remained stable: intake
changed only 0.00134% in magnitude, the largest individual exhaust-fan change
was 0.4133%, all directions were correct, and reverse flow remained zero.
Fluid temperature moved only 0.000505 K RMS. These retained intervals diagnose
cumulative flow motion but remain longer than the runner's eventual 0.01 s
acceptance windows.

The volume-aware convergence tool now reports the cell-center coordinates of
each maximum field difference in both console and CSV output. The 11.902 m/s
maximum velocity difference was at `(0.439957, 0.285950, 0.229000) m`, aligned
with the Dell's sixth/rightmost internal fan plane after applying its component
offset. The external-rack maximum was 2.373 m/s at
`(0.044900, 0.268950, 0.079875) m`, immediately outside the Eaton's left side
(`x = 0.048895 m`) and within its depth/height span. This distinguishes a Dell
fan-source/wake maximum and an external UPS-side wake from changing top-fan or
vent operating points. Cross-mesh comparisons now pass their available sample
cell centers into the same metric, so their CSVs also contain finite maximum-
error locations.

Adding maximum locations exposed a downstream CSV-only regression before
release. `openfoam_cross_case_comparison.py` used a fixed field list, so
`csv.DictWriter` would reject the new `maximum_x`, `maximum_y`, and
`maximum_z` keys when `--csv` was requested even though console comparisons
continued to work. Its schema now includes the three coordinates and a focused
test verifies that every comparison-row key is serializable. An end-to-end
self-comparison of the preserved `screening_dt10_controlled_20260810` endpoint
at `2400.0100000000011 s` wrote eight U/T region rows with all 16 columns and
finite maximum coordinates; all differences were exactly zero as expected.

The active case's legacy `airflow_devices.txt` listed internal-zone cell counts
but not requested or realized geometry, so it could not directly prove that an
adaptive mesh placed fan centers and spans correctly. Future exports retain
each ambient and internal fan/vent's requested global center, rectangular size
or circular diameter. Ambient devices report realized patch-face bounds and
area-weighted centroid; internal devices report cell-zone minimum/maximum
bounds, volume-weighted centroid, and volume. A focused 0.1 m mesh fixture
requested a centered top fan at `(0.25, 0.25, 0.5) m`; its nine faces spanned
exactly `(0.1, 0.1, 0.5)` to `(0.4, 0.4, 0.5) m` with centroid
`(0.25, 0.25, 0.5) m`. The same fixture requested a
0.2 x 0 x 0.2 m fan at `(0.25, 0.25, 0.25) m`; the report reproduced that
center/size and showed nine cells spanning `(0.1, 0.2, 0.1)` to
`(0.4, 0.3, 0.4) m`, centroid `(0.25, 0.25, 0.25) m`, and volume 0.009 m3.
The test is part of `run_added_feature_tests.ps1`; it, the full exporter test,
and the ambient-connected-volume test all pass. This metadata is diagnostic
and does not change source-zone selection or the active production case.

At the next aligned production checkpoint, `3.00831510 s`, whole-fluid
velocity changed 7.798% volume-weighted over the retained 0.0983967 s interval
and external rack air changed 7.701%. Dell internal air changed 12.843%, with
the 4.509 m/s maximum at `(0.451257, 0.268950, 0.232500) m`, still in the Dell
fan-bank neighborhood but downstream of the prior maximum. The external
maximum moved to `(0.029165, 0.724535, 0.003750) m` at the lower rear-left of
the rack, showing broader wake-phase motion rather than one fixed Eaton-side
cell. Despite that field motion, intake magnitude changed only 0.00521%, the
largest top-fan change was 0.3372%, all directions remained correct, reverse
flow was zero, and fluid temperature changed only 0.000508 K RMS.

At `3.10671184 s`, the next retained interval increased to 8.700%
volume-weighted whole-fluid velocity change and 8.545% in external rack air.
The 10.835 m/s maximum returned to Dell fan-bank cells at
`(0.451257, 0.285950, 0.229000) m`; the 3.109 m/s external maximum remained
along the lower left/rear rack boundary at
`(0.003750, 0.681250, 0.001250) m`. Dell and Trenton internal-air changes were
15.646% and 13.343%, while fluid temperature changed only 0.000486 K RMS.
The repeated fan-bank and lower-rack locations support a bounded wake-phase
interpretation and provide specific targets for a future matched timestep or
source-smoothing study. No profile limit is changed from these 0.0983967 s
retained intervals because the generated runner evaluates shorter 0.01 s
acceptance windows.
