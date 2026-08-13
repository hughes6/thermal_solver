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
airflow refreshes instead of silently overriding their normal validation
limit. The initial screening study retained a faster `maxCo = 10` refresh
ceiling and 1 ms hard step cap for ballpark heat allocation, fan-curve, vent,
and layout work. On this case the validation-quality interval cost 1.94-2.12
times the screening wall time. Screening fields must not be treated as final
local recirculation predictions when this fidelity difference matters.

A later matched study on the current 208,772-cell connected-component mesh
found that the nominal screening ceilings of 10 and 5 reached actual maxima
of 4.82 and 2.41. Against an actual-Co=0.48 reference, their velocity RMS
errors were 4.08% and 3.10%. A ceiling of 2 reached actual Co=0.96 and reduced
velocity RMS error to 1.50%, pressure RMS error to 0.00052%, and temperature
RMS error to 0.00510%. Its 236 s solver time lay between 50 s at Co=10 and
424 s for the conservative reference. Because a refresh is only part of a
2,400 s screening interval, the approximately 200 s end-to-end increment is
about 25% of the representative workflow rather than a 4.8x total penalty.
The screening profile therefore now uses `maxCo = 2` for periodic refreshes.

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

### Warm-start restart check (2026-08-06)

Extending the retained `3x2` checkpoint exposed a restart-control defect before
the solver advanced: `--warm-start` wrote its duration in seconds to
`writeInterval`, but a preceding multirate airflow stage can leave
`writeControl = timeStep`. OpenFOAM then rejected a fractional interval with
`writeInterval < 1 for writeControl timeStep`. The generated runner now forces
`adjustableRunTime` before applying the warm-start interval and restores that
production write control after both warm-start and multirate runs.

The corrected four-rank command completed 40 full steps from
`t = 9605.0899783743262 s` before a deliberate resource-safety interrupt.
Across those completed steps, maximum Courant number stayed between `7.22510`
and `7.22601`, and there were no OpenFOAM fatal errors. Wall time was
`19.95 s/step` while the host had less than `0.5 GB` free RAM, versus
`8.28 s/step` in the earlier `3x2` study. The unchanged timestep and Courant
behavior show that this slowdown was host memory pressure rather than a solver
timestep collapse. The original complete checkpoint remained intact.

A subsequent two-rank restart check found that `decomposePar -force` leaves
surplus `processorN` directories when reducing the rank count. Reconstruction
then tried to combine the new two-rank partition with stale ranks 2 and 3 and
failed on a missing field. The generated runner now reconstructs any newer
parallel checkpoint first, removes only its own `processor[0-9]*` directories,
and then performs the requested repartition. This makes changing the process
count restart-safe while preserving the reconstructed root checkpoint.

### MPI-rank study on the 8 GB workstation (2026-08-06)

Separate two-rank and four-rank cases were restarted from the identical
`t = 9605.0899783743262 s` checkpoint and advanced exactly 30 fixed steps to
`t = 9605.1799603743166 s`. Both final checkpoints reconstructed cleanly.

| Ranks | Solver wall time/step | End-to-end wall time | Pressure iterations/step |
|---:|---:|---:|---:|
| 2 | 9.70 s | 404.78 s | 85.50 |
| 4 | 17.57 s | 638.67 s | 99.47 |

Two ranks were `44.8%` faster during the solve and `36.6%` faster including
partitioning and reconstruction. Rank-2 versus rank-4 field differences were
`0.2063%` RMS for velocity and `0.001065%` RMS for temperature; maximum local
temperature difference was `0.348 K`. Nine fan mass flows differed by at most
`0.00114%`, fan mass-weighted temperatures matched at report precision, and
the main-intake flow differed by `0.00057%`. Ambient mass imbalance was
`0.0854%` of intake throughput for two ranks and `0.0860%` for four ranks.

The screening profile therefore defaults to two ranks on this memory-limited
workstation. In-depth and validation profiles retain four ranks; users with
more RAM may override `parallel_processes` after benchmarking their own host.

### Thermal-only timestep study (2026-08-07)

Two cases restarted from the identical `t = 9605.1799603743166 s` checkpoint
and advanced 100 s with frozen airflow. The screening `10 s` maximum timestep
was compared with a `1 s` reference at the same final time.

| Maximum thermal timestep | Steps | End-to-end wall time | T RMS vs 1 s | Maximum T difference |
|---:|---:|---:|---:|---:|
| 10 s | 10 | 154.32 s | 0.002738 K (0.000909%) | 0.1193 K |
| 1 s reference | 100 | 334.73 s | - | - |

