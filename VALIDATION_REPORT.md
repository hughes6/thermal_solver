# Validation fan-rack CFD report

## Scope and verdict

This report validates the `validation_fan_rack.toml` conjugate heat-transfer case: a
0.5 x 1.0 x 0.3 m rack, a 0.2 x 0.2 x 0.05 m aluminum block dissipating 50 W,
293.15 K inlet air, and 15.89 CFM nominal forced flow.

**Validated:** fluid-domain connectivity, mesh quality, mass conservation, global
energy conservation, signed mass-weighted outlet temperature, agreement with the
Fluent reference, and thermal timestep independence.

**Not mesh-converged:** aluminum/component temperature. The bulk outlet result is
stable, but component temperature changes materially with spatial refinement. Do
not use any listed grid for a final component-temperature prediction until one
more refinement demonstrates the required less-than-2% change.

## Acceptance criteria

| Check | Criterion |
|---|---:|
| Connected fluid mesh | exactly one region |
| Mass imbalance | <= 1% of larger boundary flow |
| Energy imbalance | <= 2% of applied power |
| Fluent outlet comparison | <= 1 K |
| Timestep sensitivity | <= 0.2% for solid and outlet temperature |
| Mesh sensitivity | <= 2% between the two finest grids |

## Grid study

All results are from fully reconstructed fields after a coupled airflow refresh and
thermal re-equilibration.

| Grid | Fluid cells | Solid cells | Outlet flow (kg/s) | Outlet T (K) | Solid average T (K) | Mass error | Energy error |
|---|---:|---:|---:|---:|---:|---:|---:|
| Coarse | 296 | 4 | 0.00899318 | 298.6797 | 723.1677 | 0.424% | 0.043% |
| Medium | 592 | 8 | 0.00897761 | 298.6801 | 596.7699 | 0.597% | 0.210% |
| Fine | 4,736 | 64 | 0.00905767 | 298.6418 | 488.6605 | 0.289% | 0.017% |
| Finer (12.5 mm) | 75,840 | 960 | 0.00901321 | 298.6687 | 434.5615 | 0.203% | 0.020% |

Medium-to-fine outlet temperature changes by 0.0383 K (0.013%), so the global
heat-removal result is mesh-independent. Solid average temperature changes by
108.11 K (18.1%), so the local conjugate heat-transfer result fails the 2% mesh
criterion. This is spatial error: the 0.2 x 0.2 x 0.05 m block has only 4, 8, and
64 cells respectively, and interface-face counts rise from 12 to 20 to 80.
The 12.5 mm grid adds 960 solid cells and 488 coupled interface faces, but its
solid average is another 54.10 K (11.1%) below the 25 mm result. The bulk
outlet remains insensitive: it differs from the 25 mm value by only 0.0269 K.
The component-temperature mesh criterion therefore still fails, now with a
substantially stronger bound.

## Conservation and Fluent comparison

The fine-grid case transports 49.9916 W of the applied 50 W, an error of 0.0167%.
Its signed mass-weighted outlet temperature is 298.6418 K. The independent Fluent
reference is 298.5 K, a difference of 0.142 K. The analytical well-mixed value,
`Tin + Q/(m_dot Cp)`, is 298.6427 K, only 0.0009 K from OpenFOAM.

The outlet is strongly bidirectional: gross traffic is 0.08549 kg/s and reverse
flow is 44.70% of that gross value. Therefore `weightedAverage(T,phi)` is the
physically correct energy-balance temperature. An absolute-flux average is not.

On the 12.5 mm grid, the final outlet is 298.6687 K, 0.1687 K above Fluent and
0.0011 K below the analytical balance. It transports 49.9899 W of 50 W, with
0.0203% energy error and 0.203% mass error. The outlet remains strongly
bidirectional, with 45.24% reverse-flow share of gross traffic.

## Timestep study

The converged fine case was advanced another 10,000 s after reducing the thermal
step from 1,000 s to 100 s.

| Quantity | 1,000 s step | 100 s step | Change |
|---|---:|---:|---:|
| Outlet temperature | 298.6418 K | 298.6401 K | 0.0017 K (0.0006%) |
| Solid average temperature | 488.6605 K | 488.6129 K | 0.0476 K (0.0097%) |
| Energy error | 0.0167% | 0.0472% | both pass |

The thermal solution is timestep-independent at the tested equilibrium.

## Mesh quality

OpenFOAM `checkMesh -region fluid -constant` reports `Mesh OK` on all four grids.
Every fluid mesh is one connected region with hexahedral cells, zero maximum
non-orthogonality, negligible skewness, and maximum aspect ratio 4 or less. The
12.5 mm grid is isotropic with unit aspect ratio. The component-temperature
failure is resolution, not malformed cells.

## Reproduction

Run the permanent fixture, then audit its reconstructed result:

```powershell
model_runner.exe library/models/validation_fan_rack.toml
python tools/validate_openfoam_case.py C:\OpenFOAM\thermal_sim_v2\validation_fan_rack --fluent-temperature 298.5 --json validation_results.json
```

The machine-readable evidence is stored in `validation_coarse_results.json`,
`validation_medium_results.json`, `validation_fine_results.json`, and
`validation_fine_dt100_results.json`. The additional 12.5 mm evidence is stored
in `validation_finer_results.json`. The auditor exits nonzero when an acceptance
criterion fails.

## Required follow-up for component temperatures

Add one grid finer than 12.5 mm around the solid and its downstream thermal
boundary layer, then require the component average and maximum to change by less
than 2%. The current result is a stronger bound/trend, not a mesh-converged
component-temperature prediction.
