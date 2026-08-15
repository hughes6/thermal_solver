# Porous and solid rack obstructions

This guide explains how to represent cable bundles, perforated trays, blanking
plates, solid shelves, structural members, and similar rack obstructions. It
also explains what must be measured or calculated before the result can be
treated as physical.

## 1. Choose the correct representation

Use a **solid component** when air cannot pass through the object at the scale
resolved by the model. Examples include a solid shelf, bus bar enclosure,
blanking plate, solid cable-management panel, structural rail, or a tightly
sealed equipment chassis. A zero-watt component is still a real solid: it
blocks flow, conducts heat, stores thermal energy, and exchanges heat at its
surface.

Use a **porous region** when air passes through unresolved openings. Examples
include a perforated tray, grille, filter, screen, or cable bundle represented
as a distributed resistance. A porous region remains fluid; it does not have
solid thermal mass or solid conduction. It changes pressure loss and therefore
airflow, fan operating points, recirculation, and temperature transport.

Use explicit solid cable geometry only when individual cable wakes, surface
temperatures, or local contact/conduction matter and the mesh resolves the
cables. That is normally too expensive for a full rack. For rack airflow, a
calibrated porous cable volume is usually the useful engineering model.

Do not represent the same obstruction as both solid and porous in the same
volume. Porous regions may not overlap components or other porous regions.

## 2. Fully solid obstruction

A simple zero-heat solid can be placed as a component:

```toml
[[components]]
name = "Solid blanking shelf"
watts = 0.0
internal_regions = []

[components.position]
units = "m"
x = 0.02
y = 0.20
z = 0.80

[components.size]
units = "m"
width = 0.48
depth = 0.60
height = 0.005

[components.material]
rho = 2700.0
cp = 900.0
k = 205.0
```

Use the real material when thermal storage or conduction matters. If only
flow blockage matters, retain physically reasonable material properties and
`watts = 0.0`; do not use zero density, heat capacity, or conductivity.

For a perforated shelf, do not use a solid block unless the holes are explicitly
resolved. Use a porous region across the shelf plane instead.

## 3. Percentage porosity for perforated objects

The preferred input is a percentage:

```toml
[[porous_regions]]
name = "42 percent open rear tray"
porosity_percent = 42.0
discharge_coefficient = 0.65
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

`porosity_percent = 42.0` means that 42% of the gross area normal to the flow
direction is open. Valid values are greater than 0 and no greater than 100.
The solver converts the value to the internal fraction `phi = 0.42`.

The old form remains fully supported:

```toml
free_area_ratio = 0.42
discharge_coefficient = 0.65
```

Do not specify `porosity_percent` and `free_area_ratio` together. Existing
models do not need to be migrated. All physics, native-solver behavior, and
OpenFOAM export use the same normalized internal fraction.

This new percentage field applies to top-level `[[porous_regions]]`. Existing
rack vents and component internal vents continue to use their established
fractional `free_area_ratio` input. Their schema and behavior have not changed,
so old vent/component libraries remain compatible.

### 3.1 Calculate perforated-sheet porosity

Use the open area divided by gross panel area:

```text
porosity_percent = 100 * total_hole_area / gross_panel_area
```

For `N` circular holes of diameter `d` in a panel of width `W` and height `H`:

```text
total_hole_area = N * pi * d^2 / 4
porosity_percent = 100 * N * pi * d^2 / (4 * W * H)
```

Use consistent units. Count only unobstructed holes inside the modeled gross
area. Subtract covered holes, solid borders, cable ties, and mounting hardware
when they materially reduce the open area.

For a regular pattern, a unit cell may be used. A square-pitch circular-hole
pattern has:

```text
porosity_percent = 100 * pi * d^2 / (4 * pitch^2)
```

Open-area percentage alone does not completely determine pressure loss; the
hole shape, thickness, edge geometry, Reynolds number, and downstream recovery
are represented approximately by `discharge_coefficient`.

## 4. Quantifying cable obstructions

Cable bundles are not automatically equivalent to perforated plates. Choose
one of the following levels of evidence, in descending order of confidence.

### 4.1 Measured pressure-drop curve — preferred

Measure pressure difference across the cable zone at several steady flow
rates. Convert flow to superficial velocity using the gross zone area normal
to `direction`:

```text
U = volumetric_flow / gross_area
```

Fit the Darcy–Forchheimer model:

```text
deltaP/L = mu * D * U + 0.5 * rho * F * U * abs(U)
```

where:

- `deltaP` is measured static-pressure loss in Pa;
- `L` is physical bundle thickness along `direction`, in m;
- `mu` is dynamic viscosity in Pa·s;
- `rho` is air density in kg/m3;
- `D` is `darcy_coefficient`, in 1/m2;
- `F` is `forchheimer_coefficient`, in 1/m;
- `U` is superficial velocity in m/s.

Fit all measured points rather than forcing one point. A spreadsheet or least-
squares fit can regress `deltaP/L` against `U` and `U*abs(U)`. The linear
coefficient divided by `mu` gives `D`; twice the quadratic coefficient divided
by `rho` gives `F`. Constrain both coefficients to be nonnegative.

Enter the fitted values directly:

```toml
[[porous_regions]]
name = "Measured rear cable bundle"
darcy_coefficient = 2.5e6
forchheimer_coefficient = 850.0

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

For a tangled bundle, omit transverse coefficients to use isotropic resistance,
or explicitly set transverse values equal to the normal values. For organized
cables aligned in one direction, use directional coefficients derived from
measurements or a detailed CFD submodel. Large transverse coefficients are
appropriate for a thin perforated plate but can be unrealistic for a loose
cable volume.