The fully implicit frozen-flow energy solve remained accurate at 10 s while
cutting total runtime by `53.9%`. Its reported Courant number can exceed the
live-flow limit because velocity is held and momentum is not advanced; it is
diagnostic, not the fixed-step controller. `thermal_only_maximum_time_step` is
therefore the relevant accuracy control. Live-flow stages continue to enforce
Courant limits with preflight and postflight checks.

The long two-rank case then advanced from 9605.18 s to 14,401 s. At 14,400 s,
thermal convergence was correctly rejected (`0.2879 K/300 s` peak change and
`0.2160 K/300 s` maximum component-average change). Adaptive airflow refresh
converged in two 0.01 s chunks with maximum tracked-flow change `0.2293%`,
correct fan directions, and no solver failures. The case reconstructed cleanly
at 14,401 s.

### Validated thermal convergence (2026-08-07)

The same unique screening case continued through the 16,800 s and 19,200 s
refresh checkpoints. At 16,800 s, thermal rates were `0.1341 K/300 s` peak and
`0.09758 K/300 s` maximum component-average change. The refreshed flows changed
by at most `0.0563%`, all directions were correct, and checkpoint 1/2 was
accepted.

A terminal one-second segment then exposed a state-machine defect: because it
had no scheduled airflow refresh, the generic failure branch reset the already
validated streak. The generated runner now resets a streak only when a new
airflow-validated checkpoint fails thermal criteria. A terminal partial stage
neither advances nor clears prior validated evidence.

At 19,200 s, thermal rates had fallen to `0.07536 K/300 s` peak and
`0.04085 K/300 s` maximum component-average change. The second airflow refresh
changed tracked flows by at most `0.01346%`, retained correct directions, and
checkpoint 2/2 was accepted. The solver stopped automatically at
`t = 19200.020000000237 s`, before its requested 19,201 s endpoint, and
reconstructed all fluid and solid fields successfully. This is the first full
screening workflow run in this study to satisfy both thermal and refreshed-flow
convergence criteria.

### Converged screening versus in-depth correctors (2026-08-07)

Two clones of the fully converged `t = 19200.020000000237 s` checkpoint used
the same mesh, fields, two MPI ranks, and fixed `0.0003 s` timestep
(`Co_max ~= 0.723`). Screening `3 outer x 2 pressure` correction was compared
with in-depth `3x3` for 30 solver steps. Their common reconstructed comparison
checkpoint was `t = 19200.026000000231 s`.

| Policy | Solve wall time | Pressure solves/step | Pressure iterations/step | U RMS difference | T RMS difference |
|---|---:|---:|---:|---:|---:|
| 3x2 | 318.32 s | 6 | 28.90 | reference | reference |
| 3x3 | 384.55 s | 9 | 36.97 | 0.00181% | 0.000004% |

The extra pressure corrector increased solve wall time by `20.8%`. Maximum
local differences were only `0.00109 m/s` velocity and `0.00067 K`
temperature. Because the in-depth profile already retains stricter `Co <= 1`,
five-second thermal timesteps, 1,200 s refreshes, 60 s reporting, and tighter
convergence thresholds, it now uses the validated `3x2` correction policy.
It also defaults to two ranks on the 8 GB workstation. The strict validation
profile remains at four ranks and `3x3` for independent reference studies.

### Strict-Co airflow-refresh sampling (2026-08-07)

At `Co_max ~= 0.722`, the old in-depth one-second check interval required about
3,300 live-flow steps per sample. A controlled test instead advanced two
consecutive `0.0102 s` samples of 34 fixed `0.0003 s` steps from the converged
checkpoint. The samples took `283.35 s` and `288.78 s` respectively.

Between samples 1 and 2, the nine fan boundary flows changed by at most
`0.0456%`, main-intake flow by `0.0101%`, and three internal fan operating
points by at most `0.180%`. All meaningful flows therefore satisfied the 1%
criterion after two samples while retaining strict `Co <= 1` fidelity.

The passive KVM opening crossed through zero between approximately
`-0.000073` and `+0.000048 kg/s`, only `0.016-0.024%` of main-intake flow. Its
relative change is mathematically large but thermally negligible. The in-depth
near-zero floor is now 0.1% of rack throughput, so such passive sign noise does
not control convergence; the flow remains included in ambient mass balance.

