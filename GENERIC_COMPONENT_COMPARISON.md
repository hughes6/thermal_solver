# Generic-component rack comparison

Date: 2026-08-05

## Scope

`library/models/model_generic_components.toml` preserves the production rack,
component positions, roof fans, main vent, and total component heat loads. It
replaces the detailed Eaton, Dell, Trenton, and KVM internals with one air
region, one heat block, one intake vent, and one exhaust fan per component.

The generic component fan data are provisional because measured device
pressure rise and mass flow were unavailable. For this first comparison, flow
targets were sized for a 10 K device air-temperature rise and fitted through an
assumed 80 Pa operating point. These assumptions are not validation data.

## Execution

- Profile: `screening_foam_cfg.toml`
- Detailed screening mesh: 232,140 cells
- Generic screening mesh: 166,428 cells
- Cell-count reduction: 28.3%
- Initial coupled airflow checkpoint: 0.05 s
- Thermal-only checkpoint 1: 18,000.05 s
- Thermal-only checkpoint 2: 100,000.05 s
- Direct implicit thermal-only timestep: 1,000 s

The adaptive coupled initialization did not meet its flow-change criterion and
required roughly three minutes per 0.01 s airflow step. It was stopped at a
valid field checkpoint so the requested thermal comparison could proceed. The
resulting flow field is therefore provisional, not converged.

## Temperature comparison at 100,000 s

| Component | Detailed average (K) | Generic average (K) | Difference (K) | Generic difference | Detailed max (K) | Generic max (K) |
|---|---:|---:|---:|---:|---:|---:|
| Dell R470 | 370.607 | 385.731 | +15.124 | +4.08% | 402.372 | 515.378 |
| Eaton UPS | 324.948 | 347.771 | +22.823 | +7.02% | 391.624 | 475.589 |
| Trenton 3U | 476.934 | 493.580 | +16.646 | +3.49% | 569.157 | 658.309 |

The generic averages are reasonably close for an uncalibrated first model, but
the generic maxima are 84-113 K higher. The heat-block maximum is not a real
chip temperature and must not be used as an equipment limit.

## Airflow comparison

| Metric | Detailed refreshed case | Generic provisional case | Difference |
|---|---:|---:|---:|
| Main rack intake | -0.208630 kg/s | -0.153237 kg/s | -26.5% magnitude |
| Ambient mass imbalance | -0.00000813 kg/s | +0.017081 kg/s | unacceptable |
| Roof fan range | 0.02263-0.02374 kg/s | 0.02290-0.02337 kg/s | similar |

The generic rack imbalance is approximately 11.1% of main-intake magnitude,
well above the configured 1% criterion. The generic airflow result and its
temperature field are not validated until the equivalent device curves are
calibrated and the coupled airflow stage reaches mass and device-flow
convergence.

The first comparison also incorrectly assigned an equivalent fan to the KVM.
The physical KVM is fanless. Its generic file now uses passive front and rear
vents, and the selective production experiment retains the original KVM
component. Results above are preserved as failure evidence and must not be
used as the corrected KVM result.

## Corrected startup-airflow convergence study (2026-08-06)

A controlled 166,093-cell generic case used the same exported geometry, heat
loads, fan curves, and OpenFOAM properties as its baseline. The coupled
airflow timestep remained 0.001 s. Only the post-ramp minimum duration was
extended from 0.02 s to 0.30 s so the complete transient operating-point
history could be observed.

The former rule accepted at 0.16 s while flow was still evolving. The extended
case recorded its first complete all-device baseline at 0.35 s and compared it
with 0.36 s:

- boundary mass imbalance: 0.257%;
- maximum tracked device-flow change: 0.852%;
- all intake/exhaust direction checks: passed;
- nine roof-fan flows: 28.9-30.1% higher than the old 0.16 s values;
- main-intake magnitude: 34.7% higher;
- passive KVM intake magnitude: 14.7% higher.

The corrected 0.36 s roof-fan and main-intake flows already matched the old
case's much-later 16,800 s refreshed state. Internal device fans also showed
why adjacent-window change alone is unsafe: Dell flow first rose, then slowly
reversed while individual 0.01 s changes were already below 1%.

