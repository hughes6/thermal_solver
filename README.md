# Thermal Sim

Thermal Sim is a transient, three-dimensional rack thermal solver. It builds a
rectilinear mesh, stamps components and openings into that mesh, solves a
quasi-steady airflow network, and then advances temperature using conduction,
solid-air convection, heat generation, and air advection.

The most capable configuration currently supported is:

- TOML model and reusable component files
- adaptive fine mesh
- multistage coarse-to-fine warm start
- coarse zero-cell-thickness component face walls
- resolved component geometry on the fine mesh
- internal air and heat-generating regions
- internal and rack-boundary fans and vents
- fixed-CFM or pressure-flow fan curves
- PCG or SOR pressure solution
- explicit thermal integration with optional advection subcycling
- optional OpenFOAM 2606 conjugate heat-transfer export
- k-omega SST turbulence, gravity, temperature-dependent air, fan curves, and
  vent pressure losses in exported OpenFOAM cases
- screening and in-depth OpenFOAM mesh/solver profiles
- multirate OpenFOAM execution with implicit thermal-only steps, periodic
  airflow refreshes, restart support, and automatic convergence stopping
- field, summary, and point-probe logging

The simpler modes remain supported. A model can use a single adaptive mesh, a
uniform mesh, no multistage warm start, no flow solve, no external logger, or
direct C++ construction in `main.cpp`.

## 1. Coordinate system and units

The entire project uses:

| Axis | Meaning | Positive direction |
|---|---|---|
| `x` | width | left to right |
| `y` | depth | front to rear |
| `z` | height | bottom to top |

Supported geometry units are:

- `"m"`: meters
- `"mm"`: millimeters
- `"in"`: inches
- `"u"`: rack units, where one unit is 44.45 mm

If a geometry table omits `units`, the loader currently assumes `"u"`.
Explicit units are strongly recommended.

Component positions are the lower-left-front corner of the component.
Positions for rectangular and circular fans and vents are their **centers**.
Internal fan and vent positions are centers expressed in component-local
coordinates.

Direction and normal vectors should normally be axis-aligned:

```toml
x =  1.0   # +x
x = -1.0   # -x
y =  1.0   # +y
z =  1.0   # +z
```

For a rectangular fan or vent, one size dimension is normally zero because it
is a two-dimensional opening. For example, a front/rear opening normal to `y`
has `depth = 0`.

## 2. Building and running

From the project directory in PowerShell:

```powershell
g++ -std=c++17 -O3 -DNDEBUG -fopenmp .\model_runner.cpp -o model.exe
.\model.exe
```

`model_runner.cpp` currently loads:

```cpp
ModelLoader loader;
loader.load_fan_curves("library/fan_curves/fan_curves.toml");
loader.load_model("library/models/model.toml");
loader.run();
```

To run a different model, pass it on the command line. The optional second
argument selects a different fan-curve library:

```powershell
.\model.exe library\models\model.toml
.\model.exe library\models\model.toml library\fan_curves\fan_curves.toml
```

After a successful run, `model_runner` atomically writes the machine-local
`.thermal_sim_last_run.json` in the directory where it was launched. It records
the executable, working directory, model TOML, fan-curve library, backend,
generated OpenFOAM case, geometry file, native simulation file, run mode, and
UTC timestamp. The file is ignored by Git and is only a convenience pointer;
simulation results remain in their normal case directory.

The main Python OpenFOAM plotting commands can therefore use the most recently
generated case without repeating its long directory name:

```powershell
python .\plot\heat_animation.py --format openfoam --time latest
python .\plot\fluid_results.py --time latest --streamlines --seed vents
python .\plot\recirculation_report.py --save
python .\plot\outlet_mass_weighted_temperature.py
python .\plot_outlet_flow.py
```

This is backward compatible: an explicit `--case "C:\OpenFOAM\..."` (or the
existing positional case argument for `outlet_mass_weighted_temperature.py`)
always takes precedence, which is how to plot an older archived case. Set
`THERMAL_SIM_RUN_METADATA` to a different metadata JSON path when launching a
plotter outside the repository or when maintaining several independent working
directories. A native-only last run contains no OpenFOAM case, so OpenFOAM
plotters will request an explicit case instead of silently using stale data.

The loader decides what happens from `[openfoam_solver].enabled`:

- `false` or an omitted section runs the native explicit solver.
- `true` exports a complete OpenFOAM case and prints both the WSL solver command
  and a platform-correct Python command that renders the latest reconstructed
  temperature field to `temperature_latest.png` inside the case directory.

To override an OpenFOAM-enabled model for one run and use the native solver
without editing its TOML, pass `--native`:

```powershell
.\model_runner.exe --native
```

This writes the normal native `output.txt` and simulation CSV files and does
not access the configured OpenFOAM case directory.

If only the geometry report is needed, skip both transient solvers:

```powershell
.\model_runner.exe --geometry-only
```

This writes `output.txt` without accessing OpenFOAM or running the coarse and
fine thermal simulations.

To view an existing native result without loading the TOML, starting either
solver, or rewriting `simulation.csv`/`output.txt`, use:

```powershell
.\model_runner.exe --plot-existing
```

Archived result and geometry files can be supplied explicitly:

```powershell
.\model_runner.exe --plot-existing `
  .\archive\simulation.csv `
  .\archive\output.txt
```

This mode launches `plot/heat_animation.py` directly. It never exports an
OpenFOAM case and never constructs a new native mesh, so existing solver data
is not deleted or replaced.

### 2.1 Source layout

The repository root contains the three supported C++ entry points:

| File | Purpose |
|---|---|
| `main.cpp` | Direct construction and component experiments. |
| `model_runner.cpp` | Production TOML loader for the native or OpenFOAM backend. |
| `foam_main.cpp` | Direct C++ OpenFOAM export example and smoke case. |

Shared implementation headers are under `src`, and loader-specific headers are
under `src/input`. Tests remain under `tests`; plotting and estimation tools
remain under `plot` and `tools`. The custom OpenFOAM application remains in
`openfoam_semifrozen_solver` because OpenFOAM's `wmake` requires its local
`Make/files` and `Make/options` layout.

`temp_foam_regions` is disposable test/export workspace. Production cases
should use the case directory selected in the model TOML.

For a debug build:

```powershell
g++ -std=c++17 -O0 -g -fopenmp .\model_runner.cpp -o model_debug.exe
.\model_debug.exe
```

Use the optimized build for production runs. Flow and thermal loops touch every
active cell many times, so optimization has a large effect.

To compile and run the focused regression tests after changing the mesh,
solver, exporter, loader, or plotting utilities:

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\run_added_feature_tests.ps1
```

The broader legacy unit suite is built from `src/test_runner.cpp`:

```powershell
g++ -std=c++17 -O2 -fopenmp .\src\test_runner.cpp -o test_runner.exe
.\test_runner.exe
```

## 3. Current advanced configuration

The current `library/models/model.toml` uses PCG, advection subcycling, an
adaptive fine mesh, and an adaptive multistage coarse warm start.

### 3.1 Simulation

```toml
[simulation]
dt = 0.1
duration = 10.0
output_interval = 100

max_timesteps = 1000000
max_updates = 10000000
max_cell_count = 10000000
max_megabyte_usage = 500

update_flow_interval = 100

advection_subcycling = true
advection_cfl_target = 0.8
max_advection_substeps = 10000
```

| Key | Meaning |
|---|---|
| `dt` | Global thermal timestep in seconds. |
| `duration` | Fine production-stage duration in seconds. |
| `output_interval` | Interval for the legacy `simulation.csv` output. |
| `max_timesteps` | Workload safety limit. |
| `max_updates` | Safety limit on timestep × cell work. |
| `max_cell_count` | Mesh cell-count safety limit. |
| `max_megabyte_usage` | Mesh memory safety limit. |
| `update_flow_interval` | Re-solve flow every N thermal steps. Use `-1` to retain the initial flow field. |
| `advection_subcycling` | Split air advection into smaller stable substeps while keeping the configured global thermal `dt`. |
| `advection_cfl_target` | Maximum target CFL per advection substep; must be in `(0,1]`. |
| `max_advection_substeps` | Fail-safe against an unexpectedly enormous substep count. |

When subcycling is disabled, `dt` must satisfy advection, conduction, and
convection stability limits. When it is enabled, only advection is subcycled;
the global `dt` must still satisfy the explicit conduction and convection
limits.

`update_flow_interval = -1` is appropriate when temperature-dependent density
changes are not expected to change the airflow enough to justify another
expensive pressure solve.

### 3.2 Flow solver

```toml
[flow_solver]
enable_flow_solver = true
pressure_method = "pcg"
resistivity = 4.6
tolerance = 1e-6
max_iterations = 3500
sor_omega = 1.1
max_outer_iters = 20
flow_tolerance = 1e-3
```

| Key | Meaning |
|---|---|
| `enable_flow_solver` | Enables pressure and velocity calculation. |
| `pressure_method` | `"pcg"` or `"sor"`. Defaults to `"sor"` when omitted. |
| `resistivity` | Baseline linear friction resistance per unit length for fluid-fluid faces. Larger values restrict flow more. |
| `tolerance` | Absolute cell continuity residual required from the pressure solve, in m³/s. |
| `max_iterations` | Maximum PCG or SOR iterations for each pressure solve. |
| `sor_omega` | SOR relaxation parameter. It is accepted but not used by PCG. Must remain in `(0,2)`. |
| `max_outer_iters` | Maximum nonlinear conductance/fan operating-point iterations. |
| `flow_tolerance` | Relative face-flow change target for nonlinear convergence. |

PCG is recommended for large meshes. SOR remains useful as a simple reference
method and for very small cases.

### 3.3 Environment

```toml
[environment]
humidity = 30.0
elevation = 5500.0
T_ambient = 20.0
cp = 1005.5
k = 0.02587
mu = 1.81e-5
pr = 0.71
rho = 1.225
```

These values define the initial/reference air state:

- `humidity`: percent relative humidity
- `elevation`: model metadata used by the environment
- `T_ambient`: ambient intake temperature in °C
- `cp`: air specific heat in J/(kg·K)
- `k`: air thermal conductivity in W/(m·K)
- `mu`: dynamic viscosity in Pa·s
- `pr`: Prandtl number
- `rho`: reference density in kg/m³

Fluid density and viscosity are updated with temperature during the transient.

### 3.4 Adaptive fine mesh

```toml
[mesh]
adaptive = true
fine_dx = 0.02
coarse_dx = 0.10
refinement_margin = 0.02
```

`fine_dx` and `coarse_dx` are target spacings in meters. The planner:

1. Adds exact cuts at rack boundaries.
2. Adds exact cuts at component boundaries.
3. Adds cuts at internal air and solid region boundaries.
4. Adds center/edge cuts for rectangular or circular fan and vent footprints.
5. Creates fine-spacing bands around components and openings.
6. Uses coarse spacing elsewhere.

Because exact geometry boundaries take priority, the realized minimum spacing
can be smaller than `fine_dx` when nearby cuts leave a narrow remainder. Always
read the estimator's realized `dx`, `dy`, and `dz` values.

`refinement_margin` expands the fine band around relevant geometry. It does not
change the physical dimensions of the geometry.

### 3.5 Multistage warm start

```toml
[multistage]
enabled = true
coarse_dt = 0.1
coarse_duration = 30.0
coarse_update_flow_interval = -1

[multistage.coarse_mesh]
fine_dx = 0.05
coarse_dx = 0.20
refinement_margin = 0.02
```

The multistage sequence is:

1. Build a regularized coarse adaptive mesh.
2. Represent thin component enclosures as impermeable conductive face walls.
3. Stamp internal openings so fans and vents pass through the intended walls.
4. Solve coarse flow.
5. Run the coarse thermal transient.
6. Build the fully resolved fine production mesh.
7. Transfer coarse temperatures into fine cells.
8. Solve fine flow.
9. Run the fine production transient.

The current transfer classifies each fine cell as:

- `direct`: containing coarse cell has a compatible phase
- `nearby-phase`: nearest compatible neighboring coarse cell
- `wall-face`: resolved fine enclosure wall receives temperature from the
  corresponding coarse conductive face wall
- `ambient fallback`: no suitable source was found

The coarse mesh deliberately omits exact thin internal geometry cuts. Its
`fine_dx` therefore behaves as the regular coarse-grid target used for the
face-wall representation. Component faces are snapped to nearby mesh faces and
the maximum displacement is printed.

This is a warm start, not a replacement for the fine production stage.

## 4. Rack, components, fans, and vents

### 4.1 Rack

```toml
[rack]
name = "rack"

[rack.size]
units = "m"
width = 0.6
depth = 1.2
height = 2.0

[rack.ambient]
temperature = 20.0
pressure = 101325.0
h = 0.0
k = 0.02587
rho = 1.225
cp = 1005.0
```

`pressure` defaults to 101325 Pa and `h` defaults to zero. Temperature,
conductivity, density, and specific heat are required by the current parser.

### 4.2 Reusable component template

The model places a reusable component file like this:

```toml
[[components]]
template = "library/components/DELL_R470.toml"

