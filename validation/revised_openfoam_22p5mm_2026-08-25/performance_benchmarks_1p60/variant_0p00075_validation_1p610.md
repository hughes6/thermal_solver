# OpenFOAM validation report

Overall result: **FAIL**

| Check | Result | Error/diagnostic | Status |
|---|---:|---:|---:|
| Fluid connectivity | 1 region(s), 925388 cells | expected 1 | PASS |
| Mass conservation | inlet -0.54094798 kg/s, outlet 0.5409811 kg/s | 0.00612% | PASS |
| Steady-state heat removal | 9.0524 W outlet sensible transport vs 2315.0000 W applied | 99.6090% power-removal mismatch | FAIL |

## Temperatures and outlet behavior

- Result time: 1.61 s
- Inlet signed mass-weighted temperature: 293.1500 K
- Outlet signed mass-weighted temperature: 293.1666 K
- Analytical outlet temperature from Q/(m_dot Cp): 297.4080 K
- Solid cell-weighted average temperature (adaptive-mesh diagnostic, not a physical volume average): 293.1863938270347 K
- Solid temperature range: 293.1483179930474 to 294.4684019942241 K
- Outlet gross bidirectional flow: 0.540981 kg/s
- Reverse-flow share of gross outlet traffic: 0.00%

The signed mass-flux average is required when an outlet has simultaneous
forward and reverse flow. An absolute-flow average is not an energy balance.

This is a thermal-development gate, not a transient first-law closure result;
stored energy is not included in this report.
