# Revised-model native resolved smoke

Model: `library/models/new_model_updated_native_smoke.toml`

## Outcome

The complete native mesh, fan/vent stamping, bounded PCG flow, thermal update,
and output path completed with exit code 0. This is a robustness smoke, not a
converged or physically valid flow result.

- Cells: 2,800,980 total; 2,412,079 air, 376,738 solid, 5,848 fan, 6,315 vent.
- Mesh-only estimate: 1,254,839,040 bytes; measured solver working set about
  2.34 GB.
- Unbounded production PCG attempt: interrupted after more than 11 minutes in
  its first pressure call with no residual output.
- Bounded smoke: 100 pressure iterations and two nonlinear outer passes.
- First flow pass residuals: 0.0102151 then 0.00016855 m3/s.
- Timestep flow residuals: 0.0102258 then 0.000150353 m3/s.
- Final reported source/vent imbalance: -0.128872 m3/s, therefore unacceptable.
- Thermal step: 1e-5 s, three advection substeps, global CFL 1.6823.
- Raw CSV size: 753,319,298 bytes for one timestep, an output-volume concern.

The raw `simulation.csv`, geometry `output.txt`, and last-run metadata are
preserved beside this report.
