# PIMPLE outer-corrector benchmark from the 1.600 s checkpoint

## Reproducible setup

The reference and candidate use OpenFOAM 2606, four MPI ranks, the
short-screen-selected auto-placement candidate, the same reconstructed
1.600 s restart, and a fixed 0.0005 s timestep over 1.600--1.610 s. The
reference is
`C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_benchmark_snapshots\decomposition_auto_case`
with three PIMPLE outer correctors and two pressure correctors (3x2). The
candidate is
`C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_benchmark_snapshots\pimple_outer2_auto_case`
with two outer correctors and two pressure correctors (2x2).

The only intended system change is nOuterCorrectors from 3 to 2 in the root
fvSolution; nCorrectors remains 2. `pimple_outer2_system_manifest.csv` records
the pre-run comparison: 156 identical system files and the one expected
fvSolution difference. The later interrupted long run mutated the reference
case's `controlDict`; that present-day drift does not alter the captured
benchmark comparison. Both branches were run from the exact
1.6000000000000001 checkpoint with:

    THERMAL_WARM_START_MAX_DT=0.0005 bash ./run_parallel.sh 4 --warm-start 1.61

The matched solver logs are decomposition_auto_1p600_to_1p610.stdout.log for
the 3x2 control and pimple_outer2_auto_1p600_to_1p610.stdout.log for the 2x2
candidate.

## Completion and performance

| Metric | 3x2 control | 2x2 candidate | Candidate change |
|---|---:|---:|---:|
| Timesteps | 20 | 20 | matched |
| p_rgh solves | 120 | 80 | -33.33% |
| U solves per component | 60 | 40 | -33.33% |
| Solver execution | 425.77 s | 308.35 s | 27.5783% faster |
| Solver clock | 441 s | 324 s | 26.5306% faster |
| First-step execution | 42.41 s | 31.62 s | — |
| First-step clock | 52 s | 41 s | — |
| Post-first execution | 383.36 s | 276.73 s | 27.8146% faster |
| Post-first execution per step | 20.176842 s | 14.564737 s | 27.8146% faster |
| Post-first clock | 389 s | 283 s | 27.2494% faster |
| Post-first clock per step | 20.473684 s | 14.894737 s | 27.2494% faster |
| Peak maximum Courant | 2.32872 | 2.32911 | both below 4 |
| True fatal signatures | 0 | 0 | pass |

The candidate completed exactly 20 fixed-size steps from 1.6005 through 1.610,
with four pressure solves and two solves of each velocity component per step.
It therefore passes the solver-completion and minimum 15% post-first timing
gates.

## Physics-equivalence screen

| Gate | 2x2 difference from 3x2 | Limit | Result |
|---|---:|---:|---|
| Whole-field velocity relative RMS | 0.891725% | <= 0.1% | **FAIL** |
| Gauge-adjusted p_rgh relative RMS | 1.497398% | <= 0.1% | **FAIL** |
| Whole-model temperature RMS | 0.000291910 K | <= 0.001 K | PASS |
| Maximum temperature difference | 0.0390625 K | <= 0.02 K | **FAIL** |
| One-way boundary throughput difference | 0.000823604% | <= 1% | PASS |
| 2x2 boundary mass mismatch | 0.000706532% | <= 1% | PASS |
| Boundary fan directions | 14/14 correct | all correct | PASS |
| Internal fan directions | 33/33 forward | all forward | PASS |
| Internal fan operating points | worst 4.282856%; 3/33 fail | <= 1% each | **FAIL** |

The three failed fan comparisons are:

- internal_Exhaust_fan_1_37: 1.057551%
- internal_Exhaust_fan_2_38: 4.282856%
- internal_Power_supply_exhaust_fan_40: 1.480131%

The direct endpoint validator passes connectivity and mass conservation but
reports the expected early-transient steady-state heat-removal failure: 8.9258
W outlet sensible transport versus 2,315 W applied, or 99.6144% mismatch.
Stored energy is not included, so this is not a transient first-law closure or
thermal-convergence result. That shared short-horizon limitation is not used
to excuse the matched U, pressure, temperature-maximum, or fan failures.

## Decision

**REJECT the 2x2 PIMPLE candidate despite its approximately 27% runtime
improvement.** It violates the velocity, pressure, maximum-temperature, and
three internal-fan equivalence gates. Retain 3x2 PIMPLE while auto placement
undergoes its separate longer-confirmation screen; keep the generated pinned
decomposition and nOuterCorrectors=3 defaults unchanged meanwhile.

The decision is supported by pimple_timing_comparison.csv,
pimple_outer2_vs_outer3_fields.csv, pimple_outer2_vs_outer3_fans.csv,
pimple_outer2_vs_outer3_boundary.csv, the candidate validation and fan reports,
pimple_outer2_system_manifest.csv, and both matched solver logs in this
directory.

After those artifacts and the copied solver log were verified, only the five
rejected candidate `1.6100000000000001` field directories (370 files,
265,473,197 logical bytes) were deleted, recovering 266,067,968 bytes. The
candidate's complete root-plus-four-rank 1.600 restart, system inputs, reports,
and command remain, so the endpoint is reproducible but the deleted copy is not
locally recoverable without rerunning the 20 steps.
