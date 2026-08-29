# NI provisional-separator native mesh ladder — 2026-08-26

## Decision

The provisional minimum-separator component remains inactive and rejected. No
tested mesh spacing preserves its final stamped geometry. The primary
`Card Slot wall 1` loss is a topology/stamping consequence, not a mesh-resolution
error, and refinement cannot remove it.

For source-cut representation alone, `fine_dx <= 8.8 mm` is required to retain
every NI component feature coordinate in this fixture. That is necessary but not
sufficient: even at 7.5 and 8.8 mm the internal vent tunnel removes exactly
`0.000018 m^3` from wall 1.

## Reproducible method

`tests/ni_separator_mesh_ladder_test.cpp` loads both:

- canonical `library/components/updated_NI_PXIe_Chassis.toml`;
- inactive
  `library/components/updated_NI_PXIe_Chassis_minimum_separator_sensitivity.toml`.

Each component is placed at `(0.07, 0.12, 0.06) m` in the isolated
`0.50 x 0.50 x 0.30 m` rack. Each rung uses the production
`MeshRefinementPlanner` with `coarse_dx=50 mm`, `refinement_margin=20 mm`, and
then `Mesh::stamp_component_adaptive`. Boundary rack openings and flow/thermal
solves are deliberately excluded: this is an isolated component stamping test.

For every spacing, the test runs six sequential meshes:

1. provisional component on its own plan;
2. provisional component with only `Top right side vent` omitted;
3. canonical component on its own plan;
4. canonical component with only that vent omitted;
5. canonical component on the provisional plan;
6. provisional component on the canonical plan.

This separates source geometry, mesh planning, and vent-tunnel stamping. The
175,000-cell guard is checked before allocation. The largest mesh was 160,034
cells and reported 71,695,232 bytes of cell storage; meshes are constructed and
destroyed sequentially.

Build and run:

```powershell
g++ -std=c++17 -O2 -I src tests/ni_separator_mesh_ladder_test.cpp -o validation/revised_native_regression_2026-08-26/ni_separator_mesh_ladder_test_2026-08-26.exe
& validation/revised_native_regression_2026-08-26/ni_separator_mesh_ladder_test_2026-08-26.exe
```

## Results

All provisional and canonical plans had identical cell counts at the same
spacing. Volumes below are solid wall-1 volumes after all component openings
were stamped.

| fine dx (mm) | cells | all source cuts retained | 5 mm wall cuts retained | 2.5 mm vent-edge/wall-top pair retained | provisional wall 1 (m^3) | provisional without vent (m^3) | vent-specific loss (m^3) | canonical wall 1 (m^3) | canonical without vent (m^3) |
|---:|---:|:---:|:---:|:---:|---:|---:|---:|---:|---:|
| 7.5 | 160,034 | yes | yes | yes | 0.00017400 | 0.00019200 | 0.00001800 | 0.00019200 | 0.00019200 |
| 8.8 | 131,365 | yes | yes | yes | 0.00017400 | 0.00019200 | 0.00001800 | 0.00019200 | 0.00019200 |
| 9.0 | 118,080 | no | yes | yes | 0.00017400 | 0.00019200 | 0.00001800 | 0.00019200 | 0.00019200 |
| 10.0 | 99,308 | no | yes | yes | 0.00017400 | 0.00019200 | 0.00001800 | 0.00019200 | 0.00019200 |
| 15.0 | 57,304 | no | yes | no | 0.00017664 | 0.00019464 | 0.00001800 | 0.00019464 | 0.00019464 |
| 19.0 | 47,600 | no | yes | no | 0.00017682 | 0.00019482 | 0.00001800 | 0.00019482 | 0.00019482 |
| 20.0 | 46,648 | no | yes | no | 0.00017682 | 0.00019482 | 0.00001800 | 0.00019482 | 0.00019482 |

Cross-plan controls give the same result at every rung:

- canonical on the provisional plan loses `0 m^3` when the vent is present;
- provisional on the canonical plan loses exactly `0.000018 m^3`;
- therefore neither planner selection nor added refinement is the cause.

