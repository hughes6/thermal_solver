# Updated research-lab model intake

The revised model supplied on 2026-08-24 is preserved verbatim at
`validation/baselines/pre_updated_geometry_2026-08-24/updated_model_raw_paste.txt`.
The canonical working copy is `library/models/new_model_updated.toml`. Its
resource-bounded OpenFOAM fixture is active in a separate case path derived
from the fixture filename, so it cannot overwrite the older baseline case.

## Integration status

The paste was confirmed to be a bundle rather than one directly parseable TOML
file. It contains one rack-level model, 16 fan curves, and nine standalone
component definitions. The raw source remains unchanged.

The bundle has now been split into independently parseable, versioned files:

- `library/models/new_model_updated.toml`
- `library/fan_curves/fan_curves_updated_2026_08_24.toml`
- `library/components/updated_trenton_3u_bam.toml`
- `library/components/updated_Thruster_Load_Box.toml`
- `library/components/updated_NI_PXIe_Chassis.toml`
- `library/components/updated_keysight_N6701C.toml`
- `library/components/updated_Keysight_N5766A.toml`
- `library/components/updated_eaton_UPS.toml`
- `library/components/updated_DELL_R470.toml`
- `library/components/updated_DELL_R360.toml`
- `library/components/updated_cisco_catalyst_9300_24.toml`

The updated rack references the versioned component files, so none of the
templates used by the running baseline case were overwritten. The supplied fan
curves were merged into the installed curve library while retaining legacy and
provisional entries needed by other models. The previous curve library is
archived with the baseline.

The bundled rack file also reverted air density to 1.225 kg/m^3 while retaining
the 5,500 ft laboratory elevation and 20 C ambient. That is a sea-level value,
not a geometry or fan-curve update. The working revision therefore retains the
baseline altitude-corrected 0.9833 kg/m^3 density; this prevents an unintended
roughly 25% mass-flow and pressure-scaling change from being attributed to the
new hardware geometry.

Several supplied least-squares fits contain a negative linear or quadratic
coefficient. This is mathematically valid over a bounded manufacturer-data
domain but exposes an old exporter assumption that both coefficients are
non-negative. The exporter has been revised to find the first positive
zero-pressure crossing and clamp the generated table to zero beyond it,
preventing an unphysical high-flow pressure rebound. Targeted table-generation,
isolated-fan, and revised full-rack runtime tests have since exercised this
path. C++17 syntax compilation passes for `pcg_flow_test.cpp`,
`model_config_test.cpp`, and `openfoam_export_test.cpp`, covering the directly
affected native, loader, and exporter code paths.

Focused runtime evidence now also passes for the native PCG/SOR flow regression
and model-loader regression. The native test exercised uniform and adaptive
meshes with mass imbalance down to approximately 1e-13--1e-10 m3/s and retained
PCG/SOR pressure agreement. The loader accepted a bounded signed curve and
rejected a curve with no positive zero-pressure crossing. OpenFOAM generated-
table runtime execution now also passes. `openfoam_export_test` generated the
case and confirmed a signed quadratic with a second positive-pressure branch is
clamped to zero after its first crossing. The broader exporter contract passed
on a 76,800-cell fixture.

Static component auditing initially found 22 fan-plane orientation errors:
rear fans in both Keysight models pointed along x despite lying on y-normal
planes, and the NI power-supply intake pointed along z despite lying on the
front y-plane. Those directions were corrected to the physically consistent y
axis. The supplied Trenton motherboard zone also overlapped the front block and
power-supply wall despite assigning different materials and heat sources. It
was bounded to the clear compartment behind the 150 mm front block and inside
the x=410 mm power-supply wall. The only remaining overlap is the explicitly
unfinished NI interior/card-air partition noted by the user.

Initial integration gates and their current status:

1. Compile and regression-test signed-coefficient fan-curve export: completed.
2. Run geometry-only validation before any mesh export: completed and repeated
   after the latest Trenton correction.
3. Audit the supplied nominal CFM values against each curve's first
   zero-pressure crossing; several nominal entries currently lie at or beyond
   that crossing and must not be treated as simultaneous operating points:
   domain checks pass, but measured operating-point calibration remains open.
4. Export to a new case directory, audit overlaps/connectivity/heat totals, and
   start a fresh four-rank convergence lineage. Baseline mapped fields must not
   be reused when geometry or fan curves change: completed for the independent
   revised 22.5 mm screening case.

An updated-model OpenFOAM result is now in progress. The 1,033,200-cell screen
contains 925,388 fluid cells, 13 solid regions, 24 ambient openings, 33
internal fan interfaces, and 47 nonzero heat sources totaling 2,315 W. Through
the complete 1.50 s checkpoint all internal fans flow forward and exterior
mass mismatch is 0.005651%, but whole-fluid velocity still changes 9.3467%
over 0.10 s and 14.2558% over 0.20 s. It is therefore a stable, balanced
screening lineage, not converged airflow or a thermal validation result. The
live status and exact limitations are recorded in
`validation/revised_openfoam_22p5mm_2026-08-25/RUN_STATUS.md`.

The component templates define 38 component fans in total. Five Trenton front
intake fans coincide with the exterior rack boundary and are exported as
ambient fan patches; the other 33 become internal fan zones. The rack also
defines nine roof exhaust-fan patches. Thus the 33-zone runtime count is a
geometry/export classification, not a loss of five component fan definitions.

Two staged profiles now preserve the exact revised environment, rack,
components, fans, and vents:

- `new_model_updated_native_smoke.toml` uses a one-step, 0.00001 s native-only
  diagnostic with a bounded PCG iteration budget to expose parser, stamping,
  flow, and update failures. Its preserved 2026-08-24 output predates the final
  Trenton template correction and is retained as history, not as the current
  geometry baseline.
- `new_model_updated_openfoam_export_test.toml` retains the production export
  physics but targets an isolated workspace case directory.

Contract tests verify neither profile can silently drift from the full revised
physics. Passing either staged profile will be treated only as a gate to the
full production mesh, not as validation or convergence evidence.
