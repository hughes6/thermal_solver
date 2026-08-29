# OpenFOAM validation report

Overall result: **FAIL**

| Check | Result | Error/diagnostic | Status |
|---|---:|---:|---:|
| Fluid connectivity | 3 region(s), 1538474 cells | expected 3 | PASS |
| Mass conservation | inlet -0.52979339 kg/s, outlet 0.52898471 kg/s | 0.15264% | PASS |
| Energy conservation | -12.7504 W transported vs 1515.0000 W applied | 100.8416% | FAIL |

## Temperatures and outlet behavior

- Result time: 0.3 s
- Inlet signed mass-weighted temperature: 293.1500 K
- Outlet signed mass-weighted temperature: 293.1260 K
- Analytical outlet temperature from Q/(m_dot Cp): 295.9997 K
- Solid cell-weighted average temperature (adaptive-mesh diagnostic, not a physical volume average): 293.1515178248305 K
- Solid temperature range: 293.1498559569802 to 293.1991402909633 K
- Outlet gross bidirectional flow: 0.528985 kg/s
- Reverse-flow share of gross outlet traffic: 0.00%

The signed mass-flux average is required when an outlet has simultaneous
forward and reverse flow. An absolute-flow average is not an energy balance.