[components.position]
units = "u"
x = 0.628
y = 0.0
z = 4.0
```

The position is the component's lower-left-front corner in rack coordinates.
Internal-region positions inside the component file are local to that corner.

### 4.3 Complete component file

```toml
name = "Example server"
watts = 0.0

[size]
units = "mm"
width = 482.0
depth = 700.0
height = 44.45

[material]
rho = 2700.0
cp = 900.0
k = 150.0

[[internal_regions]]
name = "Interior air"
state = "air"

[internal_regions.position]
units = "mm"
x = 5.0
y = 5.0
z = 5.0

[internal_regions.size]
units = "mm"
width = 472.0
depth = 690.0
height = 34.45

[[internal_regions]]
name = "Electronics"
state = "solid"
watts = 400.0

[internal_regions.position]
units = "mm"
x = 40.0
y = 250.0
z = 8.0

[internal_regions.size]
units = "mm"
width = 400.0
depth = 250.0
height = 20.0

[internal_regions.material]
rho = 2330.0
cp = 700.0
k = 130.0
```

Materials may instead reference a reusable TOML file. The catalog under
`library/components/materials` contains aluminum, copper, steels, FR-4, ABS,
and an effective mixed-electronics material:

```toml
material = "library/components/materials/aluminum.toml"

[[internal_regions]]
name = "Electronics"
state = "solid"
watts = 400.0
material = "library/components/materials/mixed_electronics.toml"
```

Each referenced material file contains root-level `rho`, `cp`, and `k` values.
Paths follow the existing component-template convention and are resolved from
the process working directory, normally the repository root. Inline material
tables remain supported.

The outer component is stamped first with its material and `watts`. Internal
regions then overwrite the cells they occupy:

- `state = "air"` replaces those cells with air.
- `state = "solid"` replaces material properties and assigns that region's
  watts.
- fans and vents create airflow elements/openings.

Heat is stored as volumetric generation `qdot`. Region watts are divided over
the stamped heat-source volume so their volume integral equals the requested
power. Component-wall watts should normally be zero when internal solid regions
already represent the actual heat sources.

For a component with no internal geometry in a TOML component file, use:

```toml
internal_regions = []
```

The current component parser expects the `internal_regions` array to exist,
even when it is empty.

### 4.4 Internal vent

```toml
[[internal_regions]]
name = "Front intake"
state = "vent"
shape = "rectangular"
free_area_ratio = 0.30
vent_discharge_coeff = 0.82

[internal_regions.position]
units = "mm"
x = 241.0
y = 0.0
z = 22.0

[internal_regions.size]
units = "mm"
width = 430.0
depth = 0.0
height = 30.0

[internal_regions.normal]
x = 0.0
y = 1.0
z = 0.0
```

The position is the vent center. The vent must intersect the component face or
internal airflow path it is intended to open.

### 4.5 Internal fixed-flow fan

```toml
[[internal_regions]]
name = "Cooling fan"
state = "fan"
shape = "rectangular"
flow_type = "exhaust"
cfm = 45.0

[internal_regions.position]
units = "mm"
x = 100.0
y = 250.0
z = 22.0

[internal_regions.size]
units = "mm"
width = 60.0
depth = 0.0
height = 30.0

[internal_regions.direction]
x = 0.0
y = 1.0
z = 0.0
```

The fan position is its center. For an internal fan, the mesher identifies the
fluid cells immediately upstream and downstream of the fan plane. Both must be
fluid and must be connected to the intended internal air region.

### 4.6 Internal fan with a pressure-flow curve

Add a curve reference:

```toml
curve = "server_fan_curve"
```

The curve must exist in the fan-curve library loaded before the model:

```toml
[[fan_curve]]
name = "server_fan_curve"
rho_rated = 1.2
a = 250.0
b = 1500.0
c = 2000.0
```

The implemented curve is:

```text
ΔP(Q) = a - bQ - cQ²
```

where pressure is Pa and flow is m³/s. `rho_rated` is the air density at which
the manufacturer curve was measured. The solver scales pressure for the local
density.

`cfm` remains required by the parser and supplies the initial/reference flow,
even when a curve is present.

### 4.7 Porous rack obstructions and perforated trays

For the full decision guide, percentage/pressure-drop calculations, cable
volume estimates, solid-obstruction examples, calibration workflow, and
validation commands, see
[Porous and solid rack obstructions](POROUS_AND_SOLID_OBSTRUCTIONS.md).
That guide also documents `tools/porous_obstruction_calculator.py`, which
converts hole geometry, cable inventory, or measured pressure-drop points into
reports and a copy-ready `[[porous_regions]]` block.

When cable pressure data are impractical or unavailable, use
`--estimate-cable-bundle` with cable count, average outside diameter, and a
dominant cable axis plus a `loose`, `typical`, or `dense` packing condition.
Use a tight bundle envelope, not the whole rear-rack air volume. The tool writes optimistic,
nominal, and conservative TOML alternatives. These are bounded engineering
sensitivity cases; run all three rather than treating the nominal correlation
as measured cable performance.

The memory-bounded pressure-drop acceptance model is
`library/models/validation_porous_duct.toml`; detailed native/OpenFOAM results
are recorded in `POROUS_ACCEPTANCE_VALIDATION_2026-08-15.md`. OpenFOAM thin
porous sources are automatically spread over at least two axial cells with
coefficient scaling that preserves their physical integrated pressure loss.

Use a top-level `[[porous_regions]]` volume for cable bundles, perforated
shelves, hole-pattern trays, grilles, or other obstructions that remain
air-permeable. The region stays fluid and adds a directional
Darcy-Forchheimer pressure gradient:

```text
deltaP/L = mu*D*U + 0.5*rho*F*U*abs(U)
```

`D` has units 1/m2, `F` has units 1/m, `U` is superficial velocity based on
the region's gross area, and `L` is thickness along `direction`. The same law
is applied by the native solver and exported as an OpenFOAM
`explicitPorositySource` cell zone.

For a perforated tray with known open area and discharge coefficient:

```toml
[[porous_regions]]
name = "Rear perforated cable tray"
porosity_percent = 42.0
discharge_coefficient = 0.65
# Optional high transverse resistance prevents flow travelling inside the
# numerically thickened tray instead of crossing its holes.
transverse_darcy_coefficient = 1.0e8
transverse_forchheimer_coefficient = 1.0e5

[porous_regions.position]
units = "m"
x = 0.05
y = 0.72
z = 0.30

[porous_regions.size]
units = "m"
width = 0.50
depth = 0.01
height = 1.20

[porous_regions.direction]
x = 0.0
y = 1.0
z = 0.0
```

This form derives `F = 1/(Cd^2*phi^2*L)`, giving
`deltaP = rho*U^2/(2*Cd^2*phi^2)`. Enter `porosity_percent` in `(0,100]`;
for example, `42.0` means 42% open area. `discharge_coefficient` remains a
dimensionless value in `(0,1]`. The legacy fractional `free_area_ratio = 0.42`
form remains accepted for backward compatibility, but do not specify both.

For a cable bundle or obstruction calibrated from measured/Fluent pressure
drop, enter coefficients directly:

```toml
[[porous_regions]]
name = "Rear cable field"
darcy_coefficient = 2.5e6
forchheimer_coefficient = 850.0
# Omit these for isotropic resistance, appropriate for a tangled bundle.
transverse_darcy_coefficient = 2.5e6
transverse_forchheimer_coefficient = 850.0

[porous_regions.position]
units = "m"
x = 0.10
y = 0.75
z = 0.20

[porous_regions.size]
units = "m"
width = 0.40
depth = 0.15
height = 1.40

[porous_regions.direction]
x = 0.0
y = 1.0
z = 0.0
```

The numerical zone is guaranteed to occupy at least one cell. When its
stamped thickness differs from the physical thickness, coefficients are
automatically scaled so integrated pressure drop remains mesh-independent.
Porous regions must remain inside the rack and may not overlap components or
one another. They must remain internal along their resistance direction; use
a rack vent for a porous exterior opening. Omitted transverse coefficients
default to the normal values (isotropic resistance). For final use, calibrate
cable coefficients from several
pressure-drop/velocity points, verify the direction, compare mass flow with
and without the obstruction, and repeat one case on a finer mesh.

### 4.8 Rack-boundary circular fan

```toml
[[fans]]
name = "Top exhaust"
shape = "circular"
diameter = 120.0
diameter_units = "mm"
cfm = 271.0
flow_type = "exhaust"
curve = "top_fan_MS1238E-H"

[fans.position]
units = "m"
x = 0.30
y = 0.60
z = 2.00

[fans.direction]
x = 0.0
y = 0.0
z = 1.0
```

For a rectangular rack fan, replace `diameter` with:

```toml
[fans.size]
units = "mm"
width = 120.0
depth = 0.0
height = 120.0
```

Valid fan shapes are `"circular"` and `"rectangular"`. Valid flow types are
`"intake"` and `"exhaust"`.

### 4.9 Rack-boundary vent

```toml
[[vents]]
name = "Main intake"
shape = "rectangular"
free_area_ratio = 0.5
vent_discharge_coeff = 0.5

[vents.position]
units = "m"
x = 0.30
y = 0.0
z = 0.20

[vents.size]
units = "m"
width = 0.50
depth = 0.0
height = 0.15

[vents.normal]
x = 0.0
y = 1.0
z = 0.0
```

For circular vents use `diameter` and `diameter_units`. `free_area_ratio`
reduces geometric area to open area. `vent_discharge_coeff` accounts for
orifice losses.

## 5. Logger configuration

Logging can be supplied through a template:

```toml
[logger]
template = "library/loggers/logger1.toml"
```

or directly in the model:

```toml
[logger]
output_directory = "simulation_output"
enable_field_logging = true
enable_summary_logging = true
enable_probe_logging = true
field_interval = 100
summary_interval = 10
probe_interval = 1

field_variables = [
    "temperature",
    "pressure",
    "velocity_x",
    "velocity_y",
    "velocity_z",
    "velocity_magnitude"
]

[[logger.summary]]
name = "solid_temperature"
variable = "temperature"
selection = "solid"
log_min = true
log_max = true
log_average = true
log_rms = false
log_standard_deviation = true

[[logger.probe]]
name = "rack_inlet"
position = [0.20, 0.05, 0.20]
variables = ["temperature", "pressure", "velocity_magnitude"]
```

Probe positions are in meters.

Valid logged variables include:

- `temperature` or `T`
- `pressure`
- `velocity_x` or `vx`
- `velocity_y` or `vy`
- `velocity_z` or `vz`
- `velocity_magnitude`, `velocity_mag`, `vmag`, `velocity`, or `v`
- `density` or `rho`
- `specific_heat` or `cp`
- `conductivity` or `k`
- `heat_generation` or `qdot`
- `reynolds_number` or `re`
- `convection_coefficient` or `h`

Valid summary selections are `all`, `air`, `fluid`, `solid`, and
`heat_generating`.

The logger writes under its output directory, including field, summary, and
probe CSV files. The solver also writes the legacy `simulation.csv`. A
multistage run writes `coarse_simulation.csv` for the warm-start stage.

If the `[logger]` table is omitted, built-in logging defaults are used. To keep
output small, include a logger table and explicitly disable unwanted streams:

```toml
[logger]
enable_field_logging = false
enable_summary_logging = false
enable_probe_logging = false
```

## 6. Progressively simpler model configurations

### 6.1 Single-stage adaptive model

Remove or disable the multistage section:

```toml
[multistage]
enabled = false
```

Keep:

```toml
[mesh]
adaptive = true
fine_dx = 0.02
coarse_dx = 0.10
refinement_margin = 0.02
```

The model builds and solves only the fully resolved adaptive mesh. PCG, fan
curves, internal regions, and advection subcycling remain available.

### 6.2 Uniform-mesh model

```toml
[mesh]
adaptive = false
dx = 0.05
dy = 0.05
dz = 0.05

[multistage]
enabled = false
```

Uniform mode uses one spacing per axis. Geometry is stamped into intersecting
cells, so the spacing must be small enough to preserve walls, openings, and
air channels. Uniform mode does not use the adaptive refinement planner.

### 6.3 Fixed-flow model without fan curves

Omit `curve` from every fan:

```toml
[[fans]]
name = "Fixed exhaust"
shape = "circular"
diameter = 0.12
diameter_units = "m"
cfm = 100.0
flow_type = "exhaust"
```

The fan then behaves as a prescribed volumetric source rather than finding an
operating point on a P-Q curve.

### 6.4 Thermal-only model without the flow solver

```toml
[flow_solver]
enable_flow_solver = false
```

The rest of the flow keys can remain present. With no calculated velocity,
forced advection is absent and solid-air heat transfer falls back to the local
natural/low-flow convection correlation.

Set:

```toml
[simulation]
update_flow_interval = -1
advection_subcycling = false
```

for clarity.

### 6.5 Empty-rack or conduction test

The global `[[components]]`, `[[fans]]`, and `[[vents]]` arrays are optional.
A model may contain only a rack, environment, mesh, simulation, flow-solver
table, and logger.

If the flow solver is enabled with a fan source, the network needs a vent or
other ambient pressure reference. A source with no pressure reference is
rejected.

### 6.6 Copy-ready minimal uniform model

The following is a complete small model with no components, fans, vents,
multistage run, or external logger output:

```toml
name = "minimal thermal model"

