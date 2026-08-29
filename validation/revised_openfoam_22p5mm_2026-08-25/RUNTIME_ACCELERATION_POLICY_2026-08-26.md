# Runtime acceleration policy — 2026-08-26

> **Current authority update (2026-08-27).** The measured 22.5 mm timing and
> equivalence decisions remain valid for that preserved case. Every
> `model.exe`, O2 executable, “current-source”, and “final” software claim
> below is pinned to its historical snapshot. There is **no
> production-qualified exporter binary for current source**; rebuild and hash
> current source and designate the exact result as `[CURRENT_EXPORTER_EXE]`
> before any export.
>
> The final current-source software regression,
> `../revised_native_regression_2026-08-26/added_feature_regression_runner_state_gates_2026-08-27.run.json`,
> is PASS, exit 0, 516.584 s, affinity mask 3 (two logical processors), and
> BelowNormal priority. Its JSON/stdout/stderr SHA-256 values are respectively
> `246620007dff4b6f7dea50b22bcc9bc987e359aa575310134a9930da672a6c8c`,
> `03d781c9abe21ec3722e038672ef4c6587bed734cc896d97ddde3ed17a7bd81c`,
> and
> `f4dba93c473013728126639c6b07f4c86196eeba63b61600c3abab37dc051d23`.
> This is software-only evidence. The final idle gate JSON
> `../revised_native_regression_2026-08-26/openfoam_resource_gate_final_idle_20260827T095617107Z.json`
> (SHA-256
> `33c4fb9525398af3c7ec53156fe214a422b3d0e67ca13e144d0c5fcbe5ca6d51`)
> failed with exit 21: no competitors and 17,129,152,512 free disk bytes passed,
> but 951,701,504 available-memory bytes failed the 5,368,709,120-byte floor;
> WSL was not queried.

## Decision

Retain **0.0005 s** as the live-airflow/coupled-CHT maximum timestep. The
matched 0.00075 s branch was slower and materially changed velocity, pressure,
and internal-fan operating points. Do not test or promote a still larger live
timestep under the present sequence.

For frozen-flow thermal screening, retain the exact legacy 22.5 mm case's
model-local **24 s** cap. It was measurably faster than 20 s with small
temperature differences, but it failed the deliberately strict cell and
component-temperature targets and was tested on a pre-correction geometry.
Therefore 24 s is exploratory only. The reusable screening profile remains at
20 s, and strict profiles remain unchanged.

Use **two thermal-only outer energy-coupling passes** as a screening candidate,
while every live-airflow stage continues to use three. The control is available
as `THERMAL_ONLY_OUTER_CORRECTORS=3`; omitting the override uses the exported
screening value of two. This control is implemented and guarded, but it has not
yet completed a corrected-full-rack 3-versus-2 A/B. It must not support a
quantitative result until the promotion test below passes.

No heavy OpenFOAM run was launched for this change because the host did not
meet the standing memory gate: at least **5 GiB available continuously for
60 s** before launch. Preserving a recoverable checkpoint is more important
than collecting timing under paging.

## Implemented control and software verification

The audited source snapshot's input schema, TOML merge, exporter, generated
runner, profile policy, and documentation carry
`thermal_only_pimple_outer_correctors`. `0` inherits
the live count; an explicit value must be at least two. Screening exports use
two while default, in-depth, and validation profiles inherit their live count.
The generated runner accepts `THERMAL_ONLY_OUTER_CORRECTORS=3` for the control
branch, records the selected count, restores the configured live count before
every live stage, and restores it again on exit.

An executable fake-launcher regression found that the first implementation
targeted `system/fluid/fvSolution`, while the governing outer-loop key is in
root `system/fvSolution`. All three stage/exit/final edits were corrected to
the root dictionary. The final test proves that overrides 0 and 1 exit 2 before
the launcher, run lock, summary, or case writes; it also executes the generated
exit trap and observes exactly one restore to live `nOuterCorrectors=3`.

The pinned historical 76,800-cell generated microcase passed `bash -n`; its
runner SHA-256
is `0f3ca317a4beea4d091ef2b93c804d39c3bf8634527a4449efde46f9cf89a399`.
The runtime-control snapshot's complete harness passed (295,556-byte log,
SHA-256
`933922b1cbfa191a42e95cbe6e20e4439d86575959ba327d9d5f2492f0f36379`).
The later pinned source-snapshot hardening harness also passed (301,654-byte log,
SHA-256
`bb573ac51bc8d9037b5e50b08e1199c2e2040bfbf15115b1fdc1a8139c999576`).
Its pinned exact O2 component campaign passed all 11 canonical templates and
retained one expected geometry rejection for the inactive provisional NI separator in
68.2119 s (58,145-byte log, SHA-256
`9d9f6182059470ea4a7ff89fa300b574c411814832a0b17793ba2e0571668c95`).
These checks prove configuration propagation, native robustness, and
fail-closed runner behavior; they do not substitute for the full-rack thermal
3-versus-2 A/B.

