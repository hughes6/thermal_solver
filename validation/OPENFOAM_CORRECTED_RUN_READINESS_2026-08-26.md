# Corrected OpenFOAM run readiness audit

Date: 2026-08-26 (America/Denver)

> **Current authority update (2026-08-27).** The detailed samples below preserve
> the original read-only audit. Every `model.exe`, O2 executable, and “current”
> or “production” label in that record is pinned historical evidence. There is
> **no production-qualified exporter binary for current source**. Before any
> export, rebuild the current exporter source, record its source,
> compiler/build command, byte count, and SHA-256, and designate that exact
> executable as `[CURRENT_EXPORTER_EXE]`.
>
> The final current-source software regression is
> `revised_native_regression_2026-08-26/added_feature_regression_runner_state_gates_2026-08-27.run.json`
> (SHA-256
> `246620007dff4b6f7dea50b22bcc9bc987e359aa575310134a9930da672a6c8c`):
> PASS, exit 0, 516.584 s, affinity mask 3 (two logical processors), BelowNormal
> priority. Its stdout SHA-256 is
> `03d781c9abe21ec3722e038672ef4c6587bed734cc896d97ddde3ed17a7bd81c`
> and stderr SHA-256 is
> `f4dba93c473013728126639c6b07f4c86196eeba63b61600c3abab37dc051d23`.
> This proves source-level software behavior only. The final idle gate,
> `revised_native_regression_2026-08-26/openfoam_resource_gate_final_idle_20260827T095617107Z.json`
> (SHA-256
> `33c4fb9525398af3c7ec53156fe214a422b3d0e67ca13e144d0c5fcbe5ca6d51`),
> failed closed with exit 21: no competitors and 17,129,152,512 free disk bytes
> passed, but 951,701,504 available-memory bytes failed the
> 5,368,709,120-byte floor. WSL was not queried after the host failure. The
> release gate also remains an expected FAIL with 0 of 9 assumptions verified;
> no corrected export, OpenFOAM build, preparation, or solve has started.

## Outcome

A corrected OpenFOAM screening run is **not safe to launch under the current
host-memory conditions**. No OpenFOAM, MPI, Fluent, or ANSYS solver process was
running during this audit, and instantaneous CPU pressure was low, but Windows
available physical memory remained only about 0.50--1.12 GiB and paging was
active. This fails the campaign's manual requirement of at least 5 GiB
available continuously for 60 seconds before either export/preparation or a
four-rank solve.

After Fluent was paused and cached memory was cleared, a later three-sample
check improved only to 1,809/1,847/1,845 MiB available with 0.0/31.6/70.5
pages input per second. It still fails the same launch gate, so no corrected
full-rack OpenFOAM branch was started. A final 20:26 MDT check found
1,433/1,410/1,435 MiB available with 16.9/120.8/55.9 pages input per second,
again confirming that the host was below the resource-valid launch floor.

The existing case was not modified and must not be continued as if it were the
corrected geometry. It is an intact historical, pre-N5766-intake-correction
lineage. The safest next executable action, after the resource and input gates
below pass, is a new uniquely named export; it is not an MPI solve.

## Audited corrected inputs and pinned historical artifacts

- Canonical model `library/models/new_model_updated.toml`: SHA-256
  `a6448cc3a6ee00fdc72a3f6d49a27b7aeedb89a66e71b06e525f7190a335a05a`.
- OpenFOAM export fixture
  `library/models/new_model_updated_openfoam_export_test.toml`: SHA-256
  `14ed5a43d9b2151557be9a522dc0e54f2df93d69ec444eb9e9e492598588e3ea`.
  Its reusable thermal-only cap is 20 s; the 24 s value belongs only to a
  rejected strict-temperature-gate experiment on the exact old case.
- Corrected N5766A component: SHA-256
  `17ab6fa85ca1619b555ed55b824a9dc242bc4af8ec572350e2a174d50dcbaa1d`.
- Pinned 2026-08-26 post-N5766 and post-Trenton-repair geometry-only evidence:
  39,808 bytes, SHA-256
  `dbcaaa237fb8bf59c4841cf4e3a8b0a226d28208d23560c72900ce113d2ff2ea`.
  It exactly matches the geometry retained with the 14-image 2026-08-26 plot set
  under `updated_component_plots_current_2026-08-26/`.
- A fresh read-only geometry audit of both the canonical model and the
  OpenFOAM fixture reported 11 reusable components, zero errors, and one known
  warning: the unfinished NI `Interior air` and `Card Slot air` volumes overlap
  because their separator walls have not been modeled. The audit now rejects
  solid/solid interpenetration, positive-area overlap between coplanar boundary
  surfaces, and vents that overlap a solid spanning the full opening-normal
  depth; edge contact remains valid.
