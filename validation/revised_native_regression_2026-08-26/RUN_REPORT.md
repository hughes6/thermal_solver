# Historical native backend regression (superseded)

Date: 2026-08-26

> **Superseded snapshot; current authority update (2026-08-27).** This report
> preserves the earlier conservative-flux campaign, including its intentional
> exit 1 and 7/11 matrix. Every “final”, “current-source”, `model.exe`, exact
> O2, and “current geometry” label below is pinned only to its stated historical
> snapshot. There is **no production-qualified exporter binary for current
> source**. Before any export, rebuild current source, record the source,
> compiler/build command, executable byte count, and SHA-256, and designate that
> exact executable as `[CURRENT_EXPORTER_EXE]`.
>
> The final current-source software regression is
> `added_feature_regression_runner_state_gates_2026-08-27.run.json`
> (SHA-256
> `246620007dff4b6f7dea50b22bcc9bc987e359aa575310134a9930da672a6c8c`):
> PASS, exit 0, 516.584 s, affinity mask 3 (two logical processors), BelowNormal
> priority. Stdout SHA-256 is
> `03d781c9abe21ec3722e038672ef4c6587bed734cc896d97ddde3ed17a7bd81c`;
> stderr SHA-256 is
> `f4dba93c473013728126639c6b07f4c86196eeba63b61600c3abab37dc051d23`.
> This is software evidence only. The final idle resource record,
> `openfoam_resource_gate_final_idle_20260827T095617107Z.json` (SHA-256
> `33c4fb9525398af3c7ec53156fe214a422b3d0e67ca13e144d0c5fcbe5ca6d51`),
> failed closed with exit 21: no competitors and 17,129,152,512 free disk bytes
> passed, but 951,701,504 available-memory bytes failed the
> 5,368,709,120-byte floor; WSL was not queried. The failed full-rack native
> topology/resource observations below remain valid historical evidence.

## Outcome

The resource-bounded full-rack native campaign did **not** reach a flow or
thermal solve. Two independently preserved attempts failed closed at geometry
topology gates. This is useful robustness evidence, but it produces no native
flow field, temperature field, convergence result, or quantitative comparison
with OpenFOAM.

The isolated-template replay in this preserved source snapshot reached end-to-end native
flow and two-step thermal robustness checks for **7 of 11** unique templates.
Four templates failed closed: Dell, Thruster, and Cisco at strict positive-
pressure fan-curve-domain gates, and Trenton because its front intake has no
realized fluid path. The process exit was intentionally 1 because those four
input failures remain unresolved. These component screens materially improve
solver-path coverage, but they do not replace the missing full-rack native
result, calibrated component data, or a current-geometry OpenFOAM interaction
study.

The failure is not being bypassed. A mesh coarse enough for the current memory
envelope cannot keep the thin/closely spaced internal air, fan, vent, and solid
source regions topologically distinct across all installed components.

## Attempts

| Run | Native mesh | Cells | Two `Cell` arrays | Result |
|---|---|---:|---:|---|
| 001 | uniform 20 mm | 146,850 | 65,788,800 B | Dell `Rear exhaust fan` had no immediately upstream fluid cell |
| 002 | adaptive, 50 mm fine / 200 mm coarse / zero margin | 204,768 | 91,736,064 B | Keysight N5766A `Power block` realized as 40 fluid cells and had no remaining solid source volume |

Both failures occurred before the flow solver and before the first transient
step. The exact stdout logs are retained:

- `run_001.stdout.log`: 218 B, SHA-256
  `1a920adb0cd851b7f9076b790d3a36309f9e57a8e63fefd360e98c572a300f31`;
- `run_002.stdout.log`: 302 B, SHA-256
  `67bdda26cb55474dc50886376441e856b96af671328830fee1bb3e2584ead140`.

The failure-marker files inside each run directory intentionally make the
directory non-empty. The model has `native_overwrite=false`, so an accidental
rerun cannot truncate this evidence.

## Why a finer full-rack attempt was not launched

