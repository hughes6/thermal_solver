# Portable semi-frozen solver build

Multirate OpenFOAM exports carry a complete, case-bound solver build bundle.
It contains the semi-frozen solver source, its `Make` inputs, the attestation
tool, and the clean build wrapper. The runner verifies the installed binary
against the SHA-256 calculated from this case's bundled source.

In WSL with the project’s v2606 installation, paste this from the case folder:

```bash
source "${THERMAL_SIM_OPENFOAM_BASHRC:-$HOME/OpenFOAM/OpenFOAM-v2606/etc/bashrc}"
bash ./build_semifrozen_solver.sh
THERMAL_SOLVER_OPENFOAM_ENV_READY=1 OPENFOAM_LAUNCHER=env \
  bash ./run_parallel.sh 4 --multirate 1
```

The build command first verifies any installed `semiFrozenChtMultiRegionFoam`
against this case bundle and the active OpenFOAM identity. A compatible solver
prints `build skipped`; a missing or stale one is clean-built and installed in
`FOAM_USER_APPBIN`. Before installation, it verifies the
compiled program's no-case attestation; after installation, it verifies the
installed bytes and runtime identity again. The generated runner then checks
the same identity before acquiring a case lock or changing case state.

The builder creates a unique create-only JSON attestation under `provenance/`
automatically. If the bundled source changes after a build, rebuild the solver
before running the case.
