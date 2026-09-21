# Cold-flow seed workflow

This is an opt-in workflow for expensive campaigns that share geometry, fans,
mesh, and ambient conditions but use different component heat loads. It is
separate from normal `--multirate` operation and does not change the existing
`run`, `--warm-start`, or `--multirate` behavior.

## 1. Build/export once

Build the exporter and export the case normally. The export now includes:

- `constant/fluid/fvOptions.flowOnly` (fluid heat sources disabled)
- `constant/<solid>/fvOptions.fullHeat` (normal solid heat sources)
- `constant/<solid>/fvOptions.coldFlow` (solid heat sources disabled)

The ordinary `fvOptions` files remain heat-active for backwards compatibility.

## 2. Develop one cold-flow seed

From the exported case in WSL:

```bash
bash ./run_parallel.sh 4 --cold-flow-seed 3
```

The runner uses the existing airflow-convergence gates, but installs both the
fluid `flowOnly` dictionary and every solid `coldFlow` dictionary. It exits
after the accepted airflow checkpoint and writes:

- `.cold_flow_seed_complete`
- `.cold_flow_seed_manifest`
- the complete decomposed processor checkpoint

No thermal stage is started and no temperature field is copied or altered.
If interrupted, rerun the same command; the normal checkpoint/restart logic
continues from the last complete processor time.

## 3. Create a thermal branch for a heat-load case

Export a fresh case with the desired 20%, 60%, or 100% heat-load TOMLs, using
the same geometry/mesh/fans and ambient settings. Then run:

```bash
bash ./tools/create_thermal_branch_from_cold_flow_seed.sh \
  /absolute/path/to/cold_seed_case \
  /absolute/path/to/new_thermal_case \
  4
```

The tool checks the seed marker and geometry fingerprint, reconstructs the seed
checkpoint once if needed, copies only the developed fluid fields (`U`, `p`,
`p_rgh`, `phi`, `rho`, turbulence fields) into the target `0/fluid`, and leaves
all fluid and solid `T` fields at the target's ambient `0/` values. It then
redecomposes the target for the requested process count, records accepted-flow
metadata, and prints the exact next command:

```bash
THERMAL_SOLVER_OPENFOAM_ENV_READY=1 OPENFOAM_LAUNCHER=openfoam2606 \
  bash /absolute/path/to/new_thermal_case/run_parallel.sh 4 --multirate 30
```

The imported velocity is a warm initial condition, not a permanently frozen
flow. Once the thermal branch starts, buoyancy, density, temperature, fans,
and heat sources evolve normally. A different core count can be requested on a
later continuation; the runner's existing checkpoint repartition logic handles
that transition.

## Continuation checkpoints

If a session ends after export, continue with step 2. If it ends during branch
creation, rerun the same branch command; the target's original `0/fluid` is
backed up before replacement. If it ends during the thermal run, rerun the
printed `run_parallel.sh ... --multirate ...` command with the desired process
count.
