# Reuse screening airflow on an in-depth mesh

Use this workflow when the physical rack, region names, fans, vents, materials
and ambient conditions are the same, but the mesh resolution changes. The
target may use different heat loads and a different processor count. Mesh
interpolation supplies an initial airflow estimate; the finer mesh must still
pass its own airflow acceptance checks before thermal import.

Build the updated model executable and export the in-depth model into a new
case directory. Set its initial fluid and solid temperatures to ambient and
its components to the desired watt loads. Do not start its normal run yet.
Stop the screening donor cleanly and retain a backup. Choose an exact saved
time folder present on every donor processor (do not round its name).

## Prepare the target and map

Load your OpenFOAM 2606 environment. For the packaged installation:

```bash
source /usr/lib/openfoam/openfoam2606/etc/bashrc
```

Use your existing working bashrc path instead if OpenFOAM was built under your
home directory. The Python checker requires `python3` and only its standard
library. Replace the paths and checkpoint below; `6` is the target process count.

```bash
(
set -e
cd '/absolute/path/to/FRESH_INDEPTH_CASE'
bash ./build_semifrozen_solver.sh
OPENFOAM_LAUNCHER=env bash ./prepare_regions_low_memory.sh "$PWD"
OPENFOAM_LAUNCHER=env bash ./prepare_mapped_airflow_reuse.sh \
  '/absolute/path/to/SCREENING_DONOR' "$PWD" 'EXACT_SAVED_TIME' 6
)
```

This creates a sibling directory `FRESH_INDEPTH_CASE.mapped-flow-check`. It
copies selected donor data into a private snapshot and reconstructs there,
then maps U and the target's required turbulence fields. It preserves the
fresh target's pressure, ambient temperatures and heat-source dictionaries.
The solver will rebuild density and face flux during qualification.

Preparation prints progress and log paths. It verifies region definitions,
gravity, fluid patch names/types, and interpolation coverage. A coverage field
set to one in the donor and zero in the target must map to one everywhere.
Those checks do not establish that arbitrary changed geometry, materials or
fan curves are equivalent; this workflow supports changing mesh resolution
and heat loads in an otherwise matching model.

## Qualify, import, and run thermal

Run the qualification command printed by the helper. Example:

```bash
(
cd '/absolute/path/to/FRESH_INDEPTH_CASE.mapped-flow-check' || exit
set -o pipefail
THERMAL_SOLVER_OPENFOAM_ENV_READY=1 OPENFOAM_LAUNCHER=env \
bash ./run_parallel.sh 6 --cold-flow-seed 0.2 2>&1 | tee -a cold_seed.stdout.log
)
```

The time counter starts at zero using the imported velocity. `0.2` is a
bounded initial attempt, not a promised acceptance time. If airflow is still
pending, resume the same directory with a larger endpoint within the configured
airflow warmup limit. Do not repeat preparation. The usual velocity, continuity,
fan/vent and configured air-exchange checks apply; this helper does not weaken
them. Cold qualification disables all stamped fluid/solid heat sources.

Only after `.cold_flow_seed_complete` exists, run the printed import command:

```bash
OPENFOAM_LAUNCHER=env bash '/absolute/path/to/FRESH_INDEPTH_CASE/create_thermal_branch_from_cold_flow_seed.sh' \
  '/absolute/path/to/FRESH_INDEPTH_CASE.mapped-flow-check' \
  '/absolute/path/to/FRESH_INDEPTH_CASE' 6
```

Then run the `--multirate 30` command printed by the importer. The thermal
branch starts at time zero with the target's ambient fluid/solid temperatures
and its own component watt loads. Periodic live airflow refreshes remain active.
The accepted fine-mesh seed can also be reused for other heat loads on that same
fine mesh using the existing cold-seed importer.

## Interruptions and validation

Preparation refuses to overwrite an existing qualification directory. If
`.heated_airflow_origin` exists and `.airflow_reuse_preparation_pending` does
not, preparation finished; run qualification. If pending exists, inspect
`reconstruct-donor.log` or `map-airflow.log`. Newly exported runners refuse to
start such incomplete cases. Do not remove the pending marker to bypass a failure.
Preserve the incomplete directory for diagnosis rather than rerunning over it.

The donor is never reconstructed or run in place. Preparation needs disk space
for the target copy and donor snapshot; mapping/reconstruction are currently
serial and can require substantial RAM on a rack mesh.

Real OpenFOAM 2606 validation: 128 total donor cells to 1024 target cells,
temperature-dependent air with gravity. Fine airflow accepted at 0.25 s,
maxCo 0.04794 against limit 0.5. The 60 W thermal branch stored 2.999993 J versus
3 J input after 0.05 s; ambient initialization and held velocity checks passed.
A deliberately displaced target was rejected by the coverage check. These
small tests do not predict rack convergence time or resolve fine-scale flow
features without further airflow adjustment.
