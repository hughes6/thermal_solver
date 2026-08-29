# Research-lab model goal-completion audit — 2026-08-27

## Decision

The software and static-model work is substantially hardened, but the physical
validation goal is **not complete** and the model is **not industry-ready**.
There is no corrected current-geometry OpenFOAM case, production mesh, flow
field, or thermal field. The only solved full-rack CFD lineage is the preserved
pre-correction 22.5 mm case, which fails the airflow-freeze, fan-domain, mesh,
and thermal-maturity gates.

No OpenFOAM build, export, region split, mesh, or solve was launched during
this audit. The host resource gate remained fail-closed and no qualified
current-source exporter binary exists.

## Current focused verification

The following checks ran with Windows processor-affinity mask `3` (two logical
processors) and `BelowNormal` priority:

| Check | Result | Evidence |
|---|---|---|
| Canonical component geometry audit | Exit 0; 11 reusable components, 0 errors, 1 warning | `goal_completion_geometry_audit_2026-08-27.stdout.log`, 192 B, SHA-256 `e3aff0f60fbc9f3ad53e53727d0279c6a7e3878c1da47045675d83eebfda37e9` |
| Geometry-audit, updated-model contract, and readiness unit tests | 30/30 PASS in 0.305 s for the pre-shelf-test tree | `goal_completion_static_tests_2026-08-27.stderr.log`, 5,714 B, SHA-256 `04b16bbe02a05b4c6857ac447a9c4abfe3aaebf71c38cb264e4636186416d20a`; stdout is the expected empty file |
| Fail-closed model-input readiness decision | Expected exit 1; status FAIL, 0 verified and 9 open | `model_release_readiness_goal_completion_2026-08-27.json`, 3,977 B, SHA-256 `b3ae5ecbc52732b8eb392d0861a693b94d2e90057217f70952a3ab3cfea218f7` |
| Current inline-shelf/model contract | 17/17 PASS in 0.119 s | `component_campaign_shelf_contract_2026-08-27.stderr.log`, 3,292 B, SHA-256 `471e907c5e313d773bfea25e0b9cbdfd8d9b3e1961d012fa997a6752356fd4fe`; stdout is empty |
| Dependency-complete core plotting/field-comparison suite | 77/77 PASS in 2.499 s, zero skips | stdout SHA-256 `632d7f257a455e6b50764ca30c4a67661055f271a61ff95955186fbca81c7a7c`; stderr SHA-256 `b9f5dabf750ca026036dab1f483ce7c0ce5bf81065ebbb19e99ee87537ae7c6d` |
| Current plot-artifact integrity | 23/23 PASS, zero mismatches | `plotting_current_manifest_integrity_ansys_2026-08-27.stdout.log`, 7,944 B, SHA-256 `e49eccd2446934ed48d4b28403704d588cf506cbea435eb866e40ae9070db7e6` |
| Retained-case post-archive integrity | All 82 historical input rows accounted for; exact 0/1.5/1.6000000000000001 endpoints preserved | `retained_case_post_archive_integrity_2026-08-27.json`, 11,296 B, SHA-256 `1fcb0f76ef0cb4ebd1102b259ce536397c348176cb71b597fae6563a257c3414` |

The last complete production-source regression remains the 516.584 s,
affinity-mask-3, BelowNormal run recorded in
`added_feature_regression_runner_state_gates_2026-08-27.run.json`: exit 0,
`All added-feature tests passed.`, stdout SHA-256
`03d781c9abe21ec3722e038672ef4c6587bed734cc896d97ddde3ed17a7bd81c`,
and stderr SHA-256
`f4dba93c473013728126639c6b07f4c86196eeba63b61600c3abab37dc051d23`.
That is software evidence only; its own claim boundary excludes WSL, OpenFOAM
builds, meshing, corrected fields, and thermal validation. It also predates the
new inline-shelf C++ microcase and the addition of the component campaign to the
default harness. Those C++ changes remain uncompiled because available memory
stayed between roughly 0.38 and 0.68 GiB; current hashes are
`8a1896223244149d3f6fc095e764e7d0ee0f68e45e71b54c99d435ff716c0836`
for `native_template_microcase_test.cpp` and
`f676b8ef16b13b58a5c810f086a4397b1240f1e270399d31df65efddebfc215c`
for `run_added_feature_tests.ps1`.