[simulation]
dt = 0.1
duration = 5.0
output_interval = 10
max_timesteps = 100000
max_updates = 10000000
max_cell_count = 100000
max_megabyte_usage = 100
update_flow_interval = -1
advection_subcycling = false

[flow_solver]
enable_flow_solver = false

[environment]
humidity = 30.0
elevation = 0.0
T_ambient = 20.0
cp = 1005.0
k = 0.02587
mu = 1.81e-5
pr = 0.71
rho = 1.225

[mesh]
adaptive = false
dx = 0.05
dy = 0.05
dz = 0.05

[multistage]
enabled = false

[logger]
enable_field_logging = false
enable_summary_logging = false
enable_probe_logging = false

[rack]
name = "small air volume"

[rack.size]
units = "m"
width = 0.20
depth = 0.20
height = 0.20

[rack.ambient]
temperature = 20.0
pressure = 101325.0
h = 0.0
k = 0.02587
rho = 1.225
cp = 1005.0
```

The `[simulation]`, `[flow_solver]`, `[environment]`, `[mesh]`, and `[rack]`
tables are required by the current model parser. `[multistage]` and `[logger]`
may be omitted; they are shown here to make the selected behavior explicit.

### 6.7 Inline component instead of a template

A component may be defined directly in the model:

```toml
[[components]]
name = "Simple heater block"
watts = 100.0
internal_regions = []

[components.position]
units = "m"
x = 0.05
y = 0.05
z = 0.05

[components.size]
units = "m"
width = 0.10
depth = 0.10
height = 0.05

[components.material]
rho = 2700.0
cp = 900.0
k = 150.0
```

For this simple component, its watts are distributed through its stamped solid
volume. A template is preferable when the same component is placed more than
once or contains detailed internal regions.

## 7. Running directly from `main.cpp`

`main.cpp` is useful for experiments that construct objects directly rather
than loading a complete TOML model.

Its current active example runs the component loader:

```cpp
ComponentLoader loader;
loader.load_component("library/components/eaton_2U_UPS.toml");
loader.run();
```

Build and run it with:

```powershell
g++ -std=c++17 -O3 -DNDEBUG -fopenmp .\main.cpp -o main.exe
.\main.exe
```

To run a TOML model from `main.cpp`, replace the active loader block with:

```cpp
ModelLoader loader;
loader.load_fan_curves("library/fan_curves/fan_curves.toml");
loader.load_model("library/models/model.toml");
loader.run();
```

To build a simulation entirely in C++:

1. Construct `Environment` and `Workload`.
2. Construct the `Rack`.
3. Construct components, internal regions, fans, and vents.
4. Run `CollisionChecker::check_all`.
5. Create a uniform mesh directly or obtain adaptive widths from
   `MeshRefinementPlanner::plan`.
6. Stamp components, fans, and vents.
7. Optionally run `ThermalTimeEstimator::estimate`.
8. Construct `Solver`.
9. Optionally attach `SimulationLogger`.
10. Call `solver.solve()`.

For PCG, the final `Solver` constructor argument is `"pcg"`:

```cpp
Solver solver(
    mesh,
    0.1,                  // dt
    10.0,                 // duration
    false,                // print convection diagnostics
    100,                  // output interval
    -1,                   // update flow interval
    4.6,                  // resistivity
    1e-6,                 // pressure tolerance
    3500,                 // pressure iterations
    1.1,                  // SOR omega
    20,                   // nonlinear outer iterations
    1e-3,                 // nonlinear flow tolerance
    true,                 // advection subcycling
    0.8,                  // subcycling CFL target
    10000,                // maximum substeps
    "simulation.csv",
    "pcg"
);
```

The default final argument is `"sor"`, preserving older direct C++ calls.

## 8. How the implementation works

### 8.1 Mesh representation

Both uniform and adaptive meshes are Cartesian tensor-product grids. Every cell
stores its dimensions, center, material state, temperature, heat generation,
pressure, velocity, and thermophysical properties.

Uniform mode uses global `dx`, `dy`, and `dz`. Adaptive mode stores realized
cell widths along all axes and uses per-cell volumes, face areas, and
center-to-center distances.

### 8.2 Geometry stamping order

The fine mesh is stamped in this order:

1. Base rack air.
2. Components.
3. Component internal regions.
4. Rack-level fans.
5. Rack-level vents.

Component stamping initially makes intersecting cells solid. Internal air
regions carve fluid space back into the component. Internal solid regions
replace the surrounding material and assign heat generation. Fan and vent
regions then create openings or two-node flow elements at their planes.

Because later stamps intentionally override earlier material state, fan and
vent cells are reset to air properties and do not retain accidental component
wall heat generation.

### 8.3 Fine resolved walls

On the fine production mesh, component enclosure walls are represented by
solid cells. For a nominal 5 mm wall to exist as a resolved wall, mesh cuts
must exist at:

```text
component bottom
component bottom + 5 mm
component top - 5 mm
component top
```

The internal air-region boundaries provide the inner wall cuts. The component
boundaries provide the outer cuts. A mesh that does not preserve those cuts can
erase or merge the wall.

### 8.4 Coarse face walls

Resolving every 5 mm wall with cells makes a rack mesh unnecessarily large.
The multistage coarse mesh therefore uses conductive, impermeable faces:

- no volumetric wall cells are required
- advection cannot cross a blocked wall face
- the flow network does not create a fluid link through the wall
- each face wall stores thickness, material, temperature, and thermal mass
- conduction exchanges energy between neighboring cells and the wall node
- fan and vent footprints open the required portions of the wall

The fine mesh still resolves the actual component geometry. Face walls are a
coarse-stage acceleration, not a universal replacement for fine geometry.

### 8.5 Pressure/flow network

Every fluid cell is a pressure node. Every open face between neighboring fluid
cells is a conductance. Solid cells and blocked face walls are excluded.

The continuity equation for fluid cell `i` is:

```text
(Cvent,i + Cfan,i + Σj Cij) Pi - Σj Cij Pj = Si
```

where:

- `Pi` is cell pressure relative to ambient
- `Cij` is a fluid-face conductance
- `Cvent,i` is the linearized connection to ambient through a vent
- `Cfan,i` is a curve-driven boundary fan's linearized internal conductance
- `Si` is net fan/source flow

Face conductance includes geometry, path length, hydraulic diameter,
resistivity, and linearized loss behavior. After pressure is solved:

```text
Qij = Cij(Pi - Pj)
```

Face flows are converted into cell-centered velocity components.

### 8.6 Vents

A vent connects the stamped fluid cells to ambient pressure. Its open area is:

```text
Aopen = Ageometric × free_area_ratio
```

The nonlinear orifice relation is linearized around the current pressure using
the discharge coefficient. Vent conductance is rebuilt during nonlinear outer
iterations.

### 8.7 Fixed-flow fans

A fixed-CFM rack-boundary fan is represented as a source or sink attached to a
fluid cell. An internal fixed-flow fan is a two-node element:

- remove its prescribed flow from the upstream cell
- add the same flow to the downstream cell

This preserves internal mass balance while enforcing the specified direction.

### 8.8 Fan curves

Curve-driven fans use:

```text
ΔP(Q) = a - bQ - cQ²
```

The curve is linearized about the current reference flow. A boundary fan
becomes a Norton-equivalent source in parallel with a conductance to ambient.
An internal fan becomes a source plus a conductance between its upstream and
downstream pressure nodes.

After each pressure solve, the operating flow is updated from the new pressure
difference. This is why the solver has nonlinear outer iterations even though
each individual pressure system is linear.

### 8.9 SOR pressure method

SOR visits fluid cells sequentially and updates:

```text
Pi,GS  = rhs / diagonal
Pi,new = Pi,old + ω(Pi,GS - Pi,old)
```

Advantages:

- very simple
- little additional memory
- useful as a reference implementation

Disadvantages:

- information propagates only locally per sweep
- convergence becomes slow on large, elongated, or strongly nonuniform meshes
- tuning `sor_omega` is problem-dependent

### 8.10 PCG pressure method

PCG solves the same symmetric pressure system using matrix-free,
Jacobi-preconditioned conjugate gradients.

Matrix-free means the solver does not construct a general sparse matrix.
Instead, it computes `A × vector` directly from the existing neighbor
conductances. The Jacobi preconditioner uses the inverse pressure diagonal.

Advantages:

- usually far fewer iterations than SOR on large meshes
- no large explicit sparse matrix
- deterministic convergence residual
- backward compatible through `pressure_method`

Disadvantages:

- Jacobi is a weak preconditioner for highly nonuniform meshes
- each iteration performs several complete vector/network passes
- fine adaptive meshes may still require hundreds or thousands of iterations

The current pressure reference is held at zero. PCG checks for a singular or
non-positive-definite system and reports likely disconnected fluid regions.

The pressure tolerance and nonlinear flow tolerance are different:

- `tolerance` checks local mass-continuity residual inside PCG/SOR
- `flow_tolerance` checks how much face flows/fan operating points changed
  between nonlinear outer iterations

Mass balance is reported using effective fan/source flow rather than raw
Norton source terms.

### 8.11 Thermal conduction

Thermal conduction uses a six-face finite-volume stencil. For compatible
neighboring phases, face conductivity is the harmonic mean:

```text
kface = 2 ki kj / (ki + kj)
Qcond = kface A (Tj - Ti) / distance
```

Solid-fluid neighbor pairs do not exchange heat through this conduction term;
their exchange is handled by convection.

Face walls add their own half-thickness resistance and thermal capacitance.

### 8.12 Solid-air convection

Convection is evaluated only across solid-fluid interfaces:

```text
Qconv = h A (Tneighbor - Tcell)
```

The local coefficient `h` is computed from air velocity, characteristic length,
air properties, temperature difference, and film temperature. The convection
implementation chooses forced, natural, or combined behavior through the local
correlation rather than using one constant rack-wide value.

### 8.13 Air advection

Air advection uses a first-order upwind temperature gradient:

```text
dT/dt = -vx dT/dx - vy dT/dy - vz dT/dz
```

The upstream neighbor is chosen separately for each velocity component. Solid
cells are not advected. Blocked face walls prevent advection across the face.
Intake cells are pinned to ambient temperature.

With subcycling enabled, each global step is split:

1. Advance conduction, convection, and generation once using global `dt`.
2. Compute global advection CFL.
3. Choose `ceil(CFL / advection_cfl_target)` substeps.
4. Advance only advection using the smaller substep.

This is a Lie-split explicit method. It removes the need to reduce the global
thermal timestep solely because of fast airflow, but it does not remove
conduction or convection stability limits.

### 8.14 Heat generation

Requested watts are converted to volumetric source:

```text
qdot = watts / stamped source volume
Qgeneration,cell = qdot × cell volume
```

The stamper preserves integrated internal-region power after fans, vents, and
air regions remove cells from a heat-source region.

### 8.15 Thermal estimator

Before each stage, the estimator reports:

- solid thermal mass
- solid-air interface area
- representative convection coefficient
- approximate lumped time constant
- recommended duration near five time constants
- realized minimum cell dimensions
- cell count and approximate memory
- explicit conduction stability limit
- explicit advection stability limit
- recommended timestep

The time constant is a single lumped estimate. Real rack geometry contains
multiple time constants, so the logger should still be used to verify that
important temperatures have flattened.

The estimator is intended to answer two setup questions before spending time
on a long run:

1. Is the requested duration long enough for the modeled thermal mass to react?
2. Is the native explicit timestep stable on the mesh that was actually
   stamped, including geometry-aligned cells smaller than the requested mesh
   spacing?

Its lumped thermal calculation is approximately:

```text
Cthermal = sum(rho * cp * solid volume)          [J/K]
UA       = representative h * solid-air area    [W/K]
tau      = Cthermal / UA                         [s]
suggested duration = 5 * tau
```

`5*tau` is a starting duration at which a single first-order thermal mass would
be more than 99% of the way toward equilibrium after a step in heat load. It is
not a prediction of the final temperature, and it does not prove convergence.
A rack has separate chip, chassis, internal-air, rack-air, and enclosure time
constants. Use the component averages, hotspot history, and convergence reports
from the actual run to decide whether the temperature field has flattened.

For the native solver, `recommended_dt` is 80% of the smaller computed
conduction and advection stability limits. When advection subcycling is active,
the global thermal step may be selected from the conduction/convection limit
while advection uses smaller internal substeps. For the OpenFOAM backend, the
explicit stability recommendation is diagnostic only: the implicit solver and
Courant controls choose their own steps. The `tau` and mesh/memory estimates are
still useful for selecting screening duration, write intervals, airflow refresh
intervals, and the eventual 18,000-second production horizon.

The estimate is only as credible as the three inputs that feed the model:

- material density and heat capacity determine thermal mass;
- fan curves determine airflow and therefore the convective coefficient;
- component/internal-region watts determine the applied heat load.

Sections 19.1 and 19.2 provide repeatable utilities for converting fan
datasheets and server power/temperature information into those model inputs.

## 9. Choosing practical settings

For the current large rack model:

```toml
[flow_solver]
pressure_method = "pcg"
tolerance = 1e-6
max_iterations = 3500
max_outer_iters = 20

