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
| Finer (12.5 mm) | 75,776 | 1,024 | 0.00899649 | 298.6702 | 428.7150 | 0.388% | 0.178% |
| Finest (6.25 mm local) | 528,352 | 8,192 | 0.00899599 | 298.6702 | 391.1892 | 0.394% | 0.184% |
| Targeted 6.25 mm | 116,296 | 8,192 | 0.00904508 | 298.6688 | 390.2029 | 0.151% | 0.335% |
| Targeted 3.125 mm | 636,384 | 65,536 | 0.00908735 | 298.6688 | 377.4881 | 0.614% | 0.805% |

Medium-to-fine outlet temperature changes by 0.0383 K (0.013%), so the global
heat-removal result is mesh-independent. Solid average temperature changes by
108.11 K (18.1%), so the local conjugate heat-transfer result fails the 2% mesh
criterion. This is spatial error: the 0.2 x 0.2 x 0.05 m block has only 4, 8, and
64 cells respectively, and interface-face counts rise from 12 to 20 to 80.
The corrected 12.5 mm grid has 1,024 solid cells and 512 coupled interface
faces. Its solid average is another 59.95 K (12.3%) below the 25 mm result.
The 6.25 mm locally refined grid has 8,192 solid cells and 2,048 interface
faces; its solid average is another 37.53 K (8.75%) below the corrected 12.5 mm
result. The two finest outlet temperatures agree to the shown precision. The
component-temperature mesh criterion therefore still fails, while the bulk
heat-removal result is mesh-independent.

The final two rows use the same 0.05 m refinement envelope, so their comparison
isolates halving local spacing from 6.25 to 3.125 mm. Solid average temperature
changes by 12.715 K (3.26%), still above the 2% requirement. Combining the exact
12.5 mm result with this matched pair gives an observed order of 1.60, a
Richardson-extrapolated solid mean of 371.22 K, and a fine-grid GCI of 2.11%.
These estimates support monotonic convergence but do not replace the stated 2%
direct-grid criterion.

## Conservation and Fluent comparison

The fine-grid case transports 49.9916 W of the applied 50 W, an error of 0.0167%.
Its signed mass-weighted outlet temperature is 298.6418 K. The independent Fluent
reference is 298.5 K, a difference of 0.142 K. The analytical well-mixed value,
`Tin + Q/(m_dot Cp)`, is 298.6427 K, only 0.0009 K from OpenFOAM.

The outlet is strongly bidirectional: gross traffic is 0.08549 kg/s and reverse
flow is 44.70% of that gross value. Therefore `weightedAverage(T,phi)` is the
physically correct energy-balance temperature. An absolute-flux average is not.

On the corrected 12.5 mm grid, the final outlet is 298.6702 K, 0.1702 K above
Fluent and 0.0099 K below the analytical balance. It transports 49.9111 W of
50 W, with 0.178% energy error and 0.388% mass error. The 6.25 mm grid produces
the same 298.6702 K outlet, 49.9081 W transported, 0.184% energy error, and
0.394% mass error. Both outlets have about 46.1% reverse-flow share of gross
traffic.

The matched targeted grids also produce the same 298.6688 K outlet temperature.
The 3.125 mm case transports 50.4024 W and remains within every acceptance
criterion. Its larger 0.805% energy residual is below the 2% limit and does not
explain the 3.26% solid-temperature grid change.

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

OpenFOAM `checkMesh -region fluid -constant` reports `Mesh OK` on all seven grids.
Every fluid mesh is one connected region with hexahedral cells, zero maximum
non-orthogonality, negligible skewness, and maximum aspect ratio 4 or less. The
12.5 mm grid is isotropic with unit aspect ratio. The 6.25 mm grid uses local
refinement around and downstream of the block. The component-temperature
failure is resolution, not malformed cells.

Full-face inlet/outlet footprints previously marked their entire tangential
axes as fine even though their edges already coincide with rack boundaries.
Skipping only those redundant full-span bands reduces the matched 6.25 mm fluid
mesh from 528,352 to 116,296 cells (78.0%) and total cells from 536,544 to
124,488 (76.8%). Its solid mean changes only 0.252% from the broad-margin case.
Partial-face openings still create fine tangential bands, and a regression test
covers both behaviors.

