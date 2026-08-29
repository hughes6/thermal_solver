# Workload, memory, output, and runtime hardening — 2026-08-26

> **Current authority update (2026-08-27).** Every `model.exe`, O2 executable,
> and “authoritative” or “current-source” label below is pinned to its stated
> historical source snapshot. The repository now has **no production-qualified
> exporter binary for current source**. Before any export, rebuild the current
> exporter source, record the source, compiler/build command, byte count, and
> SHA-256, and designate that exact executable as `[CURRENT_EXPORTER_EXE]`.
>
> The final current-source software regression is
> `added_feature_regression_runner_state_gates_2026-08-27.run.json`
> (SHA-256
> `246620007dff4b6f7dea50b22bcc9bc987e359aa575310134a9930da672a6c8c`):
> PASS, exit 0, 516.584 s, affinity mask 3 (two logical processors), BelowNormal
> priority. Its stdout SHA-256 is
> `03d781c9abe21ec3722e038672ef4c6587bed734cc896d97ddde3ed17a7bd81c`;
> stderr SHA-256 is
> `f4dba93c473013728126639c6b07f4c86196eeba63b61600c3abab37dc051d23`.
> This is source-level software evidence, not an exporter-binary, OpenFOAM, or
> physics result. The final idle resource record,
> `openfoam_resource_gate_final_idle_20260827T095617107Z.json` (SHA-256
> `33c4fb9525398af3c7ec53156fe214a422b3d0e67ca13e144d0c5fcbe5ca6d51`),
> failed closed with exit 21: no competing solver was found and
> 17,129,152,512 free disk bytes passed, but 951,701,504 available-memory bytes
> failed the 5,368,709,120-byte floor. WSL was requested but not queried because
> the host gate failed.

## Outcome and claim boundary

The requested runtime acceleration is implemented without relaxing the current
live-airflow stability/equivalence policy. Screening now has explicit,
stage-specific controls: 0.0005 s for the fan ramp and initial airflow, 0.001 s
for later airflow refreshes, and 20 s for frozen-flow thermal advancement after
all freeze gates pass. The exact retained pre-correction 22.5 mm case may use
24 s for exploratory screening only. The current 1.600 s field is not
freeze-eligible, so neither 20 nor 24 s can accelerate its present continuation.

The native path was also hardened to reject unaffordable work and unsafe output
mutations before large allocation or thermal advancement. These are software
robustness and workload results. No new full-rack native flow/temperature field
or OpenFOAM checkpoint was produced, and nothing here is an industry-validation
sign-off.

## Promoted and rejected runtime controls

| Control | Evidence | Decision |
|---|---|---|
| Coupled 0.00075 s versus 0.0005 s | 30% fewer steps, but 9.79% slower wall time; 1.155754% velocity RMS difference; 11.946965% gauge-adjusted `p_rgh` RMS difference; 10/33 fan operating-point checks failed, 14.676% worst | Reject 0.00075 s. Retain 0.0005 s for screening ramp/initial airflow. |
| Later screening refresh cap | Separate runner/configuration path from the initial stage | Retain 0.001 s. Do not describe this as enlarging the unfinished initial-airflow continuation. |
| Frozen 24 s versus 20 s on the exact retained 22.5 mm case | 22.190252% cumulative and 13.725195% sustained wall-time reduction; 0.006781254 K domain RMS, 0.123260498 K maximum-cell, and 0.054939449 K maximum component-average difference | Allow 24 s only for that exploratory exact case. It fails the strict 0.05 K cell and 0.02 K component-average gates. Keep reusable/current canonical screening at 20 s and confirmation at no more than 20 s. |
| Frozen 30, 40, or 60 s on the exact retained 22.5 mm campaign | Matched screening failures for that exact case | Reject for that campaign; the separately matched in-depth profile is outside this experiment. |
| Live PIMPLE 2 outer × 2 pressure versus 3 × 2 | 27.25% faster after the first step, but field, maximum-temperature, and 3/33 fan-flow equivalence gates failed | Retain live 3 × 2. |
| Frozen-flow 2 outer energy passes | Source/runner guards pass; no corrected-full-rack three-versus-two A/B exists | Screening candidate only. Validation/default/in-depth inherit live count until matched evidence exists. |

The screening profile therefore contains:

```toml
airflow_maximum_time_step = 0.0005
airflow_refresh_maximum_time_step = 0.001
thermal_only_maximum_time_step = 20.0
pimple_outer_correctors = 3
pimple_pressure_correctors = 2
thermal_only_pimple_outer_correctors = 2
```

Default and in-depth cap initial and refresh live flow at 0.001 s. Validation
uses 0.001 s initially and its independently matched 0.005 s refresh cap. The
exact retained 22.5 mm case launcher and benchmark evidence preserve the 24 s
experiment. The reusable export fixture has been reset to 20 s so a new or
corrected export cannot accidentally promote it; the reusable screening
profile remains 20 s as well.

## Why the larger thermal step is not active yet

The retained 1.600 s case has approximately 0.875 nominal air replacements.
Whole-fluid velocity changed by 8.8677% RMS over the latest accepted comparison,
above the 3% freeze gate. Pressure settling and global mass balance do not
override the velocity/air-exchange requirements. Entering frozen-flow mode now
would make a fast thermal result from an unsettled velocity field.

## Native fail-closed workload result

The pinned historical post-audit production runner was invoked with `--native`
against the canonical model, with OpenMP limited to two threads, below-normal
process priority, and
processor-affinity mask 3. It produced the expected exit 1 in 0.109 s with:

```text
fine cells=2847663, minimum fine visits=1708597800
coarse cells=216580, minimum coarse visits=129948000
minimum total visits=1838545800
```

The configured `simulation.max_updates` is 30,000,000. The minimum counts one
non-advection and one mandatory advection pass for each of 300 coarse and 300
fine steps; solved-flow CFL can only increase the work. Rejection occurred
before mesh allocation or solver construction. Consequently, it is incorrect
to interpret this preflight as a full-rack native flow, convergence, or
temperature result.

The post-audit rejection preserved all four root sentinels. The then-current
`.thermal_sim_last_run.json` is 609 bytes with SHA-256
`39ae95c8825c855dd03bbd233747cb95000bb49c888ca918c185b2f655ccd1e7`.
The authoritative stdout is 210 bytes with SHA-256
`2df73fed20722b742083324b84615c8d7871dde489c0e9a82630b3bd6c05f512`;
the 274-byte stderr has SHA-256
`1b93670fc5ce981665811837fae75b9f9eb46ba33132b54182a738989b372433`.
No flow or temperature field was produced.

The earlier 0.288 s pre-post-audit rejection produced the same plan counts and
preserved this historical sentinel snapshot:

| Artifact | Bytes | SHA-256 before and after |
|---|---:|---|
| `.thermal_sim_last_run.json` | 694 | `1b4d23bfcdc53cc5df4970388ad5c1b7bf5af4a78e7d0d6d0d2cf52ff6d0eddd` |
| `coarse_simulation.csv` | 19,666,515 | `dc60aa171c3dd484a56cbf1dc6d259c947a4af0bfa52cad2615bc565557a7e77` |
| `output.txt` | 39,808 | `dbcaaa237fb8bf59c4841cf4e3a8b0a226d28208d23560c72900ce113d2ff2ea` |
| `simulation.csv` | 432 | `8dbd7e16c09ef84eecfa84b54fdae40dd94714024400f56bc272203d9ec04d03` |

Both preservation statements are scoped to their exact early preflights.
Geometry-only mode intentionally writes `output.txt`, and a later-stage failure
is not universally output-free.

## Implemented native guards

- Timestep counts, `output_interval`, cell products, `sizeof(Cell)` pair
  payload, bitmap bytes, and cell-visit multiplication/addition use checked
  arithmetic and reject before their large allocations. Direct Solver use also
  rejects non-finite `dt`, duration, and advection-CFL targets.
- Coarse and fine stages share one cumulative `max_updates` budget. The coarse
  stage reserves the fine minimum; actual coarse work is deducted before the
  fine stage.
- Once face flux is available, the solver checks the exact current advection
  substep count and repeats the check after each flow refresh.
- A released solver mesh is terminal. All mesh/flow-dependent public methods
  fail closed, and the second full `Cell` vector is delayed until scalar and
  base-workload validation has passed.
