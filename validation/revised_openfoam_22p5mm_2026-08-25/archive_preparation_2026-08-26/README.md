# OpenFOAM Release-asset preparation

`tools/prepare_openfoam_release_archives.ps1` prepares a fixed, audited set of
28 local `.tar.zst` files for the existing **private** repository
`hughes6/thermal-sim-openfoam-archive` and release tag
`openfoam-checkpoints-2026-08-25`. It does not initialize Git, commit, push,
upload, or delete a source.

## Exact source boundary

The script requires the case parent to contain exactly the active case plus
these 13 non-active, time-zero exports:

- `lab_screening_export`
- `lab_screening_export_clean`
- `new_model_airflow_mapping_22p5mm`
- `new_model_airflow_mapping_25mm`
- `new_model_airflow_mapping_30mm`
- `new_model_airflow_mapping_40mm`
- `new_model_openfoam_export_test`
- `thermal_lab_corrected_curves`
- `thermal_lab_fan_axial`
- `thermal_lab_fan_axial_intersection`
- `thermal_lab_fan_baffles`
- `thermal_lab_fan_baffles_runner_v2`
- `thermal_lab_runner_v3`

It also requires exactly four benchmark snapshots:

- `decomposition_auto_case`
- `dt_baseline_0p0005_case`
- `dt_variant_0p00075_case`
- `pimple_outer2_auto_case`

Five later thermal-timestep branches may coexist under the same parent but are
explicitly preserved and excluded from every archive job:

- `thermal_dt_screen_20_case`
- `thermal_dt_screen_25_case`
- `thermal_dt_screen_30_case`
- `thermal_dt_screen_40_case`
- `thermal_dt_screen_60_case`

The active-case assets contain only the five aligned copies of each complete
checkpoint from exact time `0.40000000000000002` through
`1.3999999999999999`. Time `0`, `1.5`, and `1.6000000000000001` are explicitly
retained. The assets never include active `constant`, `system`, root logs,
`postProcessing`, or any project `validation` evidence.

The two auto-decomposition snapshots contain 16 known WSL relative-link
reparse points. Their exact paths and reparse payload hashes are allowlisted;
the links are archived as links and never followed. Every other reparse point
causes a failure.

## Private repository layout

The existing archive clone should contain:

```text
checkpoints.csv                         # existing checkpoint release index
manifests/
  openfoam-archive-manifest-2026-08-26.json
.archive-staging/                       # local only; ignored by Git
  assets/*.tar.zst
  receipts/*.json
```

The archive repository must ignore staging before Build is allowed:

```gitignore
/.archive-staging/
```

The generated manifest is deterministic for unchanged sources and assets. Each
asset record includes category, logical source, exact roots, source file count,
source bytes, entry/link counts, a deep source fingerprint, asset bytes and
SHA-256, tar member/listing hashes, and zstd/tar integrity status. No timestamp
is embedded, so an idempotent rerun compares cleanly.

## Commands

Plan is source-read-only and does not require the archive checkout:

```powershell
pwsh -NoLogo -NoProfile -File tools/prepare_openfoam_release_archives.ps1 -Mode Plan
```

Build requires a separately cloned checkout of the exact archive repository on
`main`:

```powershell
pwsh -NoLogo -NoProfile -File tools/prepare_openfoam_release_archives.ps1 `
  -Mode Build `
  -ArchiveRepoPath 'D:\thermal-sim-openfoam-archive'
```

Verify re-hashes all sources, tests every zstd stream and tar listing, checks
each receipt, and requires the manifest to match byte-for-byte:

```powershell
pwsh -NoLogo -NoProfile -File tools/prepare_openfoam_release_archives.ps1 `
  -Mode Verify `
  -ArchiveRepoPath 'D:\thermal-sim-openfoam-archive'
```

Build is idempotent: a matching asset and receipt are reused; a missing half of
the pair or any mismatch fails closed. A stale `.part` inside the ignored
staging directory may be rebuilt. Sources are deep-hashed before and after new
archive creation, so concurrent source changes abort preparation.

## Operational caveats

- The source set is about 12.02 GiB before compression. Use a staging drive
  with at least 14 GiB free; the script also enforces per-asset free-space
  headroom.
- Asset creation depends on Windows `bsdtar` with zstd support and `zstd.exe`
  for the independent integrity test.
- The repository-visibility requirement is documented and recorded in the
  manifest, but this local-only script deliberately performs no GitHub API
  call. Confirm the remote remains private before any upload.
- Upload, remote digest verification, manifest commit/push, and any later
  source deletion are separate, approval-gated operations.