[simulation]
advection_subcycling = true
advection_cfl_target = 0.8

[mesh]
adaptive = true
fine_dx = 0.02
coarse_dx = 0.10
refinement_margin = 0.02
```

Recommended tuning order:

1. Confirm every internal fan has fluid immediately upstream and downstream.
2. Confirm vents create an ambient pressure path.
3. Run the coarse stage and inspect physical mass imbalance.
4. Confirm integrated heat-source power matches the component specification.
5. Check realized minimum cell dimensions and memory.
6. Ensure global `dt` satisfies conduction and convection limits.
7. Let advection subcycling handle only the advection CFL restriction.
8. Increase fine resolution only where temperature results materially change.

Do not choose a tiny global `dt` solely from the non-subcycled advection limit
when advection subcycling is enabled.

## 10. Important limitations

- This is a rack-level engineering model, not a full Navier-Stokes CFD solver.
- Airflow is quasi-steady and network-based.
- Cell-centered velocity is derived from face flow and is intended for thermal
  transport, not detailed turbulence visualization.
- PCG currently uses Jacobi preconditioning, not incomplete Cholesky or
  multigrid.
- Coarse face walls are snapped approximations.
- The coarse stage transfers temperature, not pressure, into the fine stage.
- Explicit conduction and convection still restrict the global timestep.
- First-order upwind advection is stable and robust but numerically diffusive.
- Geometry and fan curves should be calibrated against measurements when
  absolute accuracy matters.

## 11. Troubleshooting

### Internal fan has no fluid cell immediately upstream

- Verify the fan position is its center.
- Verify its direction vector points through the intended air channel.
- Verify the internal air region reaches both sides of the fan plane.
- Verify solid internal regions do not occupy the upstream/downstream cell.
- Inspect the component using `plot/plot_component.py`.

### Fine flow solve takes a long time

- Confirm the optimized `-O3 -DNDEBUG` build is being used.
- Use `pressure_method = "pcg"`.
- Avoid unnecessarily small exact-cut cells.
- Use `update_flow_interval = -1` if repeated flow solves are unnecessary.
- Reduce `max_outer_iters` only after confirming acceptable nonlinear
  convergence and physical mass balance.

### Memory overload

- Increase `fine_dx`.
- Reduce `refinement_margin`.
- Increase `coarse_dx` away from geometry.
- Use multistage face walls instead of resolving thin walls on the coarse mesh.
- Review the printed cell count before starting the transient.

### Temperature remains at ambient

- Check the printed number of heat-source cells.
- Check integrated power against requested component/internal-region watts.
- Check that fans or vents did not overwrite all heat-source cells.
- Inspect `coarse_simulation.csv`, final `simulation.csv`, and summary logs.

### Advection requires too many substeps

- Check for unrealistically high fan flow or tiny realized cell widths.
- Inspect the maximum velocity and minimum realized spacing.
- Increase `advection_cfl_target` only up to `1.0`.
- Do not exceed the explicit stability target merely to suppress the warning.

## 12. OpenFOAM backend overview

The OpenFOAM path uses the same rack, component, internal-region, material,
heat-load, fan, vent, fan-curve, environment, and simulation-duration data as
the native solver. It is an alternative backend selected from the model TOML;
it does not remove or replace the native solver.

The workflow is:

1. `model_runner.cpp` loads the fan-curve library and model TOML.
2. Reusable component and OpenFOAM profile templates are merged into the model.
3. The normal mesh planner builds the rectilinear rack mesh.
4. Components, internal air/solid regions, rack openings, and internal devices
   are stamped into that mesh.
5. The OpenFOAM exporter creates fluid and solid regions, boundary patches,
   material dictionaries, heat sources, fan/porosity sources, initial fields,
   solver dictionaries, geometry metadata, and run scripts.
6. The generated case is run inside WSL2 with OpenFOAM 2606.
7. Results can be opened in ParaView or plotted directly with
   `plot/heat_animation.py`.

The exported solver is a compressible conjugate heat-transfer model:

- fluid and solid energy equations are coupled
- air density follows the perfect-gas relation
- viscosity follows the Sutherland model when
  `temperature_dependent_air = true`
- gravity is included through `p_rgh`
- k-omega SST supplies the RANS turbulence closure
- component material `rho`, `cp`, and `k` become solid-region properties
- internal-region and component watts become volumetric energy sources
- fan curves find their operating points against system resistance
- vents use their existing `free_area_ratio` and
  `vent_discharge_coeff` values

Rack-boundary fans and vents connect to ambient. A component intake vent on
the component boundary opens its internal air region to rack air; if the rack
side is itself open to ambient through a rack vent or intake, the fan-induced
pressure field draws ambient air through that path. Internal fans that do not
touch an exterior boundary remain internal momentum sources and circulate air
between their upstream and downstream internal fluid cells.

The exporter rejects or warns about common topology mistakes, including an
internal fan without fluid immediately upstream/downstream and intake/exhaust
directions inconsistent with an exterior boundary.

## 13. Selecting OpenFOAM from the model TOML

The smallest model-side section is:

```toml
[openfoam_solver]
enabled = true
template = "library/openfoam_cfg/screening_foam_cfg.toml"
case_directory = "C:/OpenFOAM/thermal_sim_v2/my_screening_case"
```

Use forward slashes in Windows TOML paths. Avoid spaces in the exported case
path because MPI/OpenFOAM shell tooling is substantially more reliable without
them.

To switch back to the native solver without changing the rack model:

```toml
[openfoam_solver]
enabled = false
```

The normal `[simulation].duration` remains authoritative for the default
OpenFOAM end time. A run-script command may explicitly override it, for
example `--multirate 18000`.

### 13.1 Template and override precedence

OpenFOAM settings are merged in this order, with later values winning:

1. built-in C++ defaults
2. the file named by `[openfoam_solver].template`
3. values written directly in the model's `[openfoam_solver]`
4. `[openfoam_solver.gravity]` and `[openfoam_solver.mesh]` inline overrides

An OpenFOAM profile may include a top-level `[mesh]`. That mesh replaces the
model's root `[mesh]` for OpenFOAM export only. This is how the same physical
model can use a fast screening mesh or a denser validation mesh. The highest
priority mesh override is:

```toml
[openfoam_solver.mesh]
adaptive = true
fine_dx = 0.025
coarse_dx = 0.15
refinement_margin = 0.01
```

The root `[mesh]` remains the native solver mesh. Older OpenFOAM templates that
do not contain `[mesh]` preserve the root model mesh.

### 13.2 Included profiles

Three reusable profiles are in `library/openfoam_cfg`:

| Profile | Intended use |
|---|---|
| `default_foam_cfg.toml` | General starting point and compatibility default. |
| `screening_foam_cfg.toml` | Faster ballpark tuning of heat loads, fan curves, vent resistance, and layout. |
| `indepth_foam_cfg.toml` | Final transient and design validation with stricter airflow and thermal criteria. |

For screening:

```toml
template = "library/openfoam_cfg/screening_foam_cfg.toml"
case_directory = "C:/OpenFOAM/thermal_sim_v2/production_rack_screening"
```

For in-depth validation:

```toml
template = "library/openfoam_cfg/indepth_foam_cfg.toml"
case_directory = "C:/OpenFOAM/thermal_sim_v2/production_rack"
```

Always use different case directories for screening and in-depth cases.
`overwrite = true` deliberately replaces an existing exported case and its
restart/convergence state.

## 14. How coarse and fine OpenFOAM mesh settings work

Both included OpenFOAM profiles use the same adaptive rectilinear planner:

```toml
[mesh]
adaptive = true
fine_dx = 0.02
coarse_dx = 0.20
refinement_margin = 0.005
```

The three values have distinct jobs:

- `fine_dx` is the target spacing in refinement bands surrounding component
  faces, internal-region boundaries, fans, and vents.
- `coarse_dx` is the target spacing in open rack volumes away from stamped
  geometry.
- `refinement_margin` expands each fine band beyond the geometry surface. It
  changes where fine cells are used, not the physical component dimensions.

A rectangular fan or vent that spans an entire rack-face direction has no
interior edge in that tangential direction: both footprint edges already lie
on rack boundaries. The adaptive planner therefore retains its exact cuts but
does not mark that whole axis fine. Partial-face openings still refine around
their tangential edges. This keeps full-face validation inlets/outlets from
silently turning local component refinement into a global fine mesh.

Exact geometry cuts are always inserted at important boundaries. Therefore,
`fine_dx` and `coarse_dx` are targets rather than guaranteed minimum cell
widths. A small clearance or two nearby exact cuts can create a cell narrower
than `fine_dx`.

Cuts closer than one quarter of `fine_dx` are merged to avoid rack-wide sliver
planes. Rack bounds take priority, then component envelopes, then internal
regions/openings, and refinement-band edges are last. A merged coordinate is
stamped to the nearest retained face. This makes represented component volume
independent of `coarse_dx` and `refinement_margin`; the former outward-only
rounding could change a 1U component volume by nearly 50% between profiles.
At the standard 20 mm fine spacing, an effective enclosure must be at least
5 mm thick to retain its own inner and outer cuts. Required chassis walls,
equivalent heat blocks, and air gaps between a heat block and its enclosing
tunnel are each assigned at least two cells. This targeted through-thickness
rule prevents singular one-cell material layers without reducing `fine_dx`
throughout the rack.

The screening profile currently uses:

```toml
fine_dx = 0.02
coarse_dx = 0.10
refinement_margin = 0.02
```

The in-depth profile uses:

```toml
fine_dx = 0.015
coarse_dx = 0.10
refinement_margin = 0.02
```

The profiles intentionally use different spatial resolutions. On the current
generic production rack, screening exports 208,772 cells and in-depth exports
335,580 cells, so in-depth is 60.7% larger. Screening is accelerated by that
coarser local mesh, writing less often, using larger implicit thermal steps,
and refreshing airflow less often. A separate same-mesh 30-step comparison is
used when isolating solver-policy changes; do not cite that comparison as a
mesh-convergence result. Blindly increasing `fine_dx` can delete a thin air
channel or strand a fan against solid cells.

The former meshes produced zero/near-zero determinants in one-cell-thick
generic chassis, heat-block, and air-gap layers. The targeted rule raises the
screening mesh by 24.8% and the in-depth mesh by 16.2%, but the current generic
production model now passes full `checkMesh` in the fluid and all four solid
regions. Minimum determinants are 0.001384 in fluid, 0.0534 in Eaton, 0.0133
in Dell, 0.0704 in Trenton, and 0.00840 in KVM on screening. The preparation
script still saves `checkMesh.prepare.log` and warns explicitly if a different
model fails, because OpenFOAM can return status zero despite reported failures.

The fanless generic KVM uses an air-tunnel cross-section exactly equal to its
front passive vent and extending to the front face, with a 5 mm rear wall. This
avoids a narrow one-cell bezel/tunnel mismatch and prevents an unresisted
opening around the vent. It remains a rack-level effective geometry rather
than a literal internal CAD model.

The corrected 208,772-cell screening case was warm-mapped from the earlier
19,200 s solution and then run with 2,400 s implicit thermal intervals plus
coupled refreshes. Two consecutive thermal/airflow checkpoints passed at
26,400.02 s and 28,800.02 s. At the final checkpoint the largest
per-component hotspot rate was Trenton at 0.0572 K per 300 s (screening limit
0.25 K per 300 s); airflow mass imbalance was 0.0041% and the largest device
flow change was 0.0444%. This validates convergence of this screening workflow,
not mesh independence or literal internal component temperatures.

When making a new component more accurate:

1. Place fans slightly inside the enclosure when they are internal devices.
2. Ensure an air region reaches both sides of every internal fan plane.
3. Keep boundary vents on the component face they open.
4. Run a mesh/export smoke test.
5. Check the printed cell count and any minimum-cell-width warnings.
6. Compare screening results at two mesh resolutions before treating a hotspot
   as mesh-independent.

The native solver's `[multistage.coarse_mesh]` is a different concept. It is a
coarse warm-start mesh used only by the native explicit backend. The
OpenFOAM-profile `[mesh]` directly controls the exported OpenFOAM case.

## 15. Every OpenFOAM profile setting

### 15.1 Case, parallelism, and output

| Setting | Meaning |
|---|---|
| `enabled` | Selects OpenFOAM export instead of running the native solver. |
| `template` | Reusable OpenFOAM TOML profile loaded before inline overrides. |
| `case_directory` | Destination for the generated case. Relative paths are relative to the project; absolute Windows paths are supported. |
| `overwrite` | Allows a new export to replace the existing case directory and old restart markers. |
| `parallel_processes` | Default MPI process count written into instructions and decomposition settings. |
| `maximum_time_step` | Largest timestep during fully coupled airflow/CHT stages. |
| `maximum_courant_number` | Coupled-stage Courant limit. Multirate live-flow stages measure the saved field's global `max(Co)`, apply a 20% safety margin, and use an exact divisible fixed timestep no larger than the configured airflow cap. |
| `field_write_interval` | Simulated seconds between full restart/visualization field writes. |
| `saved_time_directories` | Number of recent nonzero processor checkpoints retained; time `0` is also preserved. |
| `report_interval` | Simulated seconds between function-object reports such as temperature extrema, component averages, mass flow, and y-plus. |

`field_write_interval` controls large field output. `report_interval` controls
small diagnostic output. They do not have to match.

### 15.2 Turbulence, air properties, gravity, fans, and vents

| Setting | Meaning |
|---|---|
| `use_k_omega_sst` | Enables the k-omega SST RANS model and its `k`, `omega`, and `nut` fields. |
| `inlet_turbulence_intensity` | Fractional inlet turbulence intensity; `0.05` means 5%. |
| `turbulence_length_scale` | Inlet turbulence length scale in meters. |
| `turbulent_prandtl_number` | Turbulent Prandtl number used for turbulent heat transport. |
| `temperature_dependent_air` | Uses perfect-gas density and temperature-dependent Sutherland viscosity. |
| `sutherland_temperature` | Sutherland temperature constant in kelvin. |
| `use_vent_pressure_loss` | Converts vent free area and discharge coefficient into porous/orifice resistance. |
| `use_fan_curves` | Uses referenced P-Q curves instead of treating every fan as fixed flow. |
| `fan_curve_extension_multiplier` | Limits how far the numerical fan curve may be extended beyond its nominal flow range. |
| `gravity.x/y/z` | Gravity vector in m/s²; the default is `(0, 0, -9.80665)`. |

The component or rack vent remains authoritative for
`free_area_ratio` and `vent_discharge_coeff`; do not duplicate them in the
OpenFOAM profile. Fan `curve`, `cfm`, `flow_type`, direction, and geometry also
remain in the component/model TOML.

### 15.3 Startup and multirate thermal execution

| Setting | Meaning |
|---|---|
| `use_multirate_thermal` | Alternates expensive fully coupled airflow windows with implicit thermal-only periods that hold airflow fixed. |
| `airflow_warmup_time` | Safety limit, in physical seconds, for finding the initial airflow operating point. Adaptive initialization may stop much earlier. |
| `use_fan_startup_ramp` | Gradually scales fan sources from zero to full strength to avoid an impulsive startup. |
| `fan_startup_ramp_time` | Total physical duration of the fan ramp. |
| `fan_startup_ramp_steps` | Number of discrete fan-scale stages in the ramp. |
| `initial_airflow_check_interval` | Physical seconds between initial-airflow convergence checks. |
| `minimum_initial_airflow_duration` | Minimum post-ramp physical airflow duration before convergence can be accepted. |
| `thermal_only_maximum_time_step` | Maximum implicit timestep while velocity/pressure are held fixed. This is the main thermal acceleration control. |
| `thermal_only_maximum_courant_number` | Safety Courant bound used in thermal-only mode. The velocity field is frozen, so this is intentionally much larger than coupled `maxCo`. |
| `airflow_refresh_interval` | Thermal simulated seconds between fully coupled airflow refreshes. |

OpenFOAM's energy equation is implicit, so a thermal-only timestep may be far
larger than the fully coupled CFD timestep. A large implicit step can be stable
without being accurate; use screening for tuning and in-depth results for final
claims.

Do not shorten startup acceptance based only on a small change across one
0.01 s window. In the generic-rack benchmark, the former 0.02 s minimum
accepted the operating point at 0.16 s even though the nine rack-fan flows were
about 29% below the result obtained after a 0.30 s post-ramp minimum. Internal
device flow also overshot and slowly reversed while adjacent changes were less
than 1%. The supplied profiles therefore require at least 0.30 s of post-ramp
airflow and retain a conservative 0.001 s coupled-flow timestep.

During the fan ramp, the runner seeds `deltaT` below the configured Courant
limit and enables OpenFOAM adaptive stepping with `maxCo` and
`airflow_maximum_time_step` as hard caps. Each ramp stage uses
`adjustableRunTime`, so its requested endpoint is still written exactly. The
former fixed rule used `maxCo/10` of the airflow timestep cap: on the current
288,757-cell production rack it forced 50 steps to reach 0.005 s even though
peak Co was only 0.095. Adaptive startup reached the same endpoint in 10 steps,
kept peak Co at 0.454 (below the in-depth limit of 1), and reduced end-to-end
wall time from 443.47 s to 204.79 s (53.8%). The ramp is a numerical startup
device, so transient velocity fields inside the ramp are not expected to match
step-for-step; validate the fully established airflow operating point.

### 15.4 Adaptive airflow refresh

| Setting | Meaning |
|---|---|
| `airflow_refresh_duration` | Minimum physical duration of a refresh before flow metrics may accept it. |
| `use_adaptive_airflow_refresh` | Stops a refresh when mass balance, device-flow change, and direction checks pass. Within one runner invocation, the first live window is compared with the last accepted operating point before the thermal-only interval. |
| `airflow_refresh_maximum_courant_number` | `maxCo` during refresh windows. |
| `airflow_refresh_check_interval` | Physical seconds added between refresh convergence checks. |
| `maximum_airflow_refresh_duration` | Safety limit; failure to converge before this duration stops the run with an error. |
| `maximum_mass_imbalance_fraction` | Maximum exterior net mass imbalance divided by half the sum of absolute exterior mass flows. |
| `maximum_device_flow_change_fraction` | Maximum relative change in tracked fan/device flow between comparison windows. |
| `minimum_tracked_boundary_flow_fraction` | Exterior flow below this fraction of total one-way rack throughput is negligible for stability only when both consecutive samples remain below the floor. |

Initial airflow requires one measurement to establish a baseline before a
flow-change decision can pass. Later adaptive refreshes retain the last
accepted flow vector in the running process, so their first live window checks
both the thermally accumulated operating-point change and current stability.
A restarted runner conservatively reacquires a baseline because that in-memory
vector is unavailable. Its `airflow_warmup_time` limit is measured forward
from the current checkpoint, so mapped or ordinary nonzero-time restarts do
not incorrectly compare their time against an absolute five-second limit.
Flow direction checks ensure intake/exhaust devices are operating in their
intended directions.

The screening profile uses a short `airflow_refresh_duration = 0.01`. Once the
initial baseline exists, an unchanged operating point can pass after one
window; a meaningful change triggers additional comparison windows. Screening,
default, validation, and in-depth profiles all use a 1% device-flow tolerance.
In-depth and validation retain longer minimum refresh windows for
higher-fidelity updates.

The screening and in-depth profiles use
`minimum_tracked_boundary_flow_fraction = 0.001` to reject sign and
cancellation noise below 0.1% of rack throughput. This prevents numerical
noise through an effectively stagnant passive opening from blocking the whole
rack indefinitely. The floor is computed from half the sum of absolute
exterior mass flows, so it scales with rack throughput. A boundary device that
crosses the floor is still compared and cannot be hidden by this setting.
Internal fan operating points are never suppressed by the boundary-flow floor.

For a refresh-duration study, preserve a case with written live-flow times and
compare internal velocity and temperature fields directly:

```powershell
python .\tools\openfoam_field_convergence.py CASE `
  --times 7200.02 7200.10 7200.50 7201.00 `
  --fields U T `
  --reference final `
  --csv CASE\field_convergence.csv
