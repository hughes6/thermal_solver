# Final-model 300 s thermal-screening result

## Classification

This is **unvalidated thermal screening**, not a steady-state, as-built,
release, or industry-validation result.

- Screening case:
  `C:\OpenFOAM\thermal_model_final\final_model_openfoam_lowmem_init_20260829_screening_thermal_from_0p35`
- Preserved strict source case:
  `C:\OpenFOAM\thermal_model_final\final_model_openfoam_lowmem_init_20260829`
- Frozen airflow seed: `t = 0.34999999999999887 s`
- Thermal checkpoint: `t = 300 s`

The strict seed failed the airflow gate: latest/previous spatial velocity RMS
changes were 3.51968%/3.54559% against a 3% limit, and 11 fans were outside
their positive-pressure curve domains. The worst utilization was 348.475% at
Thruster exhaust fan 2. The screening fork records this in
`SCREENING_THERMAL_OVERRIDE.md` and does not alter the strict source case.

## Execution and numerical integrity

- OpenFOAM 2606 custom `semiFrozenChtMultiRegionFoam`, two MPI ranks.
- Fifteen implicit thermal-only steps from 0.35 to 300 s at
  `deltaT = 19.9766666667 s`, with two outer energy correctors.
- Thermal solve stage: 140.429 s wall time. Whole invocation, including mesh
  preflight, reconstruction, and post-processing: approximately 238 s.
- All 510 enthalpy solves completed. The second/final outer-corrector residual
  was below `9.993e-7` for every region and step. At 300 s the fluid final
  residual was `2.667e-8`; every solid was below `9.853e-7`.
- No fatal error, real floating-point exception, OOM, process kill, NaN/Inf
  field, or incomplete region was found.
- Root and both processor `300` directories contain the fluid region, all 16
  solids, and nonempty restart fields. Reconstruction and serial
  post-processing ended normally.

The large reported thermal-only Courant number (maximum approximately 70,140)
is diagnostic here: momentum and transport fields were frozen, while the fully
implicit energy solve was limited by the 20 s thermal timestep. It must not be
used as evidence that a live-flow step would be stable at this timestep.

## Exact temperature snapshot

| Region / measure | Maximum (C) | Volume average (C) | Applied W |
|---|---:|---:|---:|
| Trenton 3U BAM | 113.112 | 26.819 | 840.0 |
| NI PXIe chassis | 83.411 | 26.591 | 132.0 |
| Meanwell EDR-120-24 unit 1 | 51.440 | 26.540 | 9.6 |
| Keysight N6701C | 47.405 | 38.934 | 360.0 |
| Meanwell EDR-120-24 unit 2 | 43.977 | 24.364 | 9.6 |
| Dell PowerEdge R470 | 41.314 | 28.679 | 660.0 |
| Cisco 9300 | 32.186 | 23.689 | 105.0 |
| Fluid internal cells | 39.332 | 21.515 | n/a |

The full-field fluid maximum is 113.080 C at
`(0.490348775, 0.030000000, 0.454225000) m`. It is a mapped coupled-interface
value next to the Trenton solid maximum at approximately
`(0.490348775, 0.035000000, 0.454225000) m`; it is not a 113 C bulk-air cell.
Use the internal-cell maximum, 39.332 C, for free-fluid hotspot interpretation.

The model is not near its formal thermal convergence gate. Over the final
19.9767 s, the Trenton maximum rose 4.554 K, the NI maximum 2.168 K, and the
Meanwell-unit-1 maximum 1.634 K. The Keysight N6701C volume average rose
1.041 K. The production gate cannot pass before 2400 s and requires, twice
with accepted airflow, no more than 0.25 K maximum-cell change and 0.10 K
component-average change per 300 s.

## Flow, heat rejection, and recirculation indicators

- Intake: 0.661321776 kg/s at 293.150 K.
- Exhaust: 0.662790611 kg/s at 294.412453 K.
- Boundary imbalance: 0.221614%, passing the 1% balance gate.
- Sensible heat rejection: 840.926 W, 34.538% of 2434.8 W applied.
- Exterior thermal re-ingestion index: 0.
- Exterior bidirectional-patch mass fraction: 0.

The zero re-ingestion value is influenced by prescribed 293.15 K ambient inlet
boundaries and does not disprove internal recirculation or unmodeled room-level
exhaust return. The present domain can resolve internal eddies and return flow,
but not an exhaust plume traveling through the room outside its boundaries.

Configured expected-axis averages flag six candidate internal reverse-flow
zones for streamline seeding: NI top-right side vent (-0.7110 m/s), Cisco
side-left intake (-0.3956 m/s), NI top-left side vent (-0.0598 m/s), Keysight
N6701C left-front intake (-0.0499 m/s), Keysight N6701C right-front intake
(-0.0116 m/s), and Keysight N5766A right-front intake (-0.00032 m/s). These are
zone-average indicators, not proof of a closed recirculation path. The strict
fan-direction check passed; these are not fan-direction failures.

## Qualification limits

- Total modeled solid mass is 303.071 kg, heat capacity 272,764 J/K, and heat
  input 2434.8 W. At 300 s, much of the input is still being stored.
- No new aerodynamic operating point exists at 300 s. `U`, `k`, `omega`,
  `nut`, and momentum were inherited from 0.35 s.
- The inherited fan audit remains 31 pass, 2 warn, and 11 fail.
- All 17 y+ patches span mixed/buffer-layer values. Rack-wall y+ is
  1.46--417.47; the largest interface maximum is 545.58 at the DIN-rail region.
  This is not grid-independent wall-heat-transfer evidence.
- Full `checkMesh` retains reduced-order determinant warnings in 14 regions.
- Trenton and NI peak-to-average splits require review of local heat-source
  volume, homogenized/pseudo-electronic properties, coupled-interface cells,
  and mesh resolution before the peaks are treated as hardware predictions.

## Evidence files

- `component_temperatures_t300.csv`, `.json`, and `.md`
- `fluid_temperature_t300.csv`
- `recirculation_t300.csv`, `_face_flow.csv`, `_internal_air.csv`, and
  `_internal_velocity.csv`
- `boundary_mass_balance_t300.csv` and `boundary_opening_flows_t300.csv`
- `thermal_mass_audit.md`
- `fan_operating_domain_300_frozen_raw.csv` and `.md`
- `fan_flow_checkpoints_raw.md`
- `yplus_300_raw.json` and `.md`