- The pinned historical exact O2 isolated-template campaign passed all 11
  canonical templates through functional topology, flow, continuity,
  fan-domain, and two-step thermal robustness gates. Its twelfth selected input,
  the inactive provisional NI separator sensitivity, is one expected pre-flow geometry
  rejection. Its 59,774-byte stdout has SHA-256
  `895430bcbb8750012679daea582cf71fd9d7b42d9ca5640fd4adbf7487e60bf0`;
  the 1,108,407-byte exact O2 executable has SHA-256
  `133355e650d7c14ff0526997eb22707a4053c59dd07a8b4d301691ec2867ebd2`.
  These are short robustness screens, not a thermal soak or full-rack
  validation, and the rejected NI variant produced no flow field.
- The pinned historical production `model.exe` is 1,838,877 bytes with SHA-256
  `335efd17d7f000e90d13b4a2594cfade4ab68bde524b2910f81c73df7b00eb12`.
  Its geometry-only output matches the pinned geometry hash above, but this
  binary is not qualified to export from current source.

No corrected OpenFOAM case exists yet at the audited next target
`C:/OpenFOAM/thermal_sim_newmodel/new_model_updated_corrected_20260827`.

## Retained historical case lineage

The retained case is
`C:/Users/hconn/.codex/visualizations/2026/08/04/019fcccd-4536-7b51-a70c-8023779a1618/openfoam_cases/new_model_updated_openfoam_export_test`.

- Its `geometry.txt` SHA-256 is
  `5a1893f92d162cfe17d66240d34770351302a2eb961a332be9b2092d1a9981f1`.
- Its N5766A provenance snapshot SHA-256 is
  `ada3afb4a9b53b86fb8a8b8932127b190475a359c46010550ebe0522876f5610`.
- Both differ from the audited corrected geometry evidence, proving that the
  solved fields are pre-correction rather than merely undocumented newer fields.
- Root and `processor0`--`processor3` each contain only numeric times `0`,
  `1.5`, and `1.6000000000000001`.
- At `1.6000000000000001`, root and every processor contain 74/74 nonempty
  fields with identical relative field sets.
- The case consumes 2,177,286,050 logical bytes (2.027756 GiB); WSL `du`
  reports 2.1G.
- The text PID `682` remains in `.thermal_solver_run.lock`, but no such process
  exists. `flock` ownership, rather than the stale text, is authoritative; no
  live solver was found.

The screening profile has `overwrite = true`. Therefore **do not run** the
export fixture without a unique `--case-name`; doing so would target and
overwrite this preserved historical case.

## Resource snapshot

Windows observations:

- Total physical memory: 7.7256 GiB.
- Available memory was initially 0.4989 GiB, then ranged from about
  0.88--1.01 GiB across six two-second samples, and was 1.1245 GiB on the final
  refresh.
- Five one-second performance-counter samples reported 1,364.91--8,028.63
  pages/s and 9.97% paging-file usage.
- The top working sets were ChatGPT (~1.00 GiB), Codex (~0.45--0.74 GiB),
  Microsoft Defender (~0.43 GiB), and Explorer (~0.13--0.19 GiB). No user or
  system process was terminated.
- A three-second CPU-delta sample found only about 0.04 core in the auditing
  PowerShell, 0.02 core in Codex, and 0.01 core in Explorer. CPU/core contention
  was not the immediate blocker.

WSL observations:

- WSL reports 3,959,025,664 bytes (3.687130 GiB) total and 3,422,990,336 bytes
  (3.187908 GiB) available, with a 1 GiB unused swap allocation.
- Docker/containerd were the largest WSL residents; no `foam`, `mpirun`,
  Fluent, or ANSYS process was present.
- No user `.wslconfig` exists, so the approximately half-host-memory WSL cap is
  the platform default rather than an explicit project setting.

Disk observations:

- `/mnt/c` had 11,734,511,616 bytes (10.928616 GiB) free, although the volume
  was 98% used.
- Applying the then-reviewed exporter's checkpoint-space formula to the
  retained 1.600 s checkpoint gives `checkpoint_kb=256164`,
  `required_kb=1036616`, `available_kb=11565360`, and a positive margin of
  `10528744 KiB`.
- Disk is not the immediate blocker for one similarly sized screening case,
  but the new prepared case must run its own generated guard because its true
  checkpoint size is not known before export and region preparation.

## Launcher and preflight status

- The retained installed launcher SHA-256 is
  `7d41624a89720edbc42fa52eab0a22495f90d58654b84c68ad9a61e96c7306ac`;
  `bash -n` passes.
