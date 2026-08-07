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
The physical KVM is fanless. Its generic file now uses only its passive front
vent; the unverified rear exhaust opening was removed to avoid inventing a
through-flow path. The selective production experiment retains the original KVM
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

An additional isolated 30 mm fine / 120 mm coarse screening candidate reduced
the mesh from 167,232 to 57,420 cells (65.7%). Region preparation completed and
all heat-source, vent, and fan zones remained nonempty, but the represented
component volumes failed the geometry-invariance requirement:

| Region | Accepted 20 mm volume (m3) | 30 mm candidate volume (m3) | Change |
|---|---:|---:|---:|
| Eaton UPS | 0.0134536 | 0.0131150 | -2.52% |
| Dell 1U | 0.0102842 | 0.0133234 | +29.55% |
| Trenton 3U | 0.0176997 | 0.0180377 | +1.91% |
| Fanless KVM | 0.0063702 | 0.00543974 | -14.61% |

At 30 mm, the planner's anti-sliver spacing exceeds the generic components'
5 mm effective enclosure cut. The candidate therefore changes equipment
geometry rather than only reducing fluid resolution and is rejected. The
candidate also introduced low-determinant fractions of 41-45% in the Eaton
and Trenton solids, which are not present at that magnitude in the accepted
mesh. The 20 mm mesh remains the coarsest validated common geometry for these
templates.

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

Adaptive refreshes also retain the last accepted flow vector within a runner
invocation. Their first live-flow window therefore measures the operating-point
change accumulated during the preceding thermal-only interval. Previously the
refresh cleared that vector, spent one costly window establishing a new
baseline, and could not detect the across-interval change. A restarted runner
still reacquires a baseline conservatively because its in-memory vector is
unavailable.

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

## Long screening convergence result (2026-08-06)

The independent 167,232-cell screening case
`model_generic_components_screening_optimized_167232_v2_floor` requested
18,000 s but satisfied two consecutive thermal and refreshed-airflow
checkpoints and stopped cleanly at `t = 14400.02 s`. The KVM remained fanless;
its reported opening flow was treated as passive throughout.

| Thermal checkpoint (s) | Fluid peak T (K) | Eaton avg (K) | Dell avg (K) | Trenton avg (K) | KVM avg (K) | Peak change (K/300 s) | Max avg change (K/300 s) | Air heat rejection |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 2,400 | 472.986 | 312.457 | 326.600 | 335.299 | 300.818 | baseline | baseline | 81.55% |
| 4,800 | 544.954 | 320.716 | 334.797 | 353.512 | 305.695 | 8.996 | 2.277 | 91.84% |
| 7,200 | 568.755 | 323.907 | 337.140 | 360.195 | 308.364 | 2.975 | 0.835 | 96.77% |
| 9,600 | 575.443 | 325.121 | 337.845 | 362.441 | 309.771 | 0.836 | 0.281 | 98.48% |
| 12,000 | 576.526 | 325.593 | 338.074 | 363.065 | 310.556 | 0.135 | 0.098 | 99.16% |
| 14,400 | 576.020 | 325.800 | 338.147 | 363.108 | 311.046 | 0.063 | 0.061 | 99.40% |

The screening limits are 0.25 K/300 s for fluid peak change and 0.10 K/300 s
for every component-average change. The 12,000 s checkpoint was accepted as
1/2 and 14,400 s as 2/2. At the final airflow sample, exterior mass imbalance
was 0.128%, maximum meaningful device-flow change was 0.392%, all configured
directions passed, and the hot-air re-ingestion index remained zero.

The run also exposed the limit of boundary-only refresh convergence. Although
each late refresh passed its boundary totals after 0.02 s, volume-field audits
between its 0.01 and 0.02 s states found the following fluid-velocity changes:

| Refresh checkpoint (s) | U RMS change | U maximum local change | T RMS change |
|---:|---:|---:|---:|
| 7,200 | 2.581% | 1.832 m/s | 0.0308 K |
| 9,600 | 2.466% | 2.883 m/s | 0.0284 K |
| 14,400 | 2.286% | 2.343 m/s | 0.0273 K |

