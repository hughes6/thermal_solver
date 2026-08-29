# Revised 22.5 mm OpenFOAM performance audit

This audit applies to the four-rank transient CHT screening case exported from
`library/models/new_model_updated_openfoam_export_test.toml`. It explains the
measured runtime and defines the controlled tests required before changing the
screening defaults. Measured benchmark decisions are labeled explicitly;
unmeasured future gains remain hypotheses.

## Measured baseline

The mesh contains 1,033,200 cells: 925,388 fluid cells and 107,812 solid cells
across 13 component regions. The completed 0.1 s windows from 1.10 through
1.50 s required:

| Physical interval (s) | Timesteps | Solver execution (s) | Wall time (s) | Wall time / step (s) |
|---|---:|---:|---:|---:|
| 1.10--1.20 | 200 | 3,730.76 | 3,759 | 18.795 |
| 1.20--1.30 | 200 | 3,650.28 | 3,681 | 18.405 |
| 1.30--1.40 | 200 | 3,685.31 | 3,716 | 18.580 |
| 1.40--1.50 | 200 | 3,721.96 | 3,755 | 18.775 |
| 1.50--1.60 (resource constrained) | 200 | 4,699.38 | 4,739 | 23.695 |

The live-airflow cap is `deltaT = maxDeltaT = 0.0005 s`, so one simulated
second requires 2,000 timesteps. At the observed 18.4--18.8 wall-seconds per
step, one simulated second costs 10.2--10.4 wall-hours. The final write in the
1.40--1.50 window added approximately five seconds to a 3,755-second interval;
checkpoint I/O is not the primary runtime bottleneck.

The 1.50--1.60 window demonstrates the additional resource-contention cost.
An uncaptured live observation at the time showed only about 0.5 GiB Windows
memory available near the end; wall time rose 26% to 13.16 wall-hours per
simulated second at the same mesh, timestep, and solver controls. At that
degraded rate the 1.8415 s minimum-air-exchange
horizon costs about 24.2 wall-hours from a cold start. This is why a request
for only one or two physical seconds can consume roughly one day; the solver
is advancing 2,000 tightly coupled nonlinear timesteps per physical second,
not playing back real time.

Each timestep executes three PIMPLE outer correctors and two fluid pressure
correctors per outer pass. A representative developed-flow step therefore
contains six GAMG pressure solves, nine velocity-component solves, three each
of fluid enthalpy, `k`, and `omega`, and 39 solid-enthalpy calls (13 regions
times three outer passes).

The cap was selected after a prior startup test at `deltaT = 0.001 s` reached
maximum Courant 4.579 against `maxCo = 5`. It remains binding after startup:
the developed-flow maximum Courant is approximately 2.32 at 0.0005 s. This
provides headroom for a controlled timestep study, but does not by itself prove
that a larger timestep preserves the fan operating points or transient field.

## Controlled timestep result: larger step rejected

The controlled study has now been run from the exact 1.600 s checkpoint over
the same 0.010 s physical interval on four ranks:

| Metric | 0.0005 s reference | 0.00075 s variant |
|---|---:|---:|
| Timesteps | 20 | 14 |
| Solver clock time | 521 s | 572 s |
| Post-first-step clock/step | 23.158 s | 34.000 s |
| Peak maximum Courant | 2.32872 | 3.40500 |
| Whole-field velocity relative RMS difference | reference | 1.155754% |
| Gauge-adjusted `p_rgh` relative RMS difference | reference | 11.946965% |
| Internal fans outside tolerance | reference | 10/33 |

The nominally larger timestep reduces step count by 30% but increases total
clock time by 9.79% and post-initialization cost per step by 46.82%. The
higher-Courant coupled solves consume the expected step-count saving. It also
fails the 1% velocity and pressure equivalence gates and the per-fan gate; the
worst fan-flow difference is 14.6761%. Therefore 0.00075 s is rejected and
0.001 s will not be tested under this sequence because its prerequisite did
not pass. The authoritative cap remains 0.0005 s.

The full record is in `performance_benchmarks_1p60/TIMESTEP_BENCHMARK.md`.
Both reconstructed endpoints and raw reports are retained as standalone
benchmark snapshots; the rejected decomposed endpoint has been removed from
the active case so it cannot be mistaken for an active continuation.

## Controlled decomposition result: auto placement selected for confirmation

The original processor mesh headers give the following ownership:

| Rank | Fluid cells | Solid cells | Total cells | Difference from ideal 258,300 |
|---:|---:|---:|---:|---:|
| 0 | 321,215 | 107,812 | 429,027 | +66.1% |
| 1 | 185,323 | 0 | 185,323 | -28.3% |
| 2 | 212,126 | 0 | 212,126 | -17.9% |
| 3 | 206,724 | 0 | 206,724 | -20.0% |

Every solid region is configured with `numberOfSubdomains 1`, and the fluid
`chtCoupledInterfaces` face set was pinned to processor 0. Rank 0 consequently
owned 2.31 times as many cells as rank 1. The controlled experiment kept the
`singleProcessorFaceSets` constraint and changed only its requested processor:

```foam
sets ((chtCoupledInterfaces 0));
```

becomes:

```foam
sets ((chtCoupledInterfaces -1));
```

OpenFOAM selected totals of 291,033 / 183,503 / 214,852 / 343,812 cells. This
reduced maximum-rank/mean-rank load from 1.661 to 1.331 and the heaviest rank
by 19.86%. It retained the aggregate 120,549-face CHT interface set on one
processor and left every solid-region decomposition unchanged.

Both decompositions restarted from the same byte-identified 1.600 s fields and
ran 20 steps at 0.0005 s. Total clock time fell from 521 to 441 s (15.36%);
post-first-step cost fell from 23.158 to 20.474 s/step (11.59%). The auto case
matched the pinned case to 0.002838% velocity relative RMS, 0.010373%
gauge-adjusted `p_rgh` relative RMS, 0.000003181 K temperature RMS, and
0.000183 K maximum temperature difference. All 33 internal fans passed, with
a worst flow difference of 0.01236%; all 14 boundary fans had the intended
sign and boundary mismatch was 0.001972%.

Auto placement therefore passes the short controlled screen and is the
selected partition candidate for a longer continuation check. It is not yet
written into the generated profile: the 0.010 s result must be confirmed over
a normal continuation window before the exporter default changes. The complete record
is in `performance_benchmarks_1p60/DECOMPOSITION_BENCHMARK.md`.

The first 1.610--1.700 s confirmation attempt was stopped during its eighth
step after transcribed live observations reported 515--557 MiB available and
about 303 pages/s. Timing was therefore classified as resource-invalid:
post-first-step cost was 33.333 s/step versus 20.474 s in the matched benchmark,
with the latest complete step taking 52 s. No new checkpoint was written and
the verified 1.610 restart is intact. No solver or physics-failure signature
was observed; GAMG work remained essentially flat at 43.714 iterations/step
versus 43.450 in the matched benchmark. See
`performance_benchmarks_1p60/DECOMPOSITION_LONG_CONFIRMATION_ATTEMPT.md`.

## Controlled PIMPLE result: two outer correctors rejected

The matched PIMPLE candidate changed only `nOuterCorrectors` from 3 to 2 while
retaining two pressure correctors, the short-screen-selected auto-placement candidate, four ranks,
the exact 1.600 s restart, and the 0.0005 s timestep. It completed all 20 steps
without a fatal signature and reduced post-first-step wall cost from 20.474 to
14.895 s/step (27.25%); total clock time fell from 441 to 324 s.

That speedup does not meet the physics-equivalence gate. Relative to the 3x2
control, the 2x2 endpoint changed velocity by 0.891725% RMS and gauge-adjusted
`p_rgh` by 1.497398% RMS, both above the 0.1% limits. Temperature RMS passed at
0.000291910 K, but the 0.0390625 K maximum difference exceeded the 0.02 K
limit. Three of 33 internal fan flows failed the 1%/1e-5 m3/s tolerance, with a
worst difference of 4.28286%. Boundary mismatch (0.000707%), all 14 boundary
fan directions, and gross throughput passed, but those aggregate checks do not
override the spatial and per-device failures. The 2x2 setting is rejected;
3x2 PIMPLE remains authoritative while the generated decomposition remains
pinned. See `performance_benchmarks_1p60/PIMPLE_BENCHMARK.md`.

## Controlled optimization sequence

Every test must restart from the same complete, byte-identified processor
checkpoint. Only one setting may change in a comparison.

1. The 0.0005 s reference branch over 1.600--1.610 s is complete.
2. The matched 0.00075 s branch is complete and rejected: it is slower and
   fails velocity, pressure, and internal-fan equivalence gates.
3. A 0.001 s branch is prohibited by the stated sequence because the
   0.00075 s prerequisite failed.
4. The independent 2x2 PIMPLE branch is complete and rejected despite a 27.25%
   post-first-step speedup because velocity, pressure, maximum-temperature, and
   three internal-fan equivalence gates failed.
5. The `0` versus `-1` interface-placement benchmark passed its short screen.
   Retry its longer continuation confirmation only with adequate host memory,
   while retaining the generated 3x2 solver controls and 0.0005 s timestep;
   do not change the exporter default until that confirmation is audited.
6. A three-rank auto-decomposition branch passed all partition/restart gates
   but failed the resource criteria used during the run before its first step completed:
   409--463 MiB available, 323--1,467 pages/s, and 3.885 GiB WSL private.
   One step took 84 wall-seconds. Three ranks are rejected as a low-memory
   mitigation on this host; see
   `performance_benchmarks_1p60/THREE_RANK_RESOURCE_BENCHMARK.md`.

Mesh coarsening, steady flow, or premature frozen-flow thermal advancement are
not performance-equivalent changes. They require separate sensitivity studies
and cannot be used to claim that the existing transient result was accelerated.
