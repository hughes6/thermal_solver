# OpenFOAM archive completion record

Date: 2026-08-26

## Outcome

The explicitly authorized OpenFOAM archive transfer is complete. The private
repository `hughes6/thermal-sim-openfoam-archive`, release
`openfoam-checkpoints-2026-08-25`, now contains the 28 approved archive groups:

- 11 active-case checkpoint groups from 0.40 through 1.40 s;
- 13 older unsolved time-zero OpenFOAM cases;
- 4 controlled benchmark snapshots.

The release has 30 assets in total because the verified 0.20 and 0.30 s
checkpoint assets predated this transfer. All 30 release assets have unique
names and passed remote byte-count, `sha256:` digest, `uploaded` state, and
manifest-status checks with zero errors.

Remote `main`, the authoritative local archive clone, and its refreshed
tracking ref all resolve to commit
`82e25b584a48c9f4fe9f224c7e9a1d4cd2b13280`. The separate planning/verification
clone in `openfoam_archive_remote/` was clean and was fast-forwarded to the same
commit.

## Remote manifests

GitHub's contents API returned bytes identical to the authoritative local
files:

| Manifest | Rows relevant to the release | Bytes | SHA-256 |
|---|---:|---:|---|
| `checkpoints.csv` | 13 total; 11 newly authorized | 3,861 | `e351fb5b6ecef3bc118570a4cce4010a0005d2b40d5aaf9c17e4c2d7009c7357` |
| `manifests/openfoam-legacy-assets-2026-08-26.csv` | 17 | 9,701 | `ff03033aea229c5721932ee8b2d5296827e243c596d2c0238a729177a1396ec5` |

The manifests are the restoration authority for source file/byte/entry/link
counts, source fingerprints, archive sizes, archive SHA-256 values, and tar
listing hashes. Legacy `.tar.zst` payloads containing LX links must be restored
with a GNU tar/zstd path that preserves those links, followed by the same deep
inventory and fingerprint checks used during publication.

## Exact local deletion and preservation audit

Deletion occurred only after the corresponding remote asset and committed
manifest row passed their gates. The final audit found all 72 approved local
paths absent:

- 55 checkpoint directories (11 reconstructed plus four-rank groups);
- 13 old case roots;
- 4 benchmark snapshot roots.

Those sources total exactly 12,901,601,443 bytes (12.02 GiB). No archive
staging directory, deletion journal, recovery candidate, manual probe, or
temporary drive mapping remains.

The following excluded state remains present:

- times `0`, `1.5`, and `1.6000000000000001` in the reconstructed active case
  and all four processor cases (15 verified time paths);
- active `constant`, `system`, `postProcessing`, and `provenance` trees, with
  491, 157, 8,458, and 16 files respectively;
- 43 active root logs plus runner, lock, geometry, and airflow files;
- all five `thermal_dt_screen_*_case` benchmark roots;
- all 504 pre-existing validation files totaling 822,117,727 bytes, including the retained
  753,319,298-byte native-smoke `simulation.csv`;
- 11 completed checkpoint archive receipts and 110 OpenFOAM preparation
  markers. These are intentional audit/runner evidence, not stale payloads.

## Recovery incident and regression coverage

During the `pimple_outer2_auto_case` deletion transaction, Windows `fsutil`
could not query LX links whose quarantine paths exceeded `MAX_PATH`. The helper
failed closed: it did not upload a replacement or delete unverified content.
Independent inventory, raw Windows reparse inspection, WSL `readlink`, and a
full fingerprint comparison proved the quarantined tree exact: 3,526 entries,
2,668 files, 801,818,602 bytes, 850 directories, 8 links, and fingerprint
`821edd3bc8ca4562a85d65de27c273b10c24b4df967ee363968d1380cba51bfe`.

The publisher now leases a bounded, unused temporary drive alias only for a
long `fsutil` query and removes it in `finally`. The complete suite, including a
real greater-than-260-character WSL LX-link round trip, passed 171 assertions.
The recovered source was independently re-audited, then the ordinary archive
path was rerun and completed. Final helper SHA-256 values are:

- `tools/archive_openfoam_legacy_asset.ps1`:
  `cdb6590230a8aa813ffcf6963071026835ac8197191d573499f8f1205632817a`;
- `tests/archive_openfoam_legacy_asset_test.ps1`:
  `ff8abd5e64d6d3d51372e4fbc3b82e98d29f1930507f0691124e9ccb819544f3`.

## Interpretation

This completion record establishes archive integrity, recoverability checks,
and exact local cleanup. It does not change the scientific status of the CFD
case: the retained 1.600 s result is still an early transient with failed
airflow-freeze and thermal-soak acceptance gates, not an industry-ready thermal
validation.

## Independent post-completion re-verification

At `2026-08-26T22:20:49Z`, the read-only verifier
`tools/verify_openfoam_archive_remote.ps1` queried the private repository,
release, branch, contents, and paginated asset APIs again. It verified the 28
approved assets with zero byte-count, SHA-256-digest, upload-state, or manifest
errors. Their archive payloads total 4,530,300,140 bytes and represent exactly
12,901,601,443 source bytes.

Both contents-API manifest payloads remained byte-identical to the blobs at
remote `main` commit `82e25b584a48c9f4fe9f224c7e9a1d4cd2b13280`.
The machine-readable result is
`openfoam_archive_remote_reverification_2026-08-26.json` (SHA-256
`18e051888e25b24d28d1a7c7c431eaa0b8a96e2c2aa1cc820578099f4b727e9c`).
The verifier itself has SHA-256
`de93768a7efda9bbadbcd4c36c95e81a97cb6f2a91285f34f97baf66f82554cc`.

The same run re-audited all 72 approved local source paths as absent, all 11
checkpoint deletion receipts as complete, and the protected local boundary as
present. No second deletion pass was attempted. The current checkpoint helper
synthetic suite passed its lock, archive, rollback, resume, and exact-delete
tests; the current legacy helper suite passed 164 staging, pagination,
collision, manifest, recovery, and exact-deletion assertions.
