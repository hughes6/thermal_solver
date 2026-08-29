# Timestep benchmark from the 1.600 s checkpoint

Both branches restarted from the same complete reconstructed and four-rank
`1.6000000000000001` fields, use four MPI ranks, and solve the same
1.600--1.610 s physical interval. The only intended solver-control change is
`THERMAL_WARM_START_MAX_DT`. The baseline uses 0.0005 s and the variant uses
0.00075 s. At completion, both endpoints were verified with 74 nonempty files
in the root and on every rank, had no OpenFOAM fatal signature, and were
reconstructed and post-processed successfully. The retained snapshot cases now
preserve the reconstructed endpoints and reports; the decomposed rejected
endpoint was removed after its identity checks passed.

## Performance result

| Metric | 0.0005 s baseline | 0.00075 s variant | Interpretation |
|---|---:|---:|---|
| Timesteps | 20 | 14 | Variant uses 30% fewer steps |
| Solver execution | 483.21 s | 509.76 s | Variant is 5.49% slower |
| Solver clock | 521 s | 572 s | Variant is 9.79% slower |
| Clock after first step | 440 s | 442 s | No measurable total saving after initialization |
| Post-first clock/step | 23.158 s | 34.000 s | Variant steps are 46.82% more expensive |
| Peak maximum Courant | 2.32872 | 3.40500 | Both pass the `< 4` screen |

The larger timestep therefore fails the required material-speedup gate. The
extra pressure/nonlinear work per higher-Courant step consumes the nominal
step-count saving.

## Physics-equivalence result

| Gate | Measured variant difference from baseline | Limit | Result |
|---|---:|---:|---|
| Whole-field velocity relative RMS | 1.155754% | <= 1% | FAIL |
| Gauge-adjusted `p_rgh` relative RMS | 11.946965% | <= 1% | FAIL |
| Temperature RMS | 0.000573 K | <= 0.01 K | PASS |
| Maximum temperature difference | 0.102112 K | diagnostic | — |
| One-way boundary throughput | 0.109056% | <= 1% | PASS |
| Boundary mass mismatch | 0.006125% | <= 1% | PASS |
| Boundary fan directions | 14/14 correct | all correct | PASS |
| Internal fan operating points | 10/33 outside tolerance; worst 14.676% | max(2%, 1e-5 m3/s) each | FAIL |

The full-field and fan failures are material even though gross ventilation
balance remains excellent. The 0.00075 s setting is rejected and must not be
promoted to the generated default or used for the active continuation.

Evidence in this directory includes both solver logs, timing CSV, baseline and
variant mass/opening/fan reports, direct checkpoint validation reports, a
zero-error identity check of the preserved baseline snapshot, and the
volume-weighted cross-case field comparison. The raw reference and variant
1.610 function-object directories and reconstructed fields are preserved at
`C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_benchmark_snapshots\dt_baseline_0p0005_case`
and
`C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_benchmark_snapshots\dt_variant_0p00075_case`.
`variant_snapshot_file_identity.csv` and
`variant_snapshot_postprocessing_identity.csv` record zero SHA-256 mismatches
across all 74 reconstructed fields and all 196 refreshed report files. The
five rejected variant endpoint directories were removed from the active case
only after that identity gate passed; the active case remains at the complete
1.600 s active-continuation checkpoint.
