# OpenFOAM validation report

Overall result: **FAIL**

| Check | Result | Error/diagnostic | Status |
|---|---:|---:|---:|
| Fluid connectivity | 3 region(s), 1538474 cells | expected 3 | PASS |
| Mass conservation | inlet -0.54631805 kg/s, outlet 0.54591609 kg/s | 0.07358% | PASS |
| Energy conservation | -13.9291 W transported vs 1515.0000 W applied | 100.9194% | FAIL |

## Temperatures and outlet behavior

- Result time: 0.4 s
- Inlet signed mass-weighted temperature: 293.1500 K
- Outlet signed mass-weighted temperature: 293.1246 K
- Analytical outlet temperature from Q/(m_dot Cp): 295.9113 K
- Solid cell-weighted average temperature (adaptive-mesh diagnostic, not a physical volume average): 293.15204424502764 K
- Solid temperature range: 293.1497927345449 to 293.2155175448724 K
- Outlet gross bidirectional flow: 0.545916 kg/s
- Reverse-flow share of gross outlet traffic: 0.00%

The signed mass-flux average is required when an outlet has simultaneous
forward and reverse flow. An absolute-flow average is not an energy balance.
