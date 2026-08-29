# Thermal-only timestep acceleration — 2026-08-26

## Decision

Use a **24 s maximum thermal-only timestep** for rapid screening in the exact
22.5 mm, four-rank updated research-lab campaign. Keep the reusable screening
profile at 20 s, leave every validation profile unchanged, and leave the
live-airflow timestep at 0.0005 s.

This is a bounded speed/accuracy tradeoff, not validation-grade timestep
independence. The 24 s candidate fails the deliberately strict 0.02 K
component-average and 0.05 K cell-difference targets. It is still useful for
exploratory screening because its largest sampled component-average difference
was 0.05494 K and its largest sampled cell difference was 0.18631 K, while
larger candidate steps degraded rapidly. A final or sign-off run must repeat
the relevant interval at 20 s or smaller and demonstrate timestep convergence.

## What the two timestep controls mean

| Phase | Prior/retained cap | Changed? | Purpose |
|---|---:|---|---|
| Live airflow / coupled CHT | 0.0005 s | No | Resolves velocity, pressure, fans, and buoyancy while the flow develops. |
| Thermal-only / held airflow | 20 s | Yes, to 24 s for this campaign only | Advances implicit energy equations after the airflow-freeze gates pass. |

Increasing the thermal cap cannot speed the current live-flow continuation.
The latest 1.600 s airflow state has only about 0.875 nominal air replacements
and an 8.8677% whole-fluid velocity RMS drift, above the retained 3% freeze
gate. No gate was bypassed or weakened.

## Matched benchmark design

- Solver: OpenFOAM 2606 `semiFrozenChtMultiRegionFoam`.
- Start: the same complete decomposed checkpoint at
  `1.6000000000000001` s for every branch.
- Mesh: 1,033,200 cells across one fluid and 13 solid regions.
- Parallel policy: four MPI ranks, auto CHT-interface placement, retained 3x2
  PIMPLE settings.
- Physics held constant: identical mesh, fields, source terms, material
  properties, boundary conditions, partitioning, and frozen velocity field.
- Identity checks matched all 152 starting-checkpoint files and all 491
  constant/configuration files across variants; only the expected post-run
  `controlDict` values differed.
- Changed variable: thermal-only maximum timestep.
- Stage planning: `duration / ceil(duration / maximum_dt)`, producing equal
  fixed steps that land exactly on the requested endpoint.
- Screen: 120 simulated seconds, 1.6 to 121.6 s.
- Confirmation: another 480 simulated seconds, 121.6 to 601.6 s.
- Output: the reconstructed endpoint fields were compared cell-for-cell with
  volume-weighted metrics.

The candidate was initially invoked with a 25 s maximum, but exact endpoint
planning produced five equal **24 s** steps over 120 s and twenty equal 24 s
steps over 480 s. The promoted configuration therefore records 24 s, not 25 s.

## 120 s candidate screen

| Actual step (s) | Steps | Wall time (s) | Wall-time reduction vs 20 s | All-cell T RMS vs 20 s (K) | Maximum cell T difference (K) | Decision |
|---:|---:|---:|---:|---:|---:|---|
| 20 | 6 | 153.275178 | reference | 0 | 0 | Reference |
| 24 | 5 | 93.000030 | 39.3248% | 0.005412 | 0.186310 | Confirm as rapid-screen candidate |
| 30 | 4 | 81.994076 | 46.5053% | 0.013332 | 0.457733 | Reject |
| 40 | 3 | 81.270679 | 46.9773% | 0.026115 | 0.886566 | Reject |
| 60 | 2 | 57.666844 | 62.3769% | 0.050279 | 1.671265 | Reject |

Thirty seconds is the first larger candidate and already exceeds 0.01 K
whole-domain RMS and 0.25 K maximum difference. The 40 and 60 s candidates
worsen approximately monotonically. Stability alone was therefore not used as
the acceptance criterion.

## 600 s confirmation

| Branch | First 120 s | Next 480 s | Total steps | Cumulative wall time (s) |
|---|---:|---:|---:|---:|
| 20 s reference | 6 steps / 153.275178 s | 24 steps / 310.251912 s | 30 | 463.527090 |
| 24 s candidate | 5 steps / 93.000030 s | 20 steps / 267.669232 s | 25 | 360.669262 |

- Cumulative wall-time reduction: **22.190252%**.
- Reduction on the longer 480 s confirmation segment: **13.725195%**.
- The 39.3% short-screen reduction includes startup/cache noise and should not
  be treated as the sustained speedup. The longer-segment figure is the safer
  runtime expectation.
- These are sequential, single-replicate timing runs. They establish a useful
  engineering estimate, not a statistical wall-time confidence interval.
