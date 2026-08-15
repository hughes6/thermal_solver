# Porous-region complex validation — 2026-08-14

## Scope

This validation used the generic production-style rack geometry with four
heated equipment regions, pressure-coupled fans, an external intake, conjugate
solid/fluid heat transfer, and the screening OpenFOAM settings. A 49,818-cell
memory-bounded mesh (41,640 fluid cells) was used for two otherwise identical
cases:

- control: no porous obstruction;
- porous: rear cable-bundle Darcy–Forchheimer region plus a thin perforated
  upper tray.

Both cases were mapped from the reconstructed 40,000.01 s production-rack
checkpoint and advanced with fully coupled airflow and heat transfer to
40,005.01 s. Cases ran sequentially on two ranks; no two CFD jobs overlapped.

## Results

| Metric at 40,005.01 s | Control | Porous |
|---|---:|---:|
| Fluid cells / connected volumes | 41,640 / 1 | 41,640 / 1 |
| Total inlet mass flow | 0.42229 kg/s | 0.46495 kg/s |
| Total outlet mass flow | 0.42540 kg/s | 0.45975 kg/s |
| Mass imbalance | 0.731% | 1.119% |
| Mass-weighted inlet temperature | 293.154 K | 293.155 K |
| Mass-weighted outlet temperature | 297.269 K | 296.378 K |
| Expected outlet from Q/(m-dot Cp) | 296.768 K | 296.498 K |
| Outlet-temperature difference | +0.500 K | -0.121 K |
| Transported/applied heat | 1758.95 / 1545 W | 1489.18 / 1545 W |
| Energy error | 13.85% | 3.61% |
| Solid temperature range | 294.55–315.59 K | 294.55–315.66 K |
| Reverse flow at classified outlet | 0% | 0% |

The previously converged 177,064-cell normal-rack reference at 40,000.01 s had
0.0055% mass imbalance, 0.302% energy error, 293.15 K inlet, 298.289 K outlet,
and a 298.305 K analytical outlet. The coarse five-second cases are therefore
useful obstruction transients, not replacements for the converged reference.

All four true exhaust fan patches remained forward-flowing. The principal
intakes remained inward. Several other fan-labelled/open faces were
bidirectional in both cases, consistent with local rack recirculation rather
than reversal of the four exhaust devices.

## Assessment

The complex porous result is physically credible but is **not certified as a
fully converged rack validation**. Its mass balance narrowly misses the 1%
criterion, and the control retains 13.85% mapped/transient energy storage after
five seconds. The porous outlet temperature is nevertheless within 0.121 K of
the instantaneous first-law prediction, its energy error is 3.61%, connectivity
is correct, flow directions are sane, and solid temperatures remain in the
same range as the control and converged production reference.

The localized porous-case fluid maximum briefly rose to about 342 K before
declining. This is plausible for the intentionally strong, uncalibrated cable
and tray resistance, but it must not be used as a normal-rack prediction until
the porous coefficients are calibrated from pressure-drop data and the case is
advanced until both mass and stored-energy residuals pass.

## Defects found and corrected

1. A porous region thinner than one adaptive cell could select zero cells when
   both physical bounds fell inside the same cell. Every non-empty axis range
   now selects at least one cell; the regression test places a 0.01 m layer
   wholly inside a 0.1 m cell.
2. `mapFields` maps into target time `0`; the mapping helper previously launched
   a 40,020 s run instead of renaming the mapped target to the source checkpoint
   time. It now moves target `0` to the requested source time before warm start.
3. Warm start did not restore `stopAt endTime`, so a prior `writeNow` request
   could make the next run terminate after one step. Generated launchers now
   reset `stopAt` explicitly.

## Regression evidence

- Native Darcy–Forchheimer analytical pressure-drop test: pass; computed added
  pressure 0.000209999 Pa.
- Thin sub-cell porous selection regression: pass.
- OpenFOAM complex geometry/connectivity: pass, one connected fluid volume.
- Full `tests/run_added_feature_tests.ps1` suite: pass, including all C++ and
  Python tests.

## Recommended acceptance run

For coefficient calibration or design acceptance, use the same mesh for the
control and porous cases, advance coupled airflow until inlet/outlet imbalance
is below 1% (preferably 0.5%), then use thermal-only/multirate continuation
until transported heat agrees with applied heat within 3%. Compare
mass-weighted outlet temperature, each exhaust-fan flow, pressure drop across
the obstruction, maximum/percentile air temperature, and solid component
temperatures. Do not calibrate against the isolated cell maximum alone.
