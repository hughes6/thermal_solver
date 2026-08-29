# Low-memory OpenFOAM region preparation

Status: implemented and verified without starting WSL, compiling OpenFOAM,
exporting the corrected full rack, meshing, or solving. This removes one proven
source of preparation memory pressure. It is **not** evidence that the remaining
19 mm mesh split fits the current WSL allocation.

## Retained 19 mm failure inventory

The retained preparation log has SHA-256
`e21ef1831665f19f89794873ba8f60398990651ba8a7e933af3ecb5f4a4fce4d`.
Its single `Reading volScalarField` inventory names `cellToRegion` plus 109
unique selector masks. Its 14-row region table totals 2,800,980 cells, and its
OpenFOAM architecture reports 64-bit scalars. The selector internal-field
payload named in that log is therefore:

`109 * 2,800,980 * 8 = 2,442,454,560 bytes = 2.274713069 GiB`

That number is the scalar payload presented to `splitMeshRegions`, not a
measured or predicted RSS saving. The retained failure report records the
kernel-killed process at approximately 3,490,724 kB anonymous RSS. Mesh,
interfaces, addressing, boundary fields, allocator overhead, and temporary
output structures still consume memory after selector staging.

## Implemented workflow

`tools/openfoam_stream_region_selectors.py` discovers every generated
`fieldToCell` dictionary and the target region in `prepare_regions.sh`. For
every component-region export, the exporter copies this mapper and the wrapper
into the case before emitting the runners. Both `run_cht.sh` and
`run_parallel.sh` enter through that case-local wrapper; the component form of
`prepare_regions.sh` refuses direct invocation without the wrapper handoff. The
wrapper then:

1. atomically moves only those exact generated selector sources from `0` to
   `.openfoam_selector_fields` and records byte counts and SHA-256 hashes;
2. invokes the same generated launcher contract,
   `"$foam_launcher" splitMeshRegions -case "$case_dir" -cellZonesOnly
   -overwrite`, with selector fields no longer visible to the utility;
3. atomically records content-bound split state only after every region-mesh
   output exists; it binds `regionProperties`, the root mesh, immutable region
   points, and region-addressing files by SHA-256 while inventorying mutable
   topology files that a later `createBaffles` may rewrite;
4. follows each region's exact `cellRegionAddressing` list and streams one
   logical selector at a time into `0/<region>`;
5. requires every selected root cell to appear in the declared region, rejects
   non-logical values, missing/duplicate sources, unsafe names, malformed or
   non-monotonic addressing, count disagreement, and lossy projection;
6. removes any earlier PASS materialization audit before rebuilding, publishes
   the new audit atomically only after every derived field succeeds, and then
   re-verifies every source, dictionary, addressing, output hash, cell count,
   and selected-cell count; and
7. delegates to the exported `prepare_regions.sh`, which verifies the split
   state and materialization audit, always reruns idempotent `topoSet`, runs
   all-region `checkMesh` and determinant policy, and creates the prepared
   marker only after every gate passes.

The wrapper preflights `python3`, `flock`, and the exact launcher before selector
staging, then holds a nonblocking case preparation lock throughout the
lifecycle. An interrupted multi-field stage is resumable because each selector
exists in exactly one of the root or staging locations. A rerun hashes all
staged bytes against the prior audit. Both-present, both-missing, or
changed-source states fail closed. A failed split has no valid state and is
overwritten on retry. A failure after a successful split leaves the split state
reusable but no prepared marker; retry rebuilds all derived selectors,
re-verifies their complete audit, and reruns every topology and mesh gate.

An explicit exporter overwrite removes exactly the prior selector staging tree,
split-state directory, materialization audit, low-memory split log, and the
regenerated region mesh/time-zero directories. Helper assets are loaded and
identity-checked before that cleanup, so a missing helper cannot erase a
retained case.

## Equivalence evidence

