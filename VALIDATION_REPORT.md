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
not use the coarse or medium grids for component-temperature prediction.

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

Medium-to-fine outlet temperature changes by 0.0383 K (0.013%), so the global
heat-removal result is mesh-independent. Solid average temperature changes by
108.11 K (18.1%), so the local conjugate heat-transfer result fails the 2% mesh
criterion. This is spatial error: the 0.2 x 0.2 x 0.05 m block has only 4, 8, and
64 cells respectively, and interface-face counts rise from 12 to 20 to 80.

## Conservation and Fluent comparison

The fine-grid case transports 49.9916 W of the applied 50 W, an error of 0.0167%.
Its signed mass-weighted outlet temperature is 298.6418 K. The independent Fluent
reference is 298.5 K, a difference of 0.142 K. The analytical well-mixed value,
`Tin + Q/(m_dot Cp)`, is 298.6427 K, only 0.0009 K from OpenFOAM.

The outlet is strongly bidirectional: gross traffic is 0.08549 kg/s and reverse
flow is 44.70% of that gross value. Therefore `weightedAverage(T,phi)` is the
physically correct energy-balance temperature. An absolute-flux average is not.

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

OpenFOAM `checkMesh -region fluid -constant` reports `Mesh OK` on all three grids.
Every fluid mesh is one connected region with hexahedral cells, zero maximum
non-orthogonality, maximum skewness below 1.7e-14, and maximum aspect ratio 4 or
less. The component-temperature failure is resolution, not malformed cells.

## Reproduction

Run the permanent fixture, then audit its reconstructed result:

```powershell
model_runner.exe library/models/validation_fan_rack.toml
python tools/validate_openfoam_case.py C:\OpenFOAM\thermal_sim_v2\validation_fan_rack --fluent-temperature 298.5 --json validation_results.json
```

The machine-readable evidence is stored in `validation_coarse_results.json`,
`validation_medium_results.json`, `validation_fine_results.json`, and
`validation_fine_dt100_results.json`. The auditor exits nonzero when an acceptance
criterion fails.

## Required follow-up for component temperatures

Add local refinement around the solid and its downstream thermal boundary layer,
then repeat at least two finer grids until the component average and maximum both
change by less than 2%. The current fine result is useful as a bound/trend, not as
a mesh-converged component-temperature prediction.
