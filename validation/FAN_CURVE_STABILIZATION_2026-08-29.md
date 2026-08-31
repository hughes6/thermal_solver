# OpenFOAM fan-curve stabilization

## Scope

This change stabilizes the OpenFOAM fan-source export without altering a fan's
declared identity, CFM rating, or source curve coefficients.  In particular,
the active Thruster and Trenton source selections remain unchanged.

## What changed

The former exporter sampled the supplied quadratic through its first
zero-pressure point and then wrote a flat zero-pressure table.  A fan source
advected just above free delivery could therefore alternate between finite fan
head and zero head on successive pressure/velocity corrections.

The exporter now:

1. Uses only the first positive zero of the supplied pressure-flow fit.
2. Emits the supplied curve up to that point, with an explicit zero-pressure
   table knot.
3. Continues with a signed passive branch using the tangent at that knot:

   `dp(Q) = slope_multiplier * dp/dQ(Q_free) * (Q - Q_free)` for `Q > Q_free`.

   The derivative is negative, so the continuation is a pressure drop rather
   than a restored positive fan head.  It is C1-continuous at free delivery.
4. Rejects a curve whose first zero is not crossed with decreasing pressure.
5. Exports 48 active-curve and 32 assisted-flow intervals, retaining the exact
   free-delivery knot rather than sampling across it.

All supplied OpenFOAM profiles now export through four times free-delivery
flow with `fan_assisted_flow_slope_multiplier = 1.0`.  This multiplier retains
the supplied slope; it is configurable for a later measured windmilling-loss
calibration.

## Runtime and audit behavior

- Below 90% of free delivery: normal pass.
- From 90% to free delivery: near-limit warning.
- From free delivery through the signed table endpoint: assisted-flow warning.
- Beyond the signed table endpoint: strict failure.

The generated runner and `tools/openfoam_fan_operating_domain_audit.py` use
the same distinction.  A warning is not a claim that the branch is measured
fan data; it says that the solver is using the explicitly exported passive
continuation instead of an implicit zero-head plateau.

## Validation procedure

Run the focused source suite first:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\run_added_feature_tests.ps1
```

Then create a fresh, separately named low-memory OpenFOAM case from the final
model and run only the initial airflow window.  Review the generated
`fan_operating_domain_audit.csv`, `fan_operating_domain_audit.md`, airflow
gate summary, and velocity-change report before advancing thermal time.

Do not overwrite the preserved 0.35 s strict case or the existing frozen-flow
thermal screening case while validating this change.

## Limitation

The signed continuation is a numerical passive-resistance model outside the
measured positive-pressure fan curve.  It is not a replacement for a
manufacturer windmilling or reverse-flow characterization.  Update the
source curve only when the correct measured curve is available; this change is
deliberately independent of that future data update.