The in-depth refresh and check intervals are now `0.01 s`, with a bounded
`0.10 s` maximum refresh duration. A later full 1,200 s thermal-hold replay
found a timestep-scale period-two mode in two internal `fanMomentumSource`
operating-point reports: fan 1 alternated between about `0.01471` and
`0.01665 m3/s`, while rack boundary flows were already stable to about
`0.03%`. Direct instantaneous comparison therefore failed at `11.7-13.6%`
despite `Co_max < 0.78`, mass imbalance below `0.052%`, and correct directions.

Reducing velocity relaxation from 0.7 to 0.5 only reduced the alternating
amplitude to `9-10%`; it did not remove the numerical mode. The runner now
compares consecutive two-sample means for internal fan operating points while
continuing to compare rack boundary flows directly. This preserves sensitivity
to mean-flow drift without treating timestep parity as physical divergence.
The byte-identical-mesh replay then converged after three `0.01 s` samples:
worst smoothed internal-fan change `0.0965%`, worst boundary-flow change about
`0.033%`, mass imbalance `0.00303%`, directions valid, and `Co_max=0.779`.
The report now identifies the device responsible for the maximum change.

### Repeated in-depth convergence checkpoints (2026-08-07)

The smoothed internal-fan criterion was then exercised in the normal multirate
sequence, not only in an isolated refresh. From the reconstructed `20400.16 s`
state, the case advanced through two complete 1,200 s thermal holds:

| Checkpoint | Peak change (K/300 s) | Worst component average (K/300 s) | Refresh | Result |
|---:|---:|---:|---:|---|
| 21600 | 0.08626 | 0.03713 | 0.03 s, `Co_max=0.780` | accepted 1/2 |
| 22800 | 0.06470 | 0.02718 | 0.03 s, `Co_max=0.780` | accepted 2/2; auto-stop |

At the final checkpoint, maximum tracked-flow change was `0.0322%`, ambient
mass imbalance was `0.00270%`, and all fan directions were valid. The
recirculation report found zero thermal re-ingestion, `1538.46 W` net sensible
heat rejection, and a `99.5766%` heat-rejection fraction.

The scalar/device auto-stop is not a claim that every instantaneous turbulent
cell value is steady. Reconstructed field comparisons found `21600 -> 22800 s`
temperature RMS drift of `0.1361 K` (`0.0453%`) with a localized `5.78 K`
maximum, while velocity RMS drift remained `1.8736%`. Three consecutive live
samples at 22800 s changed by `0.664-0.687% U RMS` per 0.01 s sample; samples
two intervals apart still differed by `1.2408%`. Thus the detailed velocity
field remains transient even though mean device flows and thermal engineering
metrics have converged. Final CFD studies should report a chosen spatial-field
tolerance separately using `openfoam_field_convergence.py`.

That comparison tool now automatically selects reconstructed data when all
requested root times exist, even if newer processor directories are present.
The previous unconditional decomposed-mode selection made valid historical
root checkpoints appear unavailable. Explicit `--case-type reconstructed` and
`--case-type decomposed` overrides are available and were runtime-verified.

### In-depth mesh sensitivity and warm-start precision (2026-08-07)

A unique 15 mm near-equipment mesh was generated from the same rack model and
the converged 22,800 s fields were mapped into all five regions. The current
20 mm mesh contains 167,232 cells; the 15 mm mesh contains 288,757 cells, a
72.7% increase. The refined mesh remained orthogonal with negligible skewness.
Strict-Co live steps took about 1.9 times as long as on the 20 mm mesh.

After 0.07 s of fully coupled mapped-field relaxation, the final 0.02 s segment
changed intake flow by only `0.254%`, within the 1% operating-point criterion.
The comparison at the final fine checkpoint was:

| Metric | 20 mm | 15 mm | Fine versus current |
|---|---:|---:|---:|
| Rack intake flow | 0.296736 kg/s | 0.291756 kg/s | -1.68% |
| Exhaust temperature rise | 5.1587 K | 5.1438 K | -0.29% |
| Sensible heat rejection | 1538.46 W | 1508.71 W | -1.93% |
| Thermal re-ingestion index | 0 | 0 | unchanged |

Component-average temperatures differed by no more than about `0.03 K` because
the thermal state was mapped from the converged checkpoint. The approximately
2% airflow/heat-rejection discretization shift is material for final design
validation but does not justify paying the 1.9x live-flow cost during screening.
The in-depth profile therefore now uses `fine_dx = 0.015 m`; screening retains
`0.020 m`.

