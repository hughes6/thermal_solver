# Generic-component rack comparison

Date: 2026-08-05

## Scope

`library/models/model_generic_components.toml` preserves the production rack,
component positions, roof fans, main vent, and total component heat loads. It
replaces the detailed Eaton, Dell, Trenton, and KVM internals with one air
region, one heat block, one intake vent, and one exhaust fan per component.

The generic component fan data are provisional because measured device
pressure rise and mass flow were unavailable. For this first comparison, flow
targets were sized for a 10 K device air-temperature rise and fitted through an
assumed 80 Pa operating point. These assumptions are not validation data.

## Execution

- Profile: `screening_foam_cfg.toml`
- Detailed screening mesh: 232,140 cells
- Generic screening mesh: 166,428 cells
- Cell-count reduction: 28.3%
- Initial coupled airflow checkpoint: 0.05 s
- Thermal-only checkpoint 1: 18,000.05 s
- Thermal-only checkpoint 2: 100,000.05 s
- Direct implicit thermal-only timestep: 1,000 s

The adaptive coupled initialization did not meet its flow-change criterion and
required roughly three minutes per 0.01 s airflow step. It was stopped at a
valid field checkpoint so the requested thermal comparison could proceed. The
resulting flow field is therefore provisional, not converged.

## Temperature comparison at 100,000 s

| Component | Detailed average (K) | Generic average (K) | Difference (K) | Generic difference | Detailed max (K) | Generic max (K) |
|---|---:|---:|---:|---:|---:|---:|
| Dell R470 | 370.607 | 385.731 | +15.124 | +4.08% | 402.372 | 515.378 |
| Eaton UPS | 324.948 | 347.771 | +22.823 | +7.02% | 391.624 | 475.589 |
| Trenton 3U | 476.934 | 493.580 | +16.646 | +3.49% | 569.157 | 658.309 |

The generic averages are reasonably close for an uncalibrated first model, but
the generic maxima are 84-113 K higher. The heat-block maximum is not a real
chip temperature and must not be used as an equipment limit.

## Airflow comparison

| Metric | Detailed refreshed case | Generic provisional case | Difference |
|---|---:|---:|---:|
| Main rack intake | -0.208630 kg/s | -0.153237 kg/s | -26.5% magnitude |
| Ambient mass imbalance | -0.00000813 kg/s | +0.017081 kg/s | unacceptable |
| Roof fan range | 0.02263-0.02374 kg/s | 0.02290-0.02337 kg/s | similar |

The generic rack imbalance is approximately 11.1% of main-intake magnitude,
well above the configured 1% criterion. The generic airflow result and its
temperature field are not validated until the equivalent device curves are
calibrated and the coupled airflow stage reaches mass and device-flow
convergence.

The first comparison also incorrectly assigned an equivalent fan to the KVM.
The physical KVM is fanless. Its generic file now uses passive front and rear
vents, and the selective production experiment retains the original KVM
component. Results above are preserved as failure evidence and must not be
used as the corrected KVM result.

## Conclusion

The generic architecture is viable for rack-level work and reduced cell count
by 28.3%. Its volume-average component temperatures remained within 3.5-7.0%
of the detailed model despite no component calibration. It is not yet a
drop-in validated replacement because the provisional device fans changed the
rack airflow balance.

For each real component, measure mass-weighted intake temperature, mass-weighted
exhaust temperature, mass flow, and preferably pressure rise. Regenerate the
component and equivalent curve with
`tools/generic_server_characterizer.py`, then require:

- component mass flow within 5-10% of measurement;
- rack mass imbalance below 1%;
- exhaust temperature rise within 1-2 K of measurement;
- stable device flows across the final thermal-state airflow refresh.

Only after those checks should generic-case rack intake temperatures and
recirculation be treated as quantitative.
