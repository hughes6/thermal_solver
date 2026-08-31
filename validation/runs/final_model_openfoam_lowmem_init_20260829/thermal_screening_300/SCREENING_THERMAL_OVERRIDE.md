# Unvalidated thermal-screening continuation

This case is a fork of the preserved strict case at
`t = 0.34999999999999887 s`. It exists only to obtain early thermal behavior
without falsifying or weakening the primary airflow gate.

The strict 0.35 s audit did **not** accept the airflow:

- exterior mass imbalance: 0.221771% (passes the 1% gate);
- all 14 ambient fan directions and all 30 internal fan directions: pass;
- cumulative rack-air exchange fraction: 0.0990305;
- latest/previous velocity relative RMS: 3.51968% / 3.54559% (both exceed 3%);
- fan positive-pressure domain: 11 failures and 2 warnings;
- worst fan-domain utilization: 348.475%, Thruster Load Box exhaust fan 2.

The dominant failure is externally assisted flow through fan openings after
their supplied curves reach zero pressure. The exported tables do not model
negative-pressure/windmilling resistance. The Thruster Sunon curve is also a
known source inconsistency: its first zero is 13.764 CFM while the component
declares 18 CFM.

For this fork only, the 0.35 s velocity field is frozen as a screening seed and
full heat sources are restored. Any temperatures, hotspots, or recirculation
metrics from this fork must be labeled **unvalidated thermal screening**. They
must not be used as steady-state, as-built, release, or industry-validation
evidence. The untouched strict source case remains:

`C:\OpenFOAM\thermal_model_final\final_model_openfoam_lowmem_init_20260829`