- `SimulationLogger::initialize()` is prepare-only. Legacy and structured
  field/summary/probe streams open lazily after flow, stability, exact-CFL, and
  workload preflight. Completion is recorded only after every stream passes a
  flush/health check. Duplicate names and non-finite/out-of-domain physical
  probe coordinates reject during preparation, before any stream opens. Probe
  names must also be portable filename stems, and case-insensitive filename
  collisions reject before stream creation.
- OpenFOAM preflight and geometry output now share the covered transaction
  boundary, preventing a failed preflight from being presented as a committed
  export.
- Grapher bitmap dimensions and bytes are bounded before allocation. Planner
  ceilings are clamped below signed `int` overflow. Coarse warm-start storage is
  released before the fine Grapher scope, and all three Grapher bitmaps are
  destroyed before the fine Solver creates its second full mesh.
- Diagnostics distinguish mandatory minimum visits from the exact solved
  face-flux workload and do not confuse cell-centered visualization velocity
  with the conservative face-flux basis.

## Pinned historical rebuild and geometry check

The pinned historical production command was:

```powershell
g++ -std=c++17 -O3 -DNDEBUG -fopenmp .\model_runner.cpp -o model.exe
```

The pinned historical post-audit `model.exe` is 1,838,877 bytes with SHA-256
`335efd17d7f000e90d13b4a2594cfade4ab68bde524b2910f81c73df7b00eb12`.
The archived byte-identical artifact is
`model_post_audit_runtime_hardening_2026-08-26.exe`. The default geometry-only
path exited zero in 0.289 s. Its 272-byte stdout has SHA-256
`664849782a065fcdda6988f03a171126171a92e74dc81346c92281829b459454`,
stderr is empty, and it regenerated `output.txt` at 39,808 bytes with unchanged
SHA-256
`dbcaaa237fb8bf59c4841cf4e3a8b0a226d28208d23560c72900ce113d2ff2ea`.
Neither transient solver was invoked.

The earlier `model_final_runtime_hardening_2026-08-26.exe` remains historical at
1,830,967 bytes with SHA-256
`2febfda3b22fec8db347beabec1f1ff47af801819bd127b7d7ca093ad6952ca2`.
The following source identities belong only to that historical pre-post-audit
build:

| File | Bytes | SHA-256 |
|---|---:|---|
| `model_runner.cpp` | 9,363 | `c780d1987793b450f6a56364eb70fe20a56c6af77927987c85818cf7508ab406` |
| `src/input/model_loader.hpp` | 144,053 | `682c4ec39bce1eb1fa7f5781a583552984768b948295703a659233d4301e0162` |
| `src/mesh_refinement_planner.hpp` | 20,771 | `adf3ec453bcb3ed1d6e13584970b881aaaebee641c1c8dd524d13772be05577f` |
| `src/mesh.hpp` | 150,445 | `11da61bcbc90f31d7a98f88f064a73e53f4a0952f4b0e05de5ef4d66c6107b03` |
| `src/solver.hpp` | 62,895 | `17e5af76874f4c3d6750a02cc7f139555036aa89fa55410404ce63fb6409e6da` |
| `src/logger.hpp` | 24,459 | `5a206fe6f7219021a921f0726d0b576b58cbd33f58dc20984b8cdfb6f2ccbfa8` |
| `src/grapher.hpp` | 27,008 | `741b8e7ea9acad021dbab296299f0b0d2ee79f09df244e628467dd3854078e18` |
| `src/workload.hpp` | 2,434 | `a31dce8164c680a517e1fd644cea7b9741d350de5bd451fd326451238662a6f8` |

## Pinned historical post-audit verification evidence

