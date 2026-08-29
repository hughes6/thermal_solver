# OpenFOAM validation report

Overall result: **FAIL**

| Check | Result | Error/diagnostic | Status |
|---|---:|---:|---:|
| Fluid connectivity | 3 region(s), 1538474 cells | expected 3 | PASS |
| Mass conservation | inlet -0.40935571 kg/s, outlet 0.40773862 kg/s | 0.39503% | PASS |
| Energy conservation | -5.4101 W transported vs 1515.0000 W applied | 100.3571% | FAIL |

## Temperatures and outlet behavior

- Result time: 0.1 s
- Inlet signed mass-weighted temperature: 293.1500 K
- Outlet signed mass-weighted temperature: 293.1368 K
- Analytical outlet temperature from Q/(m_dot Cp): 296.8471 K
- Solid cell-weighted average temperature (adaptive-mesh diagnostic, not a physical volume average): 293.15049287328833 K
- Solid temperature range: 293.1499645730412 to 293.1663815362733 K
- Outlet gross bidirectional flow: 0.407739 kg/s
- Reverse-flow share of gross outlet traffic: 0.00%

The signed mass-flux average is required when an outlet has simultaneous
forward and reverse flow. An absolute-flow average is not an energy balance.
