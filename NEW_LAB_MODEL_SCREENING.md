# New research-lab rack: initial screening notes

## Scope and status

The model is intended for exploratory geometry and solver screening, not final
thermal validation. Component heat loads and most component fan flow rates are
approximations. The top-fan curve exists in the current library but is expected
to be updated later.

> **Current authority (2026-08-27).** The historical sequence below is retained
> to show how the model evolved. The canonical input is now
> `library/models/new_model_updated.toml`; the retained 1.600 s OpenFOAM fields
> predate the N5766A and Trenton repairs and are neither a corrected field result
> nor a converged thermal soak. The current reusable frozen-flow thermal cap is
> 20 s. The old exact-case 24 s experiment was faster but failed strict
> temperature gates and was not promoted. A fail-closed host-only pre-export
> gate, `openfoam_resource_gate_final_idle_20260827T095617107Z.json`, found no
> competing solver processes and passed disk at 17,129,152,512 free bytes, but
> exited 21 when its first memory sample found only 951,701,504 available bytes
> versus the mandatory 5,368,709,120-byte floor. WSL was not queried. The older
> 936,706,048-byte memory failure and 9,639,960,576-byte disk failure remain
> historical snapshots, not the current resource authority. No corrected export,
> mesh, build, or solve was started. Current evidence and exact hashes are
> indexed in `validation/README.md`.

Known geometry uncertainties are kept visible in `new_model.toml`:

- Rail 2 depth locations are provisional.
- The supplied NI chassis origin at 21 U exceeded the rack rear boundary by
  54.2 mm. Its temporary origin is 19.5 U, leaving about 12.5 mm rear clearance.
- The NI card-slot region does not yet have its thin rear and side separator
  walls. The card air and chassis air therefore communicate too freely. Its
  proposed solid/card heat regions also remain commented out, so the current NI
  chassis contributes 0 W; NI temperature predictions are not represented.
- The 3U storage shelf is represented as an unpowered hollow enclosure so the
  face-wall solver has a valid internal air region.

## Initial checks (2026-08-21)

- All added TOML files parse as TOML.
- Geometry-only export succeeds for `library/models/new_model.toml`.
- The coarse native mesh contains 157,950 cells and reports about 70.8 MB from
  cell storage.
- The plot geometry unit tests pass, and both plot scripts compile as Python.
- A native PCG run reached coarse mesh construction but produced no timestep or
  iteration progress during a bounded 90-second first-solve trial. It was
  stopped rather than left consuming time without diagnostics.

## Realism assessment

The geometry is suitable for spotting placement, opening, and gross flow-path
errors. Absolute temperatures and component flow splits are not yet credible.
The largest uncertainties are the missing NI separator walls and heat zones,
provisional Rail 2 depths, assumed internal fan CFM, assumed heat distribution,
and pending fan curves. The originally supplied `rho = 1.225` was inconsistent
with `elevation = 5500.0`; the implemented model now uses approximately
ISA-consistent `rho = 0.9833 kg/m^3` at 20 C.

The nine nominal 271 CFM roof fans total 2,439 CFM before system resistance and
fan-curve operating-point effects. Fixed-CFM interpretations would likely
overstate actual rack extraction. Results should be compared first against
measured rack inlet/outlet flow and total electrical power.
The originally referenced `top_fan_MS1238E-H` polynomial reaches zero pressure
at exactly 0.1 m3/s (211.9 CFM), and evaluating it at the stated 271 CFM gives
-113.3 Pa. The lab model now references a separate
`provisional_top_fan_271cfm` curve that preserves the assumed 300-Pa shutoff
pressure and reaches zero pressure at 271 CFM. The original named curve remains
unchanged for other models. This restores internal consistency but is still not
a substitute for measured manufacturer P-Q data.

The single modeled front intake is a stronger constraint than the nominal roof
flow suggests. Its 9.5-U by 2-U gross area is 0.0375402 m2; after the configured
0.74 free-area ratio only 0.0277798 m2 remains. Passing the roof array's combined
zero-pressure flow of 1.15108 m3/s through that opening would require 41.44 m/s.
With the configured discharge coefficient of 0.5, the exporter's exact
Darcy--Forchheimer law gives approximately 3.38 kPa intake loss at that flow,
which is impossible against a 300-Pa sea-level fan shutoff curve. Solving the
nine identical parallel fan curves against the intake loss alone, including the
exporter's density scaling from 1.2 to 0.9833 kg/m3, gives an optimistic upper
operating point of only 0.2999 m3/s (635 CFM total, 70.6 CFM per roof fan) at
229 Pa. Rack and device resistance can only reduce it. At that upper bound the
intake free-area velocity is 10.79 m/s, the ideal 1,515-W rack air rise is
5.11 K, and the 1.063823-m3 fluid domain requires at least 3.55 s per air
exchange. Thus the earlier 1.124-s exchange lower bound based on summed
zero-pressure flow is nonphysical for this inlet and must not be used for run
planning. At the measured fine-mesh rate, even this optimistic exchange takes
more than four wall-days; a mesh-validated accelerated or mapped-start workflow
is required to satisfy the one-to-two-day objective without weakening the
air-exchange acceptance gate.

A controlled topology-preservation study on the pre-outlet geometry rejected
the obvious coarse-mapping shortcut. Isolated exports at 40, 30, 25, and 22.5 mm produced 193,452,
309,008, 456,246, and 515,526 cells respectively, versus 1,715,112 at 20 mm.
All retained the 22 heat-source identities and exact 1,515-W total, but every
coarser mesh reported one connected fluid volume while that 20-mm case reported
five. The full-fan dictionaries also showed the expected mesh-dependent source
thicknesses and integrated-resistance coefficients; those can be valid
discretizations, but they do not repair the changed topology. The 40-mm PDU
heat zones fell to only 6 and 12 cells, independently prohibiting thermal use.
Accordingly, none of these cases will be prepared, solved, or mapped. The
experimental selectable profile was removed and the isolated export model was
restored to `screening_foam_cfg.toml`; the rejected case directories remain as
audit evidence. After correcting the missing Thruster/Cisco rear openings, the
20-mm case has three connected fluid volumes; the old coarse cases remain
topologically non-equivalent and are not valid mapping sources. A useful
coarse-mapped workflow therefore
requires the final NI separator geometry first, or a different same-mesh
initialization method.

The first fine-grid connectivity interpretation was wrong and the audit caught
a structural model defect. The Thruster and Cisco fan planes were 5 mm inside
their rear shells with no openings through the remaining wall. Their interiors
were therefore isolated fluid bodies; the fans could not discharge into rack
air. The invalid four-rank run was stopped cleanly at about 0.115 s and retained
only as diagnostic evidence. Matching passive rear outlets were added at every
Thruster and Cisco fan footprint, with provisional 0.70 free-area ratio and
0.82 discharge coefficient copied from the existing UPS outlet assumption.

The corrected 1,715,112-cell case contains 1,538,360 fluid cells and three
face-connected fluid bodies. `openfoam_boundary_connectivity_audit.py` maps all
16 physical openings to region 0: two UPS intakes, Trenton intake, both Thruster
intakes, Cisco intake, nine roof fans, and the rack vent. Only the KVM/storage
cavity (region 1) and PDU cavity (region 2) remain sealed. The executable gate
`--expect-regions 3 --opening-region 0` passes, and two focused tests cover
ASCII/binary OpenFOAM label lists plus failure diagnostics. This is now the
authoritative topology result.

Prepared boundary resolution is adequate for screening but not proof of local
wake fidelity. The smallest external device opening is the Cisco intake at 43
faces; roof fans use 67--319 faces, the main rack intake uses 660, and internal
fan face zones use 46--746 faces. Internal fan source zones contain 15--336
cells, with the Cisco power-supply fan the minimum at 15 cells/46 faces. No
active fan or vent is a one- or two-face artifact, but the smallest sources are
too coarse for validation of blade-scale or near-wake structure. `checkMesh`
also reports 16 faces in multiple face zones. Their location/count is
consistent with the shared side boundary of the two adjacent NI bottom-fan
zones; that side is orthogonal to the fans' vertical flow direction and does
not contribute projected through-flow. The 5,235 multiply zoned cells are
expected because source/vent zones are nested inside the overall fluid cell
zone. These observations do not override the existing high-y+ and mesh-
sensitivity limitations.

The corrected runner's air-exchange denominator is `1.0638716259 m3`, whereas
prepared `checkMesh` reports `1.08082 m3` across every fluid cell. This is intentional,
not stale geometry: `ambient_connected_fluid_volume()` flood-fills only fluid
reachable from ambient fan/vent seed cells and excludes sealed cavities. The
approximately 0.01695-m3 difference is consistent with the fanless hollow 3U
storage-shelf interior, which cannot be renewed by exterior rack throughput.
The smaller ambient-connected volume is therefore the correct denominator for
the exchange gate. README wording was tightened to state this explicitly; the
existing connected-volume unit test covers exclusion of a sealed fluid cavity.
A fresh execution of the complete compiled `test_runner.exe` harness after this
audit ended with `ALL UNIT TESTS PASSED`, including that connected-volume test,
fan/vent physics, adaptive meshing, subcycling, loaders, and the current
advanced-model configuration.

`tools/openfoam_thermal_mass_audit.py` now provides a reproducible prepared-mesh
audit of solid volume, configured density and heat capacity, exported watts,
and the resulting no-cooling temperature-rise rate. Two focused tests cover
sentence punctuation in `checkMesh`, whitespace-independent heat dictionaries,
heat summation, and the mass/capacity/rate calculation. On this case the solid
regions total 0.051320916 m3, 138.566 kg, and 124,710 J/K at the configured
equivalent material properties, with exactly 1,515 W mapped. The aggregate
adiabatic rate is therefore about 43.7 K/h. Individual equivalent-region rates
range from 4.45 K/h for the PDU/KVM to 115.8 K/h for the N5766A; the two DIN
supplies are about 101--111 K/h. These values are useful transient sanity
checks, but the masses are homogeneous geometry-derived equivalents rather
than measured device thermal inertias. Absolute warm-up time and component
temperature validation therefore remain blocked on measured mass/material or
temperature-response calibration even after airflow converges.

Source/export inspection found an additional, independent limitation behind
those equivalent masses. The component TOMLs specify heterogeneous internal
materials (for example battery `k=1.5 W/m-K`, electronics around 8--30, and
silicon-like `rho=2330 kg/m3`, `Cp=700 J/kg-K`, `k=130 W/m-K`), but OpenFOAM's
`write_solid_thermophysical_properties()` exports one homogeneous material per
component using only its outer enclosure properties. All active component
regions therefore use the outer aluminum-equivalent 2700/900/150 values while
retaining internal heat-zone geometry and watts. This is not merely input
uncertainty: it structurally over-diffuses low-conductivity battery/electronics
zones and prevents quantitative hotspot or transient-response validation.
README claims were corrected to distinguish OpenFOAM's component-wide material
model from the native backend's per-cell materials. True correction requires
separate coupled solid material regions (or a validated equivalent-material
calibration); an unvalidated arithmetic average would not restore local
conduction physics and was therefore not substituted.

The active model contains 22 fans: nine roof fans, four Keysight fans, four
Cisco fans, two thruster-box fans, two NI fans, and one UPS fan. Their declared
free-air total is 3,302 CFM. The lab contract resolves every curve, checks
finite/nonnegative coefficients, and verifies each provisional curve's zero-
pressure root exactly matches its fan's declared CFM. Density correction at
0.9833 versus the curves' 1.2 kg/m3 rating multiplies available pressure by
0.8194; it does not change the 3,302-CFM zero-pressure roots because all curve
coefficients scale together. Installed operating flows must still be obtained
against system resistance, and the provisional curve shapes/shutoff pressures
remain uncalibrated.

Prepared-case inspection closes the input-to-actuation traceability loop. The
exported metadata contains exactly 13 internal exhaust-fan zones plus nine roof
fan boundary patches, matching all 22 active fan definitions. The fluid
`fvOptions` contains exactly 13 `fanMomentumSource` entries, and root
`0/fluid/p_rgh` contains exactly nine `fanPressure` boundary conditions. Every
internal source has a nonempty named cell/face zone and an explicit expected
direction. Passive internal openings are separate (seven `intake`, seven
`outlet`); notably, the Trenton `Rear exhaust` remains passive and is not
silently promoted to a fan. Runtime direction and operating-flow acceptance
still require the completed checkpoint fields.

The export path now also emits one explicit `OpenFOAM material warning` per
component whose solid internal-region `rho`, `Cp`, or `k` differs from its outer
component. A focused C++ regression verifies that heterogeneous material emits
the warning and a truly homogeneous nested region remains silent. This makes
the approximation visible during every affected export; it does not make the
homogeneous OpenFOAM material model quantitatively adequate.

`tools/openfoam_material_fidelity_audit.py` provides the same check without
building a mesh and has an isolated regression test. On `new_model.toml`, it
finds 12 active component instances, seven heterogeneous instances, and 19
distinct internal solid regions that OpenFOAM homogenizes. The affected
instances are the UPS, both Keysight supplies, Trenton chassis, KVM, thruster
load box, and Cisco switch. The inline shelf, PDU, two Meanwell supplies, and
current NI chassis do not define differing internal solid materials. This
distinction is about source definitions only; it does not validate the
equivalent properties of the nominally homogeneous devices.

The audit also integrates the exact source-box volumes. The 19 affected solid
regions occupy 0.0259722 m3. On those same volumes, substituting outer aluminum
properties adds 9.215 kg and 16,744 J/K relative to the explicitly defined
materials. Conductivity errors remain local rather than meaningfully
summable—for example the UPS battery changes from 1.5 to 150 W/m-K, while
several electronics zones change from 8--30 to 150 W/m-K. These figures use
source dimensions and therefore complement rather than replace the prepared-
mesh thermal-mass audit; Cartesian cell realization can shift exact volumes.

## Recommended next run sequence

1. Confirm Rail 2 depth origins and add the NI separator walls.
2. Replace assumed heat loads with measured idle and representative-load power.
3. Update fan P-Q curves and confirm each component fan orientation.
4. Add iteration/residual progress output or use a coarser screening profile so
   a failed initial pressure solve is apparent within minutes.
