# Corrected-run acceleration and evidence gates — 2026-08-27

## Outcome

The current lab-model thermal-only cap is **20 s**, not 0.0005 s. The
`0.0005 s` value applies only while velocity and pressure are still being
solved during fan ramp and initial live airflow. The later screening refresh
cap is `0.001 s`. The runner selects the 20 s cap only after the initial
airflow, air-exchange, spatial-field, mass-balance, direction, fan-domain, and
long-lag gates accept freezing the flow field.

The retained 1.600 s case has not reached that boundary. It represents about
0.875 nominal air exchanges and its whole-fluid velocity RMS drift is 8.8677%,
above the 3% screening freeze gate. At 0.0005 s, one simulated second requires
2,000 nonlinear coupled steps. Measured developed-flow steps on the retained
1,033,200-cell case took about 18.4–18.8 wall seconds, or roughly 10.2–10.4
wall-hours per simulated second before memory pressure. The large thermal cap
cannot accelerate that unfinished live-flow interval.

## Promoted stage policy

| Stage | Current lab screening cap | Status |
|---|---:|---|
| Fan ramp and initial live airflow | 0.0005 s | Retained. A matched 0.00075 s branch was 9.79% slower, changed velocity by 1.155754% RMS and gauge-adjusted pressure by 11.946965% RMS, and failed 10 of 33 fan operating-point equivalence checks. |
| Later live-airflow refresh | 0.001 s | Retained as an independent refresh cap. It does not alter unfinished initial airflow. |
| Implicit thermal-only, airflow held | 20 s | Current canonical screening cap; usable only after every freeze gate passes. |
| Exact retained pre-correction 22.5 mm case only | 24 s | Historical exploratory override. It was faster but failed the strict 0.05 K cell and 0.02 K component-average gates: 0.123260498 K and 0.054939449 K were observed. |

Two thermal-only outer energy-coupling passes remain an exploratory screening
candidate. There is no corrected full-rack two-versus-three A/B yet. Live flow
continues to use three outer and two pressure corrections. Quantitative claims
must use three thermal outer passes (via `THERMAL_ONLY_OUTER_CORRECTORS=3`) until
the matched corrected-rack A/B is complete.

## Runner contract hardened in current source

- Multirate exports now default to `--multirate`.
- Multirate export requires adaptive airflow refresh; the fixed-duration
  combination is rejected before any case is written.
- `run_cht.sh` and explicit parallel `run` mode fail before environment launch,
  locking, preparation, or case writes on a multirate export. They can no
  longer bypass stage-specific live-airflow caps.
- Fan-ramp intervals use fixed divisible steps, `writeControl timeStep`, and
  `maxDeltaT=deltaT`. Each ramp endpoint is checked symmetrically and receives
  a Courant postflight.
- Warm-start timing is replanned after the optional fan ramp. The request is
  split into independently restartable windows no wider than
  `airflow_checkpoint_interval`. Every window uses fixed divisible steps no
  larger than `THERMAL_WARM_START_MAX_DT`, requires and updates nonzero
  processor `uniform/time` on every rank, checks both endpoint directions, and
  receives its own Courant postflight and summary.
- A shared latest-time Courant validator rechecks every accepted nonzero warm
  restart before the runner can skip, advance, or finish, as well as every ramp
  and window solver checkpoint. Negative, `-inf`, `nan`, corrupt, and over-limit
  values fail closed.
- Interrupted fan-ramp checkpoints are validated at partial or final ramp time.
  Full fan scale and `.fan_ramp_complete` are recovered only after successful
  validation; a missing marker beyond the ramp endpoint fails closed, and
  failed fan publication cannot publish the marker or a summary.
- Every multirate stage requires
  `abs(actual-target) <= min(1e-9*max(1,abs(target)), 1e-8)` seconds; an
  undershoot or overshoot returns exit 5. The absolute cap prevents one full
  live-flow step from being accepted merely because absolute simulation time is
  large.
