# Porous obstruction acceptance validation — 2026-08-15

## Verdict

The Darcy–Forchheimer porous implementation is accepted for native and
OpenFOAM use when its coefficients come from a measured or otherwise trusted
pressure-loss curve. The implementation conserves the specified integrated
pressure loss when a physical obstruction is thinner than the CFD mesh.

This validates the numerical model, not an arbitrary cable bundle or tray.
No measured cable/tray pressure-loss dataset is stored in this repository, so
uncalibrated geometry-derived coefficients must remain labelled estimates.

## Acceptance criteria

- Native pressure loss within 0.01% of the entered Darcy–Forchheimer law.
- OpenFOAM volume-integrated source versus measured pressure drop within 1%.
- Less than 1% pressure-drop change between accepted OpenFOAM meshes.
- One connected fluid region, valid porous cell zone, and `Mesh OK`.
- Stable, essentially one-dimensional duct flow after at least one air exchange.
- Existing Fluent bulk-temperature and rack conservation validations remain
  passing because the porous feature does not alter unobstructed cases.

## Native solver matrix

`tests/porous_region_test.cpp` now checks 16 analytical cases:

- pure Darcy, pure Forchheimer, and mixed resistance;
- velocities from 0.01 to 0.16 m/s (16:1 range);
- a physical layer thinner than one cell, exactly one cell thick, and two cells
  thick;
- 0.10 m and 0.05 m mesh spacing for the same physical resistance.

All cases pass. The test tolerance is 0.005%, consistent with the nonlinear
flow solver's `1e-5` relative operating-point tolerance.

## OpenFOAM pressure-duct benchmark

The reproducible input is
`library/models/validation_porous_duct.toml`. It defines a 0.2 x 1.0 x 0.2 m
duct, a fixed 1.999995 m/s inlet, and a 0.10 m physical porous layer with:

- Darcy coefficient: 100,000 1/m²;
- Forchheimer coefficient: 5,000 1/m;
- nominal air temperature: 293.15 K.

Pressure was compared with the source integral evaluated from each converged
cell's actual density and velocity:

`deltaP = integral[(mu*D + 0.5*rho*F*|U|)*U_axial] dx`.

| Mesh / source representation | Fluid cells | Source cells | Predicted | Measured | Error |
|---|---:|---:|---:|---:|---:|
| 50 mm, two axial source layers | 320 | 32 | 1185.899 Pa | 1186.167 Pa | +0.0226% |
| 100 x 50 x 100 mm, two axial source layers | 80 | 8 | 1185.899 Pa | 1186.167 Pa | +0.0226% |
| 100 mm input, automatically spread over two layers | 40 | 8 | 1186.065 Pa | 1186.225 Pa | +0.0135% |

The two independently resolved meshes agree to the shown precision. The
automatically stabilized coarse case differs from the 80-cell accepted result
by 0.058 Pa (0.0049%). All meshes are orthogonal, contain one connected fluid
volume, and pass full `checkMesh`.

## Defects exposed and corrected

1. A fluid-only OpenFOAM model failed because `splitMeshRegions` correctly did
   nothing for one region, while the generated workflow expected
   `constant/fluid/polyMesh`. Region preparation now moves the sole mesh into
   the required region directory.
2. Fluid-only selection masks remained at root time zero, producing an empty
   porous zone. They are now copied into `0/fluid` before `topoSet`.
3. Fluid-only decomposition referenced a nonexistent coupled-interface face
   set. The constraint is now emitted only when solid regions exist.
4. A strong porous source occupying one axial OpenFOAM cell produced a clear
   checkerboard transient (layer-average axial velocities approximately
   0.26–3.07 m/s after 0.65 s). OpenFOAM porous sources now occupy at least two
   axial cells and their coefficients are rescaled so integrated physical
   pressure loss is unchanged. The corrected 40-cell case passes at 0.0135%.

## Relationship to Fluent and Thermal Desktop validation

The existing forced-flow Fluent benchmark remains the thermal acceptance
reference: OpenFOAM's signed mass-weighted outlet temperature was 298.6418 K
versus 298.5 K in Fluent (0.142 K difference), with 0.0167% energy error on the
fine validation grid. Later refined grids retained the same bulk outlet result.

The existing Thermal Desktop cases validate transient heat capacity and stored
energy separately. Porous resistance changes momentum, not the applied wattage
or material heat capacity, so this pressure benchmark complements rather than
replaces those thermal checks.

For physical cable or perforated-tray acceptance, import measured
velocity/pressure points into `tools/porous_obstruction_calculator.py`, retain
its fit report, and repeat this duct comparison using the fitted coefficients.
The target is fit NRMSE below 5% (preferably below 2%) over the rack operating
range and CFD pressure error below 1%.