5. Run a short airflow-only screening case, audit mass balance and component
   flow direction, then enable heat.
6. Run the existing screening OpenFOAM profile before any long production case.
7. Compare at least rack inlet/outlet temperatures, component exhaust
   temperatures, and total rack flow against lab measurements.

Do not treat a one- or two-day run as inherently more trustworthy: establish
mass balance, energy balance, residual behavior, and measurement agreement on a
short case first.

## Extensive validation update (2026-08-22)

### Changes made from test findings

- Increased the main simulation horizon from 10 to 30 seconds because the
  OpenFOAM screening profile requires a 20-second airflow warmup.
- Increased the exporter memory safety cap from 500 to 1,024 MB. The screening
  export contains 1,715,112 cells and uses about 768 MB while exporting.
- Corrected N5766A/N6701C fan and vent directions that were tangent to their
  zero-thickness planes.
- Inset Keysight internal fans to their internal-air boundaries and created
  resolvable exhaust plenums.
- Removed overlapping N5766A heat volumes and corrected rear fan footprints
  that extended 5 mm outside the chassis.
- Added provisional P-Q curves to every internal fan that lacked one. These are
  explicitly placeholders, not validation-quality manufacturer data.
- A later consistency audit showed the initially reused generic curves were not
  safe placeholders: their zero-pressure flows were 318 and 2,975 CFM for fans
  labeled 18-171 CFM, leaving almost full shutoff pressure at the nominal flow.
  Six device-specific provisional curves now retain the former generic shutoff
  pressures but reach zero pressure at each stated nominal CFM (UPS 45, Dell 45,
  Keysight 35, thruster 18, Cisco 75, and NI 171 CFM). A contract test verifies
  every provisional curve's analytic zero exactly matches its fan rating. A
  seventh lab-only provisional curve applies the same correction to the 271-CFM
  roof array without changing the existing shared `top_fan_MS1238E-H` curve.
- Corrected the N5766A and Dell R360 display names.
- Fixed forced-native model loading so `--native` retains the root `[mesh]`
  instead of silently inheriting an OpenFOAM profile mesh.
- Fixed `plot_component.py` so a rack export plots one component rather than
  combining every component in incompatible local coordinate systems.
- Added `tools/audit_component_geometry.py` for reusable bounds, surface-plane,
  and rack-overlap checks.

### Regression evidence

- The project harness passed all 12 compiled C++ suites through the point where
  the initial new-model horizon policy failed. After the horizon correction,
  the failed 11-test engineering suite passed.
- The full compiled C++ harness passed all 12 suites, including airflow parsing,
  export, connectivity, PCG flow, porous-region analytics, subcycling, model
  configuration, runner commands, and run metadata.
- After discovering that the checked executable predated the native-mesh fix,
  both `model.exe` and `test_runner.exe` were rebuilt from the current sources
  with the documented optimized commands. The rebuilt complete C++ harness
  again ended with `ALL UNIT TESTS PASSED`. A focused Python rerun covering the
  lab contract, plot geometry/color policy, progress parser, and heat-source
  audit passed all 73 tests.
- Python discovery passed 164 tests with four documented optional skips. Four
  Matplotlib-dependent suites that are polluted by the aggregate harness's
  `sys.path` ordering were rerun independently with the bundled scientific
  runtime; all 37 tests passed. The two NumPy-dependent recirculation tests that
  the PowerShell system-Python stage could not import are included in those
  successful isolated runs.
- The rack/component auditor reports 0 errors across the active reusable
  components. It intentionally reports one NI air/air overlap warning until the
  missing separator walls are modeled.
- An explicit audit of all eight newly supplied component files, including the
  currently uninstalled Dell template, also reports 0 errors and only the same
  declared NI air/air overlap warning.
- An eight-test laboratory model contract parses every referenced component and
  locks the supplied rack envelope, altitude/ambient inputs, 12 installed
  component instances, provisional NI location, complete nine-fan roof array,
  and front vent. It also verifies that the supplied Dell template remains
  parseable but deliberately uninstalled because that rack entry was commented
  out in the source layout. One test locks the supplied dimensions,
  air/solid/fan/vent counts, and
  configured heat totals for each of the eight newly supplied component files.
  Another locks the installed per-instance allocation (150, 360, 360, 425, 20,
  0, 10, 150, 10, 15, 15, and 0 W) and its exact 1,515-W sum, preventing a
  correct rack total from hiding a misplaced component load. The seventh test
  verifies that every installed rack/internal fan curve resolves and has finite,
  physically signed coefficients. The eighth prevents the native-smoke and
  workspace-export variants from drifting away from the main rack's component
  placements and fan physics. All eight tests pass.
- Geometry-only export succeeds after all corrections.
- The component-export executable now accepts
  `main.exe [component.toml] [fan_curves.toml]`, loads the shared curve library,
  and exits after export instead of falling through into the legacy hardcoded
  simulation. Both its normal NI invocation and its usage-error exit were
  exercised. Headless Matplotlib renders of the UPS and NI components were
  inspected; the NI image independently confirms that both named air volumes
  use the same cyan fill while vents and fans retain distinct colors. The image
  also makes the documented unfinished/overlapping NI air geometry visible.
- The original file-open failure is not present in the current checkout:
  `library/models/validation_fan_rack.toml` exists and is tracked. More
  importantly, the explicit new-model command `.\model.exe --geometry-only
  library\models\new_model.toml` exits 0, writes `output.txt`, and atomically
  records the selected new model in `.thermal_sim_last_run.json`. Validation
  commands should name the model explicitly rather than depend on the unrelated
  default case.
- Both rack and component plotting scripts execute headlessly. Visual inspection
  confirmed consistent cyan air regions and exposed/fixed the former multi-
  component accumulation bug. The component plotter now routes region colors
  through a testable `component_region_color` policy; six focused plot tests
  verify centered surfaces, case-insensitive cyan for every air region, matching
  rack/component air colors, and unchanged cycle colors for non-air regions. The currently installed system and
  Codex-runtime Python distributions lack Matplotlib, so a fresh rendered-image
  rerun in this continuation is unavailable; the policy and syntax tests pass,
  while the earlier headless render remains the visual evidence.
- During the live four-rank continuation, 12 focused standard-library regression
  tests were rerun for vent plotting geometry, heat-animation vent geometry,
  component-report parsing/time selection, and OpenFOAM heat-source validation;
  all 12 passed in 1.286 s. The default Windows Python does not include `pytest`,
  so this targeted rerun used `python -m unittest` and required no dependency
  installation.
- A subsequent runner-policy/progress rerun passed 63 tests covering semi-frozen
  thermal behavior, profile limits, restart/checkpoint integrity, cumulative air
  exchange, gate parsing, health reporting, and ETA logic. Discovery of the
  separate field-convergence module initially failed before assertions because
  the system Python lacks NumPy. The bundled workspace runtime was subsequently
  located and used to run the field-convergence, field-delta, and OpenFOAM field
  validation suites: all 34 tests passed in 2.883 s. This removes the NumPy test
  gap. The same runtime also passed all 32 exhaust-tracer and recirculation-report
  tests in 3.431 s; only fresh Matplotlib rendering remains unavailable in
  current runtimes.
- The progress reporter initially aborted when an interactive solver launch had
  no persisted `*.stdout.log`. It now falls back to explicitly labeled durable-
  checkpoint-only reporting instead of inventing live diagnostics. It now also
  derives a conservative rate from completed `run_summary.log` airflow stages
  when the stdout log is absent, while explicitly labeling durations as measured
  from the latest durable checkpoint rather than live ETAs. All 54
  progress-tool tests pass, including a new end-to-end no-log regression. On the
  active case it reports four common checkpoints across four ranks, aligned
  53-file manifests per rank, latest durable time 0.15 s, and next checkpoint
  0.25 s and a durable rate of 94,992 wall-s/sim-s. Courant, temperatures, and
  fatal-signature scanning remain correctly unavailable without a log. At that
  measured rate, reaching the 20-s maximum warmup would take about 22 days; the
  acceptance gates must terminate much earlier to satisfy the requested one-to-
  two-day practical limit.
  The superseded generated runner embeds 1.063823 m3 of fluid. Its former roof
  curves cap ideal zero-pressure flow at 0.9 m3/s, yielding a 1.182-s exchange
  time and a roughly 35.1-wall-hour lower bound from fan-ramp start (40.4 h with
  two confirmation windows). For the corrected 271-CFM provisional roof curves,
  the equally idealized maximum is 1.15108 m3/s: the runner's mandatory 0.30-s
  observation and trapezoidal accumulator still prevent one exchange before
  about t=1.124 s, corresponding to a 28.3-hour lower bound at the measured rate,
  or 33.6 h with two additional 0.1-s confirmation windows. Real system
  resistance can only increase this bound, leaving little or no two-day budget
  for the thermal stage unless a validated acceleration or mapped start is used.
  Future validation runs should use the command emitted by Model Runner,
  including `set -o pipefail` and `2>&1 | tee -a thermal_solver.stdout.log`, so
  pipeline failures propagate while the progress reporter retains live rate,
  health, temperature, and ETA evidence. The generated-command regression tests
  assert both `pipefail` and the expected log filenames.

### Native solver benchmark

An optimized `-O3 -DNDEBUG` runner was used; the earlier debug-build timeout is
not used for the conclusions below.

- Main coarse mesh: 157,950 cells, about 70.8 MB reported cell storage.
- Initial pressure solve: roughly 3.5 minutes, 20 nonlinear outer iterations,
  88-778 PCG iterations per outer solve.
- Nonlinear relative-change target: not met in 20 iterations.
- Physical mass imbalance: 0.0581%, within the solver's 5% coarse acceptance
  limit.
- Effective source/vent flow magnitude: about 1.423 m³/s (~3,015 CFM).
- Raw Norton fan source magnitude: about 42.1 m³/s, which is not physically
  credible and reinforces that the provisional fan data/extrapolation dominate
  this screening result.
- Coarse stable conduction timestep: 0.0904 seconds; the original 0.1-second
  coarse timestep was rejected and was changed to 0.05 seconds.
- Estimated advection timestep: about 0.000257 seconds. A 0.05-second global
  step needs approximately 244 substeps on the main coarse case.

The separate `new_model_native_smoke.toml` was rerun after making the 5,500-ft
initial density consistent (0.9833 rather than 1.225 kg/m³), preserving a
resolvable 30-mm side-air channel upstream of both Keysight side fans, and
installing the CFM-consistent provisional curves. This also exposed a deployment
error: `model.exe` predated the source fix that prevents an explicitly native run
from inheriting the OpenFOAM profile mesh. The stale binary built 1,715,112 cells
and spent more than 36 minutes without completing its first serial pressure
solve. Rebuilding with the documented optimized command restored the intended
135,000-cell smoke mesh; this is now a required build-provenance check.

The rebuilt corrected-curve run completed 0.1 simulated seconds. Both pressure
solves completed 20 nonlinear outer iterations; every PCG pressure equation
reached the 1e-6 target, although nonlinear face flow still did not meet its
outer target. Final mass imbalance was 0.00073933 m³/s, about 0.0502% of the
1.4725-m³/s mean source/vent magnitude. All 405,000 inspected CSV rows were
finite. Maximum temperature reached only 20.0224 C, which is expected for this
short transient and is not thermal validation. Maximum air speed fell from
57.00 m/s initially to 53.15 m/s at 0.1 s, concentrated in top-boundary cells.
This is worse than the former 40.15-m/s diagnostic and remains physically
implausible; the native Norton/one-cell fan representation is unsuitable for
quantitative flow validation even when its curve endpoints are internally
consistent.

Conclusion: the native explicit solver can complete a shake-down case, but it
is not practical for long-duration simulation of this high-flow, thin-featured
rack. OpenFOAM is the appropriate production path after fan/geometry calibration.

### OpenFOAM export evidence

The full screening case exported successfully to a no-space validation path:

- 1,715,112 cells
- 283 exported files
- about 684 MB on disk
- 12 solid component regions plus the fluid region
- 22 non-empty heat sources totaling exactly 1,515 W
- three connected fluid volumes: main rack plus sealed KVM/storage and PDU cavities

Export initially caught missing fan curves, insufficient memory allowance,
Keysight wall-blocked fan cells, and a whitespace-invalid MPI path; all model
issues were corrected. The whitespace path was limited to the validation copy.

OpenFOAM 2606 under WSL successfully prepared the corrected case on its native
ext4 filesystem. Region splitting produced 1,538,360 fluid cells and 12 named solid
regions. All 22 heat sources became non-empty, mutually non-overlapping prepared
cell sets and the active `fvOptions` powers matched the exported metadata exactly
(1,515 W total). All 27 internal fan/vent regions and seven external porous or
intake regions were also non-empty.

The installed component allocation is UPS 150 W, N5766A 360 W, N6701C 360 W,
Trenton 425 W, KVM 20 W, storage shelf 0 W, thruster load box 10 W, Cisco 150 W,
PDU 10 W, two Meanwell supplies at 15 W each, and NI 0 W. Geometric source
densities span about 6.67 kW/m3 in the KVM zones to 375 kW/m3 in each compact
Meanwell block. Those densities are numerically resolved (the Meanwell zones
contain 112 cells each) but remain assumed spatial distributions; localized
temperature peaks must be treated as model-form sensitivity until device power
and heat-spreading volumes are measured.

`checkMesh -allRegions -allGeometry -allTopology` passed every topology,
openness, volume, non-orthogonality, skewness, AMI-weight, and interpolation
check. Twelve regions received determinant-only warnings below 0.001; the
generated preparation script accepts and records only that warning class. The
fluid region had 1,062 such cells, max aspect ratio 39.472, zero maximum
non-orthogonality, and max skewness 7.1e-13. It contains five face-disconnected
fluid volumes in the superseded pre-outlet case. The corrected case has three;
all physical openings map exclusively to the main rack region.

