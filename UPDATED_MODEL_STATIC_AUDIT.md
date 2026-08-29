# Updated research-lab model static audit

This report covers `library/models/new_model_updated.toml` before meshing or
CFD. It does not claim flow or thermal validation.

The revised model and its isolated export fixture override the reusable
screening profile to exactly four OpenFOAM ranks, matching the resource policy
for this validation campaign. The profile's two-rank default remains unchanged
for other models.

The full revised model, isolated export fixture, and resolved native smoke use
a 1,536 MiB mesh safety ceiling. A fresh current-source plan contains 2,847,663
native fine cells and an exact two-`Mesh` `Cell` payload of 1,275,753,024 bytes
(1,216.65 MiB); the prior 2,561,280-cell/1,094-MiB estimate is stale. This
payload excludes pressure-network, metadata, wall, logger, allocator, and
process overhead and is not peak RSS. Mesh count and payload are now checked
with overflow-safe `size_t` arithmetic before the `Cell` vector is allocated;
actual OpenFOAM memory remains an independent measurement.

A resolved native run allocated about 2.34 GB working memory and remained in
its first serial PCG pressure solve for more than 11 minutes without emitting a
residual or returning. It was intentionally interrupted as an impractical
smoke-test runtime. The native smoke fixture therefore uses 100 pressure
iterations and two nonlinear outer passes to provide a bounded diagnostic;
the canonical model retains its 3,500/20 production settings. A bounded smoke
pass does not establish native flow convergence.

The initial bounded native thermal step used `dt=0.01 s`; after the
nonconverged flow pass, the estimator required 1,849 advection substeps
(roughly 5.18 billion cell-substep updates) and recommended about 5.41e-6 s.
That run was interrupted and the smoke-only timestep was reduced to 1e-5 s.
The native workload guard now reports and enforces advection-inclusive cell
visits: one non-advection pass plus every advection substep. It re-evaluates the
remaining-work projection when the flow field changes and fails before thermal
advancement if `simulation.max_updates` would be exceeded.

The current canonical 19 mm model cannot start a native transient under its
30,000,000-visit ceiling. Before allocating a mesh, the exact current planner
reports 2,847,663 fine and 216,580 coarse cells. Three hundred steps in each
stage require at least 1,708,597,800 and 129,948,000 visits respectively when
subcycling is enabled, or 1,838,545,800 combined before any solved-flow CFL
adds further substeps. The new executable rejects this plan with exit 1 before
allocation. Raising the limit would authorize work; it would not make that work
fast or resolve the suppressed thin geometry documented below.

## Geometry status

The static rack/component audit covers all 11 reusable templates referenced by
the rack, including the eight versioned updated templates. Current result:

- Errors: 0
- Warnings: 1
- Remaining warning: the NI chassis `Interior air` and `Card Slot air` volumes
  overlap. This is retained because the user explicitly identified the missing
  thin separator walls as unfinished geometry.

The audit also verifies rack and component opening bounds/overlap, unit flow
directions, positive fan flow, vent free-area and discharge coefficients in
(0, 1], nonnegative finite heat loads, and positive finite rho/cp/k for every
explicit solid material. All of these checks currently pass.

Corrections made from the supplied bundle:

- Ten Keysight rear/module fan planes were y-normal surfaces but had x-directed
  flow vectors; they now point rearward along +y.
- The six internal Keysight front fans were labeled as intakes but pointed
  toward the front (-y). Internal fan devices use the direction vector directly,
  so they now point into each chassis along +y.
- The NI power-supply front intake was a y-normal plane but pointed along z; it
  now points inward along +y.
- The Trenton motherboard solid overlapped both the front block and the
  power-supply wall. Its clear compartment now starts at y=155 mm and ends at
  the x=410 mm wall, eliminating stamping-order-dependent material and heat
  assignments.
- Component labels were corrected from `N5677A` to `N5766A` and from `N6701A`
  to `N6701C`, matching their referenced model identities.
