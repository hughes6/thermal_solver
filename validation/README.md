# Updated research-lab model validation index

This directory is the evidence index for the updated 2026-08-24 research-lab
model. The present campaign is an engineering screening and robustness study;
it is not yet an industry-release validation package.

## Authoritative inputs

- Canonical model: `library/models/new_model_updated.toml`
  (`SHA-256 a6448cc3a6ee00fdc72a3f6d49a27b7aeedb89a66e71b06e525f7190a335a05a`)
- Resource-bounded OpenFOAM fixture:
  `library/models/new_model_updated_openfoam_export_test.toml`
  (current working SHA-256
  `14ed5a43d9b2151557be9a522dc0e54f2df93d69ec444eb9e9e492598588e3ea`)
- Exact active-case model snapshot: `provenance/model.toml` in the active
  case (SHA-256
  `88f1d230efa6a2fb7d24c2f1a906a22c6f7f77e6a61ae6cd118b6095ed0d50cb`);
  this snapshot, not the later commented working fixture, owns solved-field
  lineage.
- Native diagnostic fixture:
  `library/models/new_model_updated_native_smoke.toml`
- Fan curves: `library/fan_curves/fan_curves.toml`
- Corrected current N5766A component:
  `library/components/updated_Keysight_N5766A.toml` (SHA-256
  `17ab6fa85ca1619b555ed55b824a9dc242bc4af8ec572350e2a174d50dcbaa1d`).
  Its front intake now uses `z=21.8 mm`, `height=33.6 mm`, matching the
  internal-air span. The active 1.600 s OpenFOAM case and the 2026-08-25 plot
  geometry instead pin the earlier component hash
  `ada3afb4a9b53b86fb8a8b8932127b190475a359c46010550ebe0522876f5610`.
- Corrected current Trenton component:
  `library/components/updated_trenton_3u_bam.toml` (SHA-256
  `10aa7d722d00115ec33ea7961fe17aac57bc27e7e0ec1d03ccd3e8b6d6bcd635`).
  The provisional front intake now begins at the power-supply wall boundary,
  and the rear opening is represented as two surfaces on either side of that
  full-depth wall. This is a topology repair pending an as-built drawing, not
  dimensional validation.
- Inactive NI sensitivity input:
  `library/components/updated_NI_PXIe_Chassis_minimum_separator_sensitivity.toml`.
  It adds one explicitly provisional mirrored wall only; the canonical model
  retains `updated_NI_PXIe_Chassis.toml` and exposes the sensitivity path only
  as a commented opt-in. It is not an as-built geometry or solved model.
- Raw supplied update:
  `validation/baselines/pre_updated_geometry_2026-08-24/updated_model_raw_paste.txt`
- Machine-readable provisional-input ledger:
  `validation/UPDATED_MODEL_ASSUMPTIONS.toml`

The model variants are contract-tested to retain identical environment, rack,
component, fan, vent, and heat definitions. Only execution controls, mesh
resolution, backend selection, and case paths may differ.

The repository HEAD when this index was created was
`8465c86e8ceb2c2914e0ce37c4957430266717e0` on branch `v2.2`. The validation
worktree is intentionally still dirty while the live campaign continues;
therefore this hash alone is not a reproducible release revision.

## Evidence map