- Before a thermal-only stage the runner atomically journals
  `owed <start> <target>`. Only the validated stage can commit
  `active <checkpoint>`, after exact endpoint and source-restoration checks.
  No-progress interruption retries; any advanced but uncommitted checkpoint
  fails closed. A committed terminal stage stops at the exact requested end
  and validates airflow first on a later continuation.
- Initial-airflow acceptance is published marker-last and revoked marker-first.
  Missing accepted-reference time or per-rank velocity evidence forces full
  initial revalidation. Empty pending-state files cannot bypass initialization.
- `INT` and `TERM` are installed before preparation. Long preparation, solver,
  and post-processing children run in a tracked process group; signals during
  PID registration are deferred, and an exact-group watchdog escalates only
  after bounded grace. Repeated termination signals are ignored while cleanup
  is in progress. Fan files and all mutated production solver controls,
  including `deltaT`, are restored best-effort exactly once before exit 130/143;
  no post-signal stage work is allowed to continue.
- Function-object text output uses 17-digit precision. Binary fields are not
  changed by that text precision setting.

`tests/generated_runner_timestep_policy_test.ps1` executes the generated
endpoint predicate with exact, near-tolerance, short, and long cases in both
directions. It also proves explicit conventional mode exits 2 with zero
OpenFOAM-launcher calls and no run-lock or summary mutation. C++ exporter and
configuration tests bind the generated ramp, warm-start, stage, and terminal
fragments. This is software-policy evidence, not an OpenFOAM timestep or
physics result.

The complete current-source regression subsequently exited 0 with PASS in
516.584 s under affinity mask 3 (two logical processors) and BelowNormal
priority. Its 1,903-byte run record has SHA-256
`246620007dff4b6f7dea50b22bcc9bc987e359aa575310134a9930da672a6c8c`.
Stdout is 333,651 bytes / 4,115 lines / SHA-256
`03d781c9abe21ec3722e038672ef4c6587bed734cc896d97ddde3ed17a7bd81c`;
stderr is 2,946 bytes / 125 lines (100 non-empty) / SHA-256
`f4dba93c473013728126639c6b07f4c86196eeba63b61600c3abab37dc051d23`.
The stdout ends `All added-feature tests passed.` With the PASS result, stderr
is interpreted as expected successful unittest progress/summaries and explicit
dependency/platform skips. Exact process, timing, log, hash, and source
fingerprint metadata is in
`added_feature_regression_runner_state_gates_2026-08-27.run.json`. The earlier
397.559 s and 303.075 s runs remain historical evidence for older source
snapshots and are not current-source authority.

## Low-memory preparation

The failed, unprepared 19 mm case's theoretical inventory contains 109 selector
fields across 2,800,980 cells. Representing those logical selectors as full
scalar payloads accounts for 2,442,454,560 bytes (2.2747 GiB) before container
and OpenFOAM overhead. This is arithmetic inventory, not measured or predicted
RSS. The new case-local mapper stages those fields outside time zero,
lets `splitMeshRegions` read only physical fields, and reconstructs each region
selector through the exact `cellRegionAddressing` list.

Preparation reuse is content-bound to the root mesh, `regionProperties`,
immutable region points, addressing, and complete required inventory. Old
mapping PASS evidence is invalidated before materialization. Launcher/Python/
flock preflight, a nonblocking case lock, atomic audit publication, and retry
tests cover failed split, post-split failure, and topology/checkMesh failure.

The separate retained pre-correction 22.5 mm case equivalence evidence is
`openfoam_low_memory_selector_equivalence_2026-08-27.json` (SHA-256
`09fe90a02f040c25b84fbec914f7c0ea4ac287f6a245ad2a523ae927667f256d`).
It proves logical selector equivalence only on that 22.5 mm case. It does
**not** prove that the corrected 19 mm split fits the present WSL allocation;
the 2,442,454,560-byte figure above comes from the distinct 19 mm theoretical
inventory, not from this equivalence run.