A distinct post-curve-correction case, `thermal_lab_corrected_curves`, was then
exported and copied to `/home/hconner158/thermal_lab_corrected_curves` so it
cannot overwrite or restart from the superseded field. Fresh preparation again
produced 1,538,023 fluid cells and the same 12 solid regions. Every one of the
22 configured heat zones is nonempty and the audit totals exactly 1,515 W; the
exported provenance contains all seven `provisional_*` curves. Four-rank fluid
decomposition contains 440,747, 373,673, 367,595, and 356,008 cells (14.63%
maximum imbalance). The corrected run started from zero and completed the
20%-pressure ramp through an aligned four-rank 0.01-s checkpoint in about 407
solver wall-seconds, materially faster than the superseded run's approximately
1,268-second first ramp. Maximum Courant at the checkpoint was 3.33851, final
pressure corrections reached approximately 1e-7, and the run advanced into the
40%-pressure stage. This remains ramp/startup evidence only, and all airflow
acceptance gates restart from zero for this case.

The 40%-pressure ramp subsequently completed through an aligned 0.02-s
checkpoint in approximately 366 solver wall-seconds. Its terminal maximum
Courant number was 3.95265 (a following report recorded 3.98674), still below
the limit of 5, and the generated runner advanced to 60% pressure without a
restart or field error.

The 60%-pressure stage reached an aligned 0.03-s checkpoint in approximately
402 solver wall-seconds. During the 80%-pressure stage, transient Courant peaks
of 5.41 and 5.76 caused the adaptive controller to reduce `deltaT` from 0.001 s
to 0.000875 s and then 0.000729167 s. It recovered to 4.95 before writing the
aligned 0.04-s checkpoint after approximately 432 solver wall-seconds. The
runner then applied full pressure; its immediate pre-step Courant estimate was
6.68, so full-pressure acceptance depends on the controller reducing the live
timestep and producing a stable 0.05-s checkpoint.

That full-pressure gate passed numerically. Subsequent peaks through 5.67 drove
the timestep down to 0.000615385 s; terminal/postflight maximum Courant was
3.99784 and all four ranks wrote 0.05 s after approximately 449 solver
wall-seconds. The next preflight chose `deltaT=0.000440529 s` (predicted maximum
Courant 4.53755) for the 0.05-to-0.15-s adaptive airflow window. The 0.05-s wall
report also shows why this is screening rather than mesh-independent CFD:
rack-wall y+ averages 13.33 (maximum 36.47), while local component-interface
maxima reach 141 at N6701, 265 at NI, 362 at Cisco, and 384 at the thruster box.
Those local peaks require targeted mesh/wall-treatment sensitivity before final
industry validation.

At live time 0.0517621 s in the corrected 0.05-to-0.15-s window, the progress
auditor measured 63,333 wall-seconds per simulated second and estimated about
1 h 44 min to the next aligned checkpoint. Maximum Courant was 3.86478,
cumulative continuity error was -8.95e-6, no fatal signature was present, and
the four ranks remained active with 378 MiB available memory. This is a
materially better short-window rate than the superseded case, but the completed
0.15-s checkpoint and its velocity-change result remain authoritative for
convergence and runtime decisions.

The first corrected observation attempt subsequently exposed a runner
robustness issue before reaching that checkpoint. With its fixed
`deltaT=0.000440529 s`, maximum Courant increased monotonically from 3.55 to
4.49 by `t=0.06278 s`; extrapolation would cross the configured hard limit of
5 well before the 0.15-s postflight check. The attempt was stopped cleanly
instead of spending the remaining window on a result the runner would likely
reject. No partial time was durable: every rank still had the same latest
53-file checkpoint at 0.05 s. The generated live-stage preflight now retains
35% rather than 20% Courant headroom. Its contract test passes, and the
existing case runner was patched identically before restart. That restarted
window selected 280 exact steps at `deltaT=0.000357143 s`; its first completed
steps reduced maximum Courant from 3.24 to 3.18 with no fatal signature.
At approximately the same physical time (`t=0.0618 s`), the restarted window
reported maximum Courant 3.30 versus roughly 4.2--4.4 in the abandoned window.
That same-time comparison confirms the timestep change produced the intended
roughly 20% reduction while retaining about 34% margin to the hard limit of 5;
the solver therefore initially continued. It later reached maximum Courant
3.56 by only 14% of the window, with an increasing recent slope. Because that
trend again projected a hard-limit crossing far ahead of 0.15 s, the run was
cleanly stopped at its unchanged durable 0.05-s checkpoint. The production
preflight now targets 50% of the hard limit; this is slower but appropriate for
an accelerating cold start whose maximum velocity is not established at the
checkpoint.
The 50%-target restart subsequently passed the former stop region: at
`t=0.06346 s` its maximum Courant was 2.55, compared with 3.56 in the rejected
35%-headroom attempt near `t=0.06429 s`. Pressure corrections remained
convergent, cumulative continuity error was `-7.26e-6`, no fatal signature was
present, and all four ranks remained active. This directly validates the
stronger preflight margin for the observed startup transient; the 0.15-s
postflight gate is still required before accepting the window.

That restart also revealed that the progress auditor temporarily displayed the
last completed timestep from the abandoned appended-log segment. It now reads
the newest `Time` marker for live progress while retaining completed
`ExecutionTime` samples for rate fitting. A restart-regression test reproduces
the old `0.062 -> 0.0503` rollback case; all 55 progress-auditor tests pass.
These changes improve operational robustness but increase the current
0.05-to-0.15-s window estimate to roughly three hours. That cost is preferable
to accepting or late-rejecting a hard-Courant violation.

The restarted initial-airflow stage also demonstrates why its temperature field must
become the heat-on baseline rather than assuming the initialized 293.15 K is
preserved. The runner disables only direct fluid-zone heat sources; all solid-
region heat sources remain active while CHT and perfect-gas sensible-enthalpy
pressure work advance. The early fluid range reached approximately
291.77--293.74 K. It then narrowed monotonically to 291.91--293.54 K while the
solid maxima initially remained close to ambient because less than a second of
physical time had elapsed. This is consistent with a compressible fan-ramp and
buoyancy-aware CHT startup, but its eventual settled range and spatial location
must be checked before attributing later deltas to restored fluid-zone heat.
At the same sample, pressure corrections reduced final residuals into
the `1e-5` to `1e-6` range, cumulative continuity error remained about
`-9.3e-6`, and maximum Courant had peaked at 3.58 then declined to 3.51.

