# Portable semi-frozen solver build

Multirate OpenFOAM exports now carry a complete, case-bound solver build bundle.
It contains the semi-frozen solver source, its `Make` inputs, the attestation
tool, and the clean build wrapper. The bundle's source fingerprint is pinned to
the generated `run_parallel.sh` contract.

On a different machine, copy the exported case directory, start that machine's
supported OpenFOAM environment, then run:

```bash
bash ./build_semifrozen_solver.sh \
  --evidence ./provenance/semifrozen_solver_build_attestation.json
bash ./run_parallel.sh 2 --multirate 1
```

The build is clean and installs `semiFrozenChtMultiRegionFoam` into that
environment's `FOAM_USER_APPBIN`. Before installation, it verifies the
compiled program's no-case attestation; after installation, it verifies the
installed bytes and runtime identity again. The generated runner then checks
the same identity before acquiring a case lock or changing case state.

The evidence path is create-only. Choose a new evidence filename for a new
build attempt. A source mismatch means the exported bundle was altered or the
wrong case/solver was paired; do not bypass it by editing the runner pin.