| Evidence | Result | Bytes / lines | SHA-256 |
|---|---|---:|---|
| `added_feature_regression_post_audit_runtime_hardening_2026-08-26.stdout.log` | Complete harness exit 0 in 295.746 s wrapper time; final line `All added-feature tests passed.` | 332,552 / 4,112 | `36838d5811bd909116e74e9ea92c9843b1982c0d7a86f50929d2fea64480e517` |
| `added_feature_regression_post_audit_runtime_hardening_2026-08-26.stderr.log` | Only unittest summaries; all `OK`, with explicit skips | 2,585 / 88 non-empty | `1f21f65650e34384aa2720e4f2908322c134a49796d5ecc37e996adbf19d7a9e` |
| `native_template_microcase_post_audit_runtime_hardening_2026-08-26.stdout.log` | Exit 0 in 74.651 s wrapper time and 74.4218 s internal time; 11 functional passes, one expected provisional-NI rejection, 12 selected templates, 2 Meanwell placements, 2,315 W | 59,774 / 640 | `895430bcbb8750012679daea582cf71fd9d7b42d9ca5640fd4adbf7487e60bf0` |
| `native_template_microcase_post_audit_runtime_hardening_2026-08-26.stderr.log` | Empty | 0 / 0 | — |
| `native_template_microcase_post_audit_runtime_hardening_2026-08-26.exe` | Exact pinned historical O2 component-campaign executable | 1,108,407 | `133355e650d7c14ff0526997eb22707a4053c59dd07a8b4d301691ec2867ebd2` |
| `model_post_audit_runtime_hardening_2026-08-26.exe` | Archived byte-identical historical production executable | 1,838,877 | `335efd17d7f000e90d13b4a2594cfade4ab68bde524b2910f81c73df7b00eb12` |
| `geometry_only_post_audit_runtime_hardening_2026-08-26.stdout.log` | Exit 0 in 0.289 s; no transient solver | 272 / — | `664849782a065fcdda6988f03a171126171a92e74dc81346c92281829b459454` |
| `geometry_only_post_audit_runtime_hardening_2026-08-26.stderr.log` | Empty | 0 / 0 | — |
| `full_rack_native_preflight_post_audit_runtime_hardening_2026-08-26.stdout.log` | Expected exit-1 preflight in 0.109 s; unchanged exact plan counts, four sentinels preserved, no field produced | 210 / — | `2df73fed20722b742083324b84615c8d7871dde489c0e9a82630b3bd6c05f512` |
| `full_rack_native_preflight_post_audit_runtime_hardening_2026-08-26.stderr.log` | Expected workload rejection | 274 / — | `1b93670fc5ce981665811837fae75b9f9eb46ba33132b54182a738989b372433` |

### Historical pre-post-audit verification evidence

| Evidence | Historical result | Bytes / lines | SHA-256 |
|---|---|---:|---|
| `added_feature_regression_final_runtime_hardening_2026-08-26.stdout.log` | Harness exit 0; final line `All added-feature tests passed.` | 334,749 / 4,210 | `f1dc657613c9a038452d0d0c6afdc53ca28dce4682731f1982cb7c7c20fb7c17` |
| `native_template_microcase_final_runtime_hardening_2026-08-26.stdout.log` | Exit 0; 11 functional passes, one expected provisional-NI rejection, 12 selected templates, 2 Meanwell placements, 2,315 W, 72.6863 s | 59,774 / 640 | `3e2864b33c6f6d0b40937ade369f628ba925d51a7592063f39f0c6e9a641816a` |
| `native_template_microcase_final_runtime_hardening_2026-08-26.exe` | Exact historical O2 component-campaign executable | 1,108,407 | `77b843e03f837f1cfcd599ddb8c34a99d61981c358731585f57c98774d96e15f` |
| `geometry_only_final_runtime_hardening_2026-08-26.stdout.log` | Exit 0; no transient solver | 272 / — | `664849782a065fcdda6988f03a171126171a92e74dc81346c92281829b459454` |
| `full_rack_native_preflight_final_runtime_hardening_2026-08-26.stdout.log` | Historical exact plan counts | 210 / — | `2df73fed20722b742083324b84615c8d7871dde489c0e9a82630b3bd6c05f512` |
| `full_rack_native_preflight_final_runtime_hardening_2026-08-26.stderr.log` | Expected exit-1 workload rejection | 274 / — | `1b93670fc5ce981665811837fae75b9f9eb46ba33132b54182a738989b372433` |

These rows and the 1,830,967-byte historical production binary remain valid for
their pinned pre-post-audit source snapshot. The post-audit rows likewise remain
valid only for their pinned later snapshot; none represents current-source
exporter-binary identity.

The full harness explicitly skips optional NumPy/PyVista-dependent checks when
their interpreter dependencies are unavailable, and it skips real-WSL flock
lifetime integration when the service is unavailable. Those are not counted as
passes. Its paired stderr contains only successful unittest summaries with the
skips explicit. Focused advection, mesh-preflight, logger/timestep, and
model-config executables are included in the pinned post-audit replay.