- WSL provides `flock`, `awk`, `find`, `sha256sum`, `openfoam2606`,
  `chtMultiRegionFoam`, `splitMeshRegions`, `checkMesh`, `decomposePar`, and
  `mpirun`.
- The retained launcher has the hardened warm-start lineage guard, but it
  predates the later checkpoint disk guard. A new case may claim that guard only
  when a freshly rebuilt `[CURRENT_EXPORTER_EXE]` emits it and the generated
  runner is inspected and hash-recorded.
- `tools/openfoam_resource_gate.ps1` now automates the external launch
  precondition. It fails closed on a competing CFD/MPI process, any host-memory
  sample below the configured threshold, insufficient destination-disk space,
  or a provider/query error. Its initial host process/disk/memory sequence does
  not invoke or wake WSL. `-QueryWsl` adds WSL memory, disk, and process checks
  only after the complete host gate has passed, then immediately rechecks host
  memory, disk, and processes because starting WSL can consume the margin that
  just passed. Direct execution cannot lower the campaign floors below 5 GiB
  host memory, 60 seconds of sampling at intervals no longer than one second,
  or 10 GiB host disk; the optional WSL check likewise cannot lower its 5-GiB
  memory and 10-GiB disk floors. `-MaximumPagesInputPerSecond` can add a
  campaign-approved paging ceiling; it is disabled by default because no
  universal ceiling has been promoted. Passing launcher syntax or a
  point-in-time WSL `free` observation does not supersede this gate.
- Runtime policy and measured tradeoffs are recorded in
  `revised_openfoam_22p5mm_2026-08-25/RUNTIME_ACCELERATION_POLICY_2026-08-26.md`.
  Live airflow remains at three outer PIMPLE passes and a 0.0005 s maximum
  timestep. Two thermal-only outer energy-coupling passes are implemented only
  as a frozen-flow screening candidate; a corrected full-rack 3-versus-2 A/B
  and its temperature/energy gates are still required before promotion.

## Physics and release blockers

These do not prevent an explicitly provisional exploratory export, but they do
prevent treating the result as corrected quantitative validation or
industry-ready evidence:

1. The pinned historical exact O2 native template campaign passed all 11
   canonical reusable components functionally; its additional provisional NI separator
   sensitivity is an expected geometry rejection. The former Dell, Thruster,
   Cisco, and Trenton failures are
   superseded historical results: Trenton's front intake was moved so its left
   edge meets the `PS Wall` boundary, its rear opening was split around that
   wall, and Cisco's nominal flow was canonicalized. Input fidelity remains
   separate from functional solver behavior: Dell's supplied 32.9 CFM is
   approximately 0.912% above its curve's first zero-pressure flow, while the
   Thruster's supplied 18 CFM is approximately 30.77% above its curve zero of
   about 13.7644 CFM. Those curves/nominal values require measured
   reconciliation; the green microcase does not validate them as physical fan
   operating points.
2. The canonical NI chassis still lacks its measured rear/side separator
   enclosure. Its canonical isolated numerical pass reached 22.7193 m/s and 42
   stalled fan interfaces, which is a physical-realism warning rather than
   validation. The one-wall provisional sensitivity is inactive and fails
   exact geometry stamping before flow; it has no sensitivity field result.
3. Rail-2 depths, heat loads, and several fan curves remain estimates.
4. The prior 22.5 mm screening mesh had 39,878 cells below the determinant
   threshold, including 5,082 fluid cells. The screening profile deliberately
   permits determinant-only warnings, but a new case must rerun `checkMesh` and
   cannot use such a warning as an industry-readiness pass.
5. The exporter still homogenizes heterogeneous component solids, adding an
   estimated 8.9905 kg and 14,811 J/K versus the defined internal materials.
6. The retained 1.600 s flow is an early transient, not a converged airflow or
   thermal-soak state, and cannot validate the corrected case.

## Safest next executable sequence

Do not run a heavy command now. The corrected isolated-template functional gate
is green, but preserve the Dell/Thruster source-data warnings and the unfinished
NI geometry as explicit limitations. For an exploratory run that intentionally
accepts those and the remaining measured-geometry limitations, wait until
Windows reports at least 5 GiB available continuously for 60 seconds with low
paging.

For an industry/quantitative run, first require
`tools/model_release_readiness.py` to pass against
`validation/UPDATED_MODEL_ASSUMPTIONS.toml`; its current create-once evidence
fails with all nine rows open. An explicitly provisional exploratory branch may
continue only if that FAIL state is carried into every report and no result is
described as as-built or validated.

Run the lightweight external gate immediately before export/preparation and
again before the four-rank solve. Each invocation needs a new stage-specific
evidence filename: existing evidence targets are refused rather than overwritten.
The production command enforces the standing 5-GiB/60-second/10-GiB floors;
the example applies the same memory and disk floors inside WSL:

```powershell
$gateRunId = [DateTimeOffset]::UtcNow.ToString("yyyyMMddTHHmmssfffZ")
$gateEvidence = ".\validation\openfoam_gate_pre_export_$gateRunId.json"
pwsh -NoProfile -File .\tools\openfoam_resource_gate.ps1 `
  -DiskPath C:\OpenFOAM `
  -QueryWsl `
  -WslDistribution Ubuntu `
  -WslDiskPath /home/hconner158 `
  -EvidencePath $gateEvidence
if ($LASTEXITCODE -ne 0) { throw "OpenFOAM resource gate failed" }
```

For a successfully parameter-bound invocation, standard output is one
machine-readable JSON document; PowerShell-native parse or parameter-binding
errors can occur before the script runs and therefore are not JSON. The optional
file contains the same result and a unique `evidence_id`. Treat a gate as current
only when that invocation exits zero and its expected new file and
`evidence_id` match; never infer success from an older artifact. A failure exits
nonzero. If the host gate fails, the evidence records `wsl.queried=false` and
WSL is never launched. Use a different `pre_solve` filename for the second gate.

Then, from the project root, assert the audited canonical target is absent.
First perform a clean exporter rebuild from current source and record the source
hashes, compiler/build command, executable byte count, and executable SHA-256.
Only after that record designates the resulting path as
`[CURRENT_EXPORTER_EXE]` may it export the corrected 19-mm canonical model
without starting MPI:

```powershell
$casePath = 'C:\OpenFOAM\thermal_sim_newmodel\new_model_updated_corrected_20260827'
if (Test-Path -LiteralPath $casePath) { throw "Refusing existing case: $casePath" }
$exporter = '[CURRENT_EXPORTER_EXE]'
if ($exporter -eq '[CURRENT_EXPORTER_EXE]') { throw 'Set the freshly rebuilt, hashed exporter path' }
& $exporter --case-name new_model_updated_corrected_20260827 library/models/new_model_updated.toml library/fan_curves/fan_curves.toml
```

The older `new_model_updated_openfoam_export_test.toml` profile is the retained
22.5-mm export fixture, not the corrected 19-mm canonical run plan.

Before any preparation or solve, verify that:

1. the new directory did not previously exist;
2. its provenance N5766A hash is
   `17ab6fa85ca1619b555ed55b824a9dc242bc4af8ec572350e2a174d50dcbaa1d`;
3. its geometry is the corrected geometry rather than hash
   `5a1893f92d162cfe17d66240d34770351302a2eb961a332be9b2092d1a9981f1`;
4. `run_parallel.sh` passes `bash -n` and contains
   `preflight_checkpoint_space`, the exact runtime-attestation handshake, and
   the fixed-endpoint guard; and
5. the host memory gate still passes immediately before region preparation
   and again before the four-rank solver.

Region preparation must use the generated case-local
`prepare_regions_low_memory.sh`. Its **measured selector-equivalence proof**
belongs to the retained 22.5 mm, 1,033,200-cell case: 109/109 selectors and
43,603 selected cells matched without mutation. Separately, the failed 19 mm
inventory gives only the theoretical scalar payload
`109 * 2,800,980 * 8 = 2,442,454,560 bytes = 2.274713069 GiB`.
That is not a measured RSS saving and does not prove the remaining 19 mm split
fits. The generated serial launcher and explicit conventional parallel mode are
disabled for multirate cases. A clean custom-solver build and real runtime
attestation must pass before case locking; the installed historical binary is
not qualified.

Region preparation and `checkMesh` are the next separate gate. Start the
four-rank `0.0005 s` live-airflow stage only after the new mesh, determinant,
connectivity, heat-source, fan-domain, disk, and memory checks have been
captured. Do not map or continue the historical 1.600 s fields into the
corrected case unless a separately documented mapping study proves the
topology and field transfer are appropriate.

Do not use the thermal-only two-pass candidate during this unfinished live
airflow phase. It applies only after the documented airflow-freeze gates pass;
live stages must restore three outer passes. Its control branch is
`THERMAL_ONLY_OUTER_CORRECTORS=3`, and promotion requires the matched
early-heating and near-steady comparisons defined in the runtime policy.

## Commands actually run in this audit

Only read-only commands were used against the active case: process/memory/disk
queries, file hashes and inventories, the Python static geometry audit,
`bash -n`, OpenFOAM tool discovery, WSL `free`/`df`/`du`/`ps`, and field-set
comparisons. No exporter, region preparation, decomposition, MPI solver,
post-processor, or active-case cleanup command was run.