## Evidence labels

- **Measured**: obtained from a completed, matched solver comparison with saved
  logs and endpoint comparisons.
- **Fact**: present in the identified source/configuration snapshot or directly observed,
  but not itself a performance result.
- **Inference**: engineering interpretation of measured facts.
- **Unmeasured**: proposed behavior or gain without a qualifying full-case A/B.

## Decision ledger

| Control | Evidence | Result | Policy |
|---|---|---|---|
| Live timestep, 0.0005 versus 0.00075 s | **Measured.** Same 1.600 s checkpoint, four ranks, 1.600–1.610 s. The larger step used 14 instead of 20 steps but clock time increased from 521 to 572 s (+9.79%). Velocity relative RMS changed 1.155754%, gauge-adjusted `p_rgh` 11.946965%, and 10/33 internal fans failed, worst 14.676%. | Slower and not field-equivalent. | **Reject 0.00075 s; retain 0.0005 s.** |
| Thermal-only cap, 24 versus 20 s | **Measured.** Same frozen 1.600 s checkpoint and four-rank case through 601.6 s. Cumulative clock fell from 463.527090 to 360.669262 s (22.190252%); the less startup-sensitive 480 s segment was 13.725195% faster. Endpoint T RMS was 0.006781254 K, maximum cell difference 0.123260498 K, and largest component-average difference 0.054939449 K. | Useful screening tradeoff, not timestep independence. | **Allow 24 s only for the exact exploratory case; use 20 s or smaller for confirmation.** |
| Thermal-only caps, 30/40/60 s | **Measured.** The matched 120 s screens produced maximum cell differences of 0.457733/0.886566/1.671265 K and T RMS values of 0.013332/0.026115/0.050279 K versus 20 s. | Error grew materially with timestep. | **Reject.** |
| Live PIMPLE, 3x2 versus 2x2 | **Measured.** Two outer passes were about 27% faster, but changed velocity by 0.891725% RMS, gauge-adjusted pressure by 1.497398% RMS, maximum T by 0.0390625 K, and failed 3/33 fan-flow comparisons (worst 4.282856%). | Speed did not preserve live-flow physics. | **Reject live 2x2; retain live 3x2.** |
| Auto CHT-interface rank placement | **Measured short screen.** Total clock was 15.36% lower and post-first-step cost 11.59% lower; velocity, pressure, temperature, mass, and all fan gates passed. The longer confirmation was resource-invalid under paging. | Credible candidate, incomplete confirmation. | **Do not promote until a memory-stable long repeat passes.** |
| Thermal-only outer passes, 3 versus 2 | **Fact:** the screening profile selects two; default, in-depth, and validation select inheritance (`0`). The generated runner restores live three-pass PIMPLE before airflow stages and on exit. **Unmeasured:** no corrected-full-rack timing or temperature/energy comparison exists. | Likely reduces repeated 13-solid-region energy work, but nonlinear iteration cost and temperature sensitivity are unknown. | **Screening candidate only; run the A/B below.** |
| Isothermal/heat-off airflow startup | **Fact:** the reviewed source snapshot rejects every isothermal request before time advancement and also rejects dual thermal/isothermal, mixed-region, and runtime mode changes. Nine static source-policy tests passed for that snapshot. **Unproven:** no `wmake`, clean-build binary hash, installed-binary provenance match, runtime microcase, or full-case transition test exists for this patch. | The unsafe shortcut is removed at source level, but the deployed behavior is not attested and no replacement initializer exists. | **Do not use for campaign acceleration.** |

The 24-versus-20 study belongs to the retained pre-correction 22.5 mm case;
the N5766A intake and other later geometry changes were not represented.
Close numerical agreement in that branch does not validate the corrected model,
the approximate heat loads, the unfinished NI separators, or mesh independence.

## Why one or two simulated seconds take about a day

This is the live-airflow cost, not the thermal-only cost. At 0.0005 s, one
physical second requires 2,000 nonlinear timesteps. Developed-flow windows
measured about 18.4–18.8 wall-seconds per step, or roughly 10.2–10.4 wall-hours
per simulated second. A resource-constrained window reached 23.695 s/step,
equivalent to 13.16 wall-hours per simulated second. Each live step performs
three outer passes, six pressure solves, nine velocity-component solves, three
fluid solves each for enthalpy, `k`, and `omega`, and 39 solid-enthalpy calls.
Field writing accounted for only about 0.13% of a representative window, so
reducing checkpoint frequency will not solve the bottleneck.

