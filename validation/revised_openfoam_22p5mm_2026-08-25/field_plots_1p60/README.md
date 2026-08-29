# Fixed-scale field plots at t = 1.600 s

## Status

These images are visual evidence from the existing reconstructed
`1.6000000000000001` checkpoint. They are **not** evidence of a converged or
thermally settled result. The airflow convergence gates were still failing at
this time, and 1.6 seconds is far too early to judge component temperatures.

No solver was run and the source case was not written. OpenFOAM 2606
`postProcess` sampled two x-normal planes through read-only links in a temporary
WSL case. The final renderer used the bundled scientific Python runtime
(Python 3.12.13, NumPy 2.1.1, Matplotlib 3.9.2).

## Plots

| Image | Plane / crop | What it shows |
|---|---|---|
| `global_speed_vectors_x0p291.png` | x = 0.29115 m, whole rack | Total 3-D speed with arrows for the in-plane Uy/Uz direction |
| `global_p_rgh_gauge_x0p291.png` | x = 0.29115 m, whole rack | `p_rgh - 82655.5 Pa`; the reference is removed only for visualization |
| `global_air_temperature_early_transient_x0p291.png` | x = 0.29115 m, whole rack | Fluid temperature, prominently labeled as an early transient |
| `dell_speed_vectors_x0p291.png` | Dell crop on x = 0.29115 m | Local Dell air-path speed and in-plane direction |
| `dell_air_temperature_early_transient_x0p291.png` | Dell crop on x = 0.29115 m | Local Dell fluid temperature on the common temperature scale |
| `ni_speed_vectors_x0p222.png` | NI crop on x = 0.22225 m | Local NI air-path speed and in-plane direction |
| `ni_air_temperature_early_transient_x0p222.png` | NI crop on x = 0.22225 m | Local NI fluid temperature on the common temperature scale |

The speed scale is fixed at 0--14.5 m/s in all three speed images. The
temperature scale is fixed at 19.9--21.4 degC in all three temperature images.
The pressure plot uses a fixed -170--170 Pa range after subtracting the stated
82655.5 Pa reference. Values outside a displayed range are clipped, though the
selected ranges contain the plotted checkpoint extrema.

Only the fluid region is rendered. Solid intersections therefore appear as
light gaps. The dashed cyan and yellow rectangles mark the reported Dell and
NI component envelopes. Arrows represent only the two velocity components in
the x-normal plane; their direction should not be mistaken for a complete 3-D
streamline. Color uses the complete 3-D speed magnitude.

## What the checkpoint says

The whole center plane has a median speed of 0.983 m/s, a mean of 1.773 m/s,
a 99th percentile of 11.996 m/s, and a maximum of 14.434 m/s. The image shows
localized high-speed jets plus broad lower-speed and recirculating zones. This
is physically recognizable forced-convection structure, but its magnitude and
recirculation pattern are not yet converged.

The Dell crop is strongly nonuniform: median speed is 0.064 m/s, mean speed is
1.199 m/s, the 99th percentile is 10.342 m/s, and the maximum is 12.786 m/s.
The narrow fast region near the fan plane coexists with low-speed pockets and
direction reversals. That is a useful diagnostic, not yet proof that the Dell
flow distribution is realistic.

The NI crop has a median speed of 3.114 m/s, mean speed of 2.993 m/s, and a
maximum of 7.380 m/s. Its center-plane plot shows a strong upward core that is
consistent with the modeled bottom-intake arrangement. The missing thin NI
rear/side separator walls make this flow especially provisional: the present
model can mix air that the finished chassis should keep separated.

Thermal changes remain small. The whole plane is 20.034 degC on average and
21.311 degC maximum. The Dell crop is 20.040 degC average and 20.327 degC
maximum. The NI crop is 20.030 degC average and 20.062 degC maximum. These
values demonstrate that the temperature field is being transported, but they
cannot validate cooling adequacy after only 1.6 seconds.

The whole-plane `p_rgh` range is 82490.77--82743.92 Pa. Relative to the plotted
82655.5 Pa reference, it is -164.73--88.42 Pa. The strongest local change is
near modeled fan/obstruction features, including the Dell region. Subtracting
the reference does not change gradients or the solver data.

Exact descriptive statistics for the whole plane and both crops are in
`field_statistics.csv`.

## Provenance and reproducibility

`sampling_controlDict` preserves the 12-digit raw text sampling recipe.
`sampling_controlDict_vtk` preserves the final ASCII legacy-VTK sampling recipe
used by the renderer. Both use OpenFOAM `surfaces/cuttingPlane` with
`cellPoint` interpolation for `U`, `T`, and `p_rgh` at exactly 1.6 s.

The first QA render used connectivity-free triangulation of the raw text
points. Visual inspection found artificial blank bands, so that render was
rejected and overwritten. The accepted images use the exact polygon
connectivity in `sampled_vtk/`; the high-precision `sampled_data/` raw values
remain as an independent tabular audit source. All seven accepted images were
visually inspected after rerendering for crop coverage, title readability,
arrow density, solid gaps, and fixed color limits.

Reproduction is two-stage:

```text
openfoam2606 postProcess -case <temporary-read-only-link-case> \
  -region fluid -time 1.6 -dict sampling_controlDict_vtk \
  -fields '(U T p_rgh)'

<bundled-python> render_checkpoint_fields.py
```

The temporary link case is necessary because this workspace path contains a
space, which OpenFOAM rejects as an invalid case name. The sampling logs are
`postprocess_sampling.stdout.log` and `postprocess_sampling_vtk.stdout.log`;
the render log is `render_checkpoint_fields.stdout.log`.

`manifest.json` records the source case path and checkpoint, fixed scales,
runtime versions, image hashes, sampled-data hashes, and SHA-256 hashes for the
source `U`, `T`, `p_rgh`, fluid mesh, and geometry report. The renderer never
opens the source case for writing.

## Limitations

- This is one early-transient instant, not a time history or convergence proof.
- It is one x-normal plane per view, not the complete 3-D flow topology.
- Cutting-plane values are interpolated from saved cells; no new physics is
  calculated.
- Solid temperatures are not shown in this fluid-only set.
- The rail-2 depth uncertainty, approximate heat loads, unfinished NI walls,
  current fan curves, coarse-grid contacts, and homogenized component solids
  remain limitations of the underlying model.
