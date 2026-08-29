# Active-case launcher hardening

Date: 2026-08-26

## Scope

The external active launcher was first copied byte-for-byte into the validation
evidence directory. Only the warm-start preflight function and its two
exporter-defined call sites were inserted, verified, and then installed back
into the active case.

- External source: `C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_cases\new_model_updated_openfoam_export_test\run_parallel.sh`
- Install-ready copy: `C:\Users\hconn\Downloads\Thermal Sim\v2.2\validation\revised_openfoam_22p5mm_2026-08-25\performance_benchmarks_1p60\active_case_run_parallel_hardened_preflight.sh`
- Exporter source: `C:\Users\hconn\Downloads\Thermal Sim\v2.2\src\openfoam_exporter.hpp`

## Recorded hashes

| Artifact | SHA-256 |
|---|---|
| External active launcher before hardening; retained as `active_case_run_parallel_pre_hardening.sh` | `546912C9237604852C4E6800A5349B53125F43B84352F9EF233C030F592F5D6B` |
| Hardened validation copy | `E47C392EA50572DCA29C48681EC7D3457BAF871B5D22CDC8D1B229B9ADE5D7CD` |
| External active launcher after installation | `E47C392EA50572DCA29C48681EC7D3457BAF871B5D22CDC8D1B229B9ADE5D7CD` |
| `src/openfoam_exporter.hpp` used for extraction | `7CCA3D3D82B4C44B232F73C3457EF8EAE771483F2D07ED7B131D91364425E257` |
| Exporter-derived preflight function plus early call | `776E76E4021B9354AACD672297B5F24CC90E1E4424C33E6C9B21A91FB095FB02` |
| Exporter-derived post-decomposition call | `76A2BE2696836299901D2139C3CB976AEB73A4982919FB5240E0C46C7726E2E6` |

## Verification

- `bash -n active_case_run_parallel_hardened_preflight.sh`: exit `0`.
- Structural `git diff --no-index --numstat`: `120` inserted lines, `0` deleted lines.
- Diff hunks: after original line 127 (`+116` lines) and after original line 220 (`+4` lines).
- The exact exporter-derived early block occurs once; the exact post-decomposition block occurs once.
- Removing those two exact blocks from the hardened copy produces 90,077 bytes and SHA-256 `546912C9237604852C4E6800A5349B53125F43B84352F9EF233C030F592F5D6B`, byte-identical to the external active launcher.
- Hardened copy format: 96,763 bytes, 1,454 LF-terminated lines, 0 CRLF line endings.

This establishes that all active case-specific launcher content outside the two intended insertions is preserved byte-for-byte.

The installed guard runs read-only before decomposition and again afterward.
For warm starts it rejects a future active `startTime`, future numeric
post-processing directories, and future first-column timestamps in `.dat` or
`.csv` reports relative to the latest strict all-field checkpoint common to
every contiguous processor directory. It uses a `1e-9` relative time tolerance,
does not delete evidence, and exits with quarantine/recovery guidance before a
solver stage can start. Exporter, generated-script syntax, and behavioral WSL
checks passed before installation.

The initial case-wide report scan found one additional rejected-branch y+
file whose directory was 1.600 s but whose first-column samples were 1.610 s.
That file was hash-preserved and removed; the subsequent scan checked 92
required restart fields and found zero future directories, zero future report
files, and zero failures. The complete report scan took 2m29.657s on the NTFS
case. The early guard performs that scan once; the post-decomposition guard
rechecks processor completeness without repeating the unchanged report scan.

## Preserved final verification evidence

| Artifact | Result | SHA-256 |
|---|---|---|
| `../../restart_preflight_final_targeted_tests_2026-08-26.stdout.log` | Final affected-test rebuild and execution: `openfoam_export_test` and `model_config_test` passed | `55AB37250D52FF4ED27EC32EBA4505856218674BB7F4A78C09B63E0099F87DB8` |
| `active_case_lineage_scan_1p600.sh` | Read-only full-case restart/report-lineage scanner | `7389795B6C9A562A2E7AE4B14EE317D29195A51DAAC43E2959BEAA1EC93FB717` |
| `active_case_lineage_scan_1p600.stdout.log` | Checkpoint 1.600 s, 4 ranks, 92 required fields, 0 future directories, 0 future report files, 0 failures | `2610E04E20B1B0AD76D452DEA795DBA6BB150BAAA6D666BE3C384E26CC070F02` |