The latest retained 1.600 s flow state is not eligible for frozen-flow thermal
acceleration: it has approximately 0.875 nominal air replacements and 8.8677%
whole-fluid velocity RMS drift, above the 3% screening freeze gate. Raising a
thermal-only cap or reducing thermal-only outer passes cannot accelerate this
unfinished live phase, and no freeze gate should be weakened to enter thermal
mode early.

## Native workload hardening after the timestep request

The native explicit backend had a separate accounting defect: its displayed
`cells x timesteps` total omitted every advection substep. That omission did
not make the calculation cheaper; it merely hid the dominant work. The audited
source snapshot counts one non-advection pass plus every advection pass, checks
overflow, reprojects after each flow refresh, and treats `max_updates` as one
cumulative coarse-plus-fine budget. It also checks exact two-`Mesh` `Cell`
payload bytes before allocation instead of narrowing the value to a 32-bit
integer after allocation.

A pinned historical O2 `--native library/models/new_model_updated.toml`
preflight exited 1 without allocating the mesh. That exact plan contains
2,847,663 fine
cells and 216,580 coarse cells. At 300 steps per stage with subcycling enabled,
the unavoidable one advection pass plus one non-advection pass already requires
1,838,545,800 visits, versus `max_updates = 30,000,000`; solved-flow CFL can
only increase that number. The historical 1,849-substep state would still be
roughly 5.18 billion visits for a single global step. Increasing the native
global timestep does not remove this cost because stable advection substeps per
simulated second remain approximately proportional to flow speed and inverse
cell size.

This hardening is a fail-fast runtime control, not a claimed speedup. The
measured OpenFOAM speed policy remains: keep 0.0005 s for live flow; use the
case-local 24 s cap only after the airflow-freeze gates pass; keep 20 s or less
for confirmation; and do not weaken convergence, fan, mass-balance, or energy
gates to manufacture an earlier transition.

## Resource validity

Earlier preserved resource evidence showed why launch headroom is a numerical
quality control, not merely a convenience. A three-rank mitigation attempt
fell to 409–463 MiB available with 323–1,467 pages/s and 3.885 GiB WSL private
memory; one step took 84 wall-seconds and the run was stopped. Three ranks are
therefore not an accepted low-memory fallback.

During the present audit, fresh Windows observations were only about
1.2–1.4 GiB available. Starting WSL alone reduced availability to roughly
0.30 GiB and induced paging, so WSL was shut down and no OpenFOAM timing run
was attempted. These point observations are operational facts, not a persisted
peak-memory profile. After the user paused Fluent and cleared cached memory, a
new three-sample check still found only 1,809/1,847/1,845 MiB available, with
memory input rates of 0.0/31.6/70.5 pages/s. That also fails the launch gate.
A final pre-handoff performance-counter check at 20:26 MDT found only
1,433/1,410/1,435 MiB available, with 16.9/120.8/55.9 pages input per second.
The lower headroom confirms that a new full-case timing result would be
resource-contaminated even though Fluent is paused.
The launch rule remains:

1. no competing Fluent, OpenFOAM, or MPI solve;
2. at least 5 GiB Windows available for 60 continuous seconds before launch;
3. abort and classify timing as resource-invalid if the documented live-memory
   or paging limits are crossed; and
4. never infer solver speed from a paged or interrupted interval.

## Required 3-versus-2 thermal-only A/B

Run this only on a newly exported, corrected-geometry full-rack case after the
resource gate passes. The test must cover both an **early-heating** checkpoint
and a **near-steady** checkpoint because nonlinear temperature-property and
interface-coupling behavior can differ between them.

1. Verify each starting checkpoint is complete in the root and every processor
   directory. Record byte counts and SHA-256 manifests for fields, mesh,
   `constant/`, `system/`, runner, model, profile, component templates, fan
   curves, custom-solver source, and installed solver binary.
2. Clone each checkpoint into control and candidate branches. Confirm all files
   are identical before changing the single independent variable.
3. Keep four ranks, live 3x2 PIMPLE, decomposition, timestep, source terms,
   boundary conditions, fixed airflow fields, thermal timestep, write cadence,
   endpoint, and host conditions identical.
4. In the control branch invoke the generated runner with
   `THERMAL_ONLY_OUTER_CORRECTORS=3`; in the candidate branch use
   `THERMAL_ONLY_OUTER_CORRECTORS=2` (the explicit value is preferable to an
   implicit default in benchmark records). Advance the same thermal-only
   interval and land exactly on the same endpoint. The environment value and
   selected stage count must appear in `run_summary.log`.
