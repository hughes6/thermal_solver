# Persistent OpenFOAM runner quarantine — 2026-08-26

`validation/thermal_outer_microcase_2026-08-26/run_parallel.sh` was generated
before the current exporter added the required
`THERMAL_SIM_SEMIFROZEN_MODE_POLICY_V1` deployed-binary gate. Its previous
SHA-256 was
`0F3CA317A4BEEA4D091EF2B93C804D39C3BF8634527A4449EFDE46F9CF89A399`
(98,104 bytes). It must not be used with the installed solver, which is known
to be stale: SHA-256
`4DAD80E5F4C3B4A9A37599D06291DD8C93CDF9A49053C9A2582F90CD5A5C2E6D`,
required marker absent, historical isothermal-airflow string present.

The persistent runner is now hard-quarantined at lines 4–5. It emits a clear
regeneration instruction and exits 14 before `invoked_script` resolution at
line 7, OpenFOAM environment setup at line 68, or case-lock creation at line
72. The quarantined runner is 98,374 bytes with SHA-256
`2AF8FACAC3410372E2A9BF128F4C33AA65FFE2990943B18FB48C3D438EEFE79B`.
The source exporter and its tests were not changed, and neither `wmake` nor an
OpenFOAM solver was invoked.

Read-only verification used Git Bash to parse the complete script and then
attempt a normal two-process multirate invocation. A recursive case fingerprint
included every directory and, for every file, its relative path, byte count,
UTC modification ticks, and SHA-256.

```text
BashSyntaxExit=0
AttemptExit=14
StdoutBytes=0
BeforeEntries=90
AfterEntries=90
BeforeTreeSha256=483D18B713D8FBB43CABC1C2DF105D639F07FFC15FDE3ED838C1A6BC7C5A315F
AfterTreeSha256=483D18B713D8FBB43CABC1C2DF105D639F07FFC15FDE3ED838C1A6BC7C5A315F
CaseTreeUnchanged=True
```

The only accepted recovery is to regenerate this runner with the current
exporter after a newly built solver has independently passed the current
mode-policy provenance check. Removing or bypassing the quarantine is not an
accepted recovery.
