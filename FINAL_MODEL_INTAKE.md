# Final research-lab model intake

Date: 2026-08-29

Canonical model: `library/models/final_model.toml`

Source bundle SHA-256: `0b929a7a15b0c5321cb3e5f608f53132eaaf45f373f17f4d304e7f4998e5558b`

## Implemented configuration

- 17 component instances
- 44 fans (rack and component fans combined)
- 31 vent regions, matching the final supplied bundle
- 47 nonzero heat sources totaling 2,434.8 W
- OpenFOAM screening profile with four ranks, a 30 s duration, 1,536 MB configured memory ceiling, and 0.019 m OpenFOAM fine spacing
- Altitude-corrected air density of 0.9833 kg/m3 for the supplied 5,500 ft, 20 C environment

The supplied model and component data are preserved in
`validation/baselines/final_model_2026-08-29/final_model_raw_paste.txt`.
Machine-readable source/output hashes and intentional transformations are in
`TRANSFORMATION_MANIFEST.json` beside that file.

## Material policy

All outer components and solid internal regions in the canonical model use
material-file paths. No inline `rho`, `cp`, or `k` material tables remain.

- Every solid internal region in a component containing a fan uses
  `library/components/materials/mixed_electronics.toml`. This is the project's
  pseudo-electronics material: rho 1,200 kg/m3, cp 800 J/kg-K, k 10 W/m-K.
- Enclosures use
  `library/components/materials/aluminum_enclosure_effective.toml`.
- The air-block shell uses
  `library/components/materials/air_block_shell_effective.toml`.

The native loader and geometry export resolve and report these file-backed
internal-region values. The current OpenFOAM exporter still uses one homogeneous
outer material per component solid region. Consequently, a production OpenFOAM
run will not yet preserve distinct internal-region rho/cp/k values; the
material-fidelity audit quantifies this known solver-backend limitation.

## Added and repaired library entries

- Added the previously absent `2Ux2U_Air_block` component as
  `library/components/final_2Ux2U_Air_block.toml`.
- Added the previously absent `Delta_GFB0412ES_E` and
  `Delta_TFA0412CN_CN` curves to the main fan-curve library.
- Corrected supplied transpositions `Keysight N6701A` to `N6701C` and
  `Keysight N5677A` to `N5766A`.
- Preserved all 132 supplied internal regions. No earlier Trenton, NI, or
  Keysight geometry assumptions override the final supplied values. After the
  first simulation preflight, the UPS rear fan's active height was clipped from
  80.0 mm to 76.5 mm at the user's direction so it fits the 76.5 mm-tall
  interior-air cavity; its center, curve, CFM, and nominal frame identity remain
  unchanged.

No component template, material file, or fan curve referenced by the final
model is missing.

## Validation status

- Final-model and geometry-auditor tests: 14/14 passed.
- Full Python regression discovery ran 358 tests: 356 passed, 1 skipped, and
  1 failed. The failure is the existing semi-frozen-solver source attestation
  pin on a Windows CRLF checkout: the committed pin is
  `6f5b54f...`, while the byte-level working-tree fingerprint is `c5b8c820...`.
  Git reports no changes to the three solver-source inputs or exporter. This
  pre-existing cross-platform release-attestation issue is unrelated to the
  final model, but must be resolved before claiming a clean release suite.
- Source-to-install comparison: 132 source regions and 132 installed regions;
  the sole non-material deviation is the documented UPS active-height clip.
- Production C++ runner compiled and parsed the final model successfully with
  `--geometry-only`; no mesh or transient solver was run during this check.
- OpenFOAM material-fidelity audit completed successfully. It reports 55
  distinct internal solid regions affected by the homogeneous-export limitation.

The NI `Interior air`/`Card Slot air` overlap is accepted as intentional for
this model and is not a blocker. The standalone geometry lint also identifies
plane/direction and Trenton wall-intersection conditions in the supplied data;
these are retained because the final bundle is authoritative, rather than
silently replacing its internal-region definitions with older assumptions.