For this particular model, the distinction is stronger: all 22 mapped heat
sources and all 1,515 W are in solid regions. The prepared case's
`constant/fluid/fvOptions.fullFan` and `fvOptions.flowOnly` have identical
SHA-256 hashes and compare byte-for-byte equal, so the initial-airflow switch
suppresses exactly 0 W. The complete configured load is active from time zero.
At 0.15 s that represents only 227.25 J, or about 0.00182 K on the exported
aggregate 124,710-J/K solid capacity before cooling, so the current short
checkpoint remains predominantly an airflow transient. At the 20-s startup
safety horizon the same aggregate adiabatic bound is about 0.243 K, while the
fastest individual equivalent region (N5766A) could rise about 0.64 K. Long
air-exchange settling must therefore be treated as coupled heating, not a
strictly isothermal flow solve.
The reusable `openfoam_heat_source_audit.py` now reports this solid/fluid source
and watt split directly, its five focused tests pass, and the active case's
`heat_source_audit.md` was regenerated with `solid 22 / 1515 W` and
`fluid 0 / 0 W`. The complete focused lab/audit/plot set remains 82/82 passing.
The progress parser initially hid this range because its fluid-region pattern
recognized thermal-only wording but not the live solver's `Solving for fluid
region` wording. The repaired parser reports both fluid bounds explicitly; a
new regression reproduces the live wording. All 59 monitor tests pass after
the safety-factor synchronization checks below.
The monitor now also reads the safety fraction directly from the generated
runner instead of duplicating it as an independent constant; two additional
tests cover the emitted value and the current-exporter fallback.
The monitor also suppresses stale rate/ETA output between a checkpoint rollback
and the first completed timestep of the new solver segment; a regression covers
that abandoned-later-time gap explicitly.

Fresh two-rank decomposition completed in 137 seconds with 812,187 and 725,836
fluid cells (5.61% maximum imbalance). All solids are pinned to rank 0, limiting
large-rank scalability. An earlier prepared revision instantiated every region
and source and entered its first fan-ramp timestep. Its first pressure correction
reduced the residual from 0.8393 to 0.00751 in seven GAMG iterations; a later
correction reached 3.94e-4 in 16 iterations. The current revision also reached
time 0.001 s twice. Both attempts were stopped deliberately at the first velocity
solve when a second case restarted concurrently. On the latest attempt, the two
corrected-model ranks used about 2.18 GiB while the other four ranks used about
1.15 GiB; WSL had only 218 MiB available and had begun swapping under its 3.7-GiB
cap. Stopping only the corrected case returned about 2.3 GiB available. The
external case's archived provenance uses the former `x = 12.5` Keysight heat-block
layout, so its later-time results are historical runtime evidence, not validation
of the repaired geometry. The first concurrency attempt had caused a
kernel-recorded OOM kill, making this a reproducible resource-concurrency limit
rather than evidence of numerical divergence. This established that the default
two-rank layout was not safe under concurrent load; the four-rank mitigation below
was tested next. No steady or thermally developed OpenFOAM result, mass balance,
or energy balance should yet be claimed.

A subsequent four-rank trial materially improved the resource picture. The
corrected case decomposed successfully in 234.5 seconds while the older case was
also running. Its fluid partitions contain 440,415, 373,603, 355,885, and 368,120
cells (14.54% maximum imbalance, because interface-connected cells and every solid
region remain pinned to rank 0). The four corrected solver ranks used about 1.60
GiB total versus about 2.18 GiB on two ranks, leaving roughly 1.43 GiB reported
available even with the older four-rank solve resident. The corrected model then
completed the first 20%-fan-pressure stage through the written 0.01-s checkpoint
without floating-point or linear-solver failure. Pressure residuals typically fell
to about 1e-7 in the final correction, cumulative continuity error remained below
7e-6, and maximum Courant number remained below 3.7 versus the configured limit of
5. Stage 1 required about 1,230 CPU-seconds/1,268 wall-seconds. Stage 2 was then
stopped deliberately at the user's request to avoid competing with another
core-intensive process: the four solver ranks were each consuming about one full
logical core, system load averaged about 6 on eight WSL CPUs, and solver residency
had grown to about 2.9 GiB with 616 MiB swapped. All four ranks retain the same
complete 0.01-s checkpoint; stopping returned about 3.4 GiB available memory and
reduced swap use to 70 MiB. These are transient startup diagnostics, not converged
flow or thermal results.

After the competing Fluent workload was paused, the corrected case was resumed
from the common 0.01-s checkpoint on the existing four-rank decomposition. Fan
ramp stages 2 through 4 completed through 0.04 s at 40%, 60%, and 80% pressure,
respectively. Stage 2 required about 616 CPU-seconds/629 wall-seconds. The 0.02-s
wall-function report had rack-average y+ 8.46 and rack maximum 23.0; the largest
component-interface maxima were 85.27 at the Cisco switch, 65.23 at the NI
chassis, 58.07 at the Keysight N6701, 53.70 at the thruster load box, and 41.48
at the Keysight N5766. These values are screening evidence: they identify where
the current wall treatment is most stressed, but they do not establish mesh
independence.

At full fan pressure the adaptive Courant controller was exercised rather than
merely configured. A transient peak Courant number of 5.314 caused the timestep
to fall from 0.001 s to about 0.000875 s and then about 0.00044 s. Subsequent
peak values have generally remained below the limit of 5 (representative values
3.57-4.14). Final pressure corrections repeatedly reach approximately 1e-7;
velocity, turbulence, and fluid-energy equations remain finite, and cumulative
continuity error has stayed at order 1e-6. The cold-flow fluid temperature range
has remained approximately 291.7-293.7 K. Because fluid heat sources are disabled
during this initial-airflow phase, those temperatures are startup diagnostics and
must not be interpreted as equipment thermal performance.

The live run completed and durably wrote the 0.05-to-0.15-s initial-airflow
stage on all four ranks in 9,499.203 wall-seconds. Peak Courant remained
controlled near 4.75 after the
earlier 4.94 near-limit event, final pressure corrections continued reaching
approximately 1e-7, and cumulative continuity remained at order 2e-6. The
next 0.15-to-0.25-s window also completed on all four ranks in 7,379.724
wall-seconds. Its postflight maximum Courant number was 3.87367, below the
configured limit of 5, and the restart directories and field manifests are
aligned at 0.25000000000000183 s. The tracked spatial velocity change fell from
58.2997% at 0.15 s to 40.5762% at 0.25 s (`rmsDelta=0.731426 m/s`,
`rmsVelocity=1.8026 m/s`). This is meaningful numerical progress but remains
more than an order of magnitude above the 3% screening criterion and forty
times the strict 1% acceptance limit. The runner requires at least 0.30 s of
cold-start observation, a calculated full rack air exchange, no more than 1%
boundary mass imbalance, no more than 1% tracked-flow change, velocity-field
convergence, and correct fan directions before accepting the operating point.
The 0.25-s checkpoint therefore establishes restartability and numerical
stability, but correctly leaves the initial-airflow gate pending rather than
claiming physical convergence. In particular, the durable
`.velocity_convergence_state` records `0.25000000000000183 0.405762 0.582997`.
The corresponding
`.initial_air_exchange_state` remains `0.05 0 0`: the exchange accumulator has
not yet become eligible during the mandatory cold-start observation period.
These state files independently confirm that neither velocity convergence nor a
full calculated air exchange has been demonstrated yet.

The case was exported before the device-specific provisional curves were
introduced. It was intentionally stopped at the aligned 0.25-s checkpoint after
finishing the restart, stability, and runtime measurement; the run lock was
removed and no solver ranks remain. Its airflow field is now a
superseded input-sensitivity experiment and must not seed thermal conclusions.
A fresh export/restart with the corrected provisional curves is required.
At its durable 0.15-s state, the old operating-point files already show why:
the UPS reports 56.4 CFM against a 45-CFM label, the N5766 rear fan reports
80.9 CFM against 35 CFM, while several thruster/Cisco/N6701 fans remain near
zero or slightly reversed. NI fans report only about 13.2 and 12.3 CFM against
171 CFM. These are neither settled nor mutually credible operating points and
independently preclude reusing the old airflow field.

Resource behavior was also measured at full load. With no competing solver, WSL
initially had about 274 MiB available and 697 MiB of 1-GiB swap used; rank RSS was
approximately 1.02, 0.71, 0.64, and 0.62 GiB. A later short-lived peak left 227
MiB available with rank RSS about 1.10, 0.77, 0.73, and 0.66 GiB. The footprint
then receded to 458 MiB available and about 1.03, 0.68, 0.65, and 0.59 GiB RSS,
with swap essentially unchanged. During the 0.15-to-0.25-s continuation another
short-lived high-water observation reached only 115 MiB available, but one
minute later recovered to 379 MiB while rank RSS fell to approximately 1.01,
0.66, 0.64, and 0.58 GiB; swap changed by only about 4 MiB. This continues to
look like temporary solver working storage rather than monotonic leakage, but
the 115-MiB minimum is operationally fragile. This supports four ranks as a workable but
low-margin screening configuration under the 3.7-GiB WSL cap. A subsequent
sample showed 454 MiB available and 761 MiB swap used while all four ranks were
active, again demonstrating recovery rather than monotonic growth; it is not adequate
production memory margin, and concurrent solvers remain unsafe.

### Reproducible validation commands

Run from the project root in PowerShell:

```powershell
.\model.exe --geometry-only library\models\new_model.toml
python tools\audit_component_geometry.py library\models\new_model.toml
python tools\openfoam_material_fidelity_audit.py library\models\new_model.toml
python -m unittest tests.lab_model_contract_test tests.openfoam_heat_source_audit_test tests.openfoam_progress_test tests.plot_geometry_test
```

The NumPy-dependent field and recirculation suites use the bundled runtime:

```powershell
& 'C:\Users\hconn\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' -m unittest tests.openfoam_field_convergence_test tests.openfoam_field_delta_test tests.openfoam_validation_test tests.exhaust_recirculation_tracer_test tests.recirculation_report_test
```

For the current WSL case, the non-mutating audits are:

```powershell
wsl.exe -d Ubuntu -- bash -lc "python3 '/mnt/c/Users/hconn/Downloads/Thermal Sim/v2.2/tools/openfoam_progress.py' /home/hconner158/thermal_lab_corrected_curves --fast"
wsl.exe -d Ubuntu -- bash -lc "python3 '/mnt/c/Users/hconn/Downloads/Thermal Sim/v2.2/tools/openfoam_heat_source_audit.py' /home/hconner158/thermal_lab_corrected_curves"
wsl.exe -d Ubuntu -- bash -lc "python3 '/mnt/c/Users/hconn/Downloads/Thermal Sim/v2.2/tools/openfoam_thermal_mass_audit.py' /home/hconner158/thermal_lab_corrected_curves"
```

Future solves should preserve their console audit trail and pipeline exit status:

```bash
set -o pipefail
./run_parallel.sh 4 --multirate 30 2>&1 | tee -a thermal_solver.stdout.log
```

### Physical credibility verdict

The model is useful now for geometry/export shake-down and identifying missing
measurements. It is not yet credible for absolute temperatures or final flow
distribution. Before a one- or two-day run, the minimum required inputs are:

| Readiness gate | Status | Authoritative evidence / remaining proof |
| --- | --- | --- |
| Model/component parsing and bounded geometry | Pass for current provisional geometry | Project-root geometry-only run exits 0; reusable audit has 0 errors across 10 components. |
| Final as-built geometry | Not met | Rail 2 depths remain uncertain; NI separator walls are absent and produce the declared air/air overlap. |
| Plot consistency | Pass for implemented policy | Six focused tests cover centered surfaces and matching cyan rack/component air colors; fresh inspected UPS and multi-air-region NI PNGs provide runtime visual evidence. |
| Native solver shake-down | Pass only as a diagnostic | The rebuilt 135,000-cell smoke case completes with 0.0502% mass imbalance and finite output, but 53.15 m/s maximum speed and raw Norton flow are not physically credible. |
| OpenFOAM mesh/export | Screening pass with limitations | 1,715,112 cells; corrected case has three fluid bodies, all 16 physical openings in the main rack region, and only sealed KVM/storage and PDU cavities disconnected; all-region checks pass except accepted determinant warnings. |
| Heat-source mapping | Pass for configured assumptions | 22 nonempty zones allocate exactly 1,515 W; the wattages themselves are approximate inputs. |
| Initial airflow convergence | In progress; not passed | The corrected topology completed its five-stage pressure ramp and reached an aligned, restartable 0.15-s checkpoint without a fatal signature. This is only about 3.37% of one integrated air exchange, so it is not a settled-flow or convergence result. |
| Full rack air exchange | Not demonstrated | The full-pressure checkpoint implies about 4.15 simulated seconds per exchange; only 0.05 s had accumulated when adaptive observation began. The runner requires at least one exchange before acceptance. |
| Boundary mass balance and fan directions | Mixed: mass balance passes at 0.15 s; fan direction fails | Four-rank post-processing at 0.15 s reduced tracked boundary imbalance to 0.681%, passing the 1% screening limit. Cisco fan 2 recovered, but both Keysight side fans remain negative and pinned at static pressure; the operating point is not acceptable. |
| Coupled thermal and energy balance | Formal screening not run / material model inadequate for hotspot validation | This model has no fluid-zone heat sources: `flowOnly` and `fullFan` are byte-identical, so all 1,515 W of solid heat and CHT remain active during initial airflow. The present subsecond energy input is tiny, but no thermal energy-balance acceptance stage has run. OpenFOAM also homogenizes every component to its outer aluminum-equivalent properties and ignores distinct internal-region materials, so even a balanced run cannot validate absolute hotspots without separate material regions or calibrated equivalents. |
| Production resource margin | Not met | Four ranks run under the 3.7-GiB cap but observed only 115 MiB available at a transient high-water point. |
| One-to-two-day runtime | Contradicted for fine-mesh cold start | The corrected adaptive observation currently measures about 78,000 wall-s/sim-s, while the full-pressure boundary flow implies about 4.15 simulated seconds per exchange. One exchange alone is therefore about 3.7 wall-days before convergence confirmation or thermal screening. Validated coarsening or a validated mapped airflow start is mandatory for a one-to-two-day workflow. |
| Fan and load calibration | Not met | Provisional curves are now internally consistent with stated CFM, but final measured P-Q curves, operating points, rack flow, and measured device powers are unavailable. |

Accordingly, no current passing gate overrides a `Not met`, `Not demonstrated`,
or `Not run` entry in this table.

The corrected-curve 50%-Courant-target restart was still healthy at
`t=0.0722527473 s` (22.25% of the 0.05-to-0.15-s observation window). Its
recent maximum Courant sequence was 2.93, 2.90, 2.87, 2.83, 2.79, 2.83,
2.87, and 2.85, so the earlier rise has become a bounded local oscillation
rather than a continuing approach to the hard limit of 5. Cumulative
continuity error was `-7.27e-6`, fluid temperature remained finite at
291.864--293.492 K, all four ranks still shared the aligned 0.05-s durable
checkpoint, and the progress monitor found no fatal signature. The measured
rate was about 112,000 wall-s/sim-s, projecting roughly 2.4 hours to the next
durable 0.15-s checkpoint. This supports numerical stability of the restart,
not airflow convergence or physical validation. WSL had only about 351 MiB
available and 736 MiB of swap in use during the sample, reinforcing the
instruction not to run a competing solver or memory-heavy test concurrently.

A later live sample reached `t=0.0920329670 s` (42.03% of the same observation
window). Maximum Courant was 3.02416, cumulative continuity error was
`-7.20e-6`, the fluid range remained finite at 291.856--293.521 K, and the
four-rank process remained active with no fatal signature in the current log
tail. This supersedes the earlier live-status sample while retaining the same
caveat: it is stability/progress evidence, not a converged airflow checkpoint.

The next audited sample reached `t=0.103296703 s` (53.30% of the observation
window), at about 101,646 wall-s/sim-s with an estimated 1 h 19 min remaining
to 0.15 s. Maximum Courant was 3.15695; the last 40 reports rose from 3.04 to a
local peak of 3.16113 and then eased to 3.15695, so the run was retained rather
than rolled back. Cumulative continuity was `-7.29e-6`, fluid temperature was
291.825--293.540 K, all four processes were present, checkpoints remained
aligned at 0.05 s, and no fatal signature was found. Available WSL memory was
397 MiB with 788 MiB swap used, so no competing high-memory solve should start.
At `t=0.105219780 s`, the last 12 maximum-Courant reports continued downward
from 3.16113 to 3.14757, continuity remained `-7.23e-6`, and the auditor found
no fatal signature. The estimated time to the 0.15-s gate was 1 h 16 min, so
the numerically bounded four-rank window was left running.
At `t=0.110439560 s` the stage was 60.44% complete, maximum Courant had fallen
to 3.12619, cumulative continuity was `-7.18e-6`, temperatures remained finite,
and the estimated checkpoint ETA was 1 h 07 min with no fatal signature.
At `t=0.112637363 s`, after the production rebuild, maximum Courant was 3.11736,
continuity was `-7.23e-6`, no fatal signature was present, and all four ranks
remained aligned at the durable 0.05-s checkpoint. The estimated ETA was 1 h
04 min; WSL reported 506 MiB available and 801 MiB swap used.

A fresh current-worktree regression run subsequently passed all 78 focused
tests in `tests.lab_model_contract_test`,
`tests.openfoam_heat_source_audit_test`, `tests.openfoam_progress_test`, and
`tests.plot_geometry_test`. The source attachment was also split at its eight
embedded component headings and parsed as independent TOML documents: the main
model plus DELL R360, Keysight N5766A, Keysight N6701C, Thruster Load Box,
Cisco Catalyst, Eaton PDU, fan-control supply, and NI chassis definitions all
parse, and their current project counterparts have matching region/fan/vent
counts. This establishes structural transfer from the supplied documentation;
the intentional runtime, altitude-density, plotting, and provisional fan-curve
corrections mean the files are not expected to be byte-identical. A fresh
`audit_component_geometry.py` and `model.exe --geometry-only` run also exited
successfully: 10 reusable components, zero errors, and the one declared NI
air/air overlap warning caused by the missing separator walls.

After adding the heterogeneous-material export warning, thermal-mass audit, and
model-level material-fidelity audit, the expanded focused set passed 82/82
Python tests. The isolated exporter C++
test passed both warning and silence branches; the broader added-feature suite
passed through the exporter, device report, connected volume, profile policy,
PCG, porous, subcycling, and face-wall tests. It then exposed a stale UPS fan-
curve assertion that still required `generic_80mm_low_speed` after the model
had intentionally moved to `provisional_eaton_ups_45cfm`; the assertion was
updated to require exactly one reference to the current curve and the full
`model_config_test` passed. The remaining runner-command and run-metadata tests
passed, the optimized `model.exe` was rebuilt and completed geometry-only mode,
and a rebuilt complete `test_runner.exe` ended with `ALL UNIT TESTS PASSED`.
The generated runner now also prints explicitly that solid-region sources,
CHT, and buoyancy remain active during initial airflow. Its focused exporter
regression passed, and the optimized `model.exe` was rebuilt afterward so the
checked production executable contains that operational warning.

After the Thruster/Cisco outlet correction, full Python discovery initially
exposed two environment/tooling defects instead of being silently narrowed:
user-site NumPy/Matplotlib were inaccessible inside the restricted shell, and
the `plot` directory lacked `__init__.py`, allowing `plot/plot.py` to hijack
package imports and execute its CLI parser during test discovery. The package
marker was added and the suite rerun with installed dependencies visible:
231/231 tests passed. `plot_component.py` now also accepts `--component` and
`--output`; fresh rack-export renders of the Thruster and Cisco show cyan air
and aligned fan/outlet footprints.

The prepared corrected case passed the executable connectivity gate
`--expect-regions 3 --opening-region 0`, the 22-source/1,515-W heat audit, and
the 138.436-kg / 124,592-J/K exported thermal-mass audit. Its replacement run
started on exactly four OpenFOAM ranks. Fluid decomposition is 486,393,
369,627, 307,212, and 375,128 cells (26.47% maximum imbalance); this is worse
than the prior decomposition and remains a runtime concern, but all ranks are
active. At the first completed `t=0.001 s` sample, the monitor reported ramp
stage 1 at 10%, maximum Courant 0, cumulative continuity `3.5146e-6`, aligned
checkpoints, and no fatal signature. WSL had 751 MiB available and only 2 MiB
swap used. The progress monitor's pre-first-sample `IndexError` was fixed to
report an initializing state; all 59 focused progress tests pass. These are
startup checks only, not convergence, mass-balance, air-exchange, or thermal
validation.

The corrected-topology ramp subsequently reached `t=0.003/0.010 s` with a
measured rate of about 128,000 wall-s/sim-s and an estimated 15 minutes to the
first durable 0.01-s checkpoint. Maximum Courant rose from 1.442 at 0.002 s to
1.767 at 0.003 s, still below the 2.5 diagnostic target and 5 hard limit;
cumulative continuity improved to `2.83497e-6`, the fluid remained finite at
293.022--293.176 K, and no fatal signature was present. This remains fan-ramp
stability evidence only.

The thermal-mass audit now implements the previously assumed `--markdown`
interface and writes a reproducible table artifact; its three focused tests
pass. A model contract also requires every Thruster/Cisco rear fan to retain a
unique matching outlet at the chassis rear with the same footprint and 5-mm
fan-to-wall spacing. The 17 combined model/topology/plot contract tests pass.

The corrected-topology 20%-pressure stage completed and wrote an aligned
`t=0.01 s` checkpoint with 52 files on each of four ranks. Its maximum Courant
peaked at 3.49179 at 0.008 s and fell to 2.64542 at 0.01 s; cumulative
continuity was about `8.17e-6`, the fluid range was 291.931--293.157 K, and no
fatal signature occurred. This first stage took about 19.6 wall minutes.

Saved internal-fan operating points at that checkpoint prove that the topology
repair carries flow: both Thruster fans are positive at 0.002151 and 0.002130
m3/s, and all four Cisco fans are positive at 0.000732--0.001631 m3/s. The two
Keysight side fans remain reversed at -0.003499 and -0.004830 m3/s at only 20%
fan pressure, while their rear fans are positive. Their definitions point in
the intended -x exhaust direction, so this is retained as an unsettled
low-ramp operating point; the full-pressure positive-flow gate must decide
whether the provisional curves overcome rack crossflow. Stage 2 began at 40%
pressure and reached `t=0.011 s` with Courant 2.95719, continuity `9.68102e-6`,
finite temperatures, and the aligned 0.01-s restart intact.

The 40%-pressure stage then completed at `t=0.02 s` with an aligned 52-file
checkpoint on all four ranks. Maximum Courant peaked at 4.23341 and finished at
3.97092, cumulative continuity remained of order `1e-5`, temperatures stayed
finite, and no fatal signature occurred. At that checkpoint all Thruster,
Cisco, NI, UPS, and rear Keysight fans were positive, but the two Keysight side
fans were still reversed at -0.0049996 and -0.0069346 m3/s.

The 60%-pressure stage also completed without numerical failure and wrote an
aligned `t=0.03 s` checkpoint. Maximum Courant stayed below the predeclared 4.5
intervention threshold (observed peak 4.26514, final sample 3.65142), cumulative
continuity was `1.78573e-5`, and the final fluid range was
292.537--293.481 K. The topology-repaired Thruster fans remained positive at
0.003484 and 0.003502 m3/s. Three Cisco fans were positive, but one collapsed
to `5.72e-6 m3/s`, effectively stalled at this transient checkpoint. The two
Keysight side fans worsened to -0.006028 and -0.008520 m3/s while their rear
fans remained positive at 0.01512--0.01642 m3/s. Thus this checkpoint passes a
short numerical-stability screen but fails the intended fan-direction/through-
flow acceptance criterion. The automatically launched 80%-pressure stage was
retained to determine whether added provisional fan pressure resolves or
worsens the adverse cross-rack operating points; none of these ramp states is
accepted as converged airflow or validated thermal performance.

The 80%-pressure stage completed at `t=0.04 s` with an aligned four-rank
checkpoint and no fatal signature. Maximum Courant finished at 4.29095. The
physical trend nevertheless worsened: Cisco fan 2 reversed to
`-0.000965 m3/s`, and the two Keysight side fans reached -0.006797 and
-0.009795 m3/s. A first 100%-pressure attempt then exceeded the predeclared
4.5 intervention threshold (maximum Courant 4.57915 at `t=0.046 s`) and was
stopped cleanly, preserving the 0.04-s checkpoint.

The screening profile's airflow timestep cap was reduced from 0.001 to
0.0005 s, its Python policy test passed, and the rebuilt C++ configuration test
passed. The generated live runner applies 0.0005 s to the fan ramp and adaptive
initial airflow; later screening refresh stages retain their independent
0.001 s cap. A four-rank full-pressure
retry from 0.04 s then completed an aligned `t=0.05 s` checkpoint in about
11.2 minutes. Maximum Courant was 2.18 on the first half-step and 2.69 on the
final sample, cumulative continuity was `2.76e-5`, the fluid remained finite at
292.329--293.480 K, and no fatal signature occurred.

That full-pressure checkpoint still fails physical acceptance. Cisco fan 2 is
reversed at -0.001801 m3/s and the Keysight N5766/N6701 side fans are reversed
at -0.007635 and -0.010987 m3/s. The other ten internal fans are positive.
Four-rank post-processing measured approximately 0.2524 kg/s one-way tracked
ambient flow and -0.02474 kg/s net tracked flow, or about 9.8% imbalance versus
the 1% acceptance limit. The implied instantaneous rack air-exchange time is
about 4.15 s, but the state has accumulated only 0.05 s and is not settled.
The managed adaptive initial-airflow window was therefore resumed rather than
accepted. Its Courant preflight selected 0.000478469 s and its first live
sample at `t=0.0504785 s` had maximum Courant 2.48894 with no fatal signature;
the next durable direction/mass-balance checkpoint is 0.15 s.

`tools/openfoam_fan_flow_audit.py` now makes the operating-point history
reproducible. It intersects numeric checkpoints across all processor ranks,
requires identical fan inventories and rank-consistent values, flags zero or
reverse flow, and can write Markdown. Its three focused tests pass. The live
case audit produced `fan_flow_checkpoint_audit.md` for 13 fans across the four
retained 0.02--0.05-s checkpoints and returned failure for 10 nonpositive
time/fan points, matching the manually observed Cisco and Keysight trends.

The corrected full-pressure adaptive observation subsequently reached an
aligned four-rank `t=0.15 s` checkpoint after about 1 h 46 min of solver wall
time from `t=0.05 s`. The final completed sample had maximum Courant 3.00315,
cumulative continuity `1.30862e-5`, fluid temperature 292.200--293.637 K, and
no fatal signature. The maximum Courant had peaked near 3.60 earlier in the
window and then declined, so this window passes the numerical-stability screen
but is not evidence of airflow convergence.

Four-rank `phi` post-processing at `t=0.15 s` measured 0.451917 kg/s one-way
exterior flow and -0.00307803 kg/s net exterior flow. The normalized mass
imbalance is therefore 0.681%, an improvement from about 9.8% at `t=0.05 s`
and below the 1% screening gate. With the generated runner's exact
1.063871626-m3 fluid volume and 0.9833-kg/m3 density, the instantaneous exchange
time is 2.315 s. A trapezoidal integration of the `t=0.05` and `t=0.15` one-way
flows accumulates only about 3.37% of one exchange, so the balance pass cannot
be interpreted as settled airflow.

The operating-point audit at `t=0.15 s` shows that the previously reversed
Cisco fan 2 recovered to +0.002621 m3/s and the other ten non-Keysight internal
fans are positive. The Keysight N5766 and N6701 side fans remain reversed at
-0.007677 and -0.011766 m3/s, respectively, while their rear fans remain
positive. The physical direction gate therefore still fails. The retained
audit now covers 13 fans at the three pruned checkpoints 0.04, 0.05, and
0.15 s and reports eight nonpositive fan/checkpoint points. The managed runner
was stopped cleanly just after it automatically entered the next observation
segment, preserving the aligned 0.15-s restart for analysis.

A matched four-rank fan-pressure sensitivity test was then branched from that
same durable checkpoint without modifying the authoritative case. Both branches
advanced exactly five steps to `t=0.15209205 s`. At the provisional 1x curve,
the N5766 and N6701 side-fan flow rates were respectively `-0.00770856` and
`-0.0117709 m3/s`, essentially continuing the 0.15-s values. Doubling only those
two fans' entire pressure curves increased their static pressure from 49.17 to
98.34 Pa but made the corresponding flow rates more negative:
`-0.0105399` and `-0.0158075 m3/s`. Their rear-fan flows remained near the
baseline (`+0.0155249` and `+0.00171234 m3/s`). This rejects the simple
"provisional curve is too weak" diagnosis over the matched transient and makes
a fan-zone orientation, upstream-face selection, or local side-discharge
coupling defect the leading explanation. The 2x curve is diagnostic only and
must not be promoted into the model.

OpenFOAM 2606's `fanMomentumSource` defines `flowDir` as the through-fan flow
direction, finds the upstream half of the enclosing face zone from that vector,
clips negative measured flow to the static-pressure point, and applies the
resulting gradient along `flowDir`. Four-rank CHT post-processing showed that
the N5766 and N6701 fan-zone mean velocities were strongly in the declared -x
direction (-10.10 and -10.34 m/s) even while the source reported negative flow.
This proves the source direction was active and instead identifies lateral
exchange through the unsealed perimeter of the partial-area fan cell zones as
the invalid operating-point measurement path.

An isolated 4,000-cell negative-x fan regression separated sign handling from
geometry. With a full-cross-section fan, four ranks reached 0.2 s with mean fan
velocity -1.725 m/s, positive 0.01821 m3/s source flow, maximum Courant 0.088,
and ambient net mass flow about `5e-10 kg/s`. A partial-area version reproduced
the need for a housing, so the exporter now converts the four lateral perimeter
strips of curve-driven internal fan zones into no-slip baffle walls. It rebuilds
the `chtCoupledInterfaces` face set after `createBaffles -overwrite`; without
that ordering fix, decomposition failed because `createBaffles` removed the
mesh sets directory. The baffled isolated case then reached 0.2 s on exactly
four ranks with mean fan velocity -3.309 m/s, positive 0.014947 m3/s source
flow, maximum Courant about 0.162, and ambient net mass flow about
`-5e-10 kg/s`. This closes the negative-axis and partial-area regression.

A separate full comparison case, `/home/hconner158/thermal_lab_fan_baffles`,
was exported at the unchanged 1,715,112-cell resolution. Preparation converted
94 internal lateral faces into paired baffles. The pre-existing three fluid
bodies and two fluid cells with two non-boundary faces were unchanged; the new
expected diagnostics are 94 duplicate baffle faces, 188 non-standard edge
connectivity faces, and shared-edge/non-manifold notices. The mesh gate now
reports those explicitly instead of falsely calling the result
"determinant-only." The two Keysight side-fan face zones changed from closed
102-face shells to open 72-face axial measurement zones, as intended.

The full baffled case decomposed and ran from 0 to 0.005 s on exactly four
ranks. Fluid partitions were 451,263, 374,102, 367,111, and 345,884 cells; rank
0 is 17.3% above average because all CHT interface-adjacent cells are constrained
there. All 13 CHT regions and fan sources remained finite, final maximum Courant
was about 1.11, cumulative continuity was about `4e-6`, and WSL recovered to
3.3 GiB available after the run. At this extremely early checkpoint, nine fans
were positive; the two Keysight side values were only `-4.0e-6` and `-5.3e-6
m3/s`, roughly three orders smaller than the old 0.15-s failures. This is strong
topology evidence but not a settled full-pressure direction pass.

Restart testing exposed a separate runner defect: a second `--warm-start`
restarted the fan ramp at 20% even though the prior successful warm start had
restored full pressure. The invalid continuation was stopped at its aligned
0.01-s stage boundary after fluid temperature undershot to about 292.55 K; it
is not used as physical evidence. Pristine full-pressure options were restored
and byte-verified in the root and all four processor directories. Generated
runners now write `.fan_ramp_complete` after a successful ramp and require its
absence before any warm-start or multirate reramp. The C++ exporter contracts
pass, and full Python discovery passes 234 tests with four intentional skips
under the bundled NumPy runtime.

### Superseding internal-fan topology experiments

The global internal-fan baffle approach above is rejected. Its corrected,
capped full-pressure continuation reached 0.01 s, but the NI bottom fan zones
developed approximately 63--71 m/s mean axial velocity and the fluid minimum
fell to 286.66 K inside the first bottom-fan footprint. The two Keysight side
fans also retained small negative source operating points despite approximately
-12 m/s mean zone velocity. These are artificial jet/energy artifacts, not a
credible housing model. No baffled checkpoint is accepted as an airflow or
thermal initial condition.

The exporter now leaves the fluid mesh unbaffled and constructs each
`fanMomentumSource` measurement face zone by intersecting the true outside
faces of its source cell zone with a thin box at the geometric upstream axial
plane. The intersection is essential: selecting the box alone passed a uniform
rectangular regression but included coplanar faces outside circular fan cell
zones on the adaptive full mesh; OpenFOAM rejected that topology before the
first timestep because the face zone was not wholly part of the cell-zone
boundary. Generated preparation no longer runs `createBaffles` or accepts
duplicate-face/shared-edge diagnostics.

The final intersection-based isolated regression completed to 0.2 s on exactly
four ranks. Its 16-cell negative-X fan used 16 upstream faces, reported
+0.0138217 m3/s at 0.2 s, had mean source velocity -3.43344 m/s, balanced its
two ambient openings to 1.33e-9 kg/s net, and kept fluid temperature within
293.144--293.150 K. This proves the direction sign and face-zone construction
for a controlled duct; it does not validate an unshrouded partial-area fan in
the rack.

A separate 1,715,112-cell full case,
`/home/hconner158/thermal_lab_fan_axial_intersection`, then passed the existing
connectivity, heat-source, thermal-mass, and determinant-only mesh gates. All
13 fan face zones were open, singly connected axial surfaces of 15--336 faces.
The complete five-stage 0--100% pressure ramp ran to an aligned 0.05-s
checkpoint on exactly four ranks with no fatal signature. Final maximum
Courant was 2.88769, cumulative continuity error was 2.91e-6, and the fluid
range was 292.221--293.494 K. WSL recovered to 3.3 GiB available after the
run. These are numerical-startup passes only.

The full-pressure physics gate fails. Eleven of thirteen internal fans were
positive at 0.04 s, but at 0.05 s Cisco exhaust fan 2 also reversed, leaving
three nonpositive fans. The Keysight N5766/N6701 side sources reported
-0.005811/-0.005050 m3/s even though their cell-zone means were strongly in the
declared -X direction at -10.685/-11.010 m/s. This demonstrates local
recirculation across the selected upstream planes rather than a direction-sign
bug. The NI bottom fans reported only +0.005665/+0.005277 m3/s while their
cell-zone axial means were 49.83/48.43 m/s. Although less extreme than the
baffled experiment, those velocities remain about nine times the approximately
5.6 m/s implied by 171 CFM over the nominal fan area and are not credible.

The retained numerical reports make the rejection quantitative. At 0.05 s the
tracked exterior openings carried 0.312051 kg/s inward and 0.278149 kg/s
outward, for -0.033902 kg/s net and 11.49% normalized imbalance. This is far
outside the 1% screening gate and also confirms that the five-stage ramp is not
a settled airflow solution. The direct equipment-zone report flags four
additional passive-opening direction reversals: the N5766 front and back
openings, one Meanwell top vent, and the NI back vent. It also shows 10.68 and
11.01 m/s axial velocity at the Keysight side-fan zones, versus about 4.96 m/s
from 35 CFM over each nominal 150-by-22.225-mm face. Those values cannot be
used as calibrated equipment flow.

The geometry-only path was rerun directly against `new_model.toml` and its fan
curve library and completed successfully. The independent component/rack
geometry audit checked all ten referenced component templates with zero hard
errors. Its one warning is intentional and readiness-limiting: the NI interior
and card-slot air boxes overlap until the documented separator walls are
modeled. These checks also verify that the original missing
`validation_fan_rack.toml` startup failure no longer reproduces.

Therefore neither enclosing faces, lateral baffles, nor an axial measurement
plane makes thin, unshrouded partial-area `fanMomentumSource` zones suitable
for this rack. A long coupled thermal run is intentionally withheld: it would
propagate a known-invalid airflow field. The next model change must provide a
physically resolved housing/duct with adequate settling length, or replace the
thin volumetric source with a validated actuator/pressure-jump formulation.
That change requires its own isolated partial-area, adaptive-mesh, negative-axis
regression plus full-rack direction, velocity, mass-balance, and energy gates.

1. Measured or manufacturer P-Q curves for roof and internal fans.
2. Measured rack total flow or fan operating points.
3. Final Rail 2 depths and NI separator walls.
4. Measured device power at the operating condition being modeled.
5. Prefer four ranks under the current 3.7-GiB WSL limit; two ranks used 36% more
   corrected-case memory and were unsafe beside another resident solve. A larger
   allocation is still recommended for production margin.
6. A short OpenFOAM run that passes mesh, connectivity, direction, mass-balance,
   heat-source, and energy-balance audits before extending the horizon.

## Deterministic fan-face orientation correction and full-rack retest

Inspection of the OpenFOAM 2606 `fanMomentumSource` implementation and an
isolated negative-axis duct case showed that the upstream measurement face zone
must have a deterministic orientation relative to the selected fan cells.  The
exporter previously converted a single `faceSet` directly to a `faceZone`; that
left the zone flip map dependent on mesh ownership instead of the declared fan
direction.  The exporter now uses `setsToFaceZone`, supplies both the upstream
face set and fan cell set, and sets `flip true`, which orients the upstream plane
into the fan cell set.  The focused four-rank negative-axis regression retained
the expected -X flow: 0.0138217 m3/s through the fan, -3.43344 m/s mean fan-zone
velocity, 1.33e-9 kg/s ambient net flow, and 293.144--293.150 K at 0.2 s.

A fresh 1,715,112-cell full-rack case was then generated, checked, decomposed
into exactly four nonempty fluid partitions (446,581, 373,493, 358,213, and
360,073 cells), and advanced through the complete five-stage 20/40/60/80/100%
pressure ramp to 0.05 s.  All 13 face zones were singly connected and oriented
into their fan cell sets.  The solve completed without a fatal error; maximum
Courant remained below 3.31, final pressure corrections reached about 1e-7,
cumulative continuity was -5.17e-6, and the final fluid range was approximately
292.17--293.52 K.  This is numerical startup evidence, not a settled thermal
result.

The correction disproves the earlier conclusion that all three historical
reversals were purely local recirculation.  At 0.03 s the two Keysight side fans
that had previously reversed now carried +0.005300 and +0.004563 m3/s in their
declared directions, and all 13 sources were positive.  At full pressure,
however, the Cisco second exhaust fan fell from +0.000972 m3/s at 60% head to
+0.000220 m3/s at 80% and -0.001843 m3/s at 100%.  The N6701C rear exhaust
remained essentially stalled, alternating between +1.15e-6, -1.38e-6, and
+1.05e-6 m3/s at 60, 80, and 100%.  Thus deterministic orientation fixes a real
export defect but does not make every unshrouded partial-area source physically
valid.

The final rack-wide gates still fail.  Tracked exterior openings carry
0.312053 kg/s inward and 0.278153 kg/s outward, a 0.033900 kg/s deficit and
11.49% normalized imbalance.  NI bottom-fan zone means remain 49.47 and
48.43 m/s while their source flows are only 0.005697 and 0.005224 m3/s, roughly
nine times the nominal-area velocity implied by 171 CFM.  The instantaneous
boundary sensible-heat result is -1.97 W versus 1,515 W applied, which is
expected to be meaningless after only 0.05 s and explicitly fails the coupled
energy-readiness gate.  A long thermal run remains withheld until the Cisco and
N6701C flow paths, NI velocity amplification, and exterior mass balance are
resolved.

### Isolated component flow-path tests

The two Keysight templates placed each rear fan 5 mm inside the chassis rear
surface but did not define a matching external opening.  This is materially
different from the Cisco template, which has an explicit rear outlet behind
each rear fan.  Matching full-area, unit-discharge rear outlet regions were
therefore added at the existing N5766A and N6701C rear-fan footprints.  The
change does not add a new airflow path; it makes the already-declared exhaust
opening penetrate the modeled enclosure wall.  Geometry-only validation still
passes, and the reusable audit remains at 0 errors / 1 intentional NI air-air
overlap warning.  A contract test now requires each Keysight rear fan to have a
matching outlet on the chassis rear surface.

An isolated 35,280-cell N6701C enclosure was then run through the same complete
five-stage ramp on exactly four ranks.  Both fans remained positive: at 100%
head the side fan carried 0.007628 m3/s and the rear fan carried 0.011066 m3/s.
The rack openings carried -0.003461 and +0.003448 kg/s, only 1.32e-5 kg/s net.
This directly confirms that the missing rear opening caused the stalled
full-rack N6701C result and that the outlet correction is effective in isolation.

An isolated 40,572-cell Cisco enclosure also completed on four ranks.  All four
fans remained positive at 100% head (0.001744, 0.002901, 0.002770, and
0.007525 m3/s), and its rack-opening mismatch was 7.43e-5 kg/s, about 0.68% of
one-way throughput.  However, exhaust fan 2 decreased from 0.003521 m3/s at
60% head to 0.002901 m3/s at 100% while the other fans generally increased.
Its full-rack reversal is therefore interaction-sensitive but the
counter-monotonic isolated response shows that the unshrouded measurement-plane
formulation remains suspect.  The Cisco source still requires a cyclic
pressure-jump or equivalently validated shrouded-interface test before another
full-rack acceptance run.

### Cyclic pressure-jump acceptance gate

A reversible case converter now replaces only internal `fanMomentumSource`
entries with paired cyclic `p_rgh` fan patches while preserving porous openings
and all heat sources.  It writes an immutable JSON copy of each full P-Q table
and a case-local scaler.  The generated parallel runner recognizes a prepared
cyclic-baffle mesh, does not destroy it by repeating region preparation, and
applies absolute 20/40/60/80/100% pressure scales to the root and latest field
on every rank.  Unit tests cover conversion, backup refusal, OpenFOAM's counted
table serialization, non-compounding restart scaling, and cyclic-flow audits.

The fresh 40,572-cell isolated Cisco case completed that full staged ramp on
exactly four ranks.  All four retained 60/80/100% checkpoint flows increased
monotonically.  Their mass flows were respectively
`0.002828/0.002644/0.002791/0.003159`,
`0.003473/0.003335/0.003474/0.003749`, and
`0.004020/0.003938/0.004052/0.004254 kg/s`.  Final estimated volume flows were
0.004092, 0.004008, 0.004124, and 0.004330 m3/s; none reversed.  Maximum
Courant was about 1.12, cumulative continuity was 5.76e-7, and the two exterior
openings were -0.011649 and +0.011834 kg/s (1.56% net relative to one-way
throughput).  This passes the isolated numerical and fan-direction gate and is
substantially more coherent than the unshrouded momentum-source result.  It is
not yet full-rack or thermal validation.

### Full-rack cyclic pressure-jump ramp

The fresh research-lab case contains 1,715,112 total cells and 1,538,474 fluid
cells.  It was decomposed onto exactly four fluid ranks containing 454,528,
370,915, 345,474, and 367,557 cells; solid regions remain pinned to rank 0 to
limit memory pressure.  All 13 internal fans were converted to cyclic
pressure-jump interfaces and the complete 20/40/60/80/100% ramp was started.

The 20% stage reached 0.01 s without a fatal error or out-of-memory failure.
Its endpoint maximum Courant number was 1.54843, execution/clock times were
776.31/807 s, and fluid temperature remained approximately 293.13--293.15 K.
The signed fan-interface flows were:

| Fan interface | Mass flow (kg/s) | Estimated volume flow (m3/s) |
|---|---:|---:|
| Bottom_fan_1_33 | 0.004738798 | 0.004832360 |
| Bottom_fan_2_34 | 0.004742900 | 0.004836543 |
| Exhaust_fan_1_15 | 0.002573492 | 0.002624530 |
| Exhaust_fan_1_21 | 0.001404478 | 0.001432270 |
| Exhaust_fan_2_16 | 0.002576487 | 0.002627595 |
| Exhaust_fan_2_22 | 0.001136362 | 0.001158874 |
| Exhaust_fan_3_23 | 0.001205517 | 0.001229394 |
| Internal_Exhaust_Fan_10 | 0.003503809 | 0.003572987 |
| Internal_Exhaust_Fan_5 | 0.004276550 | 0.004360986 |
| Power_supply_exhaust_fan_24 | 0.001594910 | 0.001626440 |
| Rear_exhaust_fan_1 | 0.011125350 | 0.011345010 |
| back_exhaust_fan_11 | 0.003250006 | 0.003314174 |
| back_exhaust_fan_6 | 0.007655558 | 0.007806709 |

All 13 interfaces are positive at this first checkpoint, including the Cisco
and Keysight fans that failed or behaved counter-monotonically under the old
unshrouded momentum-source formulation.  The NI bottom-fan flows are also
plausible for their declared interface areas and do not reproduce the old
49 m/s source-zone artifact.  This remains an early startup checkpoint: it
does not establish settled exterior mass balance, thermal balance, or full-head
monotonicity.  The 40% stage began automatically and requires the same audit
before checkpoint pruning.

The 40% stage reached 0.02 s in 728.07 execution seconds / 751 wall seconds.
All 13 interfaces remained positive and every one increased relative to the
20% checkpoint.  The signed mass/estimated-volume flows were:

| Fan interface | Mass flow (kg/s) | Estimated volume flow (m3/s) |
|---|---:|---:|
| Bottom_fan_1_33 | 0.007116167 | 0.007257961 |
| Bottom_fan_2_34 | 0.007000822 | 0.007140319 |
| Exhaust_fan_1_15 | 0.003313379 | 0.003380573 |
| Exhaust_fan_1_21 | 0.001973178 | 0.002012813 |
| Exhaust_fan_2_16 | 0.003331308 | 0.003398904 |
| Exhaust_fan_2_22 | 0.001776399 | 0.001812215 |
| Exhaust_fan_3_23 | 0.001863184 | 0.001900729 |
| Internal_Exhaust_Fan_10 | 0.005457561 | 0.005566307 |
| Internal_Exhaust_Fan_5 | 0.006455103 | 0.006583726 |
| Power_supply_exhaust_fan_24 | 0.002100490 | 0.002142535 |
| Rear_exhaust_fan_1 | 0.016109900 | 0.016430900 |
| back_exhaust_fan_11 | 0.004857367 | 0.004954154 |
| back_exhaust_fan_6 | 0.011073810 | 0.011294460 |

The endpoint maximum fluid Courant number was approximately 2.46, the fluid
temperature range was approximately 293.106--293.153 K, and cumulative fluid
continuity was -9.67e-7.  The runner finalized the four-rank
stage cleanly, retained `processor[0-3]/0.02`, applied the absolute 60% pressure
scale, and started the next stage.  This is a second monotonic startup point,
not a developed-flow or thermal acceptance result.

The 60% stage then reached 0.03 s in 697.8 execution seconds / 742 wall seconds.
All 13 interfaces remained positive and every flow increased again relative to
40%.  The signed mass/estimated-volume flows were:

| Fan interface | Mass flow (kg/s) | Estimated volume flow (m3/s) |
|---|---:|---:|
| Bottom_fan_1_33 | 0.008723957 | 0.008882999 |
| Bottom_fan_2_34 | 0.008608750 | 0.008765692 |
| Exhaust_fan_1_15 | 0.004824574 | 0.004915456 |
| Exhaust_fan_1_21 | 0.002707640 | 0.002757795 |
| Exhaust_fan_2_16 | 0.004834045 | 0.004925214 |
| Exhaust_fan_2_22 | 0.002447139 | 0.002492799 |
| Exhaust_fan_3_23 | 0.002503262 | 0.002549917 |
| Internal_Exhaust_Fan_10 | 0.006726233 | 0.006848856 |
| Internal_Exhaust_Fan_5 | 0.007615676 | 0.007754514 |
| Power_supply_exhaust_fan_24 | 0.002845396 | 0.002897735 |
| Rear_exhaust_fan_1 | 0.017690970 | 0.018013490 |
| back_exhaust_fan_11 | 0.007719363 | 0.007860091 |
| back_exhaust_fan_6 | 0.012804240 | 0.013037670 |

The endpoint maximum fluid Courant number was approximately 2.89, cumulative
fluid continuity was -2.84e-6, and the fluid temperature range was
approximately 293.086--293.155 K.  The four-rank stage ended cleanly and
retained `processor[0-3]/0.029999999999999999`.  These first three pressure
levels establish a monotonic startup trend for every interface, but still do
not establish a developed system operating point.  The runner then applied the
absolute 80% scale and started the next four-rank stage.

The 80% stage reached 0.04 s in 793.31 execution seconds / 813 wall seconds.
All 13 fan interfaces remained positive and every flow increased again relative
to 60%.  The signed mass/estimated-volume flows were:

| Fan interface | Mass flow (kg/s) | Estimated volume flow (m3/s) |
|---|---:|---:|
| Bottom_fan_1_33 | 0.010105560 | 0.010299360 |
| Bottom_fan_2_34 | 0.009986867 | 0.010178390 |
| Exhaust_fan_1_15 | 0.005878290 | 0.005992473 |
| Exhaust_fan_1_21 | 0.003299833 | 0.003363502 |
| Exhaust_fan_2_16 | 0.005884681 | 0.005999043 |
| Exhaust_fan_2_22 | 0.003026460 | 0.003085018 |
| Exhaust_fan_3_23 | 0.003066916 | 0.003126231 |
| Internal_Exhaust_Fan_10 | 0.007580861 | 0.007726241 |
| Internal_Exhaust_Fan_5 | 0.008380123 | 0.008540830 |
| Power_supply_exhaust_fan_24 | 0.003405219 | 0.003470745 |
| Rear_exhaust_fan_1 | 0.019104390 | 0.019470760 |
| back_exhaust_fan_11 | 0.009844490 | 0.010033280 |
| back_exhaust_fan_6 | 0.013541770 | 0.013801460 |

The endpoint maximum fluid Courant number was approximately 3.31, cumulative
fluid continuity was -2.70e-6, and the fluid temperature range was
approximately 293.066--293.157 K.  The solution remained below the configured
Co=5 ceiling without timestep collapse.  Four pressure levels are now
monotonic for all interfaces, but full-head direction, final exterior balance,
and developed-flow gates remain unproven.

The 100% stage reached 0.05 s in 690.85 execution seconds / 712 wall seconds.
All 13 fan interfaces remained positive and every flow increased across the
complete 20/40/60/80/100% sequence.  Final signed mass/estimated-volume flows
were:

| Fan interface | Mass flow (kg/s) | Estimated volume flow (m3/s) |
|---|---:|---:|
| Bottom_fan_1_33 | 0.011442840 | 0.011665370 |
| Bottom_fan_2_34 | 0.011321780 | 0.011541960 |
| Exhaust_fan_1_15 | 0.006088526 | 0.006208635 |
| Exhaust_fan_1_21 | 0.003607733 | 0.003678367 |
| Exhaust_fan_2_16 | 0.006094460 | 0.006214746 |
| Exhaust_fan_2_22 | 0.003388081 | 0.003454617 |
| Exhaust_fan_3_23 | 0.003428278 | 0.003495572 |
| Internal_Exhaust_Fan_10 | 0.008228045 | 0.008388054 |
| Internal_Exhaust_Fan_5 | 0.008988096 | 0.009162886 |
| Power_supply_exhaust_fan_24 | 0.003639161 | 0.003710197 |
| Rear_exhaust_fan_1 | 0.019846350 | 0.020232300 |
| back_exhaust_fan_11 | 0.010865230 | 0.011076520 |
| back_exhaust_fan_6 | 0.014098620 | 0.014372790 |

Maximum fluid Courant settled at approximately 3.714, cumulative continuity was
-3.25e-6, and the fluid temperature range was approximately
293.045--293.158 K.  The ramp completed without reversal, OOM, timestep
collapse, or fatal solver error.  Reconstructed validation correctly reports
three physical fluid regions after the validator was fixed to treat paired
cyclic faces as topological neighbours; a regression test covers this case.

The rack is not yet flow- or thermal-ready at 0.05 s.  Exterior inflow was
0.312120 kg/s versus 0.278294 kg/s outflow, a 10.84% normalized mismatch.
Transported sensible heat was -1.98 W versus 1,515 W applied, as expected for
the undeveloped 0.05 s thermal transient.  In contrast, final NI bottom-fan
cell-zone mean velocity magnitudes were only about 3.08 and 2.96 m/s, removing
the former 48--49 m/s momentum-source artifact.  A four-rank full-pressure
continuation to 0.10 s was started to test whether the exterior imbalance is
transient before proceeding to the matched 0.15 s comparison.

The full-pressure coupled continuation completed on exactly four ranks at
`t=0.10 s`.  It required 3,172.84 execution seconds / 3,205 wall seconds and
reconstructed all 13 regions without a solver, OOM, or timestep failure.  The
final maximum fluid Courant number remained approximately 3.72.  Exterior
inflow was 0.40935571 kg/s and exterior outflow was 0.40773862 kg/s: the
normalized mismatch fell from 10.84% at 0.05 s to 0.39503%, passing the 5%
screening limit.  Outlet reverse-flow share was zero.  This demonstrates a
numerically stable, mass-balanced startup airflow state; it does not establish
one full rack air exchange or statistically converged turbulence.

All 13 cyclic pressure-jump interfaces still had positive signed flow at
0.10 s.  Their signed mass/estimated-volume flows were:

| Fan interface | Mass flow (kg/s) | Estimated volume flow (m3/s) |
|---|---:|---:|
| Bottom_fan_1_33 | 0.011497580 | 0.011714750 |
| Bottom_fan_2_34 | 0.011497810 | 0.011714990 |
| Exhaust_fan_1_15 | 0.008164775 | 0.008321651 |
| Exhaust_fan_1_21 | 0.005063270 | 0.005159694 |
| Exhaust_fan_2_16 | 0.008169993 | 0.008327062 |
| Exhaust_fan_2_22 | 0.004856221 | 0.004949037 |
| Exhaust_fan_3_23 | 0.004859221 | 0.004952046 |
| Internal_Exhaust_Fan_10 | 0.008113625 | 0.008266882 |
| Internal_Exhaust_Fan_5 | 0.008776295 | 0.008942070 |
| Power_supply_exhaust_fan_24 | 0.004941726 | 0.005035490 |
| Rear_exhaust_fan_1 | 0.021401340 | 0.021805590 |
| back_exhaust_fan_11 | 0.011932000 | 0.012157380 |
| back_exhaust_fan_6 | 0.014584070 | 0.014859550 |

The coupled energy gate intentionally remains failed at this subsecond time:
the validator measured -5.4101 W net transported sensible heat versus 1,515 W
applied, a 100.36% error, with fluid and solid temperatures still essentially
at the 293.15 K initial condition (solid range 293.14996--293.16638 K).  The
analytical mixed outlet temperature for the measured 0.40774 kg/s flow is
approximately 296.85 K.  A 0.15 s extension was not run merely to repeat an
already-passing airflow balance; meaningful thermal validation instead needs a
validated accelerated/mapped airflow start and a duration commensurate with
the device and rack thermal time constants.

### Post-checkpoint regression and visualization audit

After the 0.10 s case released WSL memory, the complete Python discovery suite
passed under the bundled scientific runtime: 242 tests, zero failures, and four
skips limited to optional PyVista/animation coverage.  All 12 C++ added-feature
targets were then rebuilt from source in an isolated temporary directory and
passed, including the model configuration, OpenFOAM exporter, connected-volume,
profile, PCG/SOR, porous, advection, wall-energy, runner-command, and metadata
tests.  The legacy combined harness later selected the system Python and could
not import NumPy; the same affected tests passed under the bundled runtime.
This is an interpreter-selection/package reproducibility issue, not a solver
assertion failure, and should be removed before a production release.

The exact geometry-only entry point was retested successfully with
`model_runner.exe --geometry-only library/models/new_model.toml`; it rewrote the
full lab `output.txt` without touching OpenFOAM or running a transient.  Fresh
NI, Cisco, Eaton UPS, and Thruster component PNGs were rendered from that
output.  Every region whose kind is `Air` uses the same cyan fill/edge color in
both rack and component plotters, while non-air regions retain their cycle
colors.  The component plotter now limits 3-D tick density and separates the
external legend, fixing the unreadable label collision seen on thin 1U Cisco
geometry.  Six focused plot geometry/color tests and Python syntax compilation
passed after the layout change.

### Readiness assessment before the later checkpoint extensions

The current model is suitable for geometry review, interface-direction tests,
and provisional airflow screening.  It is not yet industry-validated for
absolute component temperature, hotspot, or final fan operating-point claims.
The full-rack cyclic pressure-jump formulation has demonstrated a stable,
mass-balanced 0.10 s startup on exactly four ranks, with all 13 fans flowing in
the intended direction and no extreme NI velocity artifact.  However, a full
air exchange and spatial/statistical airflow convergence have not been shown,
and the fine-mesh cold-start runtime remains incompatible with the requested
one-to-two-day workflow without validated coarsening or a mapped start.

Release-blocking physical inputs remain the provisional fan curves and heat
loads, uncertain Rail 2 depth coordinates, and the missing NI separator-wall
geometry between the card and rear air regions.  Release-blocking validation
evidence remains a thermally developed coupled run with accepted energy balance,
mesh/time-step independence, calibrated material/thermal-mass equivalents, and
comparison against laboratory airflow and temperature measurements.  Until
those are supplied, results must be labelled screening estimates rather than
industry-ready predictions.

### Spatial airflow convergence: 0.05 to 0.10 s

Passing exterior mass balance at 0.10 s did not imply a settled spatial flow
field.  `tools/openfoam_field_convergence.py` compared the reconstructed 0.05
and 0.10 s fields over all 1,538,474 fluid cells, using volume weighting and
removing only the uniform pressure-gauge offset.  Whole-fluid velocity changed
by 0.5570 m/s RMS, or 43.16% relative RMS, with a 17.37 m/s maximum change near
the upper-rear rack at approximately `(0.1825, 0.9687, 1.5567) m`.  External
rack air alone changed by 48.75% relative velocity RMS.  Gauge-corrected
`p_rgh` changed by 8.37 Pa RMS (24.64% relative RMS).  Temperature differences
were tiny because both samples remain near the initial condition and are not
evidence of thermal convergence.

Component-air velocity relative-RMS changes included 45.14% for Cisco, 43.16%
for Keysight N5766A, 27.85% for Keysight N6701, 27.43% for Trenton, and 61.24%
and 49.04% for the two NI air partitions.  Near-zero-flow passive KVM/PDU
regions produced large relative percentages from negligible absolute velocity
changes and must not be used as convergence indicators.  The full CSV is
`airflow_field_convergence_0p05_0p10.csv`.

This field-level gate fails.  It supersedes the earlier decision to stop after
the mass-balanced 0.10 s checkpoint and justifies a matched four-rank extension
to 0.15 s.  The next decision must compare 0.10 to 0.15 s using the same spatial
metric; boundary balance alone is insufficient.

### Spatial airflow convergence: 0.10 to 0.15 s

The matched four-rank extension completed and reconstructed at `t=0.15 s`
without OOM, rank loss, timestep collapse, or fatal solver error.  Exterior
inflow was 0.46264189 kg/s and outflow was 0.45996581 kg/s, giving 0.57844%
normalized mismatch with zero outlet reverse flow.  All 13 cyclic fan flows
remained positive.  The thermal-energy gate remained intentionally failed:
only -8.0173 W was transported versus 1,515 W applied, and the solid range was
still only 293.14994--293.17457 K.

Spatial convergence improved but still failed.  Whole-fluid velocity changed
by 0.52308 m/s RMS, 34.13% relative RMS, between 0.10 and 0.15 s; the maximum
local change fell to 8.81 m/s near `(0.4096, 0.9959, 0.0398) m`.  External rack
air changed by 39.31% relative velocity RMS.  Gauge-corrected whole-fluid
`p_rgh` changed by 2.139 Pa RMS, 6.56% relative RMS, down substantially from
24.64% over the preceding interval.

Component-air velocity relative-RMS changes included 28.40% for Cisco, 37.69%
for Keysight N5766A, 16.54% for Keysight N6701, 13.06% for Trenton, 19.26% for
the Thruster load box, and 46.33% and 20.67% for the two NI partitions.  The
full result is `airflow_field_convergence_0p10_0p15.csv`.  Although the trend is
toward settlement, these changes are far above an acceptance threshold suitable
for a mapped thermal start.  A further matched four-rank extension to 0.20 s
was therefore started; 0.15 s must not be labelled converged airflow.

### Spatial airflow convergence: 0.15 to 0.20 s

The next four-rank extension completed, reconstructed, and post-processed at
`t=0.20 s`.  Exterior inflow was 0.49505393 kg/s and outflow was 0.49334539
kg/s, for 0.34512% normalized mismatch and zero outlet reverse flow.  Every
cyclic fan remained positive.  The energy gate still failed at the subsecond
checkpoint: -10.0799 W transported versus 1,515 W applied, with solid
temperatures only 293.14991--293.18276 K.

The spatial trend continued to improve but velocity remained unsettled.
Whole-fluid velocity changed by 0.47773 m/s RMS, or 28.05% relative RMS, and
the maximum local change fell to 6.96 m/s.  External rack air changed by
32.18% relative velocity RMS.  Whole-fluid gauge-corrected `p_rgh` changed by
1.549 Pa RMS, 4.92% relative RMS, so the pressure field passed a nominal 5%
screen while the velocity field did not.

Component-air velocity relative-RMS changes included 45.04% for Keysight
N5766A, 19.15% for Cisco, 11.80% for Keysight N6701, 6.65% for Trenton, 14.54%
for the Thruster load box, and 34.10% and 12.19% for the two NI partitions.
The full result is `airflow_field_convergence_0p15_0p20.csv`.  A matched
four-rank extension to 0.25 s was started because 0.20 s is not yet a defensible
mapped thermal-flow seed.

### Spatial airflow convergence: 0.20 to 0.25 s

The matched four-rank extension completed, reconstructed all 13 regions, and
post-processed cleanly at `t=0.25 s`.  Exterior inflow was 0.51584441 kg/s and
outflow was 0.51468783 kg/s, giving a 0.22421% normalized mismatch and zero
outlet reverse flow.  All 13 cyclic pressure-jump fan interfaces retained
positive signed flow.  The coupled energy gate remains intentionally failed at
this subsecond checkpoint: -11.6299 W was transported versus 1,515 W applied,
and the solid range was only 293.14988--293.19095 K.

The spatial airflow trend improved again but still failed the velocity gate.
Whole-fluid velocity changed by 0.44346 m/s RMS, or 24.23% relative RMS, and
the maximum local change was 6.78 m/s near `(0.2127, 0.9687, 1.5567) m`.
External rack air changed by 27.62% relative velocity RMS.  Gauge-corrected
whole-fluid `p_rgh` changed by 1.162 Pa RMS, or 3.79% relative RMS; pressure is
settling faster than velocity.

Material component-air velocity changes included 43.63% for Keysight N5766A,
26.05% and 8.11% for the two NI partitions, 11.38% for Cisco, 11.19% for the
Thruster load box, 9.07% for Keysight N6701, 7.37% for the Eaton UPS, and 3.53%
for Trenton.  The passive KVM, PDU, and shelf percentages are dominated by
near-zero absolute speeds and are not useful convergence indicators.  The full
result is `airflow_field_convergence_0p20_0p25.csv`; the independent validation
reports are `validation_0p25.md` and `validation_0p25.json`.

This checkpoint is mass-balanced and numerically stable, but it is not a
defensible mapped thermal-flow seed.  A matched four-rank extension to 0.30 s
is required because whole/external velocity changes remain far above a 5--10%
screening threshold and important device-air regions remain unsettled.

### Spatial airflow convergence: 0.25 to 0.30 s

The next matched four-rank extension completed, reconstructed all regions, and
post-processed cleanly at `t=0.30 s`.  Exterior inflow was 0.52979339 kg/s and
outflow was 0.52898471 kg/s, yielding a 0.15264% normalized mismatch with zero
outlet reverse flow.  All 13 cyclic pressure-jump fan interfaces retained
positive signed flow.  The energy gate remains intentionally failed at this
subsecond checkpoint: -12.7504 W was transported versus 1,515 W applied, with
the solid range still only 293.14986--293.19914 K.

Velocity convergence improved but remained far outside the mapped-start gate.
Whole-fluid velocity changed by 0.40845 m/s RMS, or 21.17% relative RMS, with a
6.25 m/s maximum local change near `(0.4096, 0.6089, 0.00125) m`.  External
rack air changed by 23.95% relative velocity RMS.  Whole-fluid gauge-corrected
`p_rgh` changed by 0.888 Pa RMS, or 2.95% relative RMS; as at 0.25 s, global
pressure settles materially faster than velocity.

Material component-air velocity changes included 34.84% for Keysight N5766A,
20.56% and 8.21% for the two NI partitions, 8.35% for Keysight N6701, 8.25% for
Cisco, 8.50% for the Thruster load box, 5.62% for the Eaton UPS, and 1.93% for
Trenton.  Passive near-zero-speed regions remain unsuitable relative-error
indicators.  The full result is `airflow_field_convergence_0p25_0p30.csv`; the
independent validation reports are `validation_0p30.md` and
`validation_0p30.json`.

The decreasing whole-fluid velocity sequence (43.16%, 34.13%, 28.05%, 24.23%,
21.17% over successive 0.05 s windows) is encouraging but does not prove a
settled flow field.  A matched four-rank extension to 0.35 s is required; the
0.30 s state must not be promoted to a thermally developed or industry-ready
solution.

### Spatial airflow convergence: 0.30 to 0.35 s

The matched four-rank extension completed, reconstructed all regions, and
post-processed cleanly at `t=0.35 s`.  Exterior inflow was 0.53947433 kg/s and
outflow was 0.53890887 kg/s, giving a 0.10482% normalized mismatch with no
outlet reverse flow.  All 13 cyclic pressure-jump fans retained positive
signed flow.  The energy gate correctly remains failed for this cold transient:
-13.4286 W was transported versus 1,515 W applied, while the solid range was
only 293.14982--293.20733 K.

Airflow convergence continued to improve, but remained outside the mapped-start
gate.  Whole-fluid velocity changed by 0.38185 m/s RMS, or 19.03% relative RMS,
with a 3.84 m/s maximum local change near `(0.1205, 0.9830, 0.00375) m`.
External rack air changed by 21.40% relative velocity RMS.  Whole-fluid
gauge-corrected `p_rgh` changed by 0.708 Pa RMS, or 2.39% relative RMS.

Material component-air velocity changes included 25.15% for Keysight N5766A,
18.06% and 7.43% for the two NI partitions, 7.27% for Keysight N6701, 6.68% for
Cisco, 6.55% for the Thruster load box, 4.46% for the Eaton UPS, and 1.17% for
Trenton.  Passive near-zero-speed relative percentages remain poor convergence
indicators.  The full result is `airflow_field_convergence_0p30_0p35.csv`; the
independent reports are `validation_0p35.md` and `validation_0p35.json`.

The whole-fluid sequence is now 43.16%, 34.13%, 28.05%, 24.23%, 21.17%, and
19.03% over successive 0.05 s windows.  The trend is monotonic but still far
above the 5--10% screening threshold, particularly around the Keysight N5766A
and first NI air partition.  A matched four-rank extension to 0.40 s is
therefore required; 0.35 s is not a settled airflow seed or thermal result.

## Current runtime and robustness status — 2026-08-26

This section supersedes the forward-looking checkpoint instruction immediately
above; it does not rewrite the chronological measurements. The retained
pre-correction OpenFOAM case has now reached 1.600 s. It is still not eligible
for frozen-flow thermal acceleration: it represents only about 0.875 nominal
air replacements, and the latest whole-fluid velocity change is 8.8677% RMS,
above the 3% freeze gate. The current geometry also contains the later N5766A
and Trenton repairs that are absent from those retained fields.

### Promoted timestep policy

The current policy separates three physically different stages:

| Stage | Current screening cap | Decision |
|---|---:|---|
| Fan ramp, fixed warm-up, and adaptive initial airflow | 0.0005 s | Retain. A matched 0.00075 s case used 30% fewer steps but was 9.79% slower, changed velocity by 1.155754% RMS and gauge-adjusted pressure by 11.946965% RMS, and failed 10/33 fan operating-point equivalence checks (14.676% worst). |
| Later live-airflow refresh windows | 0.001 s | Retain as the independent screening refresh cap; it does not enlarge the unfinished initial-airflow continuation. |
| Frozen-flow implicit thermal screening | 20 s | Retain for the reusable/current canonical screening profile, and only after every airflow-freeze gate passes. |
| Frozen-flow implicit thermal screening, exact retained 22.5 mm pre-correction case only | 24 s | Exploratory override only. It reduced cumulative wall time 22.190252% and the sustained segment 13.725195% versus 20 s, but failed the strict 0.05 K cell and 0.02 K component-average confirmation gates (0.123260498 K and 0.054939449 K observed). |

The default and in-depth profiles cap both initial airflow and refreshes at
0.001 s. Validation keeps 0.001 s initially and its separately matched 0.005 s
refresh cap. These settings are not interchangeable with the thermal-only cap.
For the exact retained 22.5 mm timestep campaign, the 30, 40, and 60 s frozen
candidates were rejected. Two frozen-flow outer
energy-coupling passes remain a screening candidate pending a corrected
full-rack three-versus-two A/B; live flow remains three outer by two pressure
corrections. The matched live two-by-two case was 27.25% faster after its first
step but failed field, maximum-temperature, and fan-flow equivalence.

The generated multirate runner now makes those caps authoritative. Fan-ramp,
initial-airflow, refresh, and warm-start live segments use fixed divisible
steps. Endpoint tolerance is capped at 10 ns even at large absolute simulation
times. Warm starts are split into bounded checkpoint windows; every nonzero
rank must contain `uniform/time`, and each window receives its own endpoint and
Courant postflight. Controls are replanned after the optional fan ramp.
Multirate cases require adaptive airflow refresh and default to `--multirate`;
serial `run_cht.sh`, explicit conventional `run`, and the non-adaptive
multirate combination fail before environment launch or case writes.

Thermal-only advancement is journaled transactionally. The runner writes
`owed <start> <target>` before the solver and changes it to
`active <checkpoint>` only inside the stage after its exact endpoint and source
checks pass. An interrupted stage with no checkpoint progress is retried; an
advanced but uncommitted checkpoint fails closed rather than being assumed
valid. A committed terminal thermal stage stops at the exact request and keeps
airflow validation pending for the next continuation. Every accepted nonzero
warm restart is revalidated through the shared latest-time Courant check before
the runner can skip, advance, or finish; ramp and window postflights use that
same check. Interrupted fan-ramp checkpoints are validated at partial or final
ramp time, and full fan scale plus the completion marker are recovered only
after a valid endpoint. A missing marker beyond the ramp endpoint fails closed.
Courant validation also rejects negative, `-inf`, `nan`, and malformed output.
Dedicated `INT`/`TERM` handlers are installed before preparation, track exact
child process groups, defer signals during PID registration, ignore repeated
termination signals, and use bounded TERM/KILL cleanup. They restore full fan
and production solver controls exactly once and exit 130/143 without running
post-signal work. These are runner-contract tests, not OpenFOAM physics
validation: no WSL build or corrected solve has yet run under the new policy.

### Native workload and output hardening

The authoritative post-audit production O3 runner rejected the canonical
forced-native rack before allocating a mesh or constructing a solver. It
planned 2,847,663 fine and
216,580 coarse cells. At 300 steps per stage, one mandatory non-advection pass
plus one mandatory advection pass requires at least 1,708,597,800 fine and
129,948,000 coarse visits, or 1,838,545,800 total, versus the configured
30,000,000 limit. The post-audit preflight produced the expected exit 1 in
0.109 s and no flow or temperature field. Its stdout is 210 bytes with SHA-256
`2df73fed20722b742083324b84615c8d7871dde489c0e9a82630b3bd6c05f512`;
its 274-byte stderr has SHA-256
`1b93670fc5ce981665811837fae75b9f9eb46ba33132b54182a738989b372433`.
The evidence files are
`validation/revised_native_regression_2026-08-26/full_rack_native_preflight_post_audit_runtime_hardening_2026-08-26.{stdout,stderr}.log`.
All four root sentinels were preserved; the current 609-byte
`.thermal_sim_last_run.json` has SHA-256
`39ae95c8825c855dd03bbd233747cb95000bb49c888ca918c185b2f655ccd1e7`.

Workload arithmetic is overflow checked, the coarse and fine stages share one
cumulative visit budget, and exact solved-flow CFL work is rechecked after each
flow refresh. Cell count, two-`Mesh` `Cell` payload, bitmap size, timestep count,
and output interval are rejected before the corresponding large allocation or
output mutation. Logger initialization is validation-only; legacy and
field/summary/probe CSV streams open lazily after flow, stability, exact-CFL,
and workload preflight. Sentinel tests prove those CSV bytes are preserved on
pre-advancement refusal. This is not a promise that every later failure leaves
all artifacts untouched; geometry export intentionally has a separate
lifecycle.

Post-audit coverage also closes portable probe-filename validation, including
case-insensitive collisions before stream creation, and the OpenFOAM
preflight/geometry transaction boundary.

The 1,838,877-byte `model.exe` with SHA-256
`335efd17d7f000e90d13b4a2594cfade4ab68bde524b2910f81c73df7b00eb12`
is authoritative only for its post-audit source snapshot. It predates the
current low-memory exporter, source-fingerprint attestation, and transactional
runner-state changes, so it is not a current-source production executable and
must not be used to create the corrected case. No current-source `model.exe`
has been rebuilt or promoted. The archived byte-identical
historical artifact is
`validation/revised_native_regression_2026-08-26/model_post_audit_runtime_hardening_2026-08-26.exe`.
The default
`model.exe --geometry-only` path exited zero in 0.289 s. Its 272-byte stdout has
SHA-256
`664849782a065fcdda6988f03a171126171a92e74dc81346c92281829b459454`,
stderr is empty, and it regenerated the
39,808-byte `output.txt` with its unchanged SHA-256
`dbcaaa237fb8bf59c4841cf4e3a8b0a226d28208d23560c72900ce113d2ff2ea`;
neither transient solver was invoked.

### Authoritative resource-gated software evidence and claim boundary

The last complete added-feature harness exited zero in 516.584 s under
affinity mask 3 (two logical processors) and BelowNormal priority. Its
1,903-byte run record,
`added_feature_regression_runner_state_gates_2026-08-27.run.json`, reports PASS
and has SHA-256
`246620007dff4b6f7dea50b22bcc9bc987e359aa575310134a9930da672a6c8c`.
The 4,115-line, 333,651-byte stdout log has SHA-256
`03d781c9abe21ec3722e038672ef4c6587bed734cc896d97ddde3ed17a7bd81c`
and ends `All added-feature tests passed.` The 2,946-byte stderr has 125 lines,
100 non-empty lines, and SHA-256
`f4dba93c473013728126639c6b07f4c86196eeba63b61600c3abab37dc051d23`;
with the PASS result, it is interpreted as expected successful unittest
progress/summaries and explicit dependency/platform skips for that source
snapshot. The run includes the
low-memory lifecycle, staged solver build/attestation rollback, generated
endpoint/bypass and restart-Courant guards, fan-ramp marker recovery,
repeated-signal termination coverage, 17-digit output-precision assertion, and
release-gate tests. The current test tree later added the inline storage shelf
to the default O2 component campaign. That header-heavy C++ change remains
uncompiled because available host memory stayed below 1 GiB, so this run is the
last complete production-source regression rather than a pass for the present
test tree. The earlier 397.559 s and 303.075 s harnesses remain
preserved as historical evidence for older source snapshots and are superseded
for their earlier source identities. The pinned historical post-audit O2
template campaign also exited zero in
74.651 s wrapper time (74.4218 s internal):
11 functional passes and one expected rejection of the inactive provisional NI
separator sensitivity across 12 selected templates and 2,315 W.
Its 640-line, 59,774-byte stdout log has SHA-256
`895430bcbb8750012679daea582cf71fd9d7b42d9ca5640fd4adbf7487e60bf0`;
stderr is empty. The exact 1,108,407-byte executable has SHA-256
`133355e650d7c14ff0526997eb22707a4053c59dd07a8b4d301691ec2867ebd2`.
The canonical unfinished NI case still reports 42 stalled interfaces and a
22.7193 m/s peak.

The older `final_runtime_hardening` and `post_audit_runtime_hardening` results
remain preserved as historical evidence. The 2026-08-27 runner-state-gates
artifacts supersede them for production-source coverage, but not for the later
uncompiled component-test additions.

Those microcases establish parser, geometry, topology, bounded-flow,
continuity, and short-update robustness only. They are not a full-rack coupled
solve, thermal soak, mesh/timestep-independence study, experimental comparison,
or industry-readiness sign-off. The current goal-completion host-only
pre-export gate,
`openfoam_resource_gate_goal_completion_20260827T1020Z.json` (3,426 bytes,
SHA-256
`0b9e7b4ad797b456b188bd88aa9ca568bc429525e3518ff3423573e189f5046a`),
exited 21. It found no competing solver processes and passed disk at
17,020,313,600 free bytes, but its first and only memory sample failed at
379,506,688 available bytes versus the 5,368,709,120-byte floor. The requested
60-second sampling period therefore completed zero seconds, and WSL was not
queried. Earlier host-memory and post-regression disk failures remain historical
resource snapshots. OpenFOAM compilation and runtime were withheld, the
installed custom solver is stale, and the generated runner quarantines it with
exit 14.
Rail 2 depths, NI separator walls, fan curves, heat loads, calibrated materials,
and thermally developed laboratory validation remain open.