| Evidence | Purpose | Current interpretation |
|---|---|---|
| `../UPDATED_MODEL_INTAKE.md` | Raw-to-working-model intake record | Input provenance; some status text predates the revised CFD run |
| `../UPDATED_MODEL_STATIC_AUDIT.md` | Component bounds, overlaps, devices, heat and visual audit | Strong preserved bookkeeping evidence for the pre-N5766-correction geometry; it does not validate physical dimensions or supersede the current intake correction |
| `UPDATED_MODEL_ASSUMPTIONS.toml` | Rail-2 coordinates, missing NI/obstruction geometry, shelf construction, loads, curves, and material-reduction status | Machine-readable guard against treating estimated inputs as as-built data |
| `current_geometry_export_2026-08-26/output.txt` | Preserved post-N5766 geometry export (39,609 B; SHA-256 `4e3c4a4b96b12929ab141b7975aabb08db9b4d84d86a5d04e3f42db35f25d676`) | Historical pre-Trenton-repair lineage only; the completed `updated_component_plots_current_2026-08-26/` set below is the current geometry and plot authority |
| `updated_component_plots_2026-08-25/` | Preserved 13 component plots plus full rack from the pre-N5766-correction geometry | The plotter/color evidence remains valid: all air regions use the same `tab:cyan` fill and edge color, and the manifest hashes the preserved geometry, plotters, and PNGs. These images are no longer current-canonical geometry evidence. |
| `updated_component_plots_current_2026-08-26/` | Current post-N5766/post-Trenton-repair geometry (39,808 B) plus 13 component plots and one rack plot | Geometry SHA-256 `dbcaaa237fb8bf59c4841cf4e3a8b0a226d28208d23560c72900ce113d2ff2ea` exactly matches the preserved historical post-audit exporter output. Manifest SHA-256 `7a872709ae676bad1dad91bd67fb1c7f34af28fa4abdc6a59ec24e811a2ddb85`; the full artist regression passed 10/10 and proves all air fill/edge colors agree. |
| `revised_native_smoke_2026-08-24/` | Earlier one-step native diagnostic | Historical only: its geometry output predates a later Trenton template correction and must not be used as the current geometry baseline |
| `revised_native_regression_2026-08-26/RUN_REPORT.md` | Full-rack attempts and nonlinear/active-set hardening through the earlier conservative-flux campaign | Its 7/11 matrix is preserved historical evidence and is superseded for current template functionality by the exact O2 topology/fidelity campaign below; the full-rack topology/resource limitation remains relevant |
| `revised_native_regression_2026-08-26/CONVECTION_AND_NI_HARDENING_2026-08-26.md` | Pinned convection conservation/stability, OpenFOAM mode, NI sensitivity, plotting, resource, and speed-policy audit | Historical subsystem evidence for its source snapshot. Native executable checks passed; the OpenFOAM source patch was explicitly uncompiled; the NI variant remained one expected geometry rejection rather than a solved sensitivity. Current-source identity is established by the runner-state-gates regression below. |
| `revised_native_regression_2026-08-26/ENERGY_PLOTTING_PROVENANCE_AND_NI_LADDER_2026-08-26.md` | Pinned native frozen-capacity energy ledger, NI mesh ladder, recovered plotting, deployed OpenFOAM provenance gate, and dated artifact hashes | Historical subsystem evidence. Discrete native closure reaches about `1e-11 J`; the provisional NI wall remains rejected for an invariant stamping-topology loss; the installed OpenFOAM binary is definitively stale; no physical-validation or current OpenFOAM runtime claim is authorized. |
| `revised_native_regression_2026-08-26/WORKLOAD_MEMORY_RUNTIME_HARDENING_2026-08-26.md` | Pinned workload, memory, lazy-output, production-build, resource-gate, and stage-specific timestep record | Historical evidence that introduced the 0.0005 s initial/ramp, 0.001 s screening-refresh, and 20 s reusable frozen-flow policy; confines 24 s to the exact retained exploratory case; records the 1,838,545,800-versus-30,000,000 fail-closed native preflight and its no-field claim boundary. The runner-state-gates regression below is the current software-evidence authority. |
| `revised_native_regression_2026-08-26/FULL_RACK_NATIVE_FEASIBILITY_2026-08-26.md` | Full-rack native planner, payload, work, and geometry-resolution audit | The post-audit preflight confirms 2,847,663 fine plus 216,580 coarse cells and rejects before allocation in 0.109 s. The detailed cut-suppression study remains plan-only evidence; no native full-rack flow/temperature result exists. |
| `revised_native_regression_2026-08-26/HOST_RESOURCE_CLEANUP_2026-08-27.md` | Exact-target host cleanup audit | Six inactive Visual Studio installer extraction directories totaling 8,215,369,245 logical bytes were permanently removed after a process gate. Active diagnostics, project files, OpenFOAM state, and validation evidence were explicitly excluded; C: free space rose to 16,600.8 MiB. |
| `revised_native_regression_2026-08-26/CORRECTED_RUN_ACCELERATION_GATES_2026-08-27.md` | Current stage-specific timestep, runner-contract, low-memory, attestation, release, and resource-gate authority | Separates 0.0005 s initial live flow, 0.001 s refresh, and 20 s thermal-only operation; records the rejected 0.00075/24 s alternatives; documents exact fixed endpoints and disabled bypass launchers; and states that no corrected OpenFOAM fields or industry-ready result yet exist. |
| `revised_native_regression_2026-08-26/model_release_readiness_20260827T070322145Z.json` | Create-once model-input release gate | Expected FAIL: 0 verified and 9 open assumption rows. It prevents provisional rail depths, NI walls, shelf/obstruction geometry, loads, curves, and homogenized materials from being called as-built. Passing would establish traceability only, not CFD validation. |
| `revised_native_regression_2026-08-26/LOW_MEMORY_OPENFOAM_REGION_PREPARATION_2026-08-27.md` and `openfoam_low_memory_selector_equivalence_2026-08-27.json` | Bounded-memory split lifecycle and retained-case selector proof | The failed 19 mm, 2,800,980-cell input has a theoretical 109-selector scalar-payload inventory of 2,442,454,560 bytes (2.2747 GiB). Separately, logical equivalence passed only on the retained pre-correction 22.5 mm, 1,033,200-cell case, with evidence SHA-256 `09fe90a02f040c25b84fbec914f7c0ea4ac287f6a245ad2a523ae927667f256d`. No full 19 mm split or 19 mm equivalence run was completed, so its remaining-memory fit is unproven. |
| `revised_native_regression_2026-08-26/OPENFOAM_SOLVER_RUNTIME_ATTESTATION_2026-08-27.md` and `openfoam_solver_runtime_attestation_nonwsl_2026-08-27.stdout.log` | Clean build/deploy rollback, no-case runtime handshake, generated-runner provenance, and negative-mode microcase design | Non-WSL focused evidence passed 29 policy/attestation tests with one Windows symlink-privilege skip plus generated-runner guards. Project-source digest `6f5b54fddb0218558dac8798915169133c66564c199e6c95778410f9a23c9ead` covers only repository-local Make files/options and solver C++; no real OpenFOAM build or positive physics case has run. |
| `OPENFOAM_CORRECTED_RUN_READINESS_2026-08-26.md`, `../tools/openfoam_resource_gate.ps1`, and `../tests/openfoam_resource_gate_test.ps1` | Fail-closed corrected-run launch policy and implementation | Production callers cannot lower the 5-GiB host/WSL memory, 60-second sampling, or 10-GiB disk floors. The initial host gate does not wake WSL; a requested WSL check is followed by immediate host revalidation. Evidence paths are create-once and synthetic coverage exercises process, memory, paging, disk, WSL, post-WSL invalidation, floor, and immutable-evidence failures. |
| `revised_native_regression_2026-08-26/openfoam_resource_gate_goal_resume_host_20260827T065641249Z.json` | Pre-regression actual host-only resource decision | Expected exit 21 before WSL startup: disk passed at 17,345,138,688 available bytes, but immediate host memory was 936,706,048 bytes versus the mandatory 5,368,709,120-byte floor. No OpenFOAM build/export/preparation/solve began. |
| `revised_native_regression_2026-08-26/openfoam_resource_gate_post_acceleration_20260827T081233978Z.json` | Post-regression actual host-only resource decision | Expected exit 22 before memory sampling or WSL startup: no competing solver process, but C: free space was 9,639,960,576 bytes versus the fixed 10,737,418,240-byte floor. Memory was deliberately skipped after disk failure; this result cannot be used as a current memory reading. |
| `revised_native_regression_2026-08-26/openfoam_resource_gate_pre_export_20260827T0038MDT.json` | Actual host-only pre-export resource decision (3,424 B; SHA-256 `0764fa1f14e38d525e4eeb01068bf020c8662a0e81a42cad7e0204ecb39533e0`) | Expected fail-closed exit 21: no competing CFD/MPI process and 17,417,773,056 free disk bytes passed, but 1,223,729,152 available memory bytes failed the 5,368,709,120-byte floor. WSL was neither requested nor launched, and no export/mesh/solve began. |
| `revised_native_regression_2026-08-26/openfoam_resource_gate_final_idle_20260827T095617107Z.json` | Superseded idle resource decision (3,494 B; SHA-256 `33c4fb9525398af3c7ec53156fe214a422b3d0e67ca13e144d0c5fcbe5ca6d51`) | Expected fail-closed exit 21: no competing CFD/MPI process; disk passed at 17,129,152,512 free bytes versus 10,737,418,240 required; the first memory sample failed at 951,701,504 available bytes versus 5,368,709,120 required. Superseded by the later goal-completion decision below. |
| `revised_native_regression_2026-08-26/openfoam_resource_gate_goal_completion_20260827T1020Z.json` | Current goal-completion resource decision (3,426 B; SHA-256 `0b9e7b4ad797b456b188bd88aa9ca568bc429525e3518ff3423573e189f5046a`) | Expected fail-closed exit 21 under affinity mask 3/BelowNormal: no competing CFD/MPI process; disk passed at 17,020,313,600 free bytes; host memory failed immediately at 379,506,688 versus 5,368,709,120 required. WSL was requested but skipped after the host failure, and no corrected export, mesh, build, or solve began. |
| `revised_native_regression_2026-08-26/openfoam_resource_gate_synthetic_2026-08-27.stdout.log` | Preserved synthetic resource-gate regression | Exit 0. Host/process/memory/paging/disk/WSL/post-WSL invalidation, production-floor, invalid-configuration, and create-once evidence cases passed with `realWslCalls=0`. |
| `revised_native_regression_2026-08-26/added_feature_regression_runner_state_gates_2026-08-27.{stdout,stderr}.log` and `.run.json` | Last complete production-source regression under a two-logical-core, BelowNormal guard | PASS/exit 0 in 516.584 s under affinity mask 3. Stdout is 333,651 B / 4,115 lines / SHA-256 `03d781c9abe21ec3722e038672ef4c6587bed734cc896d97ddde3ed17a7bd81c` and ends `All added-feature tests passed.` It predates the later inline-shelf C++ microcase and default-harness component-campaign addition, which remain uncompiled under the current memory gate; therefore it is not a complete pass for the present test tree. |
| `revised_native_regression_2026-08-26/component_campaign_shelf_contract_2026-08-27.{stdout,stderr}.log` | Current inline-shelf/model contract replay | 17/17 PASS in 0.119 s under affinity mask 3/BelowNormal. Stderr is 3,292 B / SHA-256 `471e907c5e313d773bfea25e0b9cbdfd8d9b3e1961d012fa997a6752356fd4fe`; stdout is empty. The C++ campaign source remains uncompiled. |
| `revised_native_regression_2026-08-26/plotting_scientific_dependency_complete_ansys_*` and `plotting_current_manifest_integrity_ansys_*` | Dependency-complete plotting/field-comparison continuation and current artifact verification | Core ANSYS-Python suite: 77/77 PASS, zero skips. Plot-artifact integrity: 23/23 PASS, zero mismatches. PyVista selection passed four methods but explicitly skipped three methods and two mesh cases because usable `vtkmodules` and `imageio` are absent. All air artist/color tests pass. |
| `revised_native_regression_2026-08-26/goal_completion_material_fidelity_2026-08-27.stdout.log` | Fresh current-model OpenFOAM material-reduction audit | Exit 0: 9 heterogeneous component instances and 55 internal solid regions are homogenized; modeled delta is +8.99054 kg and +14,811.4 J/K. Log is 6,456 B / SHA-256 `448a1d52becc003365eb819418881d90ebad6b5dae42c9f3380a36805a092ae5`. |
| `revised_openfoam_22p5mm_2026-08-25/retained_case_post_archive_integrity_2026-08-27.json` | Read-only post-archive integrity replay | All 82 historical input rows are accounted for, exact retained times are unchanged, and all ten endpoint sets have 74/74 nonempty files. This verifies preservation only; airflow, fan-domain, mesh, thermal-soak, and first-law gates remain failed or unevaluable. |
| `revised_native_regression_2026-08-26/GOAL_COMPLETION_AUDIT_2026-08-27.md` and `goal_completion_*` evidence | Current requirement-by-requirement completion decision plus a focused two-core static recheck | Confirms 30/30 current model/audit/readiness tests, zero structural geometry errors with the one known NI air/air warning, and a fresh expected readiness FAIL with 0/9 assumptions verified. It separates proven software/static behavior from missing corrected CFD, mesh, thermal-soak, first-law, calibration, and laboratory evidence. |
| `revised_native_regression_2026-08-26/added_feature_regression_acceleration_gates_2026-08-27.{stdout,stderr}.log` and `.run.json` | Historical acceleration-gates regression | Exit 0 in 397.559 s for its pinned source snapshot. Superseded for current-source identity by the runner-state-gates regression above. |
| `revised_native_regression_2026-08-26/added_feature_regression_resource_gate_2026-08-27.stdout.log` | Historical complete harness (332,820 B; 4,113 lines; SHA-256 `1bd2cf90e5b414b992bfb7d949bc04e71371e0a5249c1b38d4bf65132dbab01b`) | Exit 0 in 303.075 s wrapper time for its pinned source snapshot. Superseded for current-source identity by the runner-state-gates regression above. |
| `revised_native_regression_2026-08-26/added_feature_regression_resource_gate_2026-08-27.stderr.log` | Paired historical harness stderr (2,585 B; 110 total/88 non-empty lines; SHA-256 `d76ac635fbf0db9c53331abb56f7c12e0cad3fed44d7441a4fa1516bcd7a2287`) | Successful unittest summaries for its pinned snapshot; superseded for current-source identity by the paired runner-state-gates stderr above. |
| `revised_native_regression_2026-08-26/added_feature_regression_resource_gate_launch_quoting_failure_2026-08-27.{stdout,stderr}.log` | Preserved launcher-only quoting failure preceding the historical 303.075 s harness | Exit 64 in 0.437 s before the harness, model, or any test started. The corrected quoted-path invocation is separate exit-zero historical evidence; this record is not a software-test failure. |
| `revised_native_regression_2026-08-26/added_feature_regression_post_audit_runtime_hardening_2026-08-26.stdout.log` | Historical immediately preceding complete harness (332,552 B; 4,112 lines; SHA-256 `36838d5811bd909116e74e9ea92c9843b1982c0d7a86f50929d2fea64480e517`) | Exit 0 in 295.746 s wrapper time for its pinned source snapshot. Superseded for current-source identity by the runner-state-gates regression above. |
| `revised_native_regression_2026-08-26/added_feature_regression_post_audit_runtime_hardening_2026-08-26.stderr.log` | Paired historical harness stderr (2,585 B; 88 non-empty lines; SHA-256 `1f21f65650e34384aa2720e4f2908322c134a49796d5ecc37e996adbf19d7a9e`) | Contains only successful unittest summaries; superseded for current-source identity by the paired runner-state-gates stderr above. |
| `revised_native_regression_2026-08-26/native_template_microcase_post_audit_runtime_hardening_2026-08-26.stdout.log` | Pinned historical post-audit O2 component campaign (59,774 B; 640 lines; SHA-256 `895430bcbb8750012679daea582cf71fd9d7b42d9ca5640fd4adbf7487e60bf0`) | Exit 0 in 74.651 s wrapper time and 74.4218 s internal time: 11 canonical functional passes plus one expected provisional-NI geometry rejection, 12 selected templates, 2 Meanwell placements, and 2,315 W. Stderr is empty. This isolated microcase is not current-source identity, full-rack interaction, thermal soak, mesh/timestep independence, or experimental validation. Canonical NI still has 42 stalled interfaces and a 22.7193 m/s peak. |
| `revised_native_regression_2026-08-26/native_template_microcase_post_audit_runtime_hardening_2026-08-26.exe` | Exact historical post-audit component executable (1,108,407 B; SHA-256 `133355e650d7c14ff0526997eb22707a4053c59dd07a8b4d301691ec2867ebd2`) | Reproduces its pinned historical 11 functional plus one expected provisional-NI rejection matrix. |
| `../model.exe` and `revised_native_regression_2026-08-26/model_post_audit_runtime_hardening_2026-08-26.exe` | Installed and archived byte-identical historical post-audit executable (1,838,877 B; SHA-256 `335efd17d7f000e90d13b4a2594cfade4ab68bde524b2910f81c73df7b00eb12`) | The historical post-audit geometry-only path exited 0 in 0.289 s; its 272-byte stdout remains SHA-256 `664849782a065fcdda6988f03a171126171a92e74dc81346c92281829b459454`, stderr is empty, and `output.txt` remains 39,808 B / SHA-256 `dbcaaa237fb8bf59c4841cf4e3a8b0a226d28208d23560c72900ce113d2ff2ea`. This binary predates current runner hardening and must not export a corrected case; no current-source production exporter binary exists. |
| `revised_native_regression_2026-08-26/full_rack_native_preflight_post_audit_runtime_hardening_2026-08-26.{stdout,stderr}.log` | Historical post-audit native preflight | Expected exit 1 in 0.109 s. Stdout is 210 B / SHA-256 `2df73fed20722b742083324b84615c8d7871dde489c0e9a82630b3bd6c05f512`; stderr is 274 B / SHA-256 `1b93670fc5ce981665811837fae75b9f9eb46ba33132b54182a738989b372433`. Counts were unchanged, all four sentinels were preserved, and no flow or temperature field was produced. At that snapshot `.thermal_sim_last_run.json` was 609 B / SHA-256 `39ae95c8825c855dd03bbd233747cb95000bb49c888ca918c185b2f655ccd1e7`. |
| `revised_native_regression_2026-08-26/runtime_acceleration_runner_state_gates_2026-08-27.manifest.json` | Current runner-state provenance manifest | Generated only after the final source, regression, idle-gate, and documentation state was fixed; `create_runtime_acceleration_manifest.ps1 -VerifyOnly` verifies every listed byte count and SHA-256. The manifest deliberately excludes its own hash to avoid self-reference; report its external file hash separately without embedding it in a hashed input document. |
| `revised_native_regression_2026-08-26/runtime_acceleration_resource_gate_2026-08-27.manifest.json` | Historical 401-file resource-gate provenance manifest | Its hashes pin its creation snapshot only. It predates the runner-state source and evidence changes and is not current-source provenance. |
| `revised_native_regression_2026-08-26/runtime_acceleration_final_runtime_hardening_2026-08-26.manifest.json` | Historical earlier provenance manifest | Its independent verification remains evidence for its earlier 391-file snapshot; neither historical manifest is current-source identity. |
| `revised_native_regression_2026-08-26/added_feature_regression_final_runtime_hardening_2026-08-26.stdout.log` | Historical pre-post-audit complete harness (334,749 B; 4,210 lines; SHA-256 `f1dc657613c9a038452d0d0c6afdc53ca28dce4682731f1982cb7c7c20fb7c17`) | Exit 0 for its pinned source snapshot. Superseded for current-source identity by the runner-state-gates harness above; optional skips remain explicit. |
| `revised_native_regression_2026-08-26/native_template_microcase_final_runtime_hardening_2026-08-26.stdout.log` | Historical pre-post-audit O2 component campaign (59,774 B; SHA-256 `3e2864b33c6f6d0b40937ade369f628ba925d51a7592063f39f0c6e9a641816a`) | Exit 0 with the historical 11-plus-one matrix in 72.6863 s. Followed by the pinned historical post-audit campaign above; neither row establishes current-source identity. |
| `revised_native_regression_2026-08-26/native_template_microcase_final_runtime_hardening_2026-08-26.exe` | Historical pre-post-audit component executable (1,108,407 B; SHA-256 `77b843e03f837f1cfcd599ddb8c34a99d61981c358731585f57c98774d96e15f`) | Reproduces its pinned historical matrix; superseded by the post-audit executable above. |
| `revised_native_regression_2026-08-26/added_feature_regression_runtime_hardening_2026-08-26.stdout.log` | Earlier runtime-hardening harness (334,307 B; SHA-256 `5efce768cf3d34a950707502f7a38981bb7f6461f5a981954ca1e51dbe12e3a9`) | Exit 0 for its source snapshot; followed by historical `final_runtime_hardening` and post-audit harnesses, then superseded for current-source identity by the runner-state-gates harness above. |
| `revised_native_regression_2026-08-26/native_template_microcase_runtime_hardening_2026-08-26.stdout.log` | Earlier O2 component campaign (59,774 B; SHA-256 `da5a0991a60d91e2236a56560d37260716e0a0509ffdc08ae6acdba243155007`) | Exit 0 with the same 11-plus-one matrix in 78.1307 s; followed by the historical `final_runtime_hardening` and post-audit campaigns above. |
| `revised_native_regression_2026-08-26/added_feature_regression_energy_provenance_2026-08-26.stdout.log` | Prior complete harness (327,008 B; SHA-256 `1c1dc85928631b4713ab401309f0f4206e11b653bca989c803d5576c2ea76f12`) | Exit 0 for its source snapshot, including the native energy ledger, seven-rung NI ladder, 10 semi-frozen source-policy checks, and executable missing/stale-solver provenance rejection. Superseded for source identity by the runtime-hardening harness. |
| `revised_native_regression_2026-08-26/native_template_microcase_energy_provenance_2026-08-26.stdout.log` | Prior exact O2 component campaign (58,145 B; SHA-256 `6005a082afabfb60549b2904bc359db82cccf226d6b23c2393cf259f8a99637d`) | Exit 0 with the same 11-plus-one result for its source snapshot; superseded by the runtime-hardening campaign above. |
| `revised_native_regression_2026-08-26/added_feature_regression_convection_hardening_2026-08-26.stdout.log` | Historical convection-hardening harness (301,654 B; SHA-256 `bb573ac51bc8d9037b5e50b08e1199c2e2040bfbf15115b1fdc1a8139c999576`) | Exit 0 for its source snapshot; followed by the later energy/provenance harness above. |
| `revised_native_regression_2026-08-26/native_template_microcase_convection_hardening_2026-08-26.stdout.log` | Historical convection-hardening O2 component campaign (58,145 B; SHA-256 `9d9f6182059470ea4a7ff89fa300b574c411814832a0b17793ba2e0571668c95`) | Exit 0 with the same 11-plus-one matrix in 68.2119 s; followed by the later energy/provenance campaign above. |
| `revised_native_regression_2026-08-26/native_template_microcase_energy_provenance_2026-08-26.exe` | Prior exact component executable (1,086,574 B; SHA-256 `63f3c3e64ca4509941ff397cdfcea0bb4effbfb9beeba16343bd27fa92a0ef51`) | Reproduces the same 11-plus-one matrix for its historical source snapshot; followed by the retained `final_runtime_hardening` executable above. |
| `revised_native_regression_2026-08-26/native_template_microcase_convection_hardening_2026-08-26.exe` | Historical convection-hardening campaign executable (1,086,574 B; SHA-256 `514b76ab53429773383b5ac2eaf11480c31cf15a33f5cdcf55084061928d0027`) | Preserved for its source snapshot; superseded by the energy/provenance executable above. |
| `ni_minimum_separator_sensitivity_2026-08-26/` | Geometry-only, mesh-ladder, and plotting evidence for the inactive provisional NI wall | Native stamping rejects wall 1 changing from `0.000192` to `0.000174 m^3` before flow at every tested spacing. Plotting was recovered with the installed ANSYS Python runtime: 10/10 tests pass, both actual Air artists use the same cyan fill/edge color, and hash-verified component/rack PNGs are preserved. These plots show nominal source geometry only. |
| `revised_native_regression_2026-08-26/native_template_microcase_runtime_accel_2026-08-26.stdout.log` | Historical exact O2 campaign after runtime-control changes (57,670 B; SHA-256 `e4b99873a92ce411cb731e48abd622e66ad90e658280696c2c8c1b984ce0518e`) | Exit 0, 11/11 reusable templates, and 102.664 s elapsed. Followed by the later convection-hardening campaign above; its Dell/Thruster and NI realism warnings remain relevant. |
| `revised_native_regression_2026-08-26/native_template_microcase_runtime_accel_2026-08-26.exe` | Executable for the historical runtime-control campaign (SHA-256 `b421dc714036340d0c5575a174ba249803e401ceba37b80a8276f2bb9d517ef9`) | Reproduces its 11/11 matrix; short thermal steps establish robustness only, not soak convergence or full-rack validation. |
| `revised_native_regression_2026-08-26/native_template_microcase_topology_fidelity_2026-08-26.stdout.log` | Prior exact O2 functional campaign (57,670 B; SHA-256 `cf585be08f963e002408a81fe0f686310225e3a3f61d9ddf4e417e544d10207f`) | Preserved earlier 11/11 authority for its source snapshot; followed by the runtime-control replay above. |
| `revised_native_regression_2026-08-26/native_template_microcase_topology_fidelity_2026-08-26.exe` | Executable for the prior exact O2 campaign (1,079,372 B; SHA-256 `d1d0f2198a6a6cb5a2eee70621faf159352d90a34831c344072606be2affd61e`) | Preserved reproducibility artifact for its source snapshot. |
| `revised_native_regression_2026-08-26/native_template_microcase_conservative_flux_active_set_replay_2026-08-26.stdout.log` | Superseded exact replay (38,431 B; SHA-256 `c9ae2fd8fa92b83b86534c77ea22e4e82510317552f27516eda110f541a8c84b`) | Historical exit 1 and 7/11 result, preserved to show the former Dell/Thruster/Cisco curve-domain and Trenton path gates before topology/fidelity handling and the Trenton opening repair |
| `revised_native_regression_2026-08-26/native_template_microcase_conservative_flux_active_set_2026-08-26.exe` | Preserved executable for the superseded replay (1,080,959 B; SHA-256 `49b7215a762dffd1e0b8fe7f723f6bf980c14efa20114c0096f2169b0ffae239`) | Historical binary pinned by its manifest; it is not the current functional authority |
| `revised_native_regression_2026-08-26/native_conservative_flow_active_set_2026-08-26.manifest.json` | Historical conservative-flow manifest (9,061 B; SHA-256 `76ed26c1fcbbf5fac8b064590d9b42fd6ea2446a9e6df44b34437fdea68f75de`) | Verified its 35-file state with zero mismatches, but it predates the latest topology/fidelity campaign and final Trenton repair |
| `revised_native_regression_2026-08-26/model_final_runtime_hardening_2026-08-26.exe` | Historical pre-post-audit production executable (1,830,967 B; SHA-256 `2febfda3b22fec8db347beabec1f1ff47af801819bd127b7d7ca093ad6952ca2`) | Passed the historical default geometry-only path for its pinned source snapshot; superseded by the post-audit production executable above. |
| `revised_native_regression_2026-08-26/added_feature_regression_runtime_accel_2026-08-26.stdout.log` | Historical runtime-acceleration harness (295,556 B; SHA-256 `933922b1cbfa191a42e95cbe6e20e4439d86575959ba327d9d5f2492f0f36379`) | Exit 0 for its source snapshot. Its executable generated-runner guard rejected unsafe overrides before writes and restored root `system/fvSolution` to live three-pass PIMPLE. Followed by the later energy/provenance and runtime-hardening harnesses above. |
| `revised_native_regression_2026-08-26/runtime_acceleration_current_source_2026-08-26.manifest.json` | Historical stale pre-final runtime-acceleration manifest | Pins its creation snapshot, including an older production executable and earlier harness, and must not be used as current-source provenance. It is superseded by the runner-state-gates manifest above. |
| `revised_native_regression_2026-08-26/added_feature_regression_conservative_flux_active_set_2026-08-26.stdout.log` | Preserved conservative-flow full harness (295,285 B; SHA-256 `dc448db1441c6d1262a3bf9c57358d474ad8564b3ed9192e0b599a044f791656`) | Historical exit-0 coverage for that source snapshot; followed by the 295,556-byte runtime-acceleration harness and later snapshots above |
| `revised_native_regression_2026-08-26/native_template_microcase_final_verified_2026-08-26.stdout.log` | Historical pre-conservative-flux standalone campaign (21,478 B; SHA-256 `e59c867645204ba8b53392591a30dc41fb0fb26c45ce2eedc67685fa4b36e070`) | Preserved 8/11 historical result only; it predates exact face-flux publication, the N5766 correction, strict Trenton path detection, and the NI bounded-fan fix |
| `revised_native_regression_2026-08-26/native_template_microcase_final_verified_2026-08-26.manifest.json` | Historical manifest (6,262 B; SHA-256 `ef967d3ea16ac82c8d49cf8c70ef2e82046daf2ee7a66668b7a7c9bc9e456b8e`) | Pins its old dirty-worktree state but omits the later solver/loader/tests and must not be used as current-source authority |
| `revised_native_regression_2026-08-26/added_feature_regression_authoritative_2026-08-26.stdout.log` | Historical pre-conservative-flux harness (89,525 B; SHA-256 `0cf53e59d7125281e030c44b6f1b9948d233d5e05aa62ca65b30ef0351c9368d`) | Passed its then-registered checks, but predates the current topology, enthalpy-transfer, grounding, lifecycle, and active-set regressions |
| `revised_native_regression_2026-08-26/added_feature_regression_2026-08-26.stdout.log` | Historical pre-nonlinear-hardening harness (67,763 B; SHA-256 `92c626c262b0ab2aac0ed0845dddbf1d0bf728cc5e5939dcc75d70bc8803b787`) | Preserved history only; followed by the conservative-flux, runtime-acceleration, and later snapshots above |
| `revised_openfoam_19mm_oom_2026-08-24/` | Nominal-resolution preparation attempt | Establishes a resource failure, not mesh convergence |
| `revised_openfoam_22p5mm_2026-08-25/` | Preserved 1,033,200-cell OpenFOAM screen | Authoritative numerical screening history for its exact pre-N5766-correction lineage; it is not a solved field for the current corrected component geometry |
| `revised_openfoam_22p5mm_2026-08-25/CASE_PROVENANCE.md` | Hashes of the exact input snapshots embedded in the active case | Authority for that solved mesh lineage while the project worktree remains dirty; it pins the old N5766A hash `ada3afb4...` |
| `revised_openfoam_22p5mm_2026-08-25/MESH_QUALITY_AUDIT.md` | Exact per-region `checkMesh` determinant results | Corrects the earlier solid-only wording: 39,878 cells are below the threshold—5,082 fluid and 34,796 across 12 solids; this remains a quantitative sign-off blocker |
| `revised_openfoam_22p5mm_2026-08-25/PERFORMANCE_AUDIT.md` | Measured runtime, load balance, and controlled optimization decisions | Explains the 10--13 wall-hour/s cost; retains 0.0005 s and 3x2 PIMPLE; auto decomposition passed the short screen but its longer confirmation was resource-invalid, so the generated default remains pinned |
| `revised_openfoam_22p5mm_2026-08-25/performance_benchmarks_1p60/TIMESTEP_BENCHMARK.md` | Matched 0.0005 versus 0.00075 s timestep experiment | Larger step rejected: 9.79% slower, spatial-field failures, and 10/33 fan operating points outside tolerance |
| `revised_openfoam_22p5mm_2026-08-25/THERMAL_TIMESTEP_ACCELERATION_2026-08-26.md` | Matched implicit thermal-only timestep screen and confirmation | Retain the 0.0005 s live-airflow cap. The exact old-case 24 s experiment reduced cumulative wall time 22.19% versus 20 s but failed strict temperature gates, so reusable/current export profiles remain at 20 s. The benchmark belongs only to the preserved pre-N5766 case. |
| `revised_openfoam_22p5mm_2026-08-25/RUNTIME_ACCELERATION_POLICY_2026-08-26.md` | Consolidated runtime decisions, measured error/speed tradeoffs, resource gate, and promotion protocol | Retains live 0.0005 s and live three-pass PIMPLE. Two thermal-only outer energy-coupling passes are a screening candidate only; use `THERMAL_ONLY_OUTER_CORRECTORS=3` for its control and require corrected full-rack early-heating and near-steady A/B evidence before promotion. |
| `revised_openfoam_22p5mm_2026-08-25/performance_benchmarks_1p60/DECOMPOSITION_BENCHMARK.md` | Pinned versus auto-selected CHT interface placement | Short screen passed: 15.36% lower total clock time with strict field, fan, and balance equivalence; first longer attempt produced resource-pressure observations and must be retried under stable conditions |
| `revised_openfoam_22p5mm_2026-08-25/performance_benchmarks_1p60/DECOMPOSITION_LONG_CONFIRMATION_ATTEMPT.md` | Aborted 1.610--1.700 s auto-placement confirmation | Seven steps had no failure signature, but timing was classified resource-invalid from live observations; no new checkpoint was written |
| `revised_openfoam_22p5mm_2026-08-25/performance_benchmarks_1p60/THREE_RANK_RESOURCE_BENCHMARK.md` | Three-rank auto-decomposition memory-mitigation test | Partition passed, but live memory/paging gate failed and the first step took 84 s; three ranks are not a viable low-memory fallback on this host |
| `revised_openfoam_22p5mm_2026-08-25/performance_benchmarks_1p60/ACTIVE_CASE_RECOVERY_1P600.md` | Recovery from rejected-benchmark control/report contamination | Valid fields remain at 1.600 s; rejected 1.610 reports were hash-preserved and removed, and accepted 0.0005 s controls were restored |
| `revised_openfoam_22p5mm_2026-08-25/performance_benchmarks_1p60/ACTIVE_CASE_LAUNCHER_HARDENING.md` | Fail-closed restart-lineage guard and installed-launcher identity | Guard rejects future active control/report state before a warm-start solver stage; exporter and active case are hardened |
| `revised_openfoam_22p5mm_2026-08-25/OPENFOAM_ARCHIVE_PLAN_2026-08-26.md` | Historical disk-cleanup ledger and exact private-archive allowlist | Planning record only; its pending-authorization state was superseded by the completion record below |
| `revised_openfoam_22p5mm_2026-08-25/OPENFOAM_ARCHIVE_COMPLETION_2026-08-26.md` | Final remote/local verification and deletion ledger for the authorized archive | All 28 approved groups are remotely verified and their exact 12,901,601,443 source bytes removed; required restart, configuration, logs, post-processing, and validation evidence remain local |
| `revised_openfoam_22p5mm_2026-08-25/performance_benchmarks_1p60/PIMPLE_BENCHMARK.md` | Matched 3x2 versus 2x2 PIMPLE coupling experiment | Two outer correctors rejected despite 27.25% post-first-step speedup because velocity, pressure, maximum-temperature, and 3/33 fan-flow gates failed |
| `revised_openfoam_22p5mm_2026-08-25/boundary_mass_balance_through_1p60.csv` and `boundary_opening_directions_1p60.csv` | Exterior flow, exchange, and boundary-fan signs through 1.60 s | 0.00546% mismatch; 14/14 fans correct; 0.875 exchanges, but not yet the one-exchange minimum |
| `revised_openfoam_22p5mm_2026-08-25/fan_operating_domain_1p60.csv` and `fan_operating_domain_1p60.md` | Internal and boundary flow versus each actually exported fan curve's first zero-pressure point | FAIL: 10/47 outside the positive-pressure curve domain, 8/47 near the limit, 29/47 pass; device flow splits remain provisional |
| `revised_openfoam_22p5mm_2026-08-25/field_convergence_1p40_1p50_to_1p60.csv` | Short- and long-window spatial field change | Latest velocity change is 8.87% RMS and still fails the 3% freeze gate |
| `revised_openfoam_22p5mm_2026-08-25/validation_1p60.md` | Full checkpoint validation gate | Steady-state heat-removal gate fails as expected during warm-up; this is not a transient first-law error or a thermal pass, and high-precision storage-inclusive closure remains pending |
| `revised_openfoam_22p5mm_2026-08-25/TRANSIENT_FIRST_LAW_AUDIT_STATUS_1P50_1P60.md` | Fail-closed interval-energy audit contract and retained-evidence assessment | Formal closure is not evaluable from the present endpoint summaries because the gap-free per-step boundary/source ledger is absent; the new audit tool passed 20 focused tests and requires an instrumented rerun |
| `revised_openfoam_22p5mm_2026-08-25/field_plots_1p60/` | Fixed-scale whole-rack, Dell, and NI speed/pressure/temperature cuts from the exact pre-N5766 1.600 s checkpoint | Flow is spatially structured but not frozen; Dell has narrow jets and low-speed pockets, NI has a strong upward core, and all temperatures remain early-transient. The NI interpretation is provisional until separator walls are modeled. The 24-record manifest SHA-256 is `989d58cc85288cf1237a1bee1a855490036c71f8e77df5b925ae22334383d3ad` |
| `revised_openfoam_22p5mm_2026-08-25/scientific_targeted_tests_2026-08-26.stdout.log` | Verbatim bundled-runtime regression output (SHA-256 `80ff628cb8885c4dc30270955c927822c1d543dd430a3f75ce0cb8858279fab5`) | 33 targeted scientific, fan/balance, and plotting tests passed with zero skips and zero failures |
| `revised_openfoam_22p5mm_2026-08-25/added_feature_regression_2026-08-26.stdout.log` | Verbatim complete added-feature harness output (SHA-256 `9e765c88f25ec6955fe62f86ede12493ffc6a2ad81a60ddc7df13fca86b54e07`) | All built and invoked checks passed; real-WSL flock integration was explicitly unavailable, while its synthetic archive/lock workflow passed |
| `revised_openfoam_22p5mm_2026-08-25/added_feature_regression_fan_domain_2026-08-26.stdout.log` | Historical 66,397-byte full-harness rerun after fan-domain, assumptions-ledger, geometry-contract, and fail-closed mesh-policy integration (SHA-256 `49b83db848cd5a2b314eeef99153affb4c40c6461289132b2fc693ea5a5bb5eb`) | All then-invoked tests passed; followed by runtime-acceleration and later source snapshots. Optional dependency and real-WSL skips remain explicit. |
| `revised_openfoam_22p5mm_2026-08-25/scientific_targeted_tests_fan_domain_2026-08-26.stdout.log` | Historical targeted post-change regression (SHA-256 `1bd8e626aa6138b002c018cdb7e04c86a0c09fc896f7cb2dd4bc08dbb3d86a7c`) | 57 tests passed with one artist-test skip. The current post-Trenton geometry/plot authority instead has a separately verified 10/10 artist run and 14 hash-matched PNGs. |
| `revised_openfoam_22p5mm_2026-08-25/restart_preflight_final_targeted_tests_2026-08-26.stdout.log` | Historical affected-test rebuild/run after report-scan optimization (SHA-256 `55ab37250d52ff4ed27ec32eba4505856218674bb7f4a78c09b63e0099f87db8`) | `openfoam_export_test` and `model_config_test` both passed for that snapshot; this supplements its historical harness and does not replace the current runner-state-gates regression |
| `revised_openfoam_22p5mm_2026-08-25/ACTUAL_CASE_AUDITS_1P60.md` | Read-only heat-source, thermal-mass, material-fidelity, connectivity, component-temperature, pressure-jump applicability, and y+ audit | Bookkeeping/connectivity pass; exposes material homogenization and wall-model limitations, preserves unsupported/stale-report failures, and verifies 82 audited case inputs were unchanged; 22-file evidence manifest SHA-256 `b0084ef1db479b49c684704f5783746aa09b080cb0fa06af4e9b91c51a6f373f` |
| `baselines/pre_updated_geometry_2026-08-24/` | Frozen pre-update comparison | Historical baseline only |