The supplied profiles now require at least 0.30 s of post-ramp airflow, retain
the validated 0.001 s airflow timestep, and require no more than 1% change in
tracked device flow. A tested 0.002 s timestep was rejected because rack-fan
flows diverged from the 0.001 s reference by approximately 13% at equal
physical time and by approximately 17% at each case's former acceptance point.

## Screening mesh determinant study (2026-08-06)

The normal 20 mm screening mesh and an isolated 10 mm fine-spacing candidate
were prepared and checked without running competing solvers:

| Region | 20 mm cells | 20 mm low-det. | 20 mm fraction | 10 mm cells | 10 mm low-det. | 10 mm fraction |
|---|---:|---:|---:|---:|---:|---:|
| Fluid | 146,079 | 2,566 | 1.76% | 672,600 | 4,086 | 0.61% |
| Eaton UPS | 4,718 | 792 | 16.79% | 24,188 | 0 | 0.00% |
| Dell 1U | 5,840 | 2,700 | 46.23% | 21,392 | 9,024 | 42.18% |
| Trenton 3U | 6,748 | 0 | 0.00% | 32,064 | 0 | 0.00% |
| KVM 1U | 2,708 | 1,846 | 68.17% | 14,450 | 9,890 | 68.44% |

The 10 mm candidate increased total cells from 166,093 to 764,694 (4.60x)
and required 148 s just to split regions, create sets, and run mesh checks.
It reduced the fluid failure fraction and fixed the 2U UPS, but did not fix the
thin Dell/KVM solid topology. Those persistent solid warnings come from
one-cell-thick shell regions lacking internal/coupled faces in every direction,
not from skewness, negative volume, non-orthogonality, AMI mismatch, or device
cell-zone overlap. Global 10 mm refinement is therefore not accepted as a
cost-effective correction; thin-shell representation and a targeted fluid
refinement strategy require separate validation.

## Geometry-cut and profile-invariance correction (2026-08-06)

The determinant study exposed a more serious issue than the warnings: with
the same 20 mm fine spacing, changing only coarse spacing/refinement margin
changed represented Dell, Trenton, and KVM volumes. The KVM differed by almost
50%. Two independent defects caused this:

1. sorted coordinate deduplication allowed lower-priority internal or
   refinement-band planes to displace a component envelope;
2. cumulative floating-point coordinates made an exact maximum appear just
   beyond its mesh face, so the stamper included the next profile-dependent
   cell layer.

The planner now gives rack bounds and component envelopes semantic priority,
retains cuts exactly at the 5 mm anti-sliver threshold, and maps merged
coordinates to the nearest retained face. The final independently exported
and prepared screening/in-depth meshes produced identical component results:

| Region | Screening cells | In-depth cells | Screening volume (m^3) | In-depth volume (m^3) |
|---|---:|---:|---:|---:|
| Eaton UPS | 3,726 | 3,726 | 0.0134536 | 0.0134536 |
| Dell 1U | 5,167 | 5,167 | 0.0102842 | 0.0102842 |
| Trenton 3U | 6,438 | 6,438 | 0.0176997 | 0.0176997 |
| Fanless KVM | 3,657 | 3,657 | 0.0063702 | 0.0063702 |

The generic KVM originally used a 3.556 mm air inset, below the standard
profile's 5 mm cut threshold. Its enclosure disappeared and allowed lateral
fluid bypass. It now uses a 5 mm effective mesh-resolved enclosure. This is a
rack-level closure model, not a claim about physical sheet-metal gauge. The
exact enclosure cuts raise the final screening mesh to 204,136 cells, still
only 26.7% of the rejected 764,694-cell global-10-mm mesh.

That 204,136-cell result also revealed that the former screening settings were
counterproductive: their 200 mm coarse target and 5 mm refinement halo forced
extra transition-smoothing splits around the resolved enclosure planes. The
100 mm/20 mm in-depth layout contained only 167,232 cells with identical
component zones and volumes. Screening now uses that same mesh layout (18.1%
fewer cells than the former screening mesh); its speed advantage comes from
thermal timestep, refresh, and reporting policy rather than different
geometry.