## Requirement matrix

| Requirement | What is proven | What remains missing or failed |
|---|---|---|
| Updated model and components | Canonical model is 9,860 B, SHA-256 `a6448cc3a6ee00fdc72a3f6d49a27b7aeedb89a66e71b06e525f7190a335a05a`; 11 reusable components audit with no structural error | Six geometry assumptions remain open, including all three Rail-2 depth groups, NI separator walls, shelf construction, and missing MAU/DIN obstructions |
| Fan and heat definitions | All references parse; configured heat inventory is 2,315 W; signed-curve clamping and contract guards pass | Loads and fan curves remain estimates/uncalibrated. Two Thruster fans request 18 CFM against a roughly 13.76439 CFM positive-pressure domain; six Dell R470 definitions are about 0.9120% above their first crossing |
| NI chassis | Canonical definitions parse and the unfinished condition is detected | Interior-air and card-slot-air regions overlap. Every tested provisional separator spacing loses wall volume during stamping; no isolated, mesh-surviving separator or flow field exists |
| Storage shelf and unplaced geometry | Inventory, plotting, and a current Python contract include the inline shelf; the current C++ microcase now checks its obstruction, material, and short zero-load thermal path | The C++ test is uncompiled. The configured full-solid envelope analytically represents 66.1581 kg and 59,542.3 J/K of aluminum, so it remains a conservative placeholder rather than as-built shelf construction; MAU/DIN obstructions are absent and the R360 depth/SKU conflict remains unresolved |
| Unified air plotting | A dependency-complete core continuation passed 77/77; the three same-air tests pass and 23/23 current plot artifacts re-hash without mismatch | Plot geometry came from a preserved historical exporter lineage. `vtkmodules` and `imageio` are unavailable, leaving GIF, PyVista hotspot/convergence, and mesh-comparison runtime explicitly unverified. A corrected rerender still requires a rebuilt current exporter and closed geometry |
| Native component robustness | Pinned historical O2 microcases exercised 11 reusable templates plus one expected provisional-NI rejection; the current campaign source now includes all 11, the exact inline shelf, and the expected NI rejection | The current C++ campaign is uncompiled/unrun under the memory gate. Its controlled 0.0005 m3/s boundary and millisecond-scale thermal checks are software screens, not calibration, thermal soak, or full-rack interaction evidence |
| Full-rack native path | Workload planner rejects before allocation and preserves outputs | No native full-rack flow, temperature, convergence, or balance field exists; the estimated 1,838,545,800 cell visits exceed the configured 30,000,000 limit |
| Corrected OpenFOAM path | Runner/restart/provenance/resource guards are strongly regression-tested | Corrected target case is absent; installed solver is stale; current exporter has not been rebuilt; resource gate fails before WSL |
| Industry readiness | Fail-closed release machinery is implemented | Readiness is FAIL with 0/9 assumptions verified; no mesh/time independence, uncertainty analysis, or laboratory flow/temperature comparison exists |

## Retained OpenFOAM field: valid scope and failed physics

The retained case is historical pre-correction evidence, not a current model
result. It does prove a connected 925,388-cell fluid region, 24 connected
physical openings, 47 nonempty/nonoverlapping heat zones totaling exactly
2,315 W, complete 1.50/1.60 s checkpoints, and about 0.00546% gross exterior
mass mismatch at 1.60 s.

It cannot support a realistic-flow or temperature claim:

- Between 1.50 and 1.60 s, whole-fluid velocity RMS changed 8.8677% and
  gauge-adjusted `p_rgh` changed 6.3867%, both above the 3% freeze gate.
- Only 0.874989 nominal air exchanges had elapsed.
- Ten of 47 fans were outside the positive-pressure curve domain and eight
  more were near the limit; only 29 passed.
- The mesh has 39,878 determinant failures, including 5,082 fluid cells, and
  wall y+ spans about 0.579 to 1,017.789.
