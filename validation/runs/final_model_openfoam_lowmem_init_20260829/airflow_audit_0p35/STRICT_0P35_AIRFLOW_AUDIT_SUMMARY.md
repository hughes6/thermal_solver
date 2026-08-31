# Strict 0.35 s airflow audit

Case:
`C:\OpenFOAM\thermal_model_final\final_model_openfoam_lowmem_init_20260829`

Result: **not accepted**. The strict source case remains pending and was not
altered to create a passing result.

| Check | Result | Gate | Status |
|---|---:|---:|---|
| Exterior mass imbalance | 0.221771% | <=1% | Pass |
| Ambient fan directions | 14/14 correct | all correct | Pass |
| Internal fan directions | 30/30 positive | all positive | Pass |
| Cumulative air exchange fraction | 0.0990305 | >=0.01 | Pass |
| Latest velocity spatial relative RMS | 3.51968% | <=3% | Fail |
| Previous velocity spatial relative RMS | 3.54559% | <=3% | Fail |
| Fan positive-pressure domains | 11 fail, 2 warn | 0 fail | Fail |

Worst fan-domain utilization is 348.475% at
`internal_Exhaust_fan_2_32`; the other Thruster exhaust is 316.38%. Five
Trenton boundary fans, the Eaton UPS rear fan, and three Dell cooling fans also
operate above their supplied curves' first zero-pressure points.

No CFM/unit, sign, multiplicity, fan-count, or wrong-curve-file error was found.
The active curve and component provenance hashes match their source files. The
dominant mechanism is externally assisted flow after the exported fan tables
clamp to zero pressure, with no negative-pressure/windmilling resistance. The
Thruster input is independently inconsistent: declared 18 CFM versus a
13.764 CFM first zero in its supplied Sunon polynomial.

The 0.35 s field can be inspected as diagnostic airflow evidence, but it is not
a validated thermal-flow seed. A separate, explicitly labeled screening fork
was used for the 300 s thermal experiment.

