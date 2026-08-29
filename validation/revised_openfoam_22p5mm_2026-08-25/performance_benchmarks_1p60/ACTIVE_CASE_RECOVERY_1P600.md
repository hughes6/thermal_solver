# Active-case recovery after rejected timestep benchmark

## Finding

The active case's accepted reconstructed and four processor checkpoints ended
at `1.6000000000000001`, but a rejected timestep benchmark had left mutable
run state newer than that checkpoint:

- `system/controlDict` had `startTime 1.6100000000000001`,
  `deltaT 0.00075000000000000002`, and `maxDeltaT 1`; its SHA-256 was
  `49DA6111080C5DC6FFE7E4E4DFDAB172D73156D3065553B5D7AA41F6AFD1EDDF`.
- `run_summary.log` ended with the rejected `requestedEnd=1.61`,
  `warmStartMaxDt=0.00075` attempt; its pre-recovery SHA-256 was
  `DA7D9DAB1BC5900EE63C9232F0A02438CE31C8D65A4CD7B57BAD8423595BE730`.
- 196 post-processing function-object directories were named
  `1.6100000000000001`; they contained 196 files and 36,868 logical bytes.

No reconstructed or processor field directory newer than 1.600 s existed.
The finding was therefore control/report contamination, not loss or
replacement of the accepted 1.600 s fields.

## Preservation

Before cleanup, the rejected control dictionary, pre-recovery run summary, and
all 196 report files were copied under
`active_case_rejected_dt_1p610_contamination/`. Its
`artifact_manifest.csv` contains 198 file rows and has SHA-256
`816C4046E24FF263345619C9855E098196415B36BA5A934CE042DCD44E1C3D72`.
Every row was compared with its source immediately before cleanup: 198 passed,
zero were missing, and zero hashes mismatched.

The run summary remains in the active case as historical provenance. The
rejected attempt line is not evidence of a valid endpoint; it records a branch
that wrote no processor or reconstructed 1.610 s checkpoint. A subsequent
`case_recovery` line records the accepted checkpoint, restored controls,
preservation-manifest hash, and exact cleanup scope. The installed summary and
retained `active_case_recovered_run_summary.log` have SHA-256
`249AE497C5590B696B98DE26C98CCB9F51A99A91BDCC735D768FDECBEB1E930D`.

## Recovery

The active `system/controlDict` was restored to the common complete checkpoint
and the accepted live-flow cap:

```text
startTime       1.6000000000000001;
deltaT          0.00050000000000000001;
maxDeltaT       0.00050000000000000001;
```

The installed dictionary matches the retained
`active_case_restored_controlDict` byte-for-byte; SHA-256 is
`29D2941C87E5C40854742FD8942B59B0786D1442021A3D777633AE675B5F7C97`.
Only the 196 preserved post-processing time directories were then removed from
the active case, and a post-cleanup search found zero remaining 1.610 report
directories. No accepted field directory was in the removal target set.

The new file-content guard then found one remaining y+ report stored under the
1.600 directory whose first-column samples were actually from 1.610 s. It was
preserved as `late_detected_yPlus_1p600_file_with_1p610_samples.dat` (SHA-256
`A9B043B2148F02D15EB9747321368A46A244E161B6ED176287C0BD2B9CDC4127`)
before removal. Its one-row supplemental manifest hashes to
`9635769FA50318017CF78436883E3ED362D428920C8EA2EF745E444BDE12C0A7`.
A full follow-up lineage scan checked 92 required processor fields and found
zero future directories, zero future report files, and zero failures.

The active launcher now includes the regression-tested fail-closed restart
preflight described in `ACTIVE_CASE_LAUNCHER_HARDENING.md`; installed SHA-256
is `E47C392EA50572DCA29C48681EC7D3457BAF871B5D22CDC8D1B229B9ADE5D7CD`.
This repairs the known control/report lineage hazard. It does not change the
separate memory-validity and physics-convergence gates for continuation.
