# Custom OpenFOAM solver runtime attestation — 2026-08-27

## Outcome

The project now has a fail-closed, repository-source-bound runtime handshake for
`semiFrozenChtMultiRegionFoam`, a staged clean-build/deployment wrapper, and a
small negative runtime microcase. Generated multirate runners require the
handshake before acquiring the case lock or changing case state, and every
subsequent custom-solver execution uses the same attested absolute path.

No OpenFOAM compilation, WSL launch, or CFD solve was performed for this work.
The currently installed WSL solver therefore remains unqualified and was not
executed.

## Repository-local identity

The canonical repository-local project-source SHA-256 is:

```text
6f5b54fddb0218558dac8798915169133c66564c199e6c95778410f9a23c9ead
```

Algorithm: `thermal-sim-repo-local-solver-source-v1`.

The path-bound digest covers exactly:

- `openfoam_semifrozen_solver/Make/files`
- `openfoam_semifrozen_solver/Make/options`
- `openfoam_semifrozen_solver/semiFrozenChtMultiRegionFoam.C`

The build wrapper embeds this digest plus `FOAM_API`, `WM_PROJECT_VERSION`, and
`WM_OPTIONS` in a generated header in a disposable build tree. The exclusive
no-case invocation
`semiFrozenChtMultiRegionFoam --thermal-sim-attest` must return one exact,
ordered line and no stderr. Mixed solver/attestation arguments fail.

This is deliberately named a *repository-local project-source* fingerprint.
It is not a full reproducible-build identity: the two OpenFOAM source files
referenced through `$(FOAM_SOLVERS)`, included OpenFOAM headers, compiler,
linked shared libraries, and patched OpenFOAM trees are not cryptographically
hashed. The declared OpenFOAM identity strings distinguish normal build
variants but cannot prove those external bytes. Each successful attestation
evidence file records the resulting binary byte count and SHA-256, which pins
that artifact after the fact without reconstructing all inputs.

## Fail-closed deployment

`tools/build_openfoam_semifrozen_solver.sh` requires an already initialized
OpenFOAM environment and never starts WSL. It:

1. snapshots the three repository-local inputs into a short disposable path;
2. recomputes their digest before building;
3. runs `wclean` and `wmake` with a disposable staging app-bin;
4. rejects missing, non-executable, or symlinked staged targets;
5. runs the runtime handshake (and optional negative microcase) before install;
6. records the staged binary SHA-256;
7. installs through a same-directory temporary rename only after preinstall
   attestation, then verifies byte equality and the same binary SHA-256;
8. re-attests the installed path and restores the exact prior bytes if final
   verification or evidence creation fails.

The evidence destination is create-only. Fake-command integration tests cover
build failure, preinstall-attestation failure, final-attestation failure with
rollback, and successful byte-identical deployment.

## Generated runner binding

For multirate cases, the generated `run_parallel.sh` now:

- initializes the requested OpenFOAM environment first;
- resolves `semiFrozenChtMultiRegionFoam`, rejects a supplied symlink, and
  normalizes it to an absolute regular executable path;
- checks `FOAM_API`, `WM_PROJECT_VERSION`, and `WM_OPTIONS`;
- invokes the exclusive runtime handshake and exact-compares source, policy,
  and build identity before case locking;
- treats even newline-only stderr as a failure;
- removes the exact temporary stderr file on pass and through an exit trap on
  failure, before any case lock;
- uses the attested absolute path for the fan ramp, flow/thermal stages,
  spatial convergence post-processing, refresh reporting, and final reports.

A stale binary that only contains the old policy marker no longer passes.

## Negative runtime microcase

The preserved template is
`validation/openfoam_solver_attestation_microcase_template_2026-08-27`:

- 20 cells and 84 faces;
- 73 files, 17 directories, 143,421 bytes;
- exactly one top-level numeric time directory, `0`;
- template tree SHA-256
  `da8f17c02a88f800c82ca9f9ae3cf0b6bef6f71521cf0f9980469de31281d0f2`.

The attester copies this template to a temporary directory, forces
`startTime = endTime = 0`, injects the forbidden `isothermalAirflow` request,
and requires the solver to fail with the exact policy diagnostic. It rejects
any `Time =` log line, any copied-tree mutation (including `0/T`), and any
added/removed numeric time directory. Signed and scientific OpenFOAM time names
such as `+1e-05` are recognized. The source template is fingerprinted before
and after and is never used as the working case.

This is a negative startup-policy proof only. It does not exercise a positive
live or thermal timestep, pressure correction, fan sources, coupled energy,
mode transitions, convergence, numerical accuracy, or solver physics.

## Non-WSL verification

The native test suite covers exact/malformed handshakes, stale marker-only
binaries, source/binary/build-identity mismatches, binary/source changes during
the handshake, create-only evidence, complete-tree microcase immutability,
scientific time names, time-zero mutation, staged build/rollback behavior, and
the current exporter source pin. The generated-runner integration uses fake
launchers and verifies rejection occurs before case mutation or lock creation,
including newline-only stderr and temporary-file cleanup.

Recorded transcript:
`openfoam_solver_runtime_attestation_nonwsl_2026-08-27.stdout.log` (54 lines,
7,057 bytes, SHA-256
`23cb032681cc714bcb783c377c5510d4db6d2b2e2813b8c508fb444de19ebd1b`).
It records 29 passing Python tests with one privilege-dependent skip, a passing
Git Bash syntax check, the passing native exporter test, and passing generated
solver-provenance and thermal-outer runner tests. The provenance runner tested
missing, marker-only stale, wrong-source, and newline-stderr binaries; all four
failed at exit 14 before case writes.

Windows denied one test's attempt to create a symlink (`WinError 1314`), so
that dynamic test is skipped on this host; original-path symlink rejection is
still implemented and statically covered. An early ambiguous `bash -n`
invocation selected the Windows WSL shim and was denied before any WSL process
launched. All recorded shell syntax checks use Git Bash explicitly.

## Required resource-gated follow-up

When the host and WSL resource gates pass, run one clean build with immutable
evidence and the real negative microcase:

```bash
bash tools/build_openfoam_semifrozen_solver.sh \
  --evidence validation/openfoam_solver_runtime_attestation_<timestamp>.json \
  --negative-mode-case validation/openfoam_solver_attestation_microcase_template_2026-08-27
```

Only that future run can prove the actual OpenFOAM binary starts and rejects the
forbidden mode. A separate prepared positive microcase is still required to
exercise at least one live-flow and one thermal-only timestep before claiming
runtime or physics validation.
