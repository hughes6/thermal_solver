# Heterogeneous OpenFOAM material export

## Change

Before this change, an OpenFOAM export created one solid CHT region for each
outer component. Internal `state = "solid"` regions retained their geometry and
heat load but silently used the outer component's `rho`, `cp`, and `k`.

The exporter now creates a distinct solid CHT region for each distinct solid
material within a component. Material values are read from the component and
internal-region material definitions, including `material = "...toml"` files.
Adjacent solid material regions use a coupled temperature boundary, and only
solid regions that physically touch air receive a fluid coupling boundary.

## Consequences for a new export

- Existing component names continue to name enclosure regions.
- A different nested material produces an additional region named from the
  component and internal-region name, for example
  `heterogeneous_assembly_mixed_core_1`.
- A heat source is written into the solid region carrying that source's
  material, rather than the enclosing material region.
- A case must be re-exported after this change. Do not reuse an old exported
  case because its `regionProperties`, `0/*/T`, and material dictionaries have
  the old one-region topology.

## Verification performed

`tests/openfoam_export_test.cpp` now exports a three-cell heterogeneous CHT
case and checks all of the following:

1. Enclosure `thermophysicalProperties` retains `k=150`, `Cp=900`, and
   `rho=2700`.
2. Nested-core `thermophysicalProperties` retains `k=10`, `Cp=800`, and
   `rho=1200`.
3. Both directions of the solid-to-solid coupled temperature interface are
   present.
4. The core heat source is placed in the core region's `fvOptions`.

The test was compiled and passed on 2026-08-31 with:

```powershell
g++ -std=c++17 -O0 -I src tests/openfoam_export_test.cpp -o <temporary-exe>
<temporary-exe>
```

## Ambient-air placeholder handling

`final_2Ux2U_Air_block.toml` is a full-size, zero-load internal air region.
It is now exported as ambient fluid only: no solid cell zone, no solid material
dictionary, and no CHT region are generated for it. This prevents
`splitMeshRegions` from evaluating a zero-volume solid region, which otherwise
causes invalid bounding-box extrema and the `ill-defined primitiveEntry` error.

The same export regression test confirms that this full-air pattern leaves all
cells as fluid and creates no OpenFOAM component region.

The broader `model_config_test.cpp` was also compiled but has a pre-existing
failure before material checks: it still expects `model_runner.cpp` to default
to `library/models/new_model_updated.toml`, while the runner now defaults to
`final_model.toml`. A direct geometry-only runner check could not replace its
existing `output.txt` because that file was locked by another process, so it
was left untouched.