The live OpenFOAM case is outside this repository at:

`C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_cases\new_model_updated_openfoam_export_test`

Controlled benchmark cases are outside the repository under:

`C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_benchmark_snapshots`

Archived historical checkpoints are stored as verified GitHub Release assets
in the private repository
`https://github.com/hughes6/thermal-sim-openfoam-archive`, release
`openfoam-checkpoints-2026-08-25`. Remote byte counts, SHA-256 values, and the
manifest were verified before the 28 exact authorized local sources were
removed; the completion ledger is
`revised_openfoam_22p5mm_2026-08-25/OPENFOAM_ARCHIVE_COMPLETION_2026-08-26.md`.

## Reproducible local checks

Run from the project root in PowerShell. The exporter invocation is deliberately
blocked on a newly built, hashed, and regression-pinned current-source binary;
do not substitute the historical workspace `model.exe`:

```powershell
& '[CURRENT_EXPORTER_EXE]' --geometry-only library\models\new_model_updated.toml
python -m unittest tests.updated_lab_model_contract_test -v
python tools\audit_component_geometry.py library\models\new_model_updated.toml
python tools\openfoam_fan_operating_domain_audit.py `
  "C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_cases\new_model_updated_openfoam_export_test" `
  --density 0.9833
```

The standalone native template readiness gate is now functionally green. Build
the exact optimized campaign locally with:

```powershell
g++ -std=c++17 -O2 -I src `
  tests\native_template_microcase_test.cpp `
  -o $env:TEMP\native_template_microcase_test.exe
& $env:TEMP\native_template_microcase_test.exe
```

The current expected result is process exit 0 with 11 canonical functional
PASS lines plus one `EXPECTED REJECTION` for the inactive provisional NI
minimum-separator variant. The final summary must report `11 functional passes,
1 expected geometry rejections, 12 selected templates`. The expected rejection
must identify `Card Slot wall 1` changing from `0.000192` to `0.000174 m^3`;
silently passing that sensitivity would require review. The Dell result must
retain approximately 0.912% bootstrap curve overrun and the Thruster result
approximately 30.77%; those are source-data fidelity warnings, not reasons to
convert a numerically bounded canonical screen into a failure. Trenton and
Cisco pass. Any different matrix or materially different fidelity metrics are
a regression or a changed input set and require new preserved evidence.

The artist-level plotting test requires Matplotlib. Run the complete plotting
suite with an environment that provides Matplotlib and require zero skips:

```powershell
python -m unittest tests.plot_geometry_test -v
```

The current post-N5766/post-Trenton geometry has already been rendered with an
isolated Matplotlib/NumPy/pandas runtime. Its 14-output manifest is
`updated_component_plots_current_2026-08-26/PLOT_MANIFEST.json`; all output,
geometry, and plotter hashes verify, and the artist suite passed 10/10. To
regenerate it after a future input change, use a newly named output directory
so this evidence remains immutable:

```powershell
python tools\render_component_plots.py output.txt `
  --output-dir validation\updated_component_plots_REGENERATED_DATE
```

The initial provisional-NI render failure from a Python environment without
`mpl_toolkits` remains preserved. Plotting was subsequently recovered with the
already-installed ANSYS Student Python 3.12.11 runtime (Matplotlib 3.10.0): the
full plotting suite passed 10/10, and the actual NI export contains exactly two
Air artists with identical `tab:cyan` fill and edge colors. The component and
rack PNGs plus their verified manifest are under
`ni_minimum_separator_sensitivity_2026-08-26/plots/`. This validates rendering,
not the provisional separator's stamped topology or physical accuracy.

The renderer preserves `geometry_input.txt` and writes `PLOT_MANIFEST.json`.
A future plot set becomes current only when its manifest geometry hash equals
the newly exported `output.txt`, every output hash verifies, and the artist
tests pass without skips.

For the active OpenFOAM case, use the case-specific command block and exact
times recorded in `revised_openfoam_22p5mm_2026-08-25/RUN_STATUS.md`. At each
retained and audited restart checkpoint the minimum evidence is:

1. complete and aligned reconstructed plus processor fields;
2. no fatal solver signature and bounded Courant/continuity diagnostics;
3. exterior boundary mass balance and all fan operating points;
4. short- and long-window volume-weighted field convergence;
5. cumulative air-exchange accounting;
6. heat-source and material/thermal-mass audits; and
7. preserved command, stdout log, generated reports, and input/configuration
   hashes.

## Known blockers to an industry-ready claim

- The canonical NI chassis does not yet include the measured rear and side
  separator enclosure, so its two nominal air regions can mix unphysically.
  One inactive sensitivity file adds only a 5 x 155 x 160 mm mirrored wall;
  it leaves 5 mm lower and 2.2 mm upper bypasses and does not resolve sheet
  gauge, wall terminations, rear surfaces, or vent/plenum communication. Its
  19 mm native screen correctly rejects an existing wall-volume change before
  flow, so no separator sensitivity result exists yet.
- Rail-2 depths, the MAU obstruction, DIN rail, and several heat loads remain
  approximate or absent.
- The supplied but unplaced Dell R360 template has a 1U-correct 43 mm height,
  but its retained 817 mm depth conflicts with Dell's published 563.3 mm
  chassis depth and requires internal-layout reconciliation before use.
- The 3U shelf is represented as a full aluminum block rather than verified
  sheet/hollow construction.
- Three coarse-mesh snap contacts create unintended conductive paths.
- Nine heterogeneous component instances are homogenized into their outer
  material, biasing mass, heat capacity, conductivity, and hotspot prediction.
- Fan curves and loads lack measured operating-point/uncertainty calibration.
- The current exact O2 isolated-template campaign passes all 11 canonical
  templates functionally, including Dell, Thruster, Cisco, and Trenton, while
  the twelfth deliberately inactive NI separator sensitivity remains one
  expected geometry rejection. This does not
  erase source-data discrepancies: Dell's supplied 32.9 CFM is approximately
  0.912% above its curve zero of about 32.6027 CFM, and the Thruster's supplied
  18 CFM is approximately 30.77% above its curve zero of about 13.7644 CFM.
  Treat those nominal flows/curves as provisional until reconciled with
  measurements; the latest campaign reports the overruns separately from
  bounded solver functionality.
- The Trenton front intake and rear opening no longer overlap the full-depth
  `PS Wall`: the intake begins at `x=415 mm`, and the rear opening is split into
  main-cavity and power-supply-cavity pieces around the wall. This is a
  reasoned provisional topology repair pending the exact as-built drawing, not
  dimensional validation.
- The reusable-component audit currently reports 0 errors and one expected NI
  air/air warning across all 11 templates. Its guards cover solid/solid volume
  overlap, positive-area coplanar boundary-surface overlap, and vent overlap
  with a solid that spans the complete opening-normal depth. Passing that
  static audit does not prove manufactured clearances or resolved mesh paths.
- The N5766A front intake correction to `z=21.8 mm`, `height=33.6 mm` is present
  in current native inputs but absent from the retained 1.600 s OpenFOAM case
  and 2026-08-25 plots. Those artifacts remain valid only for their exact old
  lineage. The corrected plot directory now exists; a newly exported,
  uniquely named corrected OpenFOAM case remains outstanding.
- The passing N5766A, N6701C, and NI isolated cases peak at 7.65, 7.00, and
  22.72 m/s. NI uses 31 nonlinear iterations and places 42 fan interfaces at
  the physical lower `Q=0` bound. Exact continuity is a numerical pass, but the
  NI speed and stalled-interface pattern require measured flow/open-area review
  and are not a physical-realism or broad convergence-margin pass.
- Ten of 47 fans at 1.600 s are at or beyond the first zero-pressure point of
  their exported provisional curves; direction and global balance therefore
  do not validate device-level flow realism.
- The 22.5 mm mesh has 39,878 cells below determinant 0.001: 5,082 in the
  fluid and 34,796 across 12 solids. Only the shelf solid passes the
  determinant check.
- Future default, in-depth, and validation exports now fail before solver
  launch on any determinant failure. Only the explicitly exploratory screening
  profile may continue, and only when failed-check and determinant-diagnostic
  counts agree exactly and no other `checkMesh` diagnostic is present. The
  preserved active case was not regenerated or modified.
- The 19 mm case exceeded available WSL memory, so mesh independence is not
  established.
- Resource-safe full-rack native meshes collapse thin internal topology before
  flow; isolated component screens therefore do not establish native full-rack
  interaction or agreement with OpenFOAM.
- The retained pre-correction OpenFOAM airflow is globally balanced but not
  spatially converged, and no accepted current-geometry thermal soak or
  experimental flow/temperature comparison exists yet.
- The 24 s thermal-only cap is a rapid-screening setting only for the retained
  exact pre-correction 22.5 mm case after its airflow-freeze gates pass. The
  canonical 19 mm model still inherits 20 s. The larger cap does not accelerate
  the present initial-airflow continuation, whose 0.0005 s cap remains
  unchanged; later screening refreshes have a separate 0.001 s cap. It is not a
  validation-grade timestep setting.
- Reducing thermal-only outer energy-coupling passes from three to two is also
  only a screening candidate. Live-airflow stages remain at three passes, and
  the candidate requires the matched corrected-full-rack early-heating and
  near-steady A/B, field, energy, and restore-to-live gates in
  `revised_openfoam_22p5mm_2026-08-25/RUNTIME_ACCELERATION_POLICY_2026-08-26.md`
  before it can support a quantitative result.
- The final idle resource gate found no competing Fluent, OpenFOAM, or MPI
  process and passed disk at 17,129,152,512 free bytes versus its
  10,737,418,240-byte floor. Its first memory sample still failed at
  951,701,504 available bytes versus the 5,368,709,120-byte floor. It exited 21;
  WSL was requested but not queried after the host failure, and no corrected
  export, mesh, build, or solve began. The immutable 3,494-byte JSON has SHA-256
  `33c4fb9525398af3c7ec53156fe214a422b3d0e67ca13e144d0c5fcbe5ca6d51`.
- Isothermal airflow is not an enabled shortcut. The reviewed custom-solver
  source rejects isothermal, dual thermal/isothermal, mixed-fluid, and runtime
  mode changes before physical-time advancement. A no-case runtime-attestation
  protocol, staged clean-build/rollback wrapper, negative copied microcase, and
  generated-runner provenance tests now pass without WSL. The installed
  executable is still definitively stale: SHA-256
  `4dad80e5f4c3b4a9a37599d06291dd8c93cdf9a49053c9a2582f90cd5a5c2e6d`,
  old isothermal-mode strings present, hardened marker absent. New generated
  runners reject missing or stale binaries with exit 14 before case locking or
  writes. The hardened source remains uncompiled and unrun in OpenFOAM; the
  non-WSL fake-binary tests do not authorize a runtime or physics claim.

These limitations must remain visible in derived plots and reports. Passing
software regressions or a short stable CFD checkpoint cannot supersede them.