This study also exposed two coupled warm-start defects. Absolute end times were
written without sufficient precision, so `22800.06` became `22800.1`, and the
write interval was calculated from a stale reconstructed root time before the
newer processor checkpoint was selected. The runner now resolves the processor
checkpoint first and writes dynamic times at 16-digit precision. Runtime replay
from `22800.06` reached and reconstructed exactly `22800.080000000002` with the
correct `0.02000000019 s` write interval.

### Calibrated air-side heat sources

Validation of the compact equivalent blocks showed that increasing their
effective conductivity by 10x changed mean temperatures negligibly and left
extreme local maxima. For equipment where only intake temperature, exhaust
temperature, and mass flow are known, the measured air-side heat is better
represented directly in the internal air tunnel:

```toml
[[internal_regions]]
name = "Interior air"
state = "air"
watts = 950.0

[internal_regions.position]
units = "mm"
x = 39.1726
y = 0.0
z = 8.22325

[internal_regions.size]
units = "mm"
width = 403.6548
depth = 817
height = 28.0035
```

Air-region `watts` default to zero and must be non-negative. The mesh divides
the requested power over the final stamped fluid volume, so the native source
integral remains exactly equal to the requested watts. OpenFOAM exports a
dedicated fluid cell zone and an absolute `scalarSemiImplicitSource` on `h`.
The power is also included in `openfoamExportProperties`, so heat-rejection
fractions retain the correct denominator.

Do not assign the same power to both an air region and a solid block. The
air-side option intentionally predicts rack intake/exhaust behavior without
claiming chip, heat-sink, or chassis surface temperatures. Keep a calibrated
solid/resistance model when those temperatures are validation targets.

#### Rack-energy-validated air-side variant

`library/models/model_generic_airside.toml` is the controlled in-depth model.
It references the `_airside.toml` Eaton, Dell, Trenton, and passive KVM
templates. Their geometry and airflow definitions are identical to the
solid-source templates, and each template conserves the original watts while
setting its equivalent block to zero watts.

The characterizer supports this explicitly:

```powershell
python tools/generic_server_characterizer.py `
  --name "Measured server" --rack-units 1 --depth-mm 700 `
  --mass-flow-kg-s 0.05 --intake-temp 293.15 --exhaust-temp 303.15 `
  --heat-placement air --output measured_server_airside.toml