## Solver provenance

The custom solver exposes an exclusive no-case runtime handshake before any
OpenFOAM argument, case, mesh, field, or time access. Generated multirate
runners resolve one absolute, non-symlink executable, require its exact ordered
attestation with no stderr before case locking, and reuse that path for every
custom-solver execution. The clean build wrapper builds into a disposable
application directory, attests before installation, deploys atomically, checks
byte equality and the final binary hash, attests again, and rolls back on final
failure.

The current repository-local project-source fingerprint is
`6f5b54fddb0218558dac8798915169133c66564c199e6c95778410f9a23c9ead`.
Its declared scope is only `Make/files`, `Make/options`, and
`semiFrozenChtMultiRegionFoam.C`; external OpenFOAM source, headers, compiler,
shared libraries, and runtime are not part of that digest. Focused non-WSL
coverage passed 29 policy/attestation tests with one Windows symlink-privilege
skip, plus staged build/rollback and generated-runner rejection tests. No real
WSL/OpenFOAM build or positive live/thermal microcase has run under this
attestation yet. The installed historical binary remains unqualified. The
repository's 1,838,877-byte `model.exe` (SHA-256
`335efd17d7f000e90d13b4a2594cfade4ab68bde524b2910f81c73df7b00eb12`)
belongs only to its post-audit source snapshot; no current-source `model.exe`
has been rebuilt or promoted.

## Release and resource gates

`model_release_readiness_20260827T070322145Z.json` is an expected FAIL: 0 of 9
model assumptions are verified. Open items are Rail 2 PDU/Meanwell/NI depth,
NI separator walls, storage-shelf construction, unmodeled obstructions, heat
load inventory, fan curves, and OpenFOAM material homogenization. A row closes
only when the ledger says `verified` and points to existing SHA-256-bound
evidence. Passing this gate would establish input traceability only; it would
not prove mesh, convergence, energy balance, or measurement agreement.

The final idle host-only resource evidence is the 3,494-byte
`openfoam_resource_gate_final_idle_20260827T095617107Z.json` (SHA-256
`33c4fb9525398af3c7ec53156fe214a422b3d0e67ca13e144d0c5fcbe5ca6d51`).
It exited 21 after finding no competing solver processes. Disk passed at
17,129,152,512 available bytes, but the first memory sample failed at
951,701,504 bytes versus the mandatory 5,368,709,120-byte floor. The requested
60-second sampling period therefore completed zero seconds with one sample,
and WSL was not queried. Historical WSL total memory is about 3.687 GiB, also
below that floor. The earlier 936,706,048-byte host-memory failure and
9,639,960,576-byte post-regression disk failure remain preserved as historical
resource snapshots, not the current gate. The corrected multi-million-cell
export/build/preparation/solve must not start until both host and WSL gates
pass; lowering either floor would discard the safety margin that the earlier
kernel-killed split demonstrated was necessary.

## What remains before a realistic result

1. Close or explicitly supersede the nine input-assumption rows with measured,
   hashed evidence.
2. Pass the host and WSL resource gates without killing user, Codex, Defender,
   or system processes.
3. Perform a clean resource-gated OpenFOAM build and runtime attestation,
   including the forbidden-mode negative microcase.
4. Export and prepare a corrected case through the low-memory path; record
   actual peak RSS and all-region mesh quality.
5. Run positive live and thermal microcases that prove fixed endpoints,
   Courant postflights, source work, coupled energy, and restart integrity.
6. Establish at least one air exchange and pass spatial flow, exterior mass,
   device direction/flow, fan-domain, and long-lag gates before freezing flow.
7. Complete corrected-rack two-versus-three thermal-outer A/B, 20 s timestep
   independence, gap-free first-law accounting, mesh sensitivity, and measured
   laboratory comparison.

Until those steps pass, there is no corrected flow field, thermally developed
temperature field, or industry-ready validation result to report.