```

The report automatically reads reconstructed root times when every requested
time is available there, even if newer processor directories also exist. Use
`--case-type reconstructed` or `--case-type decomposed` only to override that
selection. It uses cell-volume weighting and prints RMS, maximum, and relative-RMS
differences for every region plus the full case. Boundary-flow stability alone
is not sufficient evidence for shortening a high-fidelity refresh; confirm that
the internal `U` field is also insensitive to the longer reference duration.

### 15.5 Automatic thermal convergence

| Setting | Meaning |
|---|---|
| `stop_when_thermally_converged` | Allows a requested long run to stop early after both thermal and airflow criteria pass. |
| `minimum_thermal_convergence_time` | Earliest simulated time at which thermal convergence may be accepted. |
| `thermal_convergence_reference_interval` | Reference seconds used to normalize temperature changes from checkpoints with different spacing. |
| `maximum_temperature_change` | Maximum allowed change of the fluid peak or any individual component peak, in K per reference interval. |
| `maximum_component_average_temperature_change` | Maximum allowed component volume-average change, in K per reference interval. |
| `thermal_convergence_required_checkpoints` | Number of consecutive accepted thermal checkpoints required. |

At each checkpoint the run script records:

- fluid peak temperature and the peak temperature of every component region
- volume-average temperature of every exported component solid region
- exterior mass imbalance
- tracked fan/device-flow change
- fan direction validity

The state is persisted in hidden case files so a restarted run continues the
convergence history. OpenFOAM may rotate function-object files after a restart
(`volFieldValue_*.dat`); the generated parser reads both original and rotated
files.

These automatic criteria establish engineering convergence of peak and
component-average temperatures plus device and ambient flows. They do not
assert that every instantaneous turbulent velocity cell is stationary. For a
final CFD validation, also compare reconstructed checkpoint fields with
`openfoam_field_convergence.py` and state the accepted `U` and `T` RMS limits.

## 16. Exporting and running the OpenFOAM suite from TOML

### 16.1 Build the TOML runner

From PowerShell in the project directory:

```powershell
g++ -std=c++17 -O3 -DNDEBUG -fopenmp .\model_runner.cpp -o model.exe
```

Export the default model:

```powershell
.\model.exe .\library\models\model.toml
```

The runner loads `library/fan_curves/fan_curves.toml` by default. To select a
different library:

```powershell
.\model.exe .\library\models\model.toml .\library\fan_curves\fan_curves.toml
```

When OpenFOAM is enabled, this command exports only; it does not launch WSL or
the CFD solver. The final console output prints a copy-ready WSL solver command
followed by a copy-ready visualization command. Run the visualization command
after `run_parallel.sh` has reconstructed the latest saved time. It uses
`plot/heat_animation.py`, overlays `geometry.txt`, selects a `y`-normal slice,
reports temperatures in Celsius, and saves `temperature_latest.png` in the
exported case. Other slices, times, interactive display, and output formats are
documented in Section 17.2.

`foam_main.cpp` is a direct C++ demonstration/export smoke case. It is useful
for development, but the TOML-driven production entry point is
`model_runner.cpp`.

Build that direct demonstration with:

```powershell
g++ -std=c++17 -O3 -DNDEBUG -fopenmp .\foam_main.cpp -o foam_model.exe
.\foam_model.exe "C:\OpenFOAM\thermal_sim_v2\basic_rack"
```

### 16.2 Confirm OpenFOAM in WSL2

Open an Ubuntu WSL terminal:

```bash
env -u LD_LIBRARY_PATH bash -lc 'source /usr/lib/openfoam/openfoam2606/etc/bashrc && foamVersion'
```

If an old OpenFOAM installation added incompatible libraries to
`LD_LIBRARY_PATH`, clearing that variable avoids errors such as missing modern
`GLIBCXX` symbols in Ubuntu tools.

The generated run scripts locate/source OpenFOAM, but this command is a useful
installation check:

```bash
source /usr/lib/openfoam/openfoam2606/etc/bashrc
which decomposePar
which reconstructPar
foamVersion
```

### 16.3 Convert a Windows case path to WSL

Windows:

```text
C:/OpenFOAM/thermal_sim_v2/production_rack_screening
```

WSL:

```text
/mnt/c/OpenFOAM/thermal_sim_v2/production_rack_screening
```

Run directly from the exported Windows directory:

```bash
cd /mnt/c/OpenFOAM/thermal_sim_v2/production_rack_screening
./run_parallel.sh 4 --multirate 18000
```

For better I/O performance on large cases, copy the case to WSL's native
filesystem:

```bash
mkdir -p ~/OpenFOAM/cases
cp -a /mnt/c/OpenFOAM/thermal_sim_v2/production_rack_screening \
  ~/OpenFOAM/cases/
cd ~/OpenFOAM/cases/production_rack_screening
./run_parallel.sh 4 --multirate 18000 2>&1 | tee log.screening18000
```

### 16.4 Generated run modes

From inside the case directory:

```bash
# Serial preparation and conventional coupled run
./run_cht.sh

# Parallel conventional coupled run to the TOML duration
./run_parallel.sh 4

# Parallel conventional run with an explicit end time
./run_parallel.sh 4 run 18000

