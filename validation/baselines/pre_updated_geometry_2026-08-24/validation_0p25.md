# OpenFOAM validation report

Overall result: **FAIL**

| Check | Result | Error/diagnostic | Status |
|---|---:|---:|---:|
| Fluid connectivity | 3 region(s), 1538474 cells | expected 3 | PASS |
| Mass conservation | inlet -0.51584441 kg/s, outlet 0.51468783 kg/s | 0.22421% | PASS |
| Energy conservation | -11.6299 W transported vs 1515.0000 W applied | 100.7677% | FAIL |

## Temperatures and outlet behavior

- Result time: 0.25 s
- Inlet signed mass-weighted temperature: 293.1500 K
- Outlet signed mass-weighted temperature: 293.1275 K
- Analytical outlet temperature from Q/(m_dot Cp): 296.0789 K
- Solid cell-weighted average temperature (adaptive-mesh diagnostic, not a physical volume average): 293.15125768679724 K
- Solid temperature range: 293.149881657456 to 293.1909511413011 K
- Outlet gross bidirectional flow: 0.514688 kg/s
- Reverse-flow share of gross outlet traffic: 0.00%

The signed mass-flux average is required when an outlet has simultaneous
forward and reverse flow. An absolute-flow average is not an energy balance.
