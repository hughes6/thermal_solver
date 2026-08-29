# OpenFOAM validation report

Overall result: **FAIL**

| Check | Result | Error/diagnostic | Status |
|---|---:|---:|---:|
| Fluid connectivity | 1 region(s), 925388 cells | expected 1 | PASS |
| Mass conservation | inlet -0.54038858 kg/s, outlet 0.5403781 kg/s | 0.00194% | PASS |
| Steady-state heat removal | 8.9321 W outlet sensible transport vs 2315.0000 W applied | 99.6142% power-removal mismatch | FAIL |

## Temperatures and outlet behavior

- Result time: 1.61 s
- Inlet signed mass-weighted temperature: 293.1500 K
- Outlet signed mass-weighted temperature: 293.1664 K
- Analytical outlet temperature from Q/(m_dot Cp): 297.4127 K
- Solid cell-weighted average temperature (adaptive-mesh diagnostic, not a physical volume average): 293.1863938282774 K
- Solid temperature range: 293.1483182112947 to 294.46840212648755 K
- Outlet gross bidirectional flow: 0.540378 kg/s
- Reverse-flow share of gross outlet traffic: 0.00%

The signed mass-flux average is required when an outlet has simultaneous
forward and reverse flow. An absolute-flow average is not an energy balance.

This is a thermal-development gate, not a transient first-law closure result;
stored energy is not included in this report.