### 4.2 Estimate cable volume fraction

When pressure data are unavailable, estimate the cable solid volume within the
chosen porous box:

```text
cable_volume = sum(pi * outside_diameter^2 / 4 * cable_length_in_zone)
zone_volume = width * depth * height
void_percent = 100 * (1 - cable_volume / zone_volume)
```

Include jackets because they displace air. Include service loops only for the
length actually inside the zone. This `void_percent` is a useful geometry audit,
but it is **not automatically the same as flow-normal open-area percentage**.
Cable orientation, bundling, ties, and tortuous passages can produce much more
pressure loss than a plate with the same percentage. Use it to define the zone
and an initial detailed submodel, not as final pressure calibration.

For predominantly parallel cables crossing a known plane, projected blockage
can provide a first open-area estimate:

```text
open_area_percent = 100 * (1 - projected_cable_area / gross_plane_area)
```

If using that estimate with the perforated form, label the model as
uncalibrated and sweep both porosity and discharge coefficient.

### 4.3 Detailed CFD submodel

Build a smaller representative cable section with resolved cable solids,
periodic or representative boundaries, and several inlet velocities. Extract
pressure loss across the section and fit `D` and `F` using the same equations.
This is substantially cheaper than resolving every cable in the rack and
produces coefficients that can be transferred to the rack porous volume.

## 5. Direction, thickness, and mesh behavior

`direction` is the primary resistance direction and must be axis-aligned in the
current rack model. The region's physical thickness `L` is its size along that
axis. For example, a `y` direction uses `depth` as `L`.

The adaptive mesh guarantees that a nonzero porous region occupies at least one
cell. If the stamped numerical thickness differs from the requested physical
thickness, the exporter scales coefficients so the integrated pressure loss is
preserved. Always enter coefficients for the **physical** thickness; do not
manually compensate for cell size.

Even with thickness scaling, repeat at least one case on a finer mesh. Compare
pressure drop, total rack flow, each fan operating point, outlet temperature,
and recirculation. A matching integrated pressure drop does not guarantee that
local wakes are mesh-independent.

## 6. Quantities required for a defensible model

Record the following for every obstruction:

| Quantity | Solid obstruction | Porous obstruction |
|---|---|---|
| Position and physical dimensions | Required | Required |
| Material `rho`, `cp`, `k` | Required | Not represented |
| Heat load | Usually 0 W | Not supported as solid heat |
| Gross flow-normal area | Blocked | Required for velocity |
| Open-area/porosity percentage | Not applicable | Required for perforated form |
| Discharge coefficient | Not applicable | Required for perforated form |
| Pressure drop versus flow | Validation | Preferred calibration input |
| Darcy and Forchheimer coefficients | Not applicable | Required for coefficient form |
| Directional behavior | Geometry resolves it | Must be specified/calibrated |

Also record uncertainty. Useful rack sweeps include the low, nominal, and high
credible resistance. Do not tune obstruction resistance and equipment watts at
the same time; incorrect values can compensate and falsely match one measured
temperature.

## 7. Validation workflow

1. Run the rack without the obstruction and save fan flows, inlet/outlet mass
   flow, static pressures, mass-weighted temperatures, and component
   temperatures.
2. Add the obstruction without changing heat loads, fan curves, or vents.
3. Confirm the porous zone selects cells and does not overlap solids.
4. Check inlet/outlet mass imbalance below 1% (preferably 0.5%).
5. Compare simulated pressure loss across the zone with measured or submodel
   data over more than one flow point.
6. Confirm every fan remains on the intended branch and within its curve range.
7. Check `Q = m_dot * cp * (Tout - Tin)` against applied heat. Use signed,
   mass-weighted temperatures.
8. Inspect recirculation and local hot zones, but do not calibrate against one
   isolated hottest cell.
9. Repeat a representative point on a finer mesh.
10. Accept the model only when pressure loss, mass conservation, energy balance,
    and temperature trends agree simultaneously.

For complex mapped CHT cases, keep the coupled thermal acceptance run at
`maxCo = 5` or lower. The project validation found that `maxCo = 10` created a
nonphysical isolated hot-cell rise even in the unobstructed control.

## 8. Useful analysis commands

Validate mass and energy balance:

```powershell
python .\tools\validate_openfoam_case.py "C:\OpenFOAM\thermal_sim_v2\case_name" `
  --json "C:\OpenFOAM\thermal_sim_v2\case_name\validation.json" `
  --markdown "C:\OpenFOAM\thermal_sim_v2\case_name\validation.md"
```

Generate pressure-versus-flow system curves and compare fan curves:

```powershell
python .\tools\rack_system_curve.py `
  --csv .\rack_pressure_sweep.csv --flow-unit cfm --pressure-unit pa
```

Generate boundary flow, heat rejection, and recirculation reports:

```powershell
python .\plot\recirculation_report.py `
  --case "C:\OpenFOAM\thermal_sim_v2\case_name" --save
```

View the fluid temperature, velocity, pressure, contours, vectors, and
streamlines:

```powershell
python .\plot\fluid_results.py `
  --case "C:\OpenFOAM\thermal_sim_v2\case_name" --time latest
```

Use identical meshes and solver profiles for baseline/obstructed comparisons.
The full complex-validation evidence and limitations are recorded in
`POROUS_COMPLEX_VALIDATION_2026-08-14.md`.