The provisional wall 3 loses a separate, invariant `0.000010185 m^3` relative
to its vent-omission control. At 7.5–10 mm its requested/realized volumes are
`0.000124000/0.000113815 m^3`.

The existing exact-feature 20 mm template microcase independently reproduces
the fail-closed result:

```text
Card Slot wall 1: requested=0.000192 m^3, realized=0.000174 m^3
```

## Exact cause and claim boundary

There is **no direct source-volume intersection** between the planar
`Top right side vent` and volumetric wall 1. The vent has zero thickness along
x and sits at the chassis exterior (`x=0 mm` local); wall 1 begins at
`x=10 mm` local.

Canonical geometry has `Interior air` beginning at `x=5 mm`, leaving a 5 mm
fluid corridor from `x=5..10 mm` before wall 1. During adaptive stamping,
regions are ordered air, solid, vent, fan. A component vent replaces its plane
cell and carves inward only until it reaches an already-fluid cell. In the
canonical chassis, the tunnel reaches that corridor and stops before wall 1.

The provisional wall 3 exactly fills `x=5..10 mm` over the relevant y/z range.
It therefore removes the canonical first-fluid stop. In the small band where
wall 3 is contiguous with wall 1, the same vent tunnel continues through wall 1:

- tangential y overlap: `5 mm` (`59.2..64.2 mm`);
- tangential z overlap: `15 mm` (`152.5..167.5 mm`);
- wall-1 tunnel length in x: `240 mm`;
- carved volume: `240 x 5 x 15 = 18,000 mm^3 = 0.000018 m^3`.

Thus the analytical 18,000 mm^3 loss does **not** exist in canonical source or
stamped geometry. It appears only after the provisional wall seals the fluid
corridor and the vent tunnel is extruded to the next fluid cell. The source data
do not say whether the as-built answer should be a wall opening, a vent split,
a shorter separator, or communication with another plenum; changing the
stamper to choose one would invent geometry.

The mesh adds a secondary error:

- the smallest distinct NI source-coordinate gap is 2.2 mm, so the planner's
  `0.25*fine_dx` anti-sliver rule retains every cut only through 8.8 mm;
- the separate 2.5 mm side-vent-edge/wall-top pair survives through 10 mm;
- all tested 5 mm wall-thickness cuts survive through 20 mm;
- at the current 19 mm fixture spacing, the no-vent wall-1 control is
  `0.00019482 m^3`, 1.47% above the requested `0.000192 m^3`, while the
  vent-specific loss remains exactly `0.000018 m^3`.

## Verification and evidence

- Ladder test: PASS, seven spacings, six controls per spacing.
- Updated lab model contract: 16/16 PASS.
- Existing template campaign: one expected NI geometry rejection, exit 0.
- Canonical component was not changed or promoted; the provisional component
  and isolated fixture received warning comments only.

SHA-256:

- test source: `3CEC1F73DD894C73C8474FECCD15D977B92EDD6BA09233C7EF8A5876EBB8C1AA`
- test executable: `19F61689AACFA6FEF635E275434C74AAF16FBD020B306EDED2C5904B9A46636F`
- final ladder log: `B56091B77C61F0F622591DBF0AB9A7F150307165D2F4CC7D3634A1A6393A1C34`
- canonical component: `5BBB6DDC13C8EA49F73C2C8F54DDA2959F87196C5A75082D1EC9E874134C029C`
- provisional component: `6A154C7C936F4C98186C6AED90F0F96E33A967E3244618D4E2FF37949F063FFC`
- isolated fixture: `9B632F7D9A664F851653A6F3D161170E4F89B54DB335C131FE09C2907FB786C9`

Evidence files:

- `ni_separator_mesh_ladder_2026-08-26.stdout.log`
- `native_template_microcase_ni_separator_mesh_diagnosis_2026-08-26.stdout.log`
- `ni_separator_mesh_ladder_test_2026-08-26.exe`

## Required next input

Do not promote or solve the provisional variant. Obtain measured separator
gauge and termination, plus an explicit statement or drawing of how the top
right side vent communicates with the card-air and interior-air regions. Then
replace this sensitivity with source-backed, non-overlapping air boxes and vent
paths and rerun the ladder, flow solve, thermal balance, and OpenFOAM topology
checks.