The next internally aligned planner point is 25 mm, approximately 915,768
cells and 410,264,064 B for the two `Cell` arrays alone. The 20 mm aligned
point jumps to approximately 2,993,088 cells and 1,340,903,424 B before flow
adjacency, pressure vectors, logger state, process overhead, or CSV buffering.

At the run-002 decision point the host reported about 1,120 MiB available and
about 210 pages/s. A 25 mm attempt could exceed the 1 GiB process ceiling and
drive sustained paging; a 20 mm attempt cannot fit. Launching either would
violate the campaign's resource gate, especially while other engineering work
may resume on the workstation.

## Native-path hardening completed during the campaign

- A model with `openfoam_solver.enabled=false` now retains its root native
  mesh even when it names an OpenFOAM profile for future use.
- `simulation.native_output_directory` rejects missing-type/empty values
  instead of silently falling back to the repository root.
- `simulation.native_overwrite=false` refuses a non-empty evidence directory.
- Relative structured logger paths are rebased beneath the native run
  directory; explicit absolute logger paths remain explicit.
- Geometry and legacy CSV writers now throw on open, write, or finalization
  errors. The solver validates workload before opening/truncating its CSV.
- Structured logger post-step labels now use `(step+1, (step+1)*dt)`.
- Legacy CSV cadence now writes the initial state and true interval/final
  states, without the former unintended step-1 dump.
- Canonical root last-run metadata remains current, with an additional native
  archival copy; geometry-only metadata no longer claims a simulation CSV.
- Top-level model errors are caught and reported cleanly. A missing model now
  exits 1 with `Model run failed: ...` rather than `terminate called after
  throwing`.
- Geometry summaries now include component-level and internal air/solid heat
  sources instead of reporting zero for this 2,315 W model.
- The final `Solver` now owns the initial airflow solution. With
  `update_flow_interval=-1`, the loader solves airflow once, estimates CFL and
  thermal limits from that same field, and then retains it; it no longer solves
  on a temporary mesh and discards the conservative fluxes.
- A nonlinear or pressure failure publishes no usable face-flux field, and the
  thermal solver refuses to advance. Periodic refreshes synchronize both
  thermal ping-pong meshes so a later swap cannot restore stale airflow.
- Ordinary mesh transport across an internal-fan plane is suppressed. The fan
  itself publishes the one physical interface flux into `qx`, `qy`, or `qz`
  after a final pressure solve from the converged nonlinear state.
- Curved fans use bounded active sets: the lower state enforces `Q=0`, the upper
  state enforces the first positive-pressure curve zero `Q=qzero`, and either
  state can release back to the interior curve solution. Bound projection is
  part of the pressure equations rather than a post-solve clamp.
- Pressure grounding is checked per connected fluid component. Active or
  source-bearing ungrounded components fail closed; passive sealed pockets
  receive deterministic gauges, and fixed internal fans require both sides to
  be grounded independently.
- Native thermal transport now consumes the published face flows with a
  finite-volume first-order upwind enthalpy balance, including ambient
  intake/exhaust exchange. Nonzero flux through a solid, exterior face, or
  blocked wall is rejected. This transport is conservative for frozen
  `rho`/`cp` within a substep; temperature-dependent property refresh means it
  is not, by itself, a complete multistep first-law-closure claim.

Focused regression results:

- `tests/solver_logger_timestep_test.cpp`: PASS; evolving temperatures at
  `(0,0)`, `(1,dt)`, `(2,2dt)` and legacy snapshots exactly `{0,2}`.
- `tests/model_config_test.cpp`: PASS; disabled/forced-native mesh selection,
  malformed output path rejection, isolated geometry output, and no-overwrite
  preservation.
- `tests.updated_lab_model_contract_test`: 14/14 PASS.
- then-current rebuilt `model.exe`: 1,762,085 B, SHA-256
  `4ad586cdd01242f829ce78d44fbc1cb6f4e9210708bf7ea1f11299601476d2e1`.