Consequently, short boundary-stable screening refreshes are not yet evidence
that the full recirculation field is steady. A byte-for-byte verified clone at
`t = 9600.02 s` was continued with preserved 0.10, 0.50, and 1.00 s fully
coupled checkpoints. Spatial drift remained large rather than disappearing
over the longer comparisons:

| Coupled-flow interval | U RMS change | U relative RMS change | U maximum local change | T RMS change | T maximum local change |
|---:|---:|---:|---:|---:|---:|
| 0.02 -> 0.10 s | 0.2201 m/s | 15.98% | 4.747 m/s | 0.4757 K | 15.634 K |
| 0.10 -> 0.50 s | 0.7196 m/s | 43.29% | 5.674 m/s | 1.7166 K | 21.253 K |
| 0.50 -> 1.00 s | 0.6431 m/s | 34.96% | 6.698 m/s | 1.5065 K | 13.735 K |
| 1.00 -> 2.00 s | 0.5426 m/s | 29.21% | 7.004 m/s | 1.4530 K | 12.956 K |
| 2.00 -> 5.00 s | 0.5076 m/s | 26.72% | 6.863 m/s | 1.4713 K | 25.853 K |

The 2.00 s result proves that neither the screening profile's 0.01 s minimum
nor a nominal 0.1-2.0 s window is sufficient to establish the internal
hot-rack flow field. Relative velocity drift decreased from 34.96% over the
0.5-1.0 s interval to 29.21% over 1.0-2.0 s, but remains much too large to call
the field settled. The isolated study therefore continues to 5 s before
profile refresh durations are tuned. Extending the same field from 2.00 s to
one nominal air-exchange time at 5.00 s reduced relative velocity drift only
to 26.72%. The field therefore remains materially transient after one nominal
exchange and the 5 s state is not an acceptable converged-flow reference.
The exact 1,001-step extension used `deltaT = 0.0029970036916897695 s`, took
approximately 8,677 wall seconds (2.41 h), and passed its postflight gate with
`max(Co) = 7.21949`. OpenFOAM named the accepted checkpoint
`9604.9999963042137`; the 3.7 microsecond representation difference is within
the scale-aware target tolerance.
The nine exterior fans carried approximately `0.2578 kg/s` in total and the
audited fluid volume was `1.0436 m3`. For a representative hot-rack density of
1.0-1.2 kg/m3, one nominal air-volume exchange therefore takes roughly
4.05-4.86 s. A 0.5 s study spans only about 0.10-0.12 air exchanges, so a
several-second continuation is physically warranted even before applying the
spatial-field convergence criterion.

The same study exposed a separate timestep-control defect. OpenFOAM
`adjustableRunTime` write alignment enlarged configured 0.001 s live-flow
steps to the complete 0.01 s stage. Generated multirate runners now use exact
divisible fixed steps, measure the saved parallel field's global `max(Co)` to
size restarted live stages with a 20% margin, and audit the final saved field
against the configured limit. Startup ramps use a conservative fixed fallback.
A two-rank runtime case verified 100 x 0.0001 s startup steps and then a
preflight-approved 10 x 0.001 s restarted stage; its checkpoint metadata and
postflight Courant audit agreed with the intended controls.

## Screening versus in-depth Courant sensitivity (2026-08-06)

An identical `t = 9600.02 s` hot checkpoint was advanced to `9600.10 s` with
screening and validation-quality Courant controls. A third control retained
the screening case's byte-identical processor partition while using the
in-depth controls. All cases used the same 167,232-cell unsplit mesh, geometry,
initial fields, material models, fan curves, and physical end time.