The first 12.5 mm export exposed a uniform-grid stamping defect: floating-point
`floor(x/dx)` started a block at cell 11 while the OpenFOAM metadata snapped the
same 0.15 m coordinate to boundary 12. One full solid layer was silently lost
(960 cells and 0.001875 m3 instead of 1,024 cells and 0.002 m3). OpenFOAM export
now stamps uniform components through the geometry-aligned boundary lookup, and
a regression test covers this exact decimal-coordinate case. The invalid
434.5615 K result has been removed from the evidence set.

The audit also found a binary parser defect. A valid IEEE-754 value beginning
with byte `0x20` was incorrectly stripped as textual whitespace, shifting the
entire patch array. The parser now determines binary versus ASCII from the
FoamFile header before reading payload bytes, with a byte-level regression test.

## Reproduction

Run the permanent fixture, then audit its reconstructed result:

```powershell
model_runner.exe library/models/validation_fan_rack.toml
python tools/validate_openfoam_case.py C:\OpenFOAM\thermal_sim_v2\validation_fan_rack --fluent-temperature 298.5 --json validation_results.json
```

The machine-readable evidence is stored in `validation_coarse_results.json`,
`validation_medium_results.json`, `validation_fine_results.json`, and
`validation_fine_dt100_results.json`. Corrected 12.5 mm and locally refined
6.25 mm evidence is stored in `validation_finer_results.json` and
`validation_finest_results.json`. The matched targeted evidence is stored in
`validation_targeted_6250_results.json` and
`validation_targeted_3125_results.json`. The auditor exits nonzero when an
acceptance criterion fails.

## Required follow-up for component temperatures

Refine the local solid/interface and downstream thermal boundary layer beyond
3.125 mm, then require the component average and maximum to change by less than
2%. The observed 3.26% change and 2.11% fine-grid GCI are close but still fail
that gate. For production racks, use measured or calibrated component thermal
characteristics when accurate case temperature is required; total rack heat
removal remains independently validated here.

## Production-profile benchmark (2026-08-07)

The current generic production rack was exported into separate, non-overwriting
case roots with the current screening and in-depth profiles. The original
screening and in-depth meshes contained 167,232 and 288,757 cells, but full
`checkMesh` reported zero/near-zero determinants in one-cell-thick generic
component layers.

The refinement planner now guarantees at least two cells through reduced-order
chassis walls, heat blocks, and sandwiched internal air gaps. The fanless KVM
tunnel was also aligned exactly with its passive front vent. Corrected screening
contains 208,772 cells (+24.8%) and corrected in-depth contains 335,580 cells
(+16.2%). Both now pass full topology/geometry checks in the fluid and all four
solid regions. Screening minimum determinants are 0.001384 (fluid), 0.0534
(Eaton), 0.0133 (Dell), 0.0704 (Trenton), and 0.00840 (KVM). A five-step
screening startup reached exactly 0.005 s, remained stable with peak Co 0.861
against the profile limit of 5, and completed end-to-end in 146.97 s.

This establishes numerically valid generic-region meshes; it does not calibrate
the effective component temperatures against physical hardware. Local generic
component temperatures remain engineering estimates until measured thermal
resistance/capacitance data are supplied.

At a 0.005 s startup checkpoint, the former fixed ramp required 50 in-depth
steps at 0.0001 s. Peak Co was only 0.095 and end-to-end wall time was 443.47 s.
The adaptive ramp used 10 steps seeded at 0.0005 s, retained OpenFOAM adaptive
control, reached the exact same 0.005 s endpoint, and held peak Co to 0.454
against the configured limit of 1. Wall time was 204.79 s, a 53.8% reduction.
Because startup fan scaling is a numerical stabilization procedure, early-ramp
velocity fields differ; acceptance remains based on the later converged airflow
operating point, mass balance, device-flow stability, and thermal convergence.

The benchmark also quantified mounted-filesystem overhead. A ten-step screening
restart spent 89 s in the solver but 194 s end-to-end, so decomposition,
reconstruction, process startup, and Windows-mounted case I/O consumed about
105 s (54%). Avoid many tiny runner invocations. Complete airflow windows in a
single invocation and use multirate thermal intervals between them.