The complete added-feature harness was rerun after the conservative-flux,
grounding, lifecycle, and bounded-active-set changes. Every invoked C++ test,
Python test, plotting check, and synthetic archive-helper workflow passed, and
the harness exited zero. Its snapshot-specific 295,285-byte stdout is retained as
`added_feature_regression_conservative_flux_active_set_2026-08-26.stdout.log`,
SHA-256
`dc448db1441c6d1262a3bf9c57358d474ad8564b3ed9192e0b599a044f791656`.
The optional NumPy/PyVista cases and real-WSL flock integration remained
explicit skips; their absence is not counted as a pass. The earlier 89,525-byte
`added_feature_regression_authoritative_2026-08-26.stdout.log` and all other
earlier harness logs remain preserved as historical evidence, but they predate
  that solver lifecycle and are no longer authoritative beyond that source
  snapshot.
This software coverage does not turn either failed full-rack native
attempt into a solved field.

## Per-template campaign and nonlinear-flow hardening

The first per-template campaign exposed exact nonlinear fixed-point cycles in
the N5766A, N6701C, Trenton, and NI cases even at 100 outer iterations. The
common cause was the former discontinuous friction-factor switch at Reynolds
2300, where the Darcy factor jumped by about 74%. The production solver now
uses exact `64/Re` through Re 2000, exact smooth-pipe Haaland from Re 4000,
and a bounded C1 smoothstep blend between them.

The convergence gate is now a per-face mixed relative/absolute criterion. It
does not let a changing low-flow branch hide behind the network's largest
flow, while changes below the pressure solver's absolute flow resolution do
not hold the nonlinear solve open. Non-finite pressure, PCG, face-flow, fan,
and mass-balance states, plus extreme scale overflow, fail closed.

The focused regression independently pins the private resistance path at
Reynolds 2100, 3000, and 3900 to exact-publication pressure drops
44.7634316211, 92.1368035531, and 161.9447870948 Pa. PCG/SOR equivalence, all
16 porous pressure-drop checks, 24 signed uniform/adaptive internal-fan
topology cases, 48 internal-fan tracer/uniform thermal invariants, nine
pressure-grounding fixtures, and bounded curved-fan lower/upper/release paths
passed in that snapshot's harness. Exact local continuity, not only global
source-versus-vent balance, is now required before face fluxes become available
to thermal transport.

The earlier NI result exposed why that distinction matters. One projected
power-supply fan interface published a clamped flow while the pressure system
still contained its interior tangent, leaving a `3.04352e-4 m^3/s` local
continuity defect. The bounded active-set formulation reduces the final NI
maximum local residual to `7.70167e-9 m^3/s`; 42 fan interfaces correctly settle
at the lower `Q=0` bound.

The N5766A front intake was also corrected as an input geometry defect, not by
loosening a solver gate. Its center/height are now `z=21.8 mm` and `33.6 mm`,
exactly matching the internal-air span from 5.0 to 38.6 mm. The prior copied
34.45 mm height extended 0.85 mm into the top wall.

Historical conservative-flux and bounded-active-set replay matrix:

| Template | Cells | Outer iterations | Maximum local continuity residual | Maximum speed | Result |
|---|---:|---:|---:|---:|---|
| Eaton UPS | 21,760 | 20 | `8.63592e-9 m^3/s` | 2.76654 m/s | PASS |
| Dell R470 | -- | -- | -- | -- | FAIL: initial fan flow 0.91203978% beyond curve zero |
| Keysight N5766A | 18,480 | 33 | `9.88997e-9 m^3/s` | 7.65183 m/s | PASS |
| Keysight N6701C | 18,920 | 32 | `7.49946e-9 m^3/s` | 6.99726 m/s | PASS |
| Trenton 3U BAM | -- | -- | -- | -- | FAIL: front intake has no immediately interior fluid path; its x-span overlaps the full-depth `PS Wall` by 3.75 mm |
| Eaton KVM | 9,100 | 10 | `9.96624e-9 m^3/s` | 0.0235822 m/s | PASS |
| Thruster load box | -- | -- | -- | -- | FAIL: initial fan flow 0.00009714405% beyond curve zero |
| Cisco 9300 | -- | -- | -- | -- | FAIL: initial fan flow 0.02886815% beyond curve zero |
| Eaton PDU | 6,000 | 10 | `5.24294e-9 m^3/s` | 0.0165195 m/s | PASS |
| Meanwell fan-control supply | 2,016 | 16 | `9.67941e-9 m^3/s` | 0.0853409 m/s | PASS |
| NI PXIe chassis | 15,708 | 31 | `7.70167e-9 m^3/s` | 22.7193 m/s | PASS numerically; 42 stalled fan interfaces and peak-speed realism require review |

