# OpenFOAM archive and disk-cleanup plan

Date: 2026-08-26

> **Historical plan—completed.** The user subsequently authorized this exact
> 28-group transfer. All Release assets, remote byte counts, SHA-256 values,
> and the committed manifest were verified before only the exact source groups
> were deleted. See `OPENFOAM_ARCHIVE_COMPLETION_2026-08-26.md` for the final
> remote/local ledger. Pending-transfer language below records the pre-approval
> state and is not current status.

## Completed safe cleanup

Only verified abandoned Visual Studio installer/extraction data was removed
from `C:\Users\hconn\AppData\Local\Temp`:

- two installer payload directories: 6.900 GiB;
- 37 signature-identified Visual Studio Setup extraction directories:
  4.536 GiB.

Total recovered: 11.436 GiB. Free space on `C:` increased from about 2.71 GiB
to 14.16 GiB. The recently written `DiagOutputDir` was retained. No OpenFOAM
case, restart, solver log, post-processing report, or validation artifact was
removed.

## Existing remote archive

The private repository already exists at
`https://github.com/hughes6/thermal-sim-openfoam-archive`. Its release
`openfoam-checkpoints-2026-08-25` already contains verified 0.20 s and 0.30 s
checkpoint assets and a committed restore manifest.

The local clone used for planning is `openfoam_archive_remote/`. It remained
clean and byte-identical to remote `main`; the attempted transfer did not begin.

## Exact planned transfer

`tools/prepare_openfoam_release_archives.ps1 -Mode Plan` passed with this
fixed allowlist:

- 11 complete active-case checkpoint groups from 0.40 through 1.40 s, each
  containing the reconstructed directory and matching `processor0` through
  `processor3` directories;
- 13 older unsolved time-zero OpenFOAM export cases;
- 4 controlled benchmark scratch cases.

Totals: 28 compressed Release assets, 15,852 source files, and
12,901,601,443 source bytes (12.02 GiB before compression). The planning
script has SHA-256
`AB6BC79D0B4DF756AC83F0D22813AF4B61BE899FB1D490FACB880EE34C7BAF89`.
It is local-only: it does not initialize Git, commit, push, upload, or delete
OpenFOAM sources.

## Explicit exclusions

The archive/deletion allowlist excludes and must preserve:

- active-case initial time `0`;
- active-case restarts `1.5` and `1.6000000000000001` in the reconstructed
  case and all four processor directories;
- active-case `constant`, `system`, `provenance`, logs, `postProcessing`, and
  runner/lock state;
- all compact validation evidence in this project.

## Pending authorization and deletion gate

No planned asset has been uploaded and no planned OpenFOAM source has been
deleted. The data exposes internal research-lab geometry and equipment details,
so the transfer requires explicit sensitive-data authorization for the exact
private GitHub destination.

After that authorization, a local source may be deleted only after the remote
asset byte count and SHA-256 match, the corresponding manifest is committed and
read back from remote `main`, and the source fingerprint is unchanged. Any
failure retains the local source.
