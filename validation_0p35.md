# OpenFOAM validation report

Overall result: **FAIL**

| Check | Result | Error/diagnostic | Status |
|---|---:|---:|---:|
| Fluid connectivity | 3 region(s), 1538474 cells | expected 3 | PASS |
| Mass conservation | inlet -0.53947433 kg/s, outlet 0.53890887 kg/s | 0.10482% | PASS |
| Energy conservation | -13.4286 W transported vs 1515.0000 W applied | 100.8864% | FAIL |

## Temperatures and outlet behavior

- Result time: 0.35 s
- Inlet signed mass-weighted temperature: 293.1500 K
- Outlet signed mass-weighted temperature: 293.1252 K
- Analytical outlet temperature from Q/(m_dot Cp): 295.9472 K
- Solid cell-weighted average temperature (adaptive-mesh diagnostic, not a physical volume average): 293.1517800868802 K
- Solid temperature range: 293.1498246920611 to 293.2073290830388 K
- Outlet gross bidirectional flow: 0.538909 kg/s
- Reverse-flow share of gross outlet traffic: 0.00%

The signed mass-flux average is required when an outlet has simultaneous
forward and reverse flow. An absolute-flow average is not an energy balance.