- Nominal fan rectangles that extended into chassis wall cells were clipped
  to their modeled air-channel apertures for Eaton, Dell R470, both Keysight
  supplies, and Cisco. Localized rear-wall air ducts were added only beneath
  the Eaton, Keysight, and Trenton exhaust apertures.
- Eleven Trenton cards now stop 10 mm before the rear face, forming a rear
  plenum; previously they intersected the rear-vent planes and the native
  vent-carving path erased their heat-source cells.

## Approximate heat inventory

| Component | Applied heat (W) | Internal fans | Vents | Solid regions | Air regions |
|---|---:|---:|---:|---:|---:|
| Eaton SU3000RTXLCD2UTAA UPS | 120 | 1 | 2 | 3 | 2 |
| Dell PowerEdge R470 1U | 1,000 | 6 | 2 | 4 | 1 |
| Keysight N5766A PS | 270 | 8 | 4 | 10 | 6 |
| Keysight N6701C PS | 270 | 8 | 4 | 10 | 6 |
| Trenton 3U BAM | 370 | 6 | 3 | 16 | 7 |
| Tripp Lite KVM | 20 | 0 | 0 | 2 | 1 |
| 3U storage shelf | 0 | 0 | 0 | 0 | 0 |
| Thruster load box | 10 | 2 | 2 | 1 | 1 |
| Cisco 9300 | 150 | 4 | 3 | 3 | 1 |
| Eaton PDU | 10 | 0 | 0 | 1 | 1 |
| Meanwell supply 1 | 15 | 0 | 2 | 1 | 1 |
| Meanwell supply 2 | 15 | 0 | 2 | 1 | 1 |
| NI PXIe chassis | 65 | 3 | 4 | 7 | 2 |
| **Total** | **2,315** | **38** | **28** | **59** | **30** |

The 38 component fans do not all become internal CFD zones. Five Trenton
front intake fans coincide with the rack exterior and are exported as ambient
fan patches, leaving 33 internal component-fan zones. The rack's nine roof
fans are additional ambient exhaust patches. This classification explains the
38-definition versus 33-internal-zone counts; it does not imply that five fans
were dropped from the model.

The prior baseline total was 1,515 W. Most of the 800 W increase is the model's
new 1,000 W Dell R470, partially offset by reduced UPS, Keysight, and Trenton
assumptions. These are engineering estimates and must be replaced or calibrated
against measured electrical input and device-level dissipation before any
industry-readiness claim.

## Pre-CFD rack system estimate

Treating the nine identical roof fans as parallel devices and the single front
opening as an isolated discharge-coefficient loss gives a first-order operating
point of approximately 0.2940 m3/s total at 220.3 Pa. This uses the supplied
roof-fan curve, 0.02778 m2 front free area, Cd=0.5, and rho=0.9833 kg/m3.

At that point:

- Mass flow is approximately 0.2891 kg/s.
- Ideal bulk temperature rise for 2,315 W is approximately 7.96 K.
- Nominal 1.1321 m3 rack-volume exchange time is approximately 3.85 s.
- Velocity through the perforation free area is approximately 10.58 m/s.

Component passages, internal turns, recirculation, and other losses are omitted,
so a resolved model should generally produce no more total flow and no less
bulk temperature rise than this simplified bound. A substantially higher CFD
flow would indicate missing rack resistance or an opening/connectivity error;
a substantially lower flow may be physically plausible but should be traced to
specific component and vent losses. This is a screening bound, not calibration
or validation data.

## Fan-curve status

Every referenced curve resolves to finite coefficients and a finite positive
first zero-pressure crossing. Several supplied polynomial fits have signed
linear or quadratic terms. The OpenFOAM exporter has been changed to use the
first crossing and clamp pressure to zero above it, preventing polynomial
rebound outside the fitted domain. The affected native, loader, and exporter
tests pass C++17 syntax compilation and focused runtime execution. The exporter
generated a bounded signed-curve table and passed its broader 76,800-cell case
contract; native PCG/SOR and model-loader runtime regressions also pass.