- Highest solid temperature was only 294.462452 K after the cold start.
  Instantaneous outlet sensible removal was 8.9274 W versus 2,315 W applied,
  a 99.6144% steady-removal mismatch.
- Formal transient first-law closure is **NOT EVALUABLE** because the required
  gap-free per-step boundary/source energy ledger was not recorded.

Excellent gross mass balance does not override unsettled spatial fields,
out-of-domain fan operating points, failed mesh quality, or immature thermal
storage.

Post-archive integrity was reverified without writing the case. The provenance
manifest has 81 of 82 rows still present and byte-identical; the sole absent
row is the documented rejected-1.610 y+ contaminant, whose preserved copy and
supplemental manifest match. Root plus processor0–3 contain only `0`, `1.5`,
and `1.6000000000000001`, and all ten retained 1.5/1.6 endpoint sets contain
74/74 nonempty files. This strengthens recoverability, not physical validity.

## Material and provisional-shelf consequences

The current material-fidelity audit reports 13 component instances, 9
heterogeneous instances, and 55 internal solid regions homogenized by the
one-region-per-component OpenFOAM export. Across 0.0232004117 m3 of defined
internal solids, the exported reduction adds 8.99054 kg and 14,811.4 J/K
relative to the declared materials. The 6,456-byte audit log has SHA-256
`448a1d52becc003365eb819418881d90ebad6b5dae42c9f3380a36805a092ae5`.

Independently, the inline shelf's configured 0.4445 by 0.413385 by 0.13335 m
full-solid aluminum envelope is 0.024503013493875 m3, 66.1581364 kg, and
59,542.3228 J/K. That is a deliberately conservative obstruction and thermal
mass until measured sheet thickness, hollowness, and openings replace it.

## Runtime-acceleration policy

The documented fast-screening policy remains:

- `0.0005 s` cap for initial live airflow and fan ramp;
- independently qualified `0.001 s` cap for later live-flow refresh windows;
- `20 s` cap for reusable frozen-flow thermal screening;
- three outer correctors for quantitative work; two is a screening candidate
  only until a corrected full-rack 3-versus-2 comparison passes.

The larger alternatives were rejected on evidence, not conservatism alone:

- A `0.00075 s` live-flow branch was 9.79% slower, changed velocity RMS by
  1.155754% and gauge pressure RMS by 11.946965%, and failed 10 of 33 fan
  equivalence checks.
- A `24 s` thermal-only branch was faster, but missed the strict thermal gates:
  0.123260498 K maximum-cell and 0.054939449 K component-average differences
  versus 0.05 K and 0.02 K limits.

The historical 0.1 s four-rank live windows took roughly 3,665–4,739 wall
seconds. The cost comes from millions of coupled cells, very small live-flow
steps, repeated pressure/velocity/energy solves, poor mesh conditioning, and a
memory-starved host. A one-to-two-day corrected run is therefore not yet
demonstrated.

## Launch decision and closure path

The fresh goal-completion resource evidence failed with no competing solver
processes: disk passed at 17,020,313,600 available bytes, but host memory failed
on the first sample at 379,506,688 bytes versus the mandatory
5,368,709,120-byte floor. WSL was requested but not queried after the host
failure, and no heavy stage began. The 3,426-byte evidence file
`openfoam_resource_gate_goal_completion_20260827T1020Z.json` has SHA-256
`0b9e7b4ad797b456b188bd88aa9ca568bc429525e3518ff3423573e189f5046a`.

Physical completion requires, in order:

1. Close or explicitly supersede all nine input-assumption rows with measured,
   hash-bound evidence.
2. Pass both host and WSL resource gates.
3. Rebuild, test, and hash a current exporter; clean-build and attest the
   current OpenFOAM solver with positive live and thermal microcases.
4. Export a uniquely named corrected case and obtain a production-quality mesh
   plus mesh-independence evidence.
5. Run live flow through exchange, spatial-convergence, mass-balance, and
   fan-domain gates.
6. Record a gap-free transient first-law ledger, complete thermal soak and
   timestep/outer-corrector sensitivity, quantify uncertainty, and compare
   airflow and temperatures with laboratory measurements.

Until those steps are complete, the correct status is robust screening
software with provisional physical inputs—not a validated rack prediction.
