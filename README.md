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
g++ -std=c++17 -O3 -DNDEBUG .\model_runner.cpp -o model
.\model
```

`model_runner.cpp` currently loads:

```cpp
ModelLoader loader;
loader.load_fan_curves("library/fan_curves/fan_curves.toml");
loader.load_model("library/models/model.toml");
loader.run();
```

To run a different model, change the path supplied to `load_model`.

For a debug build:

```powershell
g++ -std=c++17 -O0 -g .\model_runner.cpp -o model_debug
.\model_debug
```

Use the optimized build for production runs. Flow and thermal loops touch every
active cell many times, so optimization has a large effect.

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

### 4.7 Rack-boundary circular fan

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

### 4.8 Rack-boundary vent

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
g++ -std=c++17 -O3 -DNDEBUG .\main.cpp -o main
.\main
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
