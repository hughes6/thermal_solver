# OpenFOAM validation report

Overall result: **FAIL**

| Check | Result | Error/diagnostic | Status |
|---|---:|---:|---:|
| Fluid connectivity | 3 region(s), 1538474 cells | expected 3 | PASS |
| Mass conservation | inlet -0.49505393 kg/s, outlet 0.49334539 kg/s | 0.34512% | PASS |
| Energy conservation | -10.0799 W transported vs 1515.0000 W applied | 100.6653% | FAIL |

## Temperatures and outlet behavior

- Result time: 0.2 s
- Inlet signed mass-weighted temperature: 293.1500 K
- Outlet signed mass-weighted temperature: 293.1297 K
- Analytical outlet temperature from Q/(m_dot Cp): 296.2056 K
- Solid cell-weighted average temperature (adaptive-mesh diagnostic, not a physical volume average): 293.1509999281655 K
- Solid temperature range: 293.14990868615416 to 293.1827616324744 K
- Outlet gross bidirectional flow: 0.493345 kg/s
- Reverse-flow share of gross outlet traffic: 0.00%

The signed mass-flux average is required when an outlet has simultaneous
forward and reverse flow. An absolute-flow average is not an energy balance.
