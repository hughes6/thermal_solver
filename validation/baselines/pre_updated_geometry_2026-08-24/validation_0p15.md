# OpenFOAM validation report

Overall result: **FAIL**

| Check | Result | Error/diagnostic | Status |
|---|---:|---:|---:|
| Fluid connectivity | 3 region(s), 1538474 cells | expected 3 | PASS |
| Mass conservation | inlet -0.46264189 kg/s, outlet 0.45996581 kg/s | 0.57844% | PASS |
| Energy conservation | -8.0173 W transported vs 1515.0000 W applied | 100.5292% | FAIL |

## Temperatures and outlet behavior

- Result time: 0.15 s
- Inlet signed mass-weighted temperature: 293.1500 K
- Outlet signed mass-weighted temperature: 293.1327 K
- Analytical outlet temperature from Q/(m_dot Cp): 296.4273 K
- Solid cell-weighted average temperature (adaptive-mesh diagnostic, not a physical volume average): 293.15074486870964 K
- Solid temperature range: 293.14993760455195 to 293.17457176380094 K
- Outlet gross bidirectional flow: 0.459966 kg/s
- Reverse-flow share of gross outlet traffic: 0.00%

The signed mass-flux average is required when an outlet has simultaneous
forward and reverse flow. An absolute-flow average is not an energy balance.