# Restartable initial warm start
./run_parallel.sh 4 --warm-start 5

# Recommended accelerated airflow/thermal schedule
./run_parallel.sh 4 --multirate 18000

# Continue to 100000 s while refreshing airflow every 10000 simulated seconds
./run_parallel.sh 4 --multirate 100000 10000
```

The optional fourth argument overrides `airflow_refresh_interval` for that
invocation. It changes only the spacing between completed thermal-only stages;
the initial operating-point solve and each adaptive airflow refresh still use
the profile's convergence limits. Do not use a long override when temperatures
or buoyancy are changing rapidly.

`airflow_maximum_time_step` independently caps every live-airflow step. Before
each restarted live-flow stage, the runner evaluates `CourantNo` from the saved
parallel `phi` and `rho` fields, reduces the proposed timestep to 80% of the
configured `maxCo` when necessary, and divides the stage into equal fixed steps
that land exactly on the refresh target. A conservative `maxCo/10` fallback is
used before a usable saved flow field exists. This avoids OpenFOAM
`adjustableRunTime` write alignment enlarging a timestep beyond `maxDeltaT`.
The runner re-evaluates the final saved field after every live-flow stage and
stops with a diagnostic if the measured `max(Co)` exceeds the stage limit.
Fan-startup-ramp stages use the same conservative fixed-step fallback because
no established operating-point field exists yet.
Keep this cap separate from
`airflow_refresh_check_interval`: a 1 s in-depth comparison window still needs
many Courant-safe flow steps, not ten 0.1 s steps. The shipped profiles use
0.001 s. Increase it only after measuring the maximum Courant number and field
differences on the actual hot rack; changing the comparison interval must never
silently enlarge the CFD timestep.

Each airflow convergence line also reports `estimatedAirExchangeTime`. The
runner calculates it as exported fluid volume times configured ambient density
divided by one-way exterior mass throughput (half the sum of absolute exterior
patch flows). This is a nominal transport timescale, not a convergence test:
compare it with the actual coupled-flow window and verify internal `U` fields
before treating recirculation as settled.

Both generated solver launchers take an exclusive per-case lock before they
prepare, decompose, or advance a case. A second `run_cht.sh` or
`run_parallel.sh` process exits instead of concurrently writing the same time
directories. Plotting and report scripts remain usable while the solver runs
because they do not acquire this write lock. The hidden
`.thermal_solver_run.lock` file may remain after a run; the operating-system
lock, not the file's presence, determines whether a solver is active.

The multirate sequence is:

1. prepare/decompose the regions if needed
2. ramp fan strength
3. find the initial airflow operating point
4. freeze airflow and advance the implicit energy equations
5. periodically unfreeze and refresh airflow
6. evaluate thermal and airflow convergence
7. stop early when configured criteria pass, or continue to the requested end
   time
8. reconstruct the latest result for visualization

The script reuses valid processor partitions and the latest saved time. If a
refresh was interrupted, `.airflow_refresh_pending` causes it to be retried.
Do not delete hidden marker/state files when you intend to resume.

`plot/recirculation_report.py` merges every OpenFOAM restart-segment report,
including suffixed `surfaceFieldValue_<time>.dat` files. In addition to signed
flow, boundary temperature, and the thermal re-ingestion index, its CSV and
fourth plot panel report net sensible heat rejected relative to ambient. The
expected applied heat is summed automatically from
`constant/openfoamExportProperties`; use `--expected-heat-watts` only to
override that exported value. A heat-rejection fraction below 100% during a
transient indicates that energy is still accumulating in the rack or leaving
through non-airflow paths, so it is a useful independent convergence check.

Mass-weighted temperature is undefined when a boundary's mass flow approaches
zero. OpenFOAM may emit an arbitrarily large signed value for such a sample.
The recirculation tool therefore omits boundary-temperature samples at or below
`--minimum-flow-fraction` times total one-way rack throughput (default
`0.0001`) from temperature, heat-rejection, and re-ingestion calculations.
Signed flow is still plotted, so a formerly stagnant opening becoming active
remains visible.

The companion `*_latest_face_flow.csv` reads reconstructed `phi` and `T`
faces directly and separates inward from outward traffic on every patch. Use
its inward/outward mass flow and temperatures for passive openings that carry
simultaneous ingress and egress; their net signed mass-weighted temperature can
fall outside the physical face-temperature range and is not meaningful.

### 16.5 How OpenFOAM saves fields and resumes in time segments

OpenFOAM saves a simulation state in directories named by **simulated time**.
The initial condition is in `0`; later directories may be named `300`, `600`,
`900`, and so on. Each saved time contains the fields needed to resume, such
as temperature `T`, velocity `U`, pressure `p`/`p_rgh`, density-related state,
and turbulence fields. Solid and fluid fields are stored separately by region.

In a parallel run, the authoritative restart data remains decomposed:

```text
processor0/300/fluid/T
processor0/300/Dell_PowerEdge_R470_1U_1/T
processor1/300/...
processor2/300/...
processor3/300/...
```

Every `processorN` directory owns its portion of the mesh and fields. A
reconstructed time appears at the case root:

```text
300/fluid/T
300/Dell_PowerEdge_R470_1U_1/T
```

Reconstructed data is convenient for ParaView and Python, but the generated
runner finds and resumes from the latest **processor** time. At the end of a
run it reconstructs the latest time with `reconstructPar -allRegions`.

The important time controls are:

- `startFrom` selects whether OpenFOAM begins from the original start time or
  a specified/latest saved time.
- `startTime` is the absolute simulated time of the current segment.
- `endTime` is an absolute target, not the length of the segment.
- `writeInterval` determines when complete restart/visualization fields are
  written.
- `purgeWrite` bounds writes made inside one solver invocation. Multirate runs
  launch many short solver invocations, so the generated wrapper also prunes
  completed numeric `processorN` checkpoints after restart fields are copied.
  It preserves time `0` plus the newest `saved_time_directories` nonzero times.
  This second layer is required to bound disk usage across adaptive airflow
  windows, fan-ramp stages, and thermal segments.

Therefore, the segment from 300 to 600 seconds uses `startTime = 300` and
`endTime = 600`; it does **not** use `endTime = 300` again. Time and thermal
history continue from the saved 300-second fields.

#### Manually run 0–300, 300–600, and 600–900

Use `--warm-start` with successive absolute end times:

```bash
cd ~/OpenFOAM/cases/production_rack_screening

# Starts from 0 and saves/reconstructs the state at t=300 s.
./run_parallel.sh 4 --warm-start 300 2>&1 | tee log.0-300

# Detects processor*/300, resumes it, and advances to t=600 s.
./run_parallel.sh 4 --warm-start 600 2>&1 | tee log.300-600

# Detects processor*/600 and advances to t=900 s.
./run_parallel.sh 4 --warm-start 900 2>&1 | tee log.600-900
```

Each invocation asks `foamListTimes -processor -latestTime` for the newest
valid decomposed state. The runner rejects an end time that is not greater
than the latest saved time. It also updates the saved `uniform/time` timestep
metadata so OpenFOAM does not inherit an inappropriate timestep from the
previous mode.

For short warm starts after a thermal-only segment, the runner also seeds
`deltaT` to no more than the requested interval before launching the coupled
solver. This prevents a retained 100 s or 1000 s thermal timestep from making
OpenFOAM treat a 0.01 s warm start as already complete.

Warm-start timing is configured only after processor reuse or reconstruction
has selected the authoritative checkpoint. This matters when processor data is
newer than the last reconstructed root time. Absolute `startTime`, `endTime`,
and fractional write intervals are written at 17-digit precision. Later
dictionary updates use the same precision, so they cannot round an earlier
value. A request such as `--warm-start 22800.08` cannot be rounded to `22800.1`
and is guaranteed to write the requested endpoint.

These manual warm-start segments are fully coupled CHT runs. For long thermal
transients, multirate mode is normally faster.

When mapping a solution onto a different mesh with `mapFields`, do not enter
thermal-only mode directly. `mapFields` interpolates volume fields such as `U`
and `T` but does not transfer the face-flux field `phi`. First run a short fully
coupled `--warm-start` on the target mesh so OpenFOAM creates a consistent
`phi`, pressure, and velocity state; then use `--multirate` for thermal
equilibration. Also keep only one spelling of the mapped root time (for example,
do not retain both `730000.1` and `730000.09999999998`) before decomposition,
because equal numeric times with different directory names are ambiguous. The
generated runner rejects both unsafe conditions before decomposition: duplicate
numeric root times exit with an explicit directory pair, and a nonzero
`--multirate` restart without reconstructed or processor `phi` exits with the
required `--warm-start` instruction.

#### Automatically use 300-second multirate segments

Set the profile to refresh airflow every 300 simulated seconds:

```toml
[openfoam_solver]
use_multirate_thermal = true
airflow_refresh_interval = 300.0
field_write_interval = 300.0
report_interval = 60.0
saved_time_directories = 4
```

Then launch one continuous request:

```bash
./run_parallel.sh 4 --multirate 18000 2>&1 | tee log.multirate18000
```

The script handles the sequence internally:

```text
initial airflow -> thermal to 300 -> airflow refresh
                -> thermal 300–600 -> airflow refresh
                -> thermal 600–900 -> airflow refresh -> ...
```

At each thermal stage boundary, the runner sets an absolute target, writes a
complete decomposed restart state, checks convergence, and continues from that
state. The airflow refresh advances physical CFD time by a small amount, so
actual directory names can be slightly above a round boundary—for example
`600.03` after a 0.03-second refresh. The next thermal segment starts from that
exact saved time; no thermal history is lost.

`field_write_interval` controls full field data used for restart and spatial
visualization. `report_interval` controls smaller function-object results in
`postProcessing`, including temperature extrema, component averages, mass
flows, and y-plus. Logs written with `tee` are plain-text solver histories and
are independent of the OpenFOAM field directories.

If you need to retain 300, 600, 900, and additional earlier field snapshots
simultaneously, increase `saved_time_directories`. With the default value of
three, older full field directories are purged as newer ones are written, but
the smaller `postProcessing` histories and text log remain available.

Useful inspection commands are:

```bash
# List reconstructed root times.
foamListTimes -case .

# List decomposed restart times and print the latest one.
foamListTimes -case . -processor
foamListTimes -case . -processor -latestTime

# Reconstruct a particular retained time for plotting.
reconstructPar -case . -allRegions -time 600

# Reconstruct only the latest retained processor time.
latest=$(foamListTimes -case . -processor -latestTime | tail -1)
reconstructPar -case . -allRegions -time "$latest"
```

Do not manually copy only one processor directory or only one region. A valid
parallel restart requires the matching time directory for every processor and
all regions.

### 16.6 Generated case contents

Important generated files include:

| Path | Purpose |
|---|---|
| `geometry.txt` | Rack, component, internal-region, fan, and vent geometry used by the Python overlay. |
| `constant/polyMesh` | Exported rectilinear volume mesh. |
| `constant/regionProperties` | Fluid/solid region list for CHT. |
| `constant/g` | Gravity vector. |
| `constant/fluid/thermophysicalProperties` | Temperature-dependent air model. |
| `constant/fluid/fvOptions` | Fan, vent/porosity, and fluid source definitions. |
| `constant/<solid>/thermophysicalProperties` | Component material properties. |
| `constant/<solid>/fvOptions` | Volumetric heat sources. |
| `0/<region>` | Initial temperature, pressure, velocity, and turbulence fields. |
| `system/controlDict` | Time, write, function-object, and run controls. |
| `system/<region>/fvSchemes` | Discretization schemes. |
| `system/<region>/fvSolution` | Linear solvers and coupling controls. |
| `prepare_regions.sh` | Region preparation helper. |
| `run_cht.sh` | Serial case runner. |
| `run_parallel.sh` | Parallel, restart, warm-start, and multirate runner. |
| `postProcessing` | Temperature ranges/averages, flow reports, and y-plus data. |

## 17. Viewing and plotting temperatures

### 17.1 ParaView

After a run reconstructs its latest fields:

```bash
cd ~/OpenFOAM/cases/production_rack_screening
touch production_rack_screening.foam
paraFoam -builtin
```

If WSL GUI support is unavailable, open the reconstructed case from a Windows
ParaView installation. Color the fluid or solid region by `T`. OpenFOAM stores
temperature in kelvin.

Useful views:

- a `y`-normal slice for front-to-rear airflow/temperature structure
- component solid surfaces colored by `T`
- velocity glyphs or streamlines from `U`
- pressure colored by `p_rgh`
- `k`, `omega`, `nut`, and wall `yPlus` when checking turbulence behavior

### 17.2 Python/PyVista OpenFOAM overlay

Install plotting dependencies in the Python environment used from PowerShell:

```powershell
python -m pip install pyvista matplotlib numpy imageio imageio-ffmpeg
```

Plot the latest result with rack/component geometry overlaid:

```powershell
python .\plot\heat_animation.py `
  --format openfoam `
  --case "C:\OpenFOAM\thermal_sim_v2\production_rack_screening" `
  --time latest
```