`checkMesh` continues to report low determinant cells in one-cell-thick solid
shells (with positive volumes, zero non-orthogonality, negligible skewness,
and valid interfaces). Those warnings are retained as a known topology
limitation; global refinement did not remove the Dell/KVM fractions and is not
used as a substitute for thermal/flow validation.

## Multirate checkpoint-retention correction (2026-08-06)

The former 204,136-cell screening baseline exposed a disk-retention defect
during its live startup. At the 0.18 s airflow stage, every processor contained
time `0` plus 19 nonzero checkpoint directories even though `purgeWrite 3` was
configured. OpenFOAM applies `purgeWrite` only to writes made during one solver
invocation; the multirate wrapper starts a new invocation for every airflow or
thermal window, so old invocations accumulated indefinitely.

The generated parallel wrapper now prunes checkpoints after a stage completes
and, for thermal-only stages, only after the frozen flow fields have been copied
into the new restart checkpoint. It preserves time `0` and the newest
`saved_time_directories` nonzero times on every processor rank. Targets must be
numeric time-directory names beneath the explicit processor root before they
can be removed.

A later production launch showed that the five startup fan-ramp segments call
the solver directly rather than through the general stage helper. They were
bounded to five directories and pruned by the first adaptive window, but did
not obey the configured retention count during the ramp itself. The pruning
function is now defined before the ramp and called after every completed ramp
segment as well.

The generated script passed `bash -n`. A non-destructive two-rank dry run with
times `0` through `5` and retention set to three selected only times `1` and `2`
on both ranks, preserving `0`, `3`, `4`, and `5`. The full C++ and Python
regression suite also passed. The running legacy baseline was deliberately not
modified; the correction applies to newly exported cases.

## Fanless-KVM convergence correction (2026-08-06)

The completed legacy checkpoints exposed a second independent startup issue.
Rack ambient mass imbalance was already only 0.16-0.20%, and every rack fan
and the main intake changed by approximately 0.6% per comparison window,
inside the configured 1% stability tolerance. The fanless KVM's passive front
opening carried only about `1e-8` kg/s, however. Sign and roundoff changes in
that effectively zero flow produced reported relative changes from 12% to
322%, preventing the rack from ever leaving coupled-flow startup.

Boundary-flow stability now uses a throughput-scaled negligible-flow floor.
When both consecutive samples for an exterior opening are below
`minimum_tracked_boundary_flow_fraction` times total one-way exterior
throughput, its stability change is zero. If either sample crosses the floor,
the change is evaluated using the floor as the minimum denominator. This keeps
the transition into meaningful intake, exhaust, or recirculation observable.
Internal fan operating points retain their original full relative-change
check. The supplied value is `0.0001`, or 0.01% of rack throughput.

The independently exported 167,232-cell production case then passed live at
`t = 0.36 s`: ambient mass imbalance was 0.1978%, maximum meaningful device
flow change was 0.6477%, the boundary-flow floor was `2.46223e-5 kg/s`, and
all direction checks passed. Its first four 0.01 s adaptive windows averaged
113.0 wall seconds, 32.7% faster than the 167.9 s legacy-mesh average.

The same checkpoint demonstrated why downstream reports need the identical
near-zero-flow policy. The passive KVM opening carried approximately
`-1.9e-9 kg/s`; OpenFOAM's mass-weighted division reported `-215008 K` despite
the opening being effectively stagnant. `recirculation_report.py` now excludes
such undefined temperature samples from re-ingestion and sensible-heat metrics
while continuing to plot the signed flow itself.

## Conclusion

The generic architecture is viable for rack-level work and reduced cell count
by 28.3%. Its volume-average component temperatures remained within 3.5-7.0%
of the detailed model despite no component calibration. It is not yet a
drop-in validated replacement because the provisional device fans changed the
rack airflow balance.

For each real component, measure mass-weighted intake temperature, mass-weighted
exhaust temperature, mass flow, and preferably pressure rise. Regenerate the
component and equivalent curve with
`tools/generic_server_characterizer.py`, then require:

- component mass flow within 5-10% of measurement;
- rack mass imbalance below 1%;
- exhaust temperature rise within 1-2 K of measurement;
- stable device flows across the final thermal-state airflow refresh.

Only after those checks should generic-case rack intake temperatures and
recirculation be treated as quantitative.