- In the 480 s segment, the 20 s reference performed 1,008 enthalpy linear
  solves and 13,316 total iterations. The 24 s candidate performed 840 solves
  and 11,949 iterations. That is 16.7% fewer solves, but iterations per solve
  rose from 13.210 to 14.225 (7.68%), so the larger step does not translate
  linearly into wall-time savings.

At 601.6 s:

| Accuracy metric, 24 s minus 20 s | Result |
|---|---:|
| All-region volume-weighted T RMS | 0.006781254 K |
| Largest absolute cell T difference | 0.123260498 K |
| Largest absolute region-average T difference | 0.054939449 K, Keysight N5766A |
| Largest peak-temperature difference | -0.123229980 K, Meanwell instance 10 |
| Fluid U RMS / maximum difference | 0 / 0 m/s |
| Gauge-adjusted fluid `p_rgh` RMS | 0.000832714 Pa |
| Gauge-adjusted fluid `p_rgh` maximum | 0.007902151 Pa |

The larger implicit step is slightly cooler in most heated solids. That
one-sided bias is small for rapid screening but is another reason not to call
the 24 s result validation-grade. Both full branches ended normally, wrote the
requested endpoints on all four ranks, reconstructed successfully, and showed
no fatal solver signature.

## Deployment and rollback

- Current source configuration:
  `library/models/new_model_updated_openfoam_export_test.toml` has been reset
  to the accepted reusable `thermal_only_maximum_time_step = 20.0` cap. The
  24 s setting remains only in the preserved exact-case launcher/evidence below.
- Installed active launcher:
  `C:/Users/hconn/.codex/visualizations/2026/08/04/019fcccd-4536-7b51-a70c-8023779a1618/openfoam_cases/new_model_updated_openfoam_export_test/run_parallel.sh`
  uses 24 s only in the implicit thermal-only stage.
- Hardened installed-source copy:
  `performance_benchmarks_1p60/active_case_run_parallel_hardened_preflight.sh`.
- Installed/hardened SHA-256:
  `7d41624a89720edbc42fa52eab0a22495f90d58654b84c68ad9a61e96c7306ac`.
- Pre-change rollback copy:
  `performance_benchmarks_1p60/active_case_run_parallel_pre_thermal_dt24_2026-08-26.sh`;
  SHA-256
  `e47c392ea50572dca29c48681ec7d3457baf871b5d22cdc8d1b229b9ade5d7cd`.

The current `controlDict` remains a live-flow dictionary with a 0.0005 s cap.
The launcher installs the 24 s value only when it enters thermal-only mode.
Re-exporting the active case was intentionally avoided because its export
policy permits overwrite and the 1.600 s checkpoint must be preserved.

## Reproduction and evidence

The reusable harness is `tools/benchmark_openfoam_thermal_timestep.sh`:

```bash
tools/benchmark_openfoam_thermal_timestep.sh \
  CASE_BRANCH START_TIME DURATION MAXIMUM_DT 4 OUTPUT_LOG
```

It rejects future processor checkpoints, configures thermal-only mode, chooses
an exactly divisible fixed step, requires the endpoint on every rank, and emits
`OUTPUT_LOG.result.tsv`. The benchmark branches are preserved under:

`C:/Users/hconn/.codex/visualizations/2026/08/04/019fcccd-4536-7b51-a70c-8023779a1618/openfoam_benchmark_snapshots`

Compact evidence is in `performance_benchmarks_1p60/`:

- `thermal_dt_screen_{20,25,30,40,60}.stdout.log`
- `thermal_dt_screen_{20,25,30,40,60}.stdout.result.tsv`
- `thermal_dt_confirm_20_121p6_to_601p6.stdout.{log,result.tsv}`
- `thermal_dt_confirm_25_121p6_to_601p6.stdout.{log,result.tsv}`
- `thermal_dt_screen_{25,30,40,60}_vs_20.csv`
- `thermal_dt_confirm_25_vs_20_at_601p6.csv`
- `thermal_dt_confirm_24_vs_20_component_temperatures_at_601p6.csv`

Files retaining `25` in their name record the originally requested upper cap;
their result TSVs prove the actual step was 24 s.

## Physical-model limitation

This experiment establishes numerical sensitivity only. The frozen 1.600 s
airflow is provisional, the NI separator walls are missing, rail-2 depths and
heat loads remain approximate, material homogenization is known, and mesh
independence has not been established. Temperatures near 357 K in both branches
therefore warrant physical-model review, but their close agreement does not
make them calibrated predictions.

The 20 s branch is an operational reference, not a proven temporally converged
truth: no matched 5 or 10 s full confirmation was run. This study also did not
compare integrated energy balance, outlet temperature, an airflow refresh, or
the complete 2400 s screening interval. Those checks remain required before
using this cap beyond exploratory screening.