```

The in-depth rack converged by 14400 s with 1533.36 W of 1545 W rejected,
0.132% intake/exhaust mass imbalance, zero main-intake thermal re-ingestion,
and strict temperature changes below 0.10 K/300 s for every tracked maximum
and below 0.05 K/300 s for every component mean. Live airflow checks at 7200
and 10800 s each passed in one 0.01 s window. For this semi-steady rack,
3600 s airflow refreshes were accurate and much cheaper than 1200 s refreshes.

Use the air-side variant when measured mass flow and intake/exhaust
temperatures are the calibration evidence. Use a solid/resistance model only
when calibrated component surface or hotspot temperatures are required.

The supplied Eaton, Dell, and Trenton examples remain provisional because
their flow inputs were inferred from a 10 K rise and their fan curves were
fitted through an assumed 80 Pa operating point. A converged in-depth rack
state produced two-sample-mean internal flows of 0.015334, 0.057753, and
0.042860 kg/s against inputs of 0.014918, 0.094480, and 0.042268 kg/s. Eaton
and Trenton were within +2.79% and +1.40%; Dell was 38.87% low, implying a
16.36 K ideal air rise for its 950 W rather than the assumed 10 K. Rack energy
conservation and outlet-temperature convergence do not calibrate each device's
fan curve. Replace the assumed curve with measured pressure-flow data, or tune
it against a measured installed mass flow, before claiming component-level
intake/exhaust agreement.

## Air-side screening validation (2026-08-09)

`library/models/model_generic_airside_screening.toml` holds the calibrated
air-side geometry, loads, fan curves, and rack placement fixed while selecting
`screening_foam_cfg.toml`. Its 208,772-cell mesh is 37.79% smaller than the
335,580-cell in-depth reference.

### Cold-start source isolation

The first fresh screening run exposed an air-side-specific startup defect.
Fluid heat sources were active while the controller was still establishing
the cold fan operating point. At `t = 0.37 s`, localized fluid had reached
approximately 308.6 K. Device-flow change was already below 1%, but continuing
compressible mass accumulation held exterior imbalance at 1.468%; its decline
was too slow to justify hours of additional fully coupled startup.

Generated cases now include a separate `fvOptions.flowOnly` dictionary. It
contains every fan and porosity source but omits fluid energy sources. A cold
or resumed pending multirate initialization installs this dictionary before
the fan ramp. Once airflow passes, the pristine full dictionary is restored
before thermal evolution. Normal coupled warm starts and every later loaded
airflow refresh retain full heat.

The fresh corrected case remained within 293.079-293.154 K during startup and
accepted at `t = 0.37 s`:

| Metric | Heated startup | Corrected flow-only startup |
|---|---:|---:|
| Exterior mass imbalance | 1.468% | 0.257% |
| Worst device-flow change | 0.822% | 0.841% |
| Directions valid | yes | yes |
| Approximate fluid maximum | 308.6 K | 293.15 K |

All four fluid energy sources, totaling 1545 W, were runtime-verified as
restored before the first implicit thermal stage. The first loaded refresh at
3600 s then detected the real temperature-dependent operating-point change
and required 0.07 s to converge. Refresh duration fell to 0.02 s at 7200 s
and 0.01 s at both 10800 and 14400 s.

### Long-run result and mesh comparison

The corrected cold run reached 14400 s in 2 h 54 min wall time, including
fresh preparation and startup. It met strict validation-profile temperature
limits over the final 3600 s interval, not merely the looser screening limits:

| Endpoint metric | Screening | In-depth | Screening difference |
|---|---:|---:|---:|
| Worst cell change (K/300 s) | 0.05424 | 0.05399 | +0.00025 |
| Worst component-mean change (K/300 s) | 0.02870 | 0.02839 | +0.00031 |
| Rack intake (kg/s) | 0.274820 | 0.291207 | -5.63% |
| Rack exhaust (kg/s) | 0.274207 | 0.290822 | -5.71% |
| Exhaust mass-weighted T (K) | 298.736 | 298.396 | +0.340 |
| Net sensible rejection (W) | 1539.46 | 1533.36 | +0.40% |
| Heat-rejection fraction | 99.64% | 99.25% | +0.39 percentage point |
| Thermal re-ingestion index | 0 | 0 | unchanged |

Different-mesh cell-centre sampling covered 100% of screening cells. Regional
mean temperature differences ranged from +0.13 to -1.32 K. Fluid temperature
RMS difference was 1.25 K, but fluid velocity RMS difference was 17.74% and
screening rack throughput was 5.6% lower. Screening is therefore validated for
load, layout, energy-rejection, and approximate temperature iteration. Final
airflow magnitude, recirculation topology, and acceptance claims must still
use the 15 mm in-depth profile.

The run also exposed a terminal-checkpoint defect: a requested end time exactly
on a refresh boundary previously reconstructed thermal results without a
loaded airflow refresh. The generated adaptive controller now extends only its
internal airflow-validation allowance, refreshes the terminal thermal state,
then restores the user's requested end for convergence logic. Final reports no
longer silently combine current temperatures with stale flow fields.

### Screening pressure-corrector sensitivity

The 5.6% screening throughput deficit was tested separately from mesh
resolution. Two byte-size-verified clones started from the same converged
`14401 s` processor checkpoint and advanced through the same 30-step,
0.01 s loaded airflow interval. The control retained two PIMPLE pressure
correctors; the candidate changed only `nCorrectors` to three.

| Metric | Two correctors | Three correctors | Difference |
|---|---:|---:|---:|
| Solver clock time | 264 s | 360 s | +36.4% |
| Exterior mass imbalance | 0.218659% | 0.217934% | -0.000725 percentage point |
| Worst device-flow change | 0.656919% | 0.656919% | none at report precision |
| Estimated air-exchange time | 4.61798 s | 4.61796 s | -0.00002 s |

Same-mesh, same-time field comparison found only `3.11e-5 m/s` velocity RMS
difference (0.002285%), `1.69e-5 K` temperature RMS difference (0.000006%),
and `0.00239 Pa` pressure RMS difference. The third pressure corrector is
therefore rejected: it adds substantial cost without materially changing the
operating point or field. Screening retains two pressure correctors. The
remaining screening-versus-in-depth airflow difference is a mesh-resolution
effect, not evidence of insufficient pressure correction.