The template campaign is intentionally narrow. Its canonical unfinished NI
case still reports 42 stalled curved-fan interfaces and 22.7193 m/s maximum
speed; the inactive minimum-separator sensitivity rejects because a requested
0.000192 m3 solid realizes as 0.000174 m3 after overlay/opening stamping. These
are release limitations, not acceptable physical predictions.

## 2026-08-27 resource-gate and thermal-cap continuation

The reusable 22.5 mm export fixture was reset from the rejected exact-case
24 s experiment to the accepted 20 s screening cap. The preserved historical
launcher and benchmark evidence still document 24 s, but new/corrected exports
cannot inherit it. Focused verification passed 26 Python policy/contract tests
and the compiled `model_config_test`.

The previously authoritative source-snapshot harness then passed in 303.075 s.
Its 4,113-line,
332,820-byte stdout ends with `All added-feature tests passed.` and has SHA-256
`1bd2cf90e5b414b992bfb7d949bc04e71371e0a5249c1b38d4bf65132dbab01b`.
The 2,585-byte stderr contains 88 non-empty unittest summary lines, ends in
`OK`, and has SHA-256
`d76ac635fbf0db9c53331abb56f7c12e0cad3fed44d7441a4fa1516bcd7a2287`.
Optional NumPy/PyVista comparisons and real-WSL flock integration remain
explicit skips, not passes. A preceding 0.437 s launcher-only quoting failure
was preserved separately; it did not start the harness and is superseded by
the quoted-path exit-zero run.

`tools/openfoam_resource_gate.ps1` now enforces non-reducible production floors
of 5 GiB host available memory for 60 seconds, one-second-or-finer sampling,
and 10 GiB host disk. Optional WSL validation occurs only after the complete
host gate, uses full command lines for CFD/MPI detection, and immediately
rechecks host resources after WSL starts. Evidence paths are create-once and
carry unique attempt IDs. Its synthetic/adversarial suite passed every host,
paging, disk, WSL, post-WSL invalidation, immutable-evidence, and production-
floor case with `realWslCalls=0`.

The real host-only pre-export gate wrote immutable 3,424-byte JSON evidence
with SHA-256
`0764fa1f14e38d525e4eeb01068bf020c8662a0e81a42cad7e0204ecb39533e0`.
No competing CFD/MPI process was found and C: passed with 17,417,773,056 free
bytes, but available physical memory was only 1,223,729,152 bytes versus the
5,368,709,120-byte floor. It failed with gate exit 21 at the first sample; WSL
was not requested or launched.

Separately, six verified inactive Visual Studio installer extraction trees
were deleted from the Windows temp directory, totaling 8,215,369,245 logical
bytes. C: free space rose to about 16,600.8 MiB. The active diagnostic trace
and every model, case, checkpoint, configuration, log, post-processing, and
validation artifact were preserved. See `HOST_RESOURCE_CLEANUP_2026-08-27.md`.

## Resource policy and remaining blockers

The low-memory selector evidence has two independent scopes that must not be
combined. The **measured 22.5 mm result** is logical equivalence on the retained
1,033,200-cell case: 109/109 selectors and 43,603 selected cells matched without
mutating the case. The **19 mm result** is only an inventory-derived scalar
payload:
`109 * 2,800,980 * 8 = 2,442,454,560 bytes = 2.274713069 GiB`.
It is not a measured RSS reduction and does not prove that the remaining
19 mm split fits memory.

All substantial Windows work in this continuation was serialized at
below-normal priority with two-core affinity because another solver workload
was in scope. OpenFOAM compilation, `wmake`, and solver execution were withheld:
the final idle resource record still failed the required continuous five-GiB
launch gate at 951,701,504 available bytes. The installed custom OpenFOAM solver
is stale, and no production-qualified exporter binary matches current source.
The generated persistent runner rejects the stale custom solver with exit 14
before lock or case writes; an export itself must wait for a freshly rebuilt and
hashed `[CURRENT_EXPORTER_EXE]`.

Industry readiness still requires measured/final Rail 2 and NI separator
geometry, measured fan curves and device power, calibrated material/thermal-mass
equivalents, a current-geometry mesh study, spatially converged airflow, a
thermally developed energy-balanced run, timestep independence, and comparison
against laboratory flow and temperature data.
