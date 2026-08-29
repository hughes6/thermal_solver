# Three-rank memory-mitigation benchmark

## Decision

The three-rank partition is geometrically valid and better balanced than the
four-rank alternatives, but three MPI ranks do **not** reduce the live memory
footprint enough on this host. Live samples crossed the resource-abort criteria
used during the run before its first step completed, and it was intentionally
stopped after one complete step. Treat this as a resource-invalid timing experiment, not a
physics-equivalence result. Do not change the generated four-rank/pinned
decomposition or use three ranks as a claimed low-memory fallback.

## Controlled setup and identity

The isolated case was created at
`C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_benchmark_snapshots\decomposition_auto_3rank_case`
from the verified auto-decomposition case. Only `constant/`, `system/`, the
reconstructed `1.6000000000000001` fields, required scripts/reports, and three
preparation markers were physically copied. Old processor directories,
post-processing, later times, logs, case files, summaries, and lock files were
excluded.

`three_rank_clone_identity.csv` compares all 731 copied files: 731 pass, zero
missing, zero mismatches; manifest SHA-256 is
`D601FBA3A6E5BEE3ADB740FBC445BEEE536BD2B5A7331AE6EE462A71453AD6BA`.
After decomposition, `three_rank_postdecompose_start_identity.csv` confirms all
74 reconstructed start fields still match the source; its SHA-256 is
`5CABC7B087C682F9BEB3682BBAA2493BE32F9F05CC06D304215F0380B5B0AAD7`.
The 157-file system comparison has 156 identical files and one intended
difference, root `decomposeParDict` process count 4 to 3; the manifest SHA-256
is `D5BD0DF440F9F8F34566AFE1C34DE1692EFD046A5AFCE3135CAC03BCA8B3D7F7`.

The benchmark retained 3x2 PIMPLE, the 0.0005 s cap, and auto placement of
`chtCoupledInterfaces`. The copied three-rank dictionary hashes to
`E27A65169FD79B3F4B9E9DAC22EABCDB34ED34D7505E9528EFF0A85FEE975161`.

## Partition and restart preflight

| Rank | Fluid cells | Solid cells | Total cells | Difference from ideal |
|---:|---:|---:|---:|---:|
| 0 | 283,460 | 107,812 | 391,272 | +13.61% |
| 1 | 400,425 | 0 | 400,425 | +16.27% |
| 2 | 241,503 | 0 | 241,503 | -29.88% |

The total is exactly 1,033,200 cells: 925,388 fluid and 107,812 solid. Every
fluid rank is nonempty, all 13 solids remain singly owned, the heaviest rank is
400,425 cells (below the known-working pinned maximum of 429,027), maximum over
ideal is 1.1627, and maximum/minimum is 1.658. The decomposition audit passed.

During the live preflight, fresh WSL-native restart links were checked on all
three ranks. The intended targets were:

```text
uniform       -> ../../1.6000000000000001/uniform
fluid/uniform -> ../../../1.6000000000000001/fluid/uniform
```

The live check recorded 74 nonempty entries in each processor start time: 36
files resolved through the two `uniform` directory links and 38 were physical
field files. That per-rank link/count observation was not preserved in a
standalone manifest before the scratch case was deleted; the retained 74-row
post-decomposition identity manifest verifies the reconstructed root start
fields, not the individual processor link targets. `decompose_3rank.stdout.log`
is copied here with SHA-256
`532A18AC3199E381B668011D5E60469DBD0857771FE7866C019E613256D5E7FA`.

## Resource gate and observed run

Before launch, the settled sample showed 2,540 MiB available, 48.31 pages/s,
no WSL allocation, and zero Fluent processes. The abort criterion used during
the run was available memory below 1 GiB or paging above 100 pages/s. Two live
samples then showed:

| Stage | Available MiB | Pages/s | WSL private GiB | Completed steps |
|---|---:|---:|---:|---:|
| startup | 409 | 1,467.44 | 3.303 | 0 |
| first step in progress | 463 | 322.75 | 3.885 | 0 |

The run therefore failed its resource-validity gate. It was interrupted after
one complete 0.0005 s step and after the second step began. The complete first
step performed the expected six `p_rgh` solves and three solves of each velocity
component, reached maximum Courant 2.32215, and had no true fatal signature.
It required 60.93 execution-seconds and 84 wall-seconds, versus 42.41 and 52 s
for the four-rank short benchmark's first step. No 1.610 endpoint was written,
so no field/fan equivalence claim is made.

The copied solver log is `three_rank_resource_aborted.stdout.log`, SHA-256
`BC7CD2F877C56AF7C34EEBFE7AA37EFA3B49727F05ABF013EC0AF234E6B0303E`.
`three_rank_resource_samples.csv` preserves six timestamped pre-run, live, and
post-abort counter/process samples. After the solver and wrapper abort settled,
the observation recovered to 2,850 MiB available and 30.96 pages/s; WSL still
reported 0.251 GiB working set and 0.567 GiB private allocation. This supports
the resource classification without asserting a solver-physics failure.

## Interpretation

The three-rank point samples reached 3.885 GiB WSL private memory, similar to
the 3.88 GiB point sampled during the four-rank attempt. These sparse samples
were not a peak monitor and the runs did not have matched host-state controls,
so they do not establish which allocation dominates memory. They do establish
that reducing ranks alone did not pass the memory gate on this host. A fair
long continuation needs more memory headroom or a separately validated
lower-memory formulation. The valid partition result may be reused on a
higher-memory host, but it is not a performance pass here.

After the 11 evidence artifacts were hashed in
`three_rank_evidence_manifest.csv`, the isolated scratch case was deleted:
2,071 files and 798,457,589 logical bytes, recovering 804,163,584 bytes. No
simulation endpoint was lost. The clone can be recreated from the retained
source, identity manifests, three-rank dictionary, and commands, but the
deleted copy itself is not locally recoverable without repeating those steps.