At intake, three supplied `cfm` groups materially exceeded their fitted
curve's first zero-pressure flow: the nine roof fans (271 versus 211.89 CFM),
both Thruster fans (18 versus 13.76 CFM), and the NI power-supply fan (171
versus 100 CFM). The current source aligns the roof nominal to 211.888 CFM and
the NI power-supply nominal to 100 CFM, but intentionally retains the Thruster
nominal at 18 CFM while its curve first crosses zero at about 13.76439 CFM.
That 30.7723% Thruster overrun is an open calibration gate, not an aligned
operating point. The current contract also records six Dell R470 nominals
about 0.9120% above their first crossing and nine roof definitions only about
1.29e-6% above because of decimal precision. The raw paste preserves the
original supplied values; `tests/updated_lab_model_contract_test.py` is the
authority for the current allowlisted above-root definitions.

## Component-plot verification

All twelve unique placed component types now have plots in
`validation/updated_component_plots_2026-08-25/` and were visually inspected: Eaton UPS,
Dell R470, Keysight N5766A, Keysight N6701C, Trenton 3U BAM, Thruster load box,
Cisco 9300, NI PXIe, KVM, storage shelf, Eaton PDU, and Meanwell supply. The
external envelopes and internal regions are not
clipped, the fan and vent surfaces appear on the intended component faces, and
the plots remain readable at their saved resolution. All `Air` regions use the
same cyan style, including both NI air volumes; non-air regions retain distinct
colors so solids, vents, and fans can still be distinguished.

The authoritative batch contains 13 instance plots plus a full-rack plot. Its
preserved `geometry_input.txt` is byte-identical to the active OpenFOAM case's
`geometry.txt`; `PLOT_MANIFEST.json` records the geometry, plotter and output
SHA-256 values, exact commands, and tool versions. All 14 output hashes verify.

The rack contains two identically named Meanwell instances. The component
plotter now accepts a 1-based `--component-index` selector so duplicate names
can be inspected without weakening the existing unique-name check. Ten
focused plot tests cover index selection, range validation, the duplicate-name
diagnostic, face centering, and the shared air-color policy. The Matplotlib
artist-level test executes the real plotter with two differently capitalized
air regions and confirms both fill and edge artist colors are `tab:cyan`.

This is a geometry-presentation check, not evidence that internal flow paths
are physically correct. The NI plot still contains the known overlapping air
volumes because the thin separator walls have not yet been supplied. Dense
models such as the Keysight supplies and NI chassis also produce long legends.
The geometry export currently identifies regions by number and type rather
than preserving their source names, so the plotter cannot show semantic region
names without an export-format change.

The supplied `updated_DELL_R360.toml` is not placed in this rack. Its standalone
geometry audit reports no bounds or overlap errors, and its name has been
corrected to the official 1U form factor consistent with the modeled 43 mm
height. Its retained 817 mm supplied depth conflicts with Dell's published
563.3 mm chassis depth, so it remains explicitly uncalibrated and must not be
placed until the internal layout is reconciled.

## OpenFOAM material-fidelity limitation

`python tools/openfoam_material_fidelity_audit.py
library/models/new_model_updated.toml` found 9 heterogeneous component
instances and 55 internal solid regions whose material differs from the
component's outer material. The current OpenFOAM representation creates one
homogeneous solid region per component, so these source-region properties are
not retained. Across 0.0232389 m3 of affected internal solids, the exported
homogenization adds approximately 8.9905 kg and 14,811 J/K relative to the
defined region materials.

This bias is material for transient temperature predictions: added heat
capacity generally slows the predicted warm-up, while replaced conductivity
changes internal spreading and surface heat rejection. It does not change the
configured 2,315 W source total, but it prevents component-level transient
temperatures from being treated as calibrated or industry-ready. Resolving it
requires multi-solid-region component export (or a measured equivalent
homogeneous material), not merely a longer solver run.
