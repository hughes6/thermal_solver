# OpenFOAM validation report

Overall result: **FAIL**

| Check | Result | Error/diagnostic | Status |
|---|---:|---:|---:|
| Fluid connectivity | 1 region(s), 925388 cells | expected 1 | PASS |
| Mass conservation | inlet -0.54041415 kg/s, outlet 0.54038463 kg/s | 0.00546% | PASS |
| Steady-state heat removal | 8.9274 W outlet sensible transport vs 2315.0000 W applied | 99.6144% power-removal mismatch | FAIL |

## Temperatures and outlet behavior

- Result time: 1.6 s
- Inlet signed mass-weighted temperature: 293.1500 K
- Outlet signed mass-weighted temperature: 293.1664 K
- Analytical outlet temperature from Q/(m_dot Cp): 297.4127 K
- Solid cell-weighted average temperature (adaptive-mesh diagnostic, not a physical volume average): 293.18616449416504 K
- Solid temperature range: 293.14832434915974 to 294.46245200090476 K
- Outlet gross bidirectional flow: 0.540385 kg/s
- Reverse-flow share of gross outlet traffic: 0.00%

The signed mass-flux average is required when an outlet has simultaneous
forward and reverse flow. An absolute-flow average is not an energy balance.

This is a thermal-development failure, not a demonstrated transient
first-law conservation error. At 1.60 s, most input energy is expected to be
stored in the fluid and solids. Formal transient closure requires
high-precision stored-energy changes plus boundary enthalpy and applicable
work/heat-flux terms over a common interval.

The retained 1.50--1.60 s evidence cannot supply that formal interval ledger;
see `TRANSIENT_FIRST_LAW_AUDIT_STATUS_1P50_1P60.md`. Its verdict is **not
evaluable**, not a pass or a demonstrated conservation failure.