The create-once audit is
`openfoam_low_memory_selector_equivalence_2026-08-27.json` (70,589 bytes,
SHA-256 `09fe90a02f040c25b84fbec914f7c0ea4ac287f6a245ad2a523ae927667f256d`).
It read, but did not mutate, the retained 1,033,200-cell case whose geometry
hash is
`5a1893f92d162cfe17d66240d34770351302a2eb961a332be9b2092d1a9981f1`.

- 109 of 109 selectors were logically identical, cell for cell, to the fields
  previously produced by OpenFOAM's normal `splitMeshRegions` path.
- All 13 referenced regions passed; 43,603 selected cells were reconciled.
- The ordered campaign logical SHA-256 is
  `7ad52c1c6d02f311fedd28b842c1937f00e6773ad3e8ff592c16a9cec7fea1b0`.
- The audit embeds every source, addressing, existing-field, and logical-field
  SHA-256 plus the independently parsed 19 mm OOM inventory.

The generated fields use compact ASCII `0`/`1` representation, so their raw
bytes intentionally differ from OpenFOAM's binary scalar output. The cell
values and selected sets are identical.

## Lightweight verification

From the project root:

```powershell
python -m py_compile tools\openfoam_stream_region_selectors.py `
  tests\openfoam_stream_region_selectors_test.py
python -m unittest tests.openfoam_stream_region_selectors_test -v
& 'C:\Program Files\Git\bin\bash.exe' -n `
  tools/prepare_openfoam_regions_low_memory.sh
```

Twelve focused tests pass. They cover ASCII and binary addressing, exact projection,
selected-cell conservation, lossy projection rejection without overwriting
prior evidence, ambiguous-state rejection before mutation, interrupted-stage
resume, staged-source hash integrity, deterministic materialization, create-once
audit refusal, stale root-mesh/addressing/output inventory rejection, deleted or
changed materialized-output/dictionary rejection, legacy/current generated
command discovery, launcher preflight before case mutation, and a live Git-Bash
failure/recovery fake showing that a post-split failure resumes without a second
split. The compiled exporter test verifies explicit-overwrite cleanup,
case-local helper generation, fail-closed direct preparation, and normal serial
and parallel runner wiring. All four generated shell scripts pass `bash -n`, and
the actual C++-generated case discovers all four expected selectors.

## Gated production invocation

First obtain a passing, unique host/WSL resource-gate record and a fresh
corrected export. Do not use the wrapper to bypass either gate. The generated
serial and parallel runners invoke case-local low-memory preparation
automatically. For preparation-only diagnosis from the already initialized
OpenFOAM environment, run:

```bash
bash "/mnt/c/OpenFOAM/thermal_sim_newmodel/new_model_updated_corrected_20260827/prepare_regions_low_memory.sh" \
  "/mnt/c/OpenFOAM/thermal_sim_newmodel/new_model_updated_corrected_20260827"
```

Required acceptance evidence remains: split exit zero, selector mapping audit
PASS, generated `topoSet` completion, all-region `checkMesh` policy PASS, region
and cell-count reconciliation, heat-source watt reconciliation, connectivity,
and the later solver/physics gates. If the split still reaches the resource
limit after removing selector fields, the next action is a measured residual
peak investigation or a larger WSL/host allocation—not coarsening the canonical
19 mm geometry under this workflow.

## Implementation hashes

| File | Bytes | SHA-256 |
|---|---:|---|
| `tools/openfoam_stream_region_selectors.py` | 43,969 | `87a6ab9578d82de4a62dd5b651f0b7d895bfedec845da6dccd71dd11569d39b2` |
| `tools/prepare_openfoam_regions_low_memory.sh` | 2,936 | `62bc447903ff0363ee462ecbb1e9532b0e55995c89c8965f70fe5815717443e6` |
| `tests/openfoam_stream_region_selectors_test.py` | 24,011 | `7e7bccabcbee4ade133389b8dc73668a3d3ff5523e04d97619bda14252ed5bfa` |