Save a PNG:

```powershell
python .\plot\heat_animation.py `
  --format openfoam `
  --case "C:\OpenFOAM\thermal_sim_v2\production_rack_screening" `
  --time latest `
  --slice-axis y `
  --temperature-units C `
  --output .\plot\screening_latest.png `
  --save
```

Animate the complete 3D rack through all written OpenFOAM result times:

```powershell
python .\plot\heat_animation.py `
  --format openfoam `
  --case "C:\OpenFOAM\thermal_sim_v2\production_rack_screening" `
  --animate `
  --slice-axis none `
  --opacity 0.35 `
  --temperature-units C `
  --fps 15 `
  --skip 1 `
  --output .\plot\screening_full_rack.mp4 `
  --save
```

Use `--start-time 300 --end-time 18000` to restrict the inclusive time range.
Use `--skip 5` for a quick preview containing every fifth written result. A
`.gif` output name creates a GIF instead of MP4. The script scans the selected
times before rendering so every frame uses one fixed temperature color scale;
this prevents apparent temperature changes caused only by color-bar rescaling.
The animation uses OpenFOAM's written time directories, not every internal
solver time step, so its temporal resolution is controlled by the case's field
write interval.

Generate a convergence report from the same written results:

```powershell
python .\plot\heat_animation.py `
  --format openfoam `
  --case "C:\OpenFOAM\thermal_sim_v2\production_rack_screening" `
  --convergence-report `
  --temperature-units C `
  --skip 1 `
  --output .\plot\screening_temperature_convergence.png `
  --save
```

This writes both a PNG chart and a CSV with the same base name. The chart shows
fluid mean/maximum temperature, the rack-wide maximum, and each solid component
region's mean and maximum temperature over time. Means are volume weighted when
OpenFOAM cell volumes are available. The terminal summary reports each region's
final mean and maximum plus the maximum-temperature change and rate since the
previous written result. A small final rate is useful evidence of stabilization,
but it should be evaluated over several output intervals rather than treated as
an automatic proof of convergence.

Select a specific time and physical slice coordinate:

```powershell
python .\plot\heat_animation.py `
  --format openfoam `
  --case "C:\OpenFOAM\thermal_sim_v2\production_rack_screening" `
  --time 4800 `
  --slice-axis z `
  --slice-position 0.50 `
  --opacity 0.85 `
  --output .\plot\screening_z_050.png `
  --save
```

OpenFOAM plotting options:

| Option | Meaning |
|---|---|
| `--case` | Case directory; required for OpenFOAM input. |
| `--time latest` | Latest available time, or provide a numeric time. |
| `--animate` | Render a transient MP4/GIF from written OpenFOAM result times. |
| `--convergence-report` | Save temperature-history PNG and CSV reports. |
| `--start-time`, `--end-time` | Optional inclusive animation time range. |
| `--rack` | Optional geometry report; defaults to `<case>/geometry.txt`. |
| `--slice-axis x/y/z` | Slice normal. |
| `--slice-axis none` | Render region surfaces instead of a slice. |
| `--slice-position` | Physical coordinate of the slice; defaults to each region midpoint. |
| `--temperature-units C/K` | Display units. |
| `--opacity` | Geometry/field opacity. |
| `--fps` | Animation playback frames per second. |
| `--skip` | Animate every Nth written OpenFOAM result. |
| `--stride` | Plot every Nth cell when a lighter visualization is needed. |
| `--output` | Saved image path. |
| `--save` | Save instead of opening the interactive window. |

The script creates a harmless `.foam` reader marker in the case directory when
needed and supports reconstructed or decomposed cases. OpenFOAM mode reads
fields directly; no CSV conversion step is required.

### 17.3 Python fluid-results viewer

`plot/fluid_results.py` is the focused viewer for the OpenFOAM fluid volume. It
reads both reconstructed cases and live decomposed `processor*` results, ignores
solid regions, prints the selected field's minimum/mean/maximum, and opens an
interactive PyVista window by default.

View the latest fluid temperature with velocity arrows on a front-to-rear
slice:

```powershell
python .\plot\fluid_results.py `
  --case "C:\OpenFOAM\thermal_sim_v2\model_generic_airside_screening_20260813" `
  --time latest `
  --field temperature `
  --slice-axis y `
  --vectors
```

View velocity magnitude on a horizontal plane and save it without opening a
window:

```powershell
python .\plot\fluid_results.py `
  --case "C:\OpenFOAM\thermal_sim_v2\model_generic_airside_screening_20260813" `
  --field speed `
  --slice-axis z `
  --slice-position 1.0 `
  --vectors `
  --save `
  --output .\fluid_speed_z1m.png
```

View the complete fluid-volume surface colored by pressure:

```powershell
python .\plot\fluid_results.py `
  --case "C:\OpenFOAM\thermal_sim_v2\model_generic_airside_screening_20260813" `
  --field p_rgh `
  --slice-axis none
```

Accepted convenient field names are `temperature`, `speed`, `pressure`,
`p_rgh`, `k`, `omega`, `nut`, and `alphat`. An exact OpenFOAM field name can
also be supplied. Use `--clim MIN MAX` to hold a common color scale across
screenshots, `--vector-factor` to reduce arrow density, `--vector-scale` to
change arrow length, `--opacity` to see through the plotted volume, and
`--no-geometry` to hide the rack/component wireframe. If a requested field is
not present at the selected time, the tool prints every available fluid field.

Overlay long airflow streamlines and explicit temperature contour lines:

```powershell
python .\plot\fluid_results.py `
  --case "C:\OpenFOAM\thermal_sim_v2\model_generic_airside_screening_20260813" `
  --field temperature `
  --slice-axis y `
  --contours 15 `
  --streamlines `
  --seed vents `
  --streamline-direction both `
  --streamline-color speed
```

Streamlines are integrated through the saved OpenFOAM `U` field. `--seed auto`
uses rack vents when geometry metadata are available and otherwise uses a plane;
select `plane`, `fans`, or `vents` explicitly when needed. Plane seeds use
`--seed-axis` and `--seed-position`. Control density with `--seed-count`, maximum
path length with `--streamline-length`, and tube thickness with
`--streamline-radius 0.002`; use a zero radius for thin lines. Streamlines can
be colored by `speed`, `temperature`, the displayed `field`, or a solid color.
Contour lines are calculated on the selected scalar slice; `--contours 15`
draws fifteen levels across the active or explicitly supplied `--clim` range.

### 17.4 Native-solver plotting

The original CSV workflow remains available:

```powershell
python .\plot\heat_animation.py `
  --format native `
  --sim .\simulation.csv `
  --rack .\output.txt
```

Save an animation:

```powershell
python .\plot\heat_animation.py `
  --format native `
  --sim .\simulation.csv `
  --rack .\output.txt `
  --output .\plot\rack_temperature_animation.mp4 `
  --fps 15 `
  --skip 2 `
  --save
```

## 18. OpenFOAM troubleshooting and recommended workflow

### Package commands fail with `GLIBCXX_* not found`

An old OpenFOAM environment is ahead of Ubuntu's system libraries. Run package
tools without the inherited library path:

```bash
env -u LD_LIBRARY_PATH sudo apt-get update
env -u LD_LIBRARY_PATH sudo apt-get install -y openfoam2606-default
```

Remove or update any shell startup line that automatically sources an obsolete
OpenFOAM release before normal Ubuntu commands.

### `Unable to locate package openfoam2606-default`

Confirm the OpenFOAM repository is installed for the Ubuntu release, run
`env -u LD_LIBRARY_PATH sudo apt-get update`, then inspect:

```bash
env -u LD_LIBRARY_PATH apt-cache policy openfoam2606-default
```

### Internal fan has no upstream/downstream fluid cell

- keep the fan center inside its intended internal air region
- leave fluid clearance on both sides of the fan plane
- ensure the direction vector points from upstream to downstream
- keep the local `fine_dx` small enough to resolve that clearance
- do not treat a component-boundary opening as an internal fan unless it has
  fluid on both sides

### Coupled timestep is extremely small

That is normal when fast fan jets cross small cells. OpenFOAM's energy equation
is implicit, but the coupled momentum/pressure stages remain Courant-limited.
Use:

- fan startup ramping
- adaptive initial-airflow convergence
- multirate thermal mode
- larger implicit thermal-only steps
- periodic short airflow refreshes
- a screening mesh/profile for tuning

Do not simply raise coupled `maximum_courant_number` until the solver becomes
unstable. Check pressure residuals, continuity, directions, and fan flows.

### Results are implausibly hot

Check, in this order:

1. integrated watts for every component/internal solid region
2. whether watts were accidentally assigned to both the component shell and
   its internal heat-source regions
3. fan operating-point flows and directions
4. exterior intake area, free-area ratio, and discharge coefficient
5. internal air paths and blocked clearances
6. component material density, heat capacity, and conductivity
7. mesh sensitivity
8. comparison with thermal-camera or probe measurements

For calibration when equipment cannot be shut down, record steady surface
temperatures and ambient/intake temperature under known operating workloads.
Tune uncertain heat allocation and contact/effective conductivity against
those measurements, then validate on a different workload rather than fitting
and validating on the same data.

### Recommended end-to-end workflow

1. Build and export with `screening_foam_cfg.toml`.
2. Run a short multirate screening case.
3. Inspect airflow mass balance, device flow, directions, and temperature
   bounds.
4. Tune uncertain watts, fan curves, vents, and layout.
5. Repeat until the ballpark model is credible.
6. Switch to `indepth_foam_cfg.toml` and a new case directory.
7. Run to automatic thermal and airflow convergence or the required end time.
8. Compare screening and in-depth component averages, peak location, exterior
   mass flow, and fan operating points.
9. Plot the final fields and archive the model TOML, profile, case log, and
   convergence state together.

## 19. Fan-curve and component-watt utilities

Two standard-library-only Python tools are provided under `tools`. Run either
without data arguments for a short interactive prompt, or provide command-line
arguments for a repeatable calculation.

These tools form the model-calibration front end to the thermal estimator and
solver:

```text
manufacturer fan plot/data points
    -> tools/fan_curve_fitter.py
    -> a, b, c in library/fan_curves/fan_curves.toml
    -> pressure/flow operating point and convective cooling

server wall power, efficiency, or calibrated temperature response
    -> tools/heat_load_estimator.py
    -> watts in a component or internal solid region TOML
    -> qdot heat source in the stamped mesh

material rho/cp + stamped volume + solid/air area
    -> built-in ThermalTimeEstimator
    -> estimated tau, starting duration, memory, and timestep guidance
```

The utilities produce copy-ready TOML rather than silently modifying the model
library. This keeps the source measurement, fitting diagnostics, and engineering
judgment visible during review. A useful project record is to save the raw fan
points or server measurements beside the generated text, then copy only the
reviewed TOML block into the appropriate library file.

### 19.1 Fit fan-curve `a`, `b`, and `c`

Thermal Sim uses:

```text
deltaP(Q) = a - b Q - c Q^2
```

where `Q` is m³/s and pressure is Pa. The fitter converts datasheet units,
performs a least-squares quadratic fit, reports RMSE/R², and prints a TOML
block for `library/fan_curves/fan_curves.toml`.

The coefficients mean:

- `a`: shutoff pressure at zero flow, in Pa;
- `b`: linear pressure-drop coefficient, in Pa/(m³/s);
- `c`: quadratic pressure-drop coefficient, in Pa/(m³/s)².

The solver does not assume that the datasheet free-air CFM is the installed
flow. It evaluates this pressure curve against rack, vent, and internal-system
resistance to find the operating point. Consequently, an accurate curve affects
both predicted mass flow and the convection used by the thermal calculation.

Enter points directly from a CFM/Pa datasheet:

```powershell
python .\tools\fan_curve_fitter.py `
  --name "my_server_fan" `
  --rho-rated 1.2 `
  --flow-unit cfm `
  --pressure-unit pa `
  --point 0,300 `
  --point 100,260 `
  --point 200,170 `
  --point 300,0
```

Supported flow units are `m3/s`, `cfm`, `l/s`, and `m3/h`. Supported pressure
units are `pa`, `kpa`, `inh2o`, and `mmh2o`.

For a manufacturer graph measured in CFM and inches of water:

```powershell
python .\tools\fan_curve_fitter.py `
  --name "datasheet_fan" `
  --flow-unit cfm `
  --pressure-unit inh2o `
  --point 0,1.20 `
  --point 100,1.05 `
  --point 200,0.72 `
  --point 300,0.00
```

Or provide a CSV:

```csv
flow,pressure
0,1.20
100,1.05
200,0.72
300,0.00
```

```powershell
python .\tools\fan_curve_fitter.py `
  --name "datasheet_fan" `
  --flow-unit cfm `
  --pressure-unit inh2o `
  --csv .\fan_points.csv
```

Use at least three distinct flow values. More points are preferable because
the fitted curve is less sensitive to errors introduced while reading pixels
from a datasheet graph. If the tool warns that `b` or `c` is negative, inspect
the points and verify that the fitted curve decreases over the actual fan
operating range.

