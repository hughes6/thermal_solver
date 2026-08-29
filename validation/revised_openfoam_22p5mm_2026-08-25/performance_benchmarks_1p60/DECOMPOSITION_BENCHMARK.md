# Four-rank coupled-interface decomposition benchmark

Both branches restarted from the same reconstructed 1.600 s fields, used four
ranks, the unchanged 3x2 PIMPLE controls, and a fixed 0.0005 s timestep over
1.600--1.610 s. The only intended configuration change is
`system/fluid/decomposeParDict`:

```foam
sets ((chtCoupledInterfaces 0));
```

becomes:

```foam
sets ((chtCoupledInterfaces -1));
```

At the retained short-benchmark state, `decomposition_system_manifest.csv`
confirms that this was the only changed system file. The reconstructed restart
fields were byte-identical. The tested auto case is
`C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_benchmark_snapshots\decomposition_auto_case`.
Fresh
decomposition represents the 36 saved `uniform` restart-state files as native
WSL links on every rank; all eight links resolve inside the cloned 1.600 s
root checkpoint and expose the expected 2 root plus 34 fluid state files.

## Load balance and runtime

| Metric | Pinned interface rank 0 | Auto-selected interface rank |
|---|---:|---:|
| Rank cell totals | 429,027 / 185,323 / 212,126 / 206,724 | 291,033 / 183,503 / 214,852 / 343,812 |
| Maximum / ideal rank load | 1.661 | 1.331 |
| Maximum / minimum rank load | 2.315 | 1.874 |
| Solver execution | 483.21 s | 425.77 s |
| Solver clock | 521 s | 441 s |
| First-step clock | 81 s | 52 s |
| Post-first clock per step | 23.1579 s | 20.4737 s |
| Peak maximum Courant | 2.32872 | 2.32872 |

The auto branch reduces the heaviest rank by 19.86%, total solver clock by
15.36%, and post-initialization time per step by 11.59%. It completed exactly
20 timesteps at 0.0005 s, 120 `p_rgh` solves, and 60 `Ux` solves. The solver,
reconstruction, and wrapper exited cleanly; the root and all four processor
endpoints each contain 74 nonempty files.

## Physics equivalence

| Gate | Auto difference from pinned reference | Limit | Result |
|---|---:|---:|---|
| Whole-field velocity relative RMS | 0.002838% | <= 0.1% | PASS |
| Gauge-adjusted `p_rgh` relative RMS | 0.010373% | <= 0.1% | PASS |
| Temperature RMS | 0.000003181 K | <= 0.001 K | PASS |
| Maximum temperature difference | 0.000183 K | <= 0.02 K | PASS |
| One-way boundary throughput difference | 0.0000375% | <= 1% | PASS |
| Auto-branch mass mismatch | 0.001972% | <= 1% | PASS |
| Boundary fan directions | 14/14 correct | all correct | PASS |
| Internal fan directions | 33/33 forward | all forward | PASS |
| Internal fan operating points | worst 0.01236%; 0/33 fail | <= 1% each | PASS |

The full validation report retains the expected early-transient steady-state
heat-removal failure (8.93 W outlet sensible transport versus 2,315 W
applied). Stored energy is not included, so this is neither a transient
first-law closure result nor a decomposition failure, and it is not relabeled
as thermal convergence.

## Decision

Auto placement passes this matched short-interval screen and is the selected
partition candidate for longer confirmation. It is not the generated project
default: the measured steady-step gain clears the lower 10% screen but not the
preferred 15% threshold, and host-memory effects change runtime materially.
The subsequent 2x2 PIMPLE benchmark was completed and rejected independently;
it does not alter this decomposition result. The first longer auto-placement
attempt was intentionally stopped after live observations were consistent with
severe memory pressure, so its timing was treated as non-representative.
Promotion still requires a memory-stable continuation or
repeat that passes the same field and fan gates; see
`DECOMPOSITION_LONG_CONFIRMATION_ATTEMPT.md`.

The interrupted attempt left the current auto-case `system/controlDict` in its
requested 1.700 s continuation state, so its present hash no longer matches the
short-benchmark manifest. The exact retained short-benchmark control is copied
here as `decomposition_short_benchmark_retained_controlDict` (SHA-256
`38FBA8F6A1F79F28C944DC3B1A920D2E377B72F0D7CEF5716916BD8FEB7A953E`).

Evidence includes the two solver logs, timing comparison, partition reports,
cell-addressing hashes, system manifest, boundary/fan comparisons, endpoint
validation, and volume-weighted reconstructed-field comparison in this
directory.