| Run | Steps | Fixed deltaT (s) | Terminal max(Co) | Solver wall time |
|---|---:|---:|---:|---:|
| Screening | 81 | 0.0009876543 | 2.33820 | 712 s |
| In-depth | 236 | 0.0003389831 | 0.807879 | 1,380 s |
| In-depth, screening partition | 236 | 0.0003389831 | 0.807878 | 1,507 s |

Reconstructed-root comparisons verify cell-centre alignment before comparing
fields, so different Scotch processor orderings cannot corrupt the result.
Changing only the valid Scotch partition changed velocity RMS by
`5.18e-6 m/s` (`0.000370%`) and temperature RMS by `2.07e-5 K`. Partition
sensitivity is therefore negligible for this interval. In contrast, screening
versus the same-partition in-depth control differed by `0.07355 m/s` velocity
RMS (`5.34%`) and `0.10740 K` temperature RMS (`0.0357%`). The largest local
velocity difference was `18.75 m/s` immediately downstream of the Dell rear
exhaust, where streamwise velocity reversed between the two integrations; the
largest local temperature difference was `6.504 K`.

The validation and in-depth profiles now retain `maxCo = 1` during multirate
airflow refreshes instead of silently overriding their normal validation limit
with `maxCo = 10`. Screening retains its faster `maxCo = 10` refresh ceiling
and 1 ms hard step cap for ballpark heat allocation, fan-curve, vent, and
layout work. On this case the validation-quality interval cost 1.94-2.12 times
the screening wall time. Screening fields must not be treated as final local
recirculation predictions when this fidelity difference matters.

`tools/openfoam_cross_case_comparison.py` performs the reconstructed-root
comparison, rejects cell-order/geometry mismatches, and reports the owning
region, cell, coordinate, volume, and values at each maximum discrepancy.

The `t = 9601 s` temperature audit also clarified an easy-to-misread solver
diagnostic. The solver printed a fluid-region maximum near 575 K because its
field min/max line includes coupled boundary values copied from a hot solid
interface. The internal-cell reader found a rack-fluid maximum of only
`340.035 K`; the global internal-cell maximum was `578.121 K` at the centre of
the Generic Trenton equivalent heat block. The block is an intentionally
simplified rack-level heat source and its temperature is not a predicted chip
temperature. `plot/heat_animation.py` now prints the owning mesh region and
coordinates of the global internal-cell hotspot, and suppresses only VTK's
false empty-processor-partition warnings while retaining explicit empty-mesh
and missing-temperature checks.

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

## Screening PIMPLE-corrector study (2026-08-06)

A controlled 30-step study restarted three cases from the identical
`t = 9604.9999963042137 s` screening checkpoint at `Co_max ~= 7.23`. The
existing `3 outer x 3 pressure` policy was compared with `2x2` and `3x2`.
Each case used the same mesh, fields, four MPI ranks, and approximately
`0.0029994 s` timestep.

| Policy | Wall time (s) | Pressure solves/step | Pressure iterations/step | U RMS vs 3x3 | T RMS vs 3x3 |
|---|---:|---:|---:|---:|---:|
| 3x3 reference | 306.31 | 9 | 117.2 | - | - |
| 2x2 | 191.02 | 4 | 83.9 | 0.032556 m/s (1.7146%) | 0.089128 K |
| 3x2 | 248.52 | 6 | 99.23 | 0.006615 m/s (0.3484%) | 0.007713 K |

The `2x2` case was rejected for the default screening profile because its
largest local difference included a velocity-direction reversal and a
`4.90 K` cell-temperature difference. The `3x2` case retained every outer
fan/thermal coupling pass while reducing pressure work. Relative to `3x3`,
its nine fan flows differed by at most `0.00448%`, main-intake flow by
`0.00064%`, and reported fan temperatures were unchanged at report precision.
Its ambient mass imbalance was `0.0881%` of throughput versus `0.0838%` for
the reference, both well inside the configured 1% limit. Screening therefore
uses `pimple_outer_correctors = 3` and `pimple_pressure_correctors = 2`;
default, in-depth, and validation profiles retain `3x3`.