The seven passing cases additionally passed finite flow, exact local continuity,
fan-domain checks, and two 10-microsecond thermal steps. Dell, Thruster, and
Cisco stopped before flow because the provisional reference flow is outside the
positive-pressure curve domain. Trenton stopped at its realized fluid-path
gate. None of those four received a flow or thermal pass. The short thermal
steps are robustness checks, not a thermal soak or a complete first-law closure
result.

The exact replay intentionally exits 1 with seven PASS and four fail-closed
cases. Its stdout is 38,431 B, retained as
`native_template_microcase_conservative_flux_active_set_replay_2026-08-26.stdout.log`,
SHA-256
`c9ae2fd8fa92b83b86534c77ea22e4e82510317552f27516eda110f541a8c84b`.
The preserved exact executable is
`native_template_microcase_conservative_flux_active_set_2026-08-26.exe`,
1,080,959 B, SHA-256
`49b7215a762dffd1e0b8fe7f723f6bf980c14efa20114c0096f2169b0ffae239`.

The source-snapshot manifest is
`native_conservative_flow_active_set_2026-08-26.manifest.json`, 9,061 B,
SHA-256
`76ed26c1fcbbf5fac8b064590d9b42fd6ea2446a9e6df44b34437fdea68f75de`.
It pins 35 files and verifies with zero mismatches, including `solver.hpp`,
`model_loader.hpp`, the exact executable, replay and harness logs, all 11
component inputs, focused tests, and the corrected geometry export. The former
21,478-byte replay, 6,262-byte manifest, and 89,525-byte harness are preserved
unchanged as historical pre-conservative-flux evidence and must not be
described as current-source authority.

## Evidence-preservation check

The then-current post-N5766, pre-Trenton-repair geometry export is retained separately as
`../current_geometry_export_2026-08-26/output.txt`, 39,609 B, SHA-256
`4e3c4a4b96b12929ab141b7975aabb08db9b4d84d86a5d04e3f42db35f25d676`.
Only the three N5766A report lines affected by its intake correction differ
from the earlier geometry output.

The rejected full-rack native attempts did not alter these pre-existing
repository-root artifacts. They remain historical pre-N5766-correction
lineage, not the current geometry export:

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `output.txt` | 39,614 | `5a1893f92d162cfe17d66240d34770351302a2eb961a332be9b2092d1a9981f1` |
| `simulation.csv` | 432 | `ed1766dfc612ab4dbcd587511429cd5e45b8682b484aaa79db42cdac97065e3e` |
| `.thermal_sim_last_run.json` | 671 | `1152a6f716e2032979ae847188157e0aed25bc66517bd39e1ec7b586a552dbf4` |

## Historical next-action record

At this snapshot, the prescribed next action was to correct or deliberately
re-rate the Dell, Thruster, and Cisco fan inputs, then
rerun the same fail-closed campaign rather than relaxing its curve gate. Resolve
the Trenton front-intake/PS-wall overlap from measured geometry; do not weaken
the fluid-path guard. Review the N5766A and especially NI peak speeds against
measured device flow and effective open area, and add the missing NI separator
walls before interpreting its two air regions. A full-rack native solve still
requires a topology-preserving mesh that fits the resource gate; isolated
microcases are software-path tests, not substitutes for full-rack OpenFOAM
interaction, calibrated physics, or experimental validation. The later
topology/fidelity handling and Trenton repair completed the functional 11/11
screen; the fan-data and NI realism warnings still require measurement.
