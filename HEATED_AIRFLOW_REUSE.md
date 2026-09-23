# Reuse a heated airflow checkpoint with new thermal loads

Status: tiny OpenFOAM 2606 physical regressions passed on 2026-09-23 with both
constant-density air and temperature-dependent air with gravity. These short,
weakly heated fixtures do not establish full-rack accuracy or convergence time.
Keep a backup of valuable production checkpoints before using the workflow.

Your previous three-second case is a heated donor, not a cold seed. Its velocity
can be a useful initial guess, but its hot density, mass flux and enthalpy are not
consistent with resetting every region to ambient temperature. The new workflow
imports U and available turbulence fields into a separate unheated qualification
case. Pressure and temperature start from the fresh target model. OpenFOAM then
rebuilds density/flux and adjusts the flow. Only the normal airflow acceptance
gates can produce a cold-seed completion marker.

The donor is never run or reconstructed in place: selected donor fields and mesh
are copied to a private snapshot before reconstruction. This needs additional
disk space. A failed or pending qualification does not erase your donor data.

## Commands

Build the updated model executable and export the desired heat-load model into
a DIFFERENT case directory. Keep geometry, mesh, fans, materials and ambient
conditions the same. Stop the donor cleanly and use an exact saved time folder
name common to all its processors, including every decimal digit.

```bash
source /usr/lib/openfoam/openfoam2606/etc/bashrc
cd '/absolute/path/to/new_heat_load_case'
bash ./build_semifrozen_solver.sh
OPENFOAM_LAUNCHER=env bash ./prepare_regions_low_memory.sh "$PWD"
OPENFOAM_LAUNCHER=env bash ./prepare_heated_airflow_reuse.sh \
  '/absolute/path/to/old_100pct_case' "$PWD" 'EXACT_SAVED_TIME' 4
```

The preparation tool prints two commands. First run its separate
`new_heat_load_case.heated-flow-check` case using `--cold-flow-seed 0.2`.
The 0.2 seconds is a bounded first attempt, not a promise of convergence or an
estimate for a rack. Existing minimum-duration, exchange and convergence gates
still apply. Resume that qualification with a larger endpoint when necessary;
do not rerun preparation or restamp the donor. Once `.cold_flow_seed_complete`
exists, run the printed cold importer into the fresh thermal target. All target
temperatures are again taken from its ambient 0/ fields, and its watts remain
the requested load. Then run the printed `--multirate` command.

Normal thermal runs still refresh airflow when heating changes buoyancy. A
previous 100%-load operating point is not certified as a cold operating point
merely because its velocities have been copied. No acceptance markers are
manufactured by the heated preparation tool.

## Work-session continuation

Active tiny physical fixture (128 cells, OpenFOAM 2606):
`/home/hconner158/OpenFOAM/cases/reuse_validation_20260923_b` in WSL.

```bash
tail -40 /home/hconner158/OpenFOAM/cases/reuse_validation_20260923_b/seed.log
```

The heated-donor → unheated qualification → thermal-import regression passed.
Qualification accepted at 0.2 s; the 60 W branch stored 2.999997 J of its 3 J
input during a 0.05 s thermal stage. Initial temperatures were ambient, held
velocity was unchanged, and the donor's before/after file hashes matched.
The temperature-dependent-air/gravity variant also passed all numerical checks.
Maximum heat-off temperature drift was 0.00072 K; donor and branch energy were
4.999994 J and 2.999996 J. It uses only a small thermal perturbation, not a
strongly buoyant production operating point.

If the agent session ends, resume with: "Continue heated-airflow reuse validation
from HEATED_AIRFLOW_REUSE.md. Review production safeguards and inspect the exact
production donor/target settings before helping apply the tested workflow.
Preserve all production cases. Changes are local and not pushed."