5. Reconstruct both endpoints, run the same reports, compare volume-weighted
   fields cell-for-cell, and retain solver/residual/iteration timing plus the
   complete transient energy ledger.
6. Perform one guarded live refresh after each thermal branch. Verify the runner
   restored three outer passes and that no thermal-stage setting leaked into
   the airflow stage.

Example branch invocations, with identical `END_TIME` and refresh policy, are:

```bash
THERMAL_ONLY_OUTER_CORRECTORS=3 ./run_parallel.sh 4 --multirate END_TIME REFRESH_INTERVAL
THERMAL_ONLY_OUTER_CORRECTORS=2 ./run_parallel.sh 4 --multirate END_TIME REFRESH_INTERVAL
```

Use separate case directories; never run those commands concurrently against
one case. Replace the placeholders with a stage that contains no unmatched
airflow refresh and is long enough to suppress startup/cache noise.

### Promotion gates

Both early-heating and near-steady comparisons must pass all of these gates:

- normal solver/wrapper exit, complete endpoints on every rank, no fatal or
  non-finite signature, and no skipped convergence check;
- a predeclared meaningful sustained speed gain (policy target: at least 10%
  lower post-first-step wall time) without a slowdown at either checkpoint;
- volume-weighted all-region temperature RMS difference no greater than
  0.01 K;
- maximum absolute cell, region-peak, and component-average temperature
  differences each no greater than 0.02 K;
- `U` and `phi` unchanged during the frozen-flow interval; any difference is a
  hard failure, not a tolerance to average away;
- no new linear-solver tolerance miss, stagnation, residual growth pattern, or
  materially worse iterations per simulated second;
- both branches independently pass the fail-closed transient first-law audit
  and the complete CHT-interface cancellation/balance inventory; endpoint-only
  heat-removal estimates are insufficient; and
- the subsequent live refresh runs with three outer passes and passes the same
  airflow, fan-direction, per-fan operating-point, and mass-balance gates.

The retained 1.50–1.60 s evidence cannot satisfy the first-law item:
it lacks a gap-free, high-precision per-step energy ledger. That interval is
formally **not evaluable**, not a closure pass or failure. Full promotion
therefore also depends on completing the existing instrumentation contract.

If any gate fails, set `THERMAL_ONLY_OUTER_CORRECTORS=3` for screening or use
`thermal_only_pimple_outer_correctors = 0` to inherit the authoritative live
count. A one-pass thermal option is prohibited because it omits the additional
energy-coupling/relinearization behavior retained by two passes.

## Future isothermal initializer release requirements

The reviewed source-level policy intentionally refuses isothermal startup. Any future
scratch-case initializer is a separate model-form optimization and cannot be
folded into the outer-corrector A/B. Before it can even enter a screening
campaign:

1. perform a clean OpenFOAM build and record the source SHA-256, compiler/build
   identity, linked OpenFOAM version, and installed executable SHA-256 together;
2. prove the launcher selects that exact binary and fails closed on a provenance
   mismatch;
3. pass a focused microcase covering pressure correction, fan sources,
   turbulence, temperature hold, and transition back to coupled CHT;
4. compare heated and isothermal startup from identical full-rack initial fields
   through an accepted post-transition live checkpoint; and
5. pass velocity/pressure fields, all boundary and internal fan operating
   points, mass balance, temperatures after transition, and transient-energy
   accounting.

Until then, historical results produced by an older custom-solver binary are
not evidence for a newer source patch, the installed binary must not be
assumed to enforce the source-level rejection, and no runtime saving should be
quoted.

## Evidence pointers

- `PERFORMANCE_AUDIT.md`
- `THERMAL_TIMESTEP_ACCELERATION_2026-08-26.md`
- `performance_benchmarks_1p60/TIMESTEP_BENCHMARK.md`
- `performance_benchmarks_1p60/PIMPLE_BENCHMARK.md`
- `performance_benchmarks_1p60/DECOMPOSITION_BENCHMARK.md`
- `performance_benchmarks_1p60/THREE_RANK_RESOURCE_BENCHMARK.md`
- `TRANSIENT_FIRST_LAW_AUDIT_STATUS_1P50_1P60.md`
- `../revised_native_regression_2026-08-26/CONVECTION_AND_NI_HARDENING_2026-08-26.md`

This policy changes screening controls only. It does not make the current
coarse mesh, provisional flow checkpoint, incomplete NI geometry, approximate
loads, or uncalibrated fan curves industry-ready.