RPM versus equipment-load data may be supplied as repeated
`--rpm-load-point LOAD_PERCENT,RPM` values or as a CSV with `load_percent,rpm`
columns. Select the operating condition with `--target-load-percent`. RPM/load
data alone defines a controller schedule, not a pressure-flow curve. To produce
scaled Thermal Sim coefficients, also provide pressure-flow points measured at
`--reference-rpm`; the tool applies the fan affinity laws:

```powershell
python .\tools\fan_curve_fitter.py `
  --name "fan_at_50_percent_load" `
  --flow-unit cfm `
  --pressure-unit pa `
  --point 0,300 --point 100,150 --point 200,0 `
  --reference-rpm 3000 `
  --rpm-load-point 0,1500 `
  --rpm-load-point 100,3000 `
  --target-load-percent 50
```

The selected RPM is linearly interpolated between load points and clamped to
the end RPM outside the supplied schedule range.

After copying the printed block into the fan-curve library, reference it from
a rack or internal fan:

```toml
curve = "datasheet_fan"
```

For traceability, save the complete fitting report while still displaying it:

```powershell
python .\tools\fan_curve_fitter.py `
  --name "datasheet_fan" `
  --flow-unit cfm `
  --pressure-unit inh2o `
  --csv .\fan_points.csv |
  Tee-Object .\datasheet_fan_fit.txt
```

Keep `fan_points.csv` and `datasheet_fan_fit.txt` as calibration records. Copy
the final `[[fan_curve]]` block printed at the bottom into
`library/fan_curves/fan_curves.toml`. Check that the reported RMSE is small
relative to the fan's pressure range, R-squared is close to one, `a` is close
to the plotted shutoff pressure, and the fitted pressure decreases throughout
the expected operating range.

### 19.2 Build a rack system static-pressure curve

`tools/rack_system_curve.py` turns completed airflow cases or measured points
into `deltaP = B Q + C Q^2`, where `Q` is total rack flow in m3/s and pressure
is Pa. It saves CSV/JSON data and an SVG plot, overlays a library fan curve,
combines identical fans in parallel, and calculates the operating intersection.

For OpenFOAM, it sums `phi/rho` over all `fanPressure` patches and evaluates
each exported fan-pressure table at the solved flow. Unequal parallel fan
points are combined by air-power weighting. This uses the pressure rise implied
by the solved fan boundary, not arbitrary cell-pressure extrema.

Use the latest case recorded in `.thermal_sim_last_run.json`:

```powershell
python .\tools\rack_system_curve.py `
  --fan-curve "top_fan_MS1238E-H" --fan-count 9 `
  --output .\fan_analysis\rack_system_curve
```

For a proper CFD sweep, preserve at least four converged, isothermal cases at
distinct flows spanning about 50-125% of expected rack flow, then run:

```powershell
python .\tools\rack_system_curve.py `
  --case "C:\OpenFOAM\thermal_sim_v2\rack_flow_50" `
  --case "C:\OpenFOAM\thermal_sim_v2\rack_flow_75" `
  --case "C:\OpenFOAM\thermal_sim_v2\rack_flow_100" `
  --case "C:\OpenFOAM\thermal_sim_v2\rack_flow_125" `
  --fan-curve "top_fan_MS1238E-H" --fan-count 9 `
  --operating-density 1.184 --output .\fan_analysis\rack_sweep
```

A single case gives one nonzero point, so its pure-quadratic result is only a
provisional local estimate. Copies at the same operating flow do not add curve
information. `--fan-count` means identical fans in parallel: pressure is
evaluated at `Q_total/fan_count`, because parallel fan pressures do not add.
`--operating-density` scales pressure relative to the curve's `rho_rated`.

Manual or Fluent-derived points and CSV data are also accepted:

```powershell
python .\tools\rack_system_curve.py `
  --flow-unit cfm --pressure-unit inh2o `
  --point 500,0.08 --point 1000,0.29 `
  --point 1500,0.66 --point 2000,1.15 `
  --fan-curve "top_fan_MS1238E-H" --fan-count 9

python .\tools\rack_system_curve.py `
  --csv .\rack_pressure_sweep.csv --flow-unit cfm --pressure-unit pa
```

CSV columns must be named `flow,pressure`. Before fan selection, verify each
case has converged flow, inlet/outlet mass imbalance below about 1%, intended
flow direction through every fan, and no fan beyond its lookup-table range.
Large reported per-fan flow spread can indicate starvation or recirculation.
Repeat one point with a finer mesh, then compare the predicted intersection
against a complete fan-enabled simulation.

### 19.3 Estimate watts for an internal solid region

This utility estimates the heat that belongs inside the CFD/thermal domain. It
does not estimate IT workload from temperature alone. Prefer inputs in this
order when available:

1. measured whole-device wall power under the workload being modeled;
2. DC load plus measured or specified PSU efficiency;
3. total air mass flow plus mass-weighted inlet/exhaust temperatures;
4. steady temperature plus a calibrated total thermal resistance;
5. transient temperature rise plus calibrated effective thermal mass and heat
   loss.

For a printable/fillable measurement worksheet, thermal-camera guidance,
calibration sequence, and a reusable data-to-heat-load prompt, use
`heat_load_characterization.txt` in the repository root. Fill one copy per
device and operating condition, then use the commands below to convert the
reviewed measurements into TOML watts.

Wall power is usually the strongest rack-level heat estimate because almost all
electrical energy consumed by a server becomes heat within the room. Subtract
only energy that genuinely leaves the modeled domain in another form, such as
power delivered to an external device outside the rack boundary.

For measured wall/input power:

```powershell
python .\tools\heat_load_estimator.py `
  --name "CPU and system board" `
  --input-power-w 500 `
  --exported-power-w 10
```

This estimates 490 W of heat because 10 W was explicitly identified as energy
leaving the modeled device. For ordinary servers, nearly all wall power
eventually becomes heat. Do **not** multiply measured wall power by PSU
efficiency; PSU losses and delivered DC power both become heat.

If only the DC load and PSU efficiency are known:

```powershell
python .\tools\heat_load_estimator.py `
  --name "Server electronics" `
  --dc-load-w 450 `
  --efficiency 90
```

The tool first calculates wall power:

```text
wall power = DC load / efficiency
heat = wall power - exported nonthermal power
```

Efficiency accepts either `0.90` or `90`.

If total device air mass flow and mass-weighted inlet/exhaust temperatures are
available, estimate the heat transferred to the air with:

```powershell
python .\tools\heat_load_estimator.py `
  --name "Server electronics" `
  --mass-flow-kg-s 0.10 `
  --inlet-temp 25 `
  --outlet-temp 30
```

This uses `heat = mass flow * cp_air * (outlet - inlet)` and gives 502.75 W
with the default `--cp-air-j-kg-k 1005.5`. Temperatures may be in C or K but
must use the same unit. Use total device flow and mass-weighted temperatures;
leakage or an incomplete flow traverse biases this estimate. When electrical
and airflow inputs are both provided, the tool prints their percentage energy-
balance difference and keeps measured electrical input as the default selected
heat load.

Temperature alone cannot uniquely determine watts. A steady temperature
estimate requires a measured or calibrated total thermal resistance:

```powershell
python .\tools\heat_load_estimator.py `
  --name "Calibrated electronics" `
  --surface-c 60 `
  --ambient-c 20 `
  --thermal-resistance-k-per-w 0.20
```

This uses:

```text
heat = (surface temperature - ambient temperature) / thermal resistance
```

A transient warm-up estimate requires effective mass, specific heat, initial
temperature, final temperature, and elapsed time:

```powershell
python .\tools\heat_load_estimator.py `
  --name "Chassis thermal mass" `
  --mass-kg 12 `
  --specific-heat-j-kg-k 700 `
  --start-c 25 `
  --end-c 35 `
  --duration-s 600
```

That calculation estimates the power stored in thermal mass:

```text
stored power = mass * specific heat * temperature rise / elapsed time
```

Add `--ambient-c` and `--thermal-resistance-k-per-w` to include an approximate
heat-loss correction during the warm-up. The effective mass and resistance
must be calibrated for credible results; a thermal-camera surface temperature
by itself is not sufficient.

When multiple complete methods are supplied, the tool prints them separately.
Select the value written into the TOML snippet with, for example:

```powershell
--method electrical_input
--method airflow_temperature
--method steady_temperature
--method transient_temperature
--method median
```

The result is a copy-ready starting block:

```toml
[[internal_regions]]
name = "CPU and system board"
state = "solid"
watts = 490.0
# Add position, size, and material tables below this block.
```

Save the complete calculation report for later calibration:

```powershell
python .\tools\heat_load_estimator.py `
  --name "CPU and system board" `
  --input-power-w 500 `
  --exported-power-w 10 `
  --method electrical_input |
  Tee-Object .\cpu_system_board_heat_estimate.txt
```

Copy the printed `watts` value into either the reusable component file under
`library/components` or the model's `[[components.internal_regions]]` entry.
Use an internal solid region when the heat is localized to electronics inside a
larger chassis. Use component-level watts only when distributing heat across the
whole component is the intended approximation.

After stamping/exporting, verify that the integrated heat-source report equals
the requested watts. Then use a screening run to compare simulated surface or
probe temperatures with any available thermal-camera or sensor measurements.
Adjust uncertain watts, airflow curves, or thermal resistance one category at a
time; otherwise several incorrect inputs can compensate for one another and
produce a misleading temperature match.

Do not also place the same watts on the outer component shell. Heat assigned
to an internal solid region and heat assigned to the enclosing component are
separate sources and would be added together.

### Generic server boundary characterization

When internal electronics geometry is unavailable, use the generic 1U, 2U,
or 3U component templates instead of inventing detailed heat-source regions.
Each template contains one internal air region, one equivalent solid heat
block, one front intake vent, and one rear exhaust fan. These models are for
rack intake temperature, exhaust temperature, airflow, heat rejection, and
recirculation studies. Their solid-block temperature is not a CPU or junction
temperature.

The generated air region is a continuous front-to-rear tunnel. The intake and
fan planes are inset from the chassis faces, and both span the tunnel section
so flow cannot bypass around the equivalent fan. The adaptive mesher requires
that an internal
fan must have fluid cells on both sides and must not be placed directly on an
enclosure boundary. For a real fanless device (for example, the Eaton KVM), do
not use a generic server fan or fabricate a fan curve. Model its measured
openings as passive vents and retain natural or rack-driven flow. A measured
fanless-device mass flow may be used with its temperature rise to infer heat,
but it is not a flow boundary condition: calibrate vent free area/discharge
coefficient or another measured pressure-loss relation so the rack pressure
field reproduces that flow.

OpenFOAM exports a volume-average temperature history for every internal vent
and fan cell zone under
`postProcessing/fluid/internal_<device>_temperature_average`. Pair the front
vent and rear fan reports to check the modeled component air-temperature rise.
These are small cell-zone averages rather than exact surface mass-weighted
temperatures, so use them as calibration diagnostics and retain measured
intake/exhaust values as the acceptance reference.

Only rack height, chassis depth, mass-weighted intake/exhaust temperatures,
and device mass flow are required. Width defaults to 482 mm:

```powershell
python .\tools\generic_server_characterizer.py `
  --name "Unknown 2U Server" `
  --rack-units 2 `
  --depth-mm 700 `
  --mass-flow-kg-s 0.14 `
  --intake-temp 298.0 `
  --exhaust-temp 302.3 `
  --heat-load-source airflow `
  --output .\library\components\unknown_2u_server.toml `
  --json-output .\unknown_2u_server.json
```

The generated heat-block load is:

```text
Q_air = mass_flow * cp_air * (T_exhaust - T_intake)
```

Use `--heat-load-source input --input-watts VALUE` for a server with measured
wall power. Nearly all server input power ultimately becomes heat, so PSU
efficiency must not be used to reduce total server heat. For a standalone UPS
or converter whose useful output leaves the modeled component, use
`--heat-load-source conversion-loss --input-watts VALUE
--efficiency-percent VALUE`.

Mass flow alone cannot identify a pressure-flow curve. If pressure rise is
measured, add `--fan-pressure-rise-pa VALUE --fan-curve-output FILE.toml`.
The fitted curve passes through the measured operating point and is explicitly
an equivalent device curve, not a manufacturer fan curve. Without pressure,
the generated component retains a provisional generic curve and its simulated
mass flow must be checked and calibrated.

The base files are `generic_server_1u.toml`, `generic_server_2u.toml`, and
`generic_server_3u.toml` under `library/components`. The calculator can also
derive a separate rack model without editing the detailed original by using
`--model-source`, `--model-output`, and repeated
`--replace-component OLD=NEW` arguments.

Replace detailed components selectively. Accept a generic replacement only
after its simulated mass flow agrees with measured device flow and the rack
inlet/outlet mass imbalance is acceptably small. A temperature match with the
wrong component airflow is not a valid calibration.
